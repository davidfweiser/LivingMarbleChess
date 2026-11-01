#!/usr/bin/env python3
"""Living Marble - the server for online games on the web page.

Browsers connect with a WebSocket at /ws (behind nginx). The server
- passes the moves between the two players of a game, checks each one with the game's own
  chess rules (web/chess_rules.cpp, loaded as a library) and decides how games end;
- pairs players who want an opponent (Play someone online) by rating;
- keeps players' names and ratings (SQLite) and the leaderboard.

Two kinds of game:
- with a friend: one player creates it and sends the link (?join=CODE). Untimed, unrated.
- matched: two players from the queue, 10 minutes each plus 5 seconds a move. Rated (Elo).
  Resigning, running out of time or staying away for 90 seconds loses; a game that ends
  before both players have moved is aborted (unrated).

A player is known by a secret key the server hands the browser when they choose a name (no
passwords). Each side of a game has a token of its own, so a reload or a lost connection
comes back to the same side.

    python3 relay.py --rules liblmrules.so --db players.db
    python3 relay.py --rules ... --db ... --static build-web --allow-same-ip   (local tests)

Needs the websockets package (Ubuntu: apt install python3-websockets).
"""
import argparse
import asyncio
import ctypes
import hashlib
import http
import json
import logging
import mimetypes
import os
import random
import re
import secrets
import sqlite3
import time

import websockets

CODE_ALPHABET = "ABCDEFGHJKMNPQRSTUVWXYZ23456789"  # no 0/O, 1/I/L
CODE_LENGTH = 6
MAX_CONNECTIONS = 20000
MAX_CONNECTIONS_PER_IP = 40
MAX_ROOMS = 20000
ROOM_IDLE_SECONDS = 24 * 3600    # an unfinished game nobody has touched for this long goes
ENDED_ROOM_SECONDS = 600         # a finished one, 10 minutes after both players left
CREATES_PER_IP_PER_HOUR = 30
NAMES_PER_IP_PER_DAY = 20         # (a school or an office may share one address)
MAX_PLIES = 1200
MAX_MESSAGE = 2048
CLOCK_MS = 10 * 60 * 1000        # matched games: 10 minutes each ...
INCREMENT_MS = 5000              # ... plus 5 seconds a move
AWAY_SECONDS = 90                # away this long from a matched game: lost
START_RATING = 1200
TOP_MIN_GAMES = 5                # on the leaderboard after this many rated games
TOP_SIZE = 20
NAME = re.compile(r"^[A-Za-z0-9_-]{3,16}$")
UCI = re.compile(r"^[a-h][1-8][a-h][1-8][nbrq]?$")
# the rules library's verdicts (see chess_rules.cpp)
ENDINGS = {1: ("1-0", "checkmate"), 2: ("0-1", "checkmate"), 3: ("1/2-1/2", "stalemate"),
           4: ("1/2-1/2", "fifty"), 5: ("1/2-1/2", "repetition"), 6: ("1/2-1/2", "material")}

log = logging.getLogger("relay")
allow_same_ip = False


def other(seat):
    return "b" if seat == "w" else "w"


def loss_for(seat):
    return "0-1" if seat == "w" else "1-0"


# ---------------------------------------------------------------------------
# The rules: the game's own chess.cpp

class Rules:
    def __init__(self, path):
        lib = ctypes.CDLL(path)
        lib.lm_game_new.restype = ctypes.c_void_p
        lib.lm_game_free.argtypes = [ctypes.c_void_p]
        lib.lm_game_play.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        lib.lm_game_play.restype = ctypes.c_int
        self.lib = lib


rules = None


class ChessGame:
    """One game's position, kept by the rules library: play() says -1 (illegal) or how it stands."""

    def __init__(self):
        self.handle = rules.lib.lm_game_new() if rules else None

    def play(self, uci):
        return rules.lib.lm_game_play(self.handle, uci.encode()) if self.handle else 0

    def close(self):
        if self.handle:
            rules.lib.lm_game_free(self.handle)
            self.handle = None


# ---------------------------------------------------------------------------
# Players: names, secret keys (stored hashed) and ratings

class Player:
    def __init__(self, row):
        self.id, self.name, self.rating, self.games, self.wins, self.draws, self.losses = row

    def public(self):
        return {"name": self.name, "rating": self.rating}


class Players:
    def __init__(self, path):
        self.db = sqlite3.connect(path)
        self.db.execute("PRAGMA journal_mode=WAL")
        self.db.execute("PRAGMA synchronous=NORMAL")  # (safe with WAL; far fewer waits for the disk)
        self.db.executescript("""
            CREATE TABLE IF NOT EXISTS players (
              id INTEGER PRIMARY KEY,
              name TEXT NOT NULL UNIQUE COLLATE NOCASE,
              secret_hash TEXT NOT NULL UNIQUE,
              rating INTEGER NOT NULL, games INTEGER NOT NULL DEFAULT 0,
              wins INTEGER NOT NULL DEFAULT 0, draws INTEGER NOT NULL DEFAULT 0, losses INTEGER NOT NULL DEFAULT 0,
              created INTEGER NOT NULL, seen INTEGER NOT NULL);
            CREATE INDEX IF NOT EXISTS players_by_rating ON players (rating DESC);
            CREATE TABLE IF NOT EXISTS games (
              id INTEGER PRIMARY KEY, white INTEGER, black INTEGER, result TEXT, why TEXT, moves TEXT,
              white_before INTEGER, white_after INTEGER, black_before INTEGER, black_after INTEGER,
              ended INTEGER NOT NULL);
        """)
        self.cache = {}  # id -> Player: one object per player, whatever pages they have open

    COLUMNS = "id, name, rating, games, wins, draws, losses"

    def _player(self, row):
        p = self.cache.get(row[0])
        if p is None:
            p = self.cache[row[0]] = Player(row)
        return p

    def by_secret(self, secret):
        h = hashlib.sha256(secret.encode()).hexdigest()
        row = self.db.execute(f"SELECT {self.COLUMNS} FROM players WHERE secret_hash = ?", (h,)).fetchone()
        if row is None:
            return None
        self.db.execute("UPDATE players SET seen = ? WHERE id = ?", (int(time.time()), row[0]))
        self.db.commit()
        return self._player(row)

    def register(self, name):
        """A new player, and their secret key; None if the name is taken."""
        secret = secrets.token_urlsafe(24)
        now = int(time.time())
        try:
            cur = self.db.execute(
                "INSERT INTO players (name, secret_hash, rating, created, seen) VALUES (?, ?, ?, ?, ?)",
                (name, hashlib.sha256(secret.encode()).hexdigest(), START_RATING, now, now))
            self.db.commit()
        except sqlite3.IntegrityError:
            return None, None
        row = self.db.execute(f"SELECT {self.COLUMNS} FROM players WHERE id = ?", (cur.lastrowid,)).fetchone()
        return self._player(row), secret

    def top(self):
        rows = self.db.execute("SELECT name, rating, games FROM players WHERE games >= ? "
                               "ORDER BY rating DESC, games DESC, id LIMIT ?", (TOP_MIN_GAMES, TOP_SIZE)).fetchall()
        return [list(r) for r in rows]

    def rank(self, p):
        if p.games < TOP_MIN_GAMES:
            return 0
        above = self.db.execute("SELECT COUNT(*) FROM players WHERE games >= ? AND rating > ?",
                                (TOP_MIN_GAMES, p.rating)).fetchone()[0]
        return above + 1

    def record(self, room, result, why, changes):
        w, b = room.players["w"], room.players["b"]
        with self.db:
            if changes:
                for seat, p in (("w", w), ("b", b)):
                    score = {"1-0": 1, "0-1": 0}.get(result, 0.5)
                    if seat == "b":
                        score = 1 - score
                    col = "wins" if score == 1 else "losses" if score == 0 else "draws"
                    self.db.execute(f"UPDATE players SET rating = ?, games = games + 1, {col} = {col} + 1 "
                                    f"WHERE id = ?", (changes[seat][1], p.id))
                    p.rating = changes[seat][1]
                    p.games += 1
                    setattr(p, col, getattr(p, col) + 1)
            self.db.execute(
                "INSERT INTO games (white, black, result, why, moves, white_before, white_after, black_before, "
                "black_after, ended) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                (w.id, b.id, result, why, " ".join(room.moves),
                 changes["w"][0] if changes else w.rating, changes["w"][1] if changes else w.rating,
                 changes["b"][0] if changes else b.rating, changes["b"][1] if changes else b.rating,
                 int(time.time())))


players = None


def elo(rating, opponent, score, games):
    k = 40 if games < 30 else 20  # (a new player's rating settles faster)
    expected = 1 / (1 + 10 ** ((opponent - rating) / 400))
    return max(100, round(rating + k * (score - expected)))


# ---------------------------------------------------------------------------
# Games and the people connected

class Room:
    def __init__(self, code, rated):
        self.code = code
        self.rated = rated
        self.tokens = {"w": secrets.token_urlsafe(18), "b": secrets.token_urlsafe(18) if rated else None}
        self.conns = {"w": None, "b": None}
        self.players = {"w": None, "b": None}  # matched games
        self.moves = []
        self.game = ChessGame()
        self.result = None   # (result, why)
        self.changes = None  # ratings: {"w": [before, after], "b": [...]}
        self.clock = {"w": CLOCK_MS, "b": CLOCK_MS} if rated else None
        self.turn_since = time.monotonic()
        self.flag_timer = None
        self.away = {"w": None, "b": None}  # (timer, when) while a player is away from a matched game
        self.draw_offer = None              # the side that offered
        self.touched = time.monotonic()

    def to_move(self):
        return "w" if len(self.moves) % 2 == 0 else "b"

    def clock_now(self):
        if not self.clock:
            return None
        c = dict(self.clock)
        running = self.to_move() if self.result is None else "-"
        if running != "-":
            c[running] -= int((time.monotonic() - self.turn_since) * 1000)
        return {"w": max(0, c["w"]), "b": max(0, c["b"]), "run": running}

    def stop_timers(self):
        if self.flag_timer:
            self.flag_timer.cancel()
            self.flag_timer = None
        for seat in "wb":
            if self.away[seat]:
                self.away[seat][0].cancel()
                self.away[seat] = None


class Conn:
    def __init__(self, ws, ip):
        self.ws, self.ip = ws, ip
        self.player = None
        self.room = self.seat = None
        self.seeking = None  # since when


rooms = {}
queue = {}           # Conn -> since: the players waiting for an opponent
recent = {}          # (kind, ip) -> times: new games and names from each address
conns_by_ip = {}
connections = 0


def limited(kind, ip, most, seconds):
    now = time.monotonic()
    times = [t for t in recent.get((kind, ip), []) if now - t < seconds]
    recent[(kind, ip)] = times
    if len(times) >= most:
        return True
    times.append(now)
    return False


def new_code():
    while True:
        code = "".join(secrets.choice(CODE_ALPHABET) for _ in range(CODE_LENGTH))
        if code not in rooms:
            return code


def same_token(a, b):
    return bool(a) and bool(b) and secrets.compare_digest(a.encode(), b.encode())


async def send(ws, msg):
    if ws is not None:
        try:
            await ws.send(json.dumps(msg))
        except websockets.ConnectionClosed:
            pass


async def to_seat(room, seat, msg):
    c = room.conns[seat]
    if c is not None:
        await send(c.ws, msg)


def end_fields(room):
    return {"result": room.result[0], "why": room.result[1], "ratings": room.changes}


def room_message(room, seat):
    """What a player gets on joining or coming back: their side and the game so far."""
    m = {"t": "room", "room": room.code, "seat": seat, "token": room.tokens[seat], "moves": room.moves,
         "full": room.tokens["b"] is not None, "peer": room.conns[other(seat)] is not None, "rated": room.rated}
    if room.rated:
        m["players"] = {s: room.players[s].public() for s in "wb"}
        m["clock"] = room.clock_now()
    if room.result:
        m["end"] = end_fields(room)
    elif room.draw_offer == other(seat):
        m["draw"] = True
    away = room.away[other(seat)]
    if away:
        m["forfeit"] = max(0, round(away[1] - time.monotonic()))
    return m


async def end_game(room, result, why):
    if room.result:
        return
    if room.rated and len(room.moves) < 2 and why in ("resignation", "timeout", "abandoned", "agreement"):
        result, why = "*", "aborted"  # before both players moved: no rating
    if room.clock:
        room.clock = {k: v for k, v in room.clock_now().items() if k in "wb"}
    room.result = (result, why)
    room.stop_timers()
    room.game.close()  # (no more moves: the position isn't needed)
    room.draw_offer = None
    if room.rated and players:
        if result != "*":
            w, b = room.players["w"], room.players["b"]
            score = {"1-0": 1.0, "0-1": 0.0}.get(result, 0.5)
            room.changes = {"w": [w.rating, elo(w.rating, b.rating, score, w.games)],
                            "b": [b.rating, elo(b.rating, w.rating, 1 - score, b.games)]}
        players.record(room, result, why, room.changes)
        log.info("game %s: %s (%s) after %d moves", room.code, result, why, len(room.moves))
    msg = dict({"t": "end"}, **end_fields(room))
    for seat in "wb":
        await to_seat(room, seat, msg)


def schedule_flag(room):
    if room.flag_timer:
        room.flag_timer.cancel()
        room.flag_timer = None
    if not room.clock or room.result:
        return
    side, ply = room.to_move(), len(room.moves)

    def flag():
        if room.result is None and len(room.moves) == ply:
            asyncio.ensure_future(end_game(room, loss_for(side), "timeout"))

    room.flag_timer = asyncio.get_running_loop().call_later(room.clock[side] / 1000 + 0.05, flag)


async def detach(c):
    """The player's page left the game (closed, reloaded, or chose something else)."""
    if c in queue:
        del queue[c]
    c.seeking = None
    room, seat = c.room, c.seat
    c.room = c.seat = None
    if room is None or room.conns.get(seat) is not c:
        return
    room.conns[seat] = None
    room.touched = time.monotonic()
    msg = {"t": "peer", "here": False}
    if room.rated and room.result is None:
        def abandon():
            if room.result is None and room.conns[seat] is None:
                asyncio.ensure_future(end_game(room, loss_for(seat), "abandoned"))
        room.away[seat] = (asyncio.get_running_loop().call_later(AWAY_SECONDS, abandon),
                           time.monotonic() + AWAY_SECONDS)
        msg["forfeit"] = AWAY_SECONDS
    await to_seat(room, other(seat), msg)


async def attach(c, room, seat):
    if c.room is room and c.seat == seat:  # already here: the game as it stands
        return await send(c.ws, room_message(room, seat))
    await detach(c)
    old = room.conns[seat]
    room.conns[seat] = c
    c.room, c.seat = room, seat
    room.touched = time.monotonic()
    if room.away[seat]:
        room.away[seat][0].cancel()
        room.away[seat] = None
    if old is not None and old is not c:
        old.room = old.seat = None
        await old.ws.close(4000, "opened elsewhere")  # the same player, from another page
    await send(c.ws, room_message(room, seat))
    await to_seat(room, other(seat), {"t": "peer", "here": True})


async def start_match(a, b):
    for c in (a, b):
        queue.pop(c, None)
        c.seeking = None
    if random.random() < 0.5:
        a, b = b, a
    room = Room(new_code(), rated=True)
    rooms[room.code] = room
    room.players = {"w": a.player, "b": b.player}
    log.info("game %s: %s (%d) - %s (%d)", room.code, a.player.name, a.player.rating, b.player.name, b.player.rating)
    for c, seat in ((a, "w"), (b, "b")):
        room.conns[seat] = c
        c.room, c.seat = room, seat
    room.turn_since = time.monotonic()
    schedule_flag(room)
    for seat in "wb":
        await to_seat(room, seat, room_message(room, seat))


async def matchmaker():
    """Pairs the players waiting for an opponent, closest ratings first. The range of
    ratings a player takes on widens the longer they wait."""
    while True:
        await asyncio.sleep(1)
        now = time.monotonic()
        waiting = sorted((c for c in queue if c.player), key=lambda c: c.player.rating)
        reach = lambda c: 100 + 10 * (now - queue[c])  # noqa: E731 (100 points, +100 every 10 s)
        i = 0
        while i < len(waiting) - 1:
            a = waiting[i]
            for j in range(i + 1, min(i + 6, len(waiting))):
                b = waiting[j]
                if b.player.id == a.player.id or (b.ip == a.ip and not allow_same_ip):
                    continue
                if a not in queue or b not in queue:  # (left while others were being paired)
                    continue
                if abs(a.player.rating - b.player.rating) <= max(reach(a), reach(b)):
                    del waiting[j]
                    await start_match(a, b)
                    break
            i += 1


# ---------------------------------------------------------------------------

async def handle(c, msg):
    kind = msg.get("t")
    room, seat = c.room, c.seat

    if kind == "hello":  # the browser's secret key, if it has one
        p = players.by_secret(str(msg.get("secret", ""))[:64]) if players and msg.get("secret") else None
        c.player = p
        await send(c.ws, dict({"t": "me"}, **({"name": p.name, "rating": p.rating, "games": p.games} if p else {})))

    elif kind == "register":
        name = str(msg.get("name", "")).strip()
        if not players:
            return await send(c.ws, {"t": "notice", "why": "This server keeps no names."})
        if not NAME.match(name):
            return await send(c.ws, {"t": "notice", "why": "3 to 16 letters, digits, _ or -, please."})
        if limited("name", c.ip, NAMES_PER_IP_PER_DAY, 86400):
            return await send(c.ws, {"t": "notice", "why": "Too many new names from here today."})
        p, secret = players.register(name)
        if p is None:
            return await send(c.ws, {"t": "notice", "why": f"The name {name} is taken."})
        c.player = p
        log.info("player %s registered", name)
        await send(c.ws, {"t": "me", "name": p.name, "rating": p.rating, "games": p.games, "secret": secret})

    elif kind == "seek":
        if not rules or not players:
            return await send(c.ws, {"t": "notice", "why": "Games against other players aren't available here."})
        if c.player is None:
            return await send(c.ws, {"t": "need_name"})
        if room and room.result is None:
            return await send(c.ws, {"t": "notice", "why": "Finish this game first."})
        await detach(c)
        for other_c in [o for o in queue if o.player is c.player]:  # (the same player in another page)
            queue.pop(other_c, None)
            other_c.seeking = None
        c.seeking = time.monotonic()
        queue[c] = c.seeking
        await send(c.ws, {"t": "seeking", "waiting": len(queue)})

    elif kind == "unseek":
        queue.pop(c, None)
        c.seeking = None

    elif kind == "leave":
        await detach(c)

    elif kind == "top":
        if not players:
            return await send(c.ws, {"t": "top", "players": []})
        me = None
        if c.player:
            me = {"name": c.player.name, "rating": c.player.rating, "games": c.player.games,
                  "rank": players.rank(c.player)}
        await send(c.ws, {"t": "top", "players": players.top(), "me": me, "min": TOP_MIN_GAMES})

    elif kind == "create":  # a game with a friend
        if len(rooms) >= MAX_ROOMS:
            return await send(c.ws, {"t": "error", "why": "The server has too many games right now. Try again later."})
        if limited("create", c.ip, CREATES_PER_IP_PER_HOUR, 3600):
            return await send(c.ws, {"t": "error", "why": "Too many new games from here. Try again in an hour."})
        r = Room(new_code(), rated=False)
        rooms[r.code] = r
        await attach(c, r, "w")

    elif kind == "join":
        code = str(msg.get("room", ""))[:16].upper()
        token = str(msg.get("token", ""))[:64]
        r = rooms.get(code)
        if r is None:
            return await send(c.ws, {"t": "error", "why": "There is no game at this link (unfinished games end "
                                                          "after a day without moves)."})
        s = next((s for s in "wb" if same_token(r.tokens[s], token)), None)
        if s is None:
            if r.tokens["b"] is not None:
                return await send(c.ws, {"t": "error", "why": "This game already has two players."})
            s = "b"
            r.tokens["b"] = secrets.token_urlsafe(18)
        await attach(c, r, s)

    elif room is None:
        return

    elif kind == "move":
        room.touched = time.monotonic()
        uci, ply = msg.get("uci"), msg.get("ply")
        if not isinstance(uci, str) or not UCI.match(uci) or not isinstance(ply, int):
            return
        if room.result or seat != room.to_move() or ply != len(room.moves) or len(room.moves) >= MAX_PLIES:
            return await send(c.ws, room_message(room, seat))  # out of step: the game as it stands
        left = None
        if room.clock:
            left = room.clock[seat] - int((time.monotonic() - room.turn_since) * 1000)
            if left <= 0:
                return await end_game(room, loss_for(seat), "timeout")
        verdict = room.game.play(uci)
        if verdict < 0:
            return await send(c.ws, room_message(room, seat))  # not legal: the game as it stands
        room.moves.append(uci)
        room.draw_offer = None  # (a move answers a draw offer)
        if room.clock:
            room.clock[seat] = left + INCREMENT_MS
            room.turn_since = time.monotonic()
            schedule_flag(room)
        clock = room.clock_now()
        await to_seat(room, other(seat), dict({"t": "move", "uci": uci, "ply": ply}, **({"clock": clock} if clock else {})))
        if clock:
            await send(c.ws, {"t": "clock", "clock": clock})
        if verdict > 0:
            await end_game(room, *ENDINGS[verdict])

    elif kind == "resign":
        if room.result is None:
            await end_game(room, loss_for(seat), "resignation")

    elif kind == "draw":  # an offer, or accepting the other side's
        if room.result is None:
            if room.draw_offer == other(seat):
                await end_game(room, "1/2-1/2", "agreement")
            elif room.draw_offer is None:
                room.draw_offer = seat
                await to_seat(room, other(seat), {"t": "draw"})

    elif kind == "decline":
        if room.result is None and room.draw_offer == other(seat):
            room.draw_offer = None
            await to_seat(room, other(seat), {"t": "declined"})

    elif kind == "new":  # games with a friend: start again
        if not room.rated:
            room.touched = time.monotonic()
            room.moves = []
            room.game.close()
            room.game = ChessGame()
            room.result = None
            room.draw_offer = None
            await to_seat(room, other(seat), {"t": "new"})


async def player(ws, path=None):
    global connections
    ip = ws.request_headers.get("X-Real-IP") or (ws.remote_address or ("?",))[0]
    if connections >= MAX_CONNECTIONS or conns_by_ip.get(ip, 0) >= MAX_CONNECTIONS_PER_IP:
        await ws.close(4002, "busy")
        return
    connections += 1
    conns_by_ip[ip] = conns_by_ip.get(ip, 0) + 1
    c = Conn(ws, ip)
    try:
        async for raw in ws:
            try:
                msg = json.loads(raw)
                if not isinstance(msg, dict):
                    break
            except ValueError:
                break
            await handle(c, msg)
    except websockets.ConnectionClosed:
        pass
    finally:
        connections -= 1
        conns_by_ip[ip] -= 1
        if not conns_by_ip[ip]:
            del conns_by_ip[ip]
        await detach(c)


async def sweep():
    while True:
        await asyncio.sleep(60)
        now = time.monotonic()
        for code, r in list(rooms.items()):
            idle = now - r.touched
            if r.conns["w"] is None and r.conns["b"] is None and (
                    idle > ROOM_IDLE_SECONDS or (r.result and idle > ENDED_ROOM_SECONDS)):
                r.stop_timers()
                r.game.close()
                del rooms[code]
        for key in [k for k, t in recent.items() if not t or now - t[-1] > 86400]:
            del recent[key]


def static_files(root):
    """For local tests: serves the page itself, with the headers the game's threads need."""
    root = os.path.realpath(root)
    types = {".wasm": "application/wasm", ".js": "text/javascript", ".data": "application/octet-stream"}

    async def process_request(path, headers):
        p = path.split("?", 1)[0]
        if p == "/ws":
            return None
        if p.endswith("/"):
            p += "index.html"
        f = os.path.realpath(os.path.join(root, p.lstrip("/")))
        if not f.startswith(root + os.sep) or not os.path.isfile(f):
            return http.HTTPStatus.NOT_FOUND, [("Content-Type", "text/plain")], b"not found\n"
        ext = os.path.splitext(f)[1]
        ctype = types.get(ext) or mimetypes.guess_type(f)[0] or "application/octet-stream"
        with open(f, "rb") as fh:
            body = fh.read()
        return http.HTTPStatus.OK, [("Content-Type", ctype), ("Cross-Origin-Opener-Policy", "same-origin"),
                                    ("Cross-Origin-Embedder-Policy", "require-corp"),
                                    ("Cache-Control", "no-cache")], body

    return process_request


async def main():
    global rules, players, allow_same_ip
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description="The server for Living Marble's online games.")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--rules", default=os.path.join(here, "liblmrules.so"),
                    help="the chess rules library (web/chess_rules.cpp); without it moves aren't checked "
                         "and there are no matched games")
    ap.add_argument("--db", default="players.db", help="the players' database (SQLite); 'none': no names")
    ap.add_argument("--static", metavar="FOLDER", help="also serve the web page from FOLDER (local tests)")
    ap.add_argument("--allow-same-ip", action="store_true",
                    help="pair players from the same address (local tests)")
    ap.add_argument("--per-ip", type=int, default=MAX_CONNECTIONS_PER_IP,
                    help=f"connections from one address at most (default {MAX_CONNECTIONS_PER_IP})")
    ap.add_argument("--clock", type=float, default=CLOCK_MS / 60000, help="matched games: minutes each (default 10)")
    ap.add_argument("--increment", type=float, default=INCREMENT_MS / 1000,
                    help="matched games: seconds added each move (default 5)")
    ap.add_argument("--away", type=float, default=AWAY_SECONDS,
                    help=f"matched games: seconds away before losing (default {AWAY_SECONDS})")
    ap.add_argument("--names-per-ip", type=int, default=NAMES_PER_IP_PER_DAY,
                    help=f"new names from one address a day at most (default {NAMES_PER_IP_PER_DAY})")
    ap.add_argument("--top-min", type=int, default=TOP_MIN_GAMES,
                    help=f"rated games to be on the leaderboard (default {TOP_MIN_GAMES})")
    args = ap.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    logging.getLogger("websockets").setLevel(logging.WARNING)  # (not every connection)
    allow_same_ip = args.allow_same_ip
    globals().update(MAX_CONNECTIONS_PER_IP=args.per_ip, CLOCK_MS=int(args.clock * 60000),
                     INCREMENT_MS=int(args.increment * 1000), AWAY_SECONDS=args.away, TOP_MIN_GAMES=args.top_min,
                     NAMES_PER_IP_PER_DAY=args.names_per_ip)
    try:
        rules = Rules(args.rules)
    except OSError as e:
        log.warning("no chess rules (%s): moves aren't checked, no matched games", e)
    if args.db != "none":
        players = Players(args.db)
    asyncio.ensure_future(sweep())
    asyncio.ensure_future(matchmaker())
    async with websockets.serve(player, args.host, args.port, max_size=MAX_MESSAGE, max_queue=16,
                                process_request=static_files(args.static) if args.static else None):
        log.info("Living Marble server on %s:%d%s", args.host, args.port,
                 f", serving {args.static}" if args.static else "")
        await asyncio.Future()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
