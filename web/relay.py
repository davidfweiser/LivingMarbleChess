#!/usr/bin/env python3
"""Living Marble - the server for online games on the web page.

Browsers connect with a WebSocket at /ws (behind nginx). The server
- passes the moves between the two players of a game, checks each one with the game's own
  chess rules (web/chess_rules.cpp, loaded as a library) and decides how games end;
- keeps the lobby (Play someone online): the players there with their pictures and records,
  who is waiting for a game, invitations to play from one player to another, and each
  player's card with statistics from their games;
- pairs players who wait for an opponent by rating;
- keeps players' names, pictures and ratings (SQLite) and the leaderboard.

Two kinds of game:
- with a friend: one player creates it and sends the link (?join=CODE). Untimed, unrated.
- matched: two players from the queue, 10 minutes each plus 5 seconds a move. Rated (Elo).
  Resigning, running out of time or staying away for 90 seconds loses; a game that ends
  before both players have moved is aborted (unrated).

Players have accounts: a name and a password (kept as a scrypt hash), and a recovery code
for a forgotten password, shown once. A browser that logs in gets a session key. Each side of
a game has a token of its own, so a reload or a lost connection comes back to the same side.

    python3 relay.py --rules liblmrules.so --db players.db
    python3 relay.py --rules ... --db ... --static build-web --allow-same-ip   (local tests)

Needs the websockets package, version 10 or later (Ubuntu: apt install python3-websockets).
"""
import argparse
import asyncio
import ctypes
import hashlib
import http
import ipaddress
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

# From websockets 14 on, serve() is a new implementation, with another interface for the
# request's headers and for answering plain HTTP requests (Ubuntu 24.04 has 10.4, the original)
NEW_WEBSOCKETS = websockets.serve.__module__.startswith("websockets.asyncio")

CODE_ALPHABET = "ABCDEFGHJKMNPQRSTUVWXYZ23456789"  # no 0/O, 1/I/L
CODE_LENGTH = 8                  # (31^8 codes: far too many to guess one, at a few guesses a minute)
MAX_CONNECTIONS = 20000
MAX_CONNECTIONS_PER_IP = 40
MAX_ROOMS = 20000
ROOM_IDLE_SECONDS = 24 * 3600    # an unfinished game nobody has touched for this long goes
ENDED_ROOM_SECONDS = 600         # a finished one, 10 minutes after both players left
CREATES_PER_IP_PER_HOUR = 30
NAMES_PER_IP_PER_DAY = 20         # new accounts (a school or an office may share one address)
LOGIN_FAILS_PER_IP = 30           # wrong passwords or recovery codes in 10 minutes: from one address,
LOGIN_FAILS_PER_NAME = 10         # for one name from one address,
LOGIN_FAILS_PER_NAME_ALL = 100    # and for one name from everywhere (passwords and codes counted apart)
JOIN_FAILS_PER_IP = 20            # links to games that aren't there, in 10 minutes, from one address
MESSAGES_PER_SECOND = 10          # what one page may send: this many a second on average,
MESSAGES_BURST = 40               # and this many at once
MAX_PLIES = 1200
MAX_MESSAGE = 2048
CLOCK_MS = 10 * 60 * 1000        # matched games: 10 minutes each ...
INCREMENT_MS = 5000              # ... plus 5 seconds a move
AWAY_SECONDS = 90                # away this long from a matched game: lost
START_RATING = 1200
TOP_MIN_GAMES = 5                # on the leaderboard after this many rated games
TOP_SIZE = 20
LOBBY_SIZE = 80                  # the lobby lists this many players at most,
LOBBY_GAMES = 30                 # and this many of the games going on
CHALLENGE_SECONDS = 30           # an invitation to play waits this long for an answer
CHALLENGES_PER_IP = 30           # invitations from one address in 10 minutes
ASK_AGAIN_SECONDS = 60           # after a "no thanks", the same player can't ask again for this long
PROFILES_PER_IP = 60             # players' cards looked at from one address in a minute
PROFILE_GAMES = 2000             # a card's statistics come from this many of the player's latest games
PROFILE_SECONDS = 30             # (and are kept this long, or until the player's next game)
# The pictures players choose for themselves (web/avatars.js draws them, with the same names)
ICONS = frozenset("""knight king queen rook pawn dragon wizard shield castle unicorn ufo alien rocket planet
                     psychic robot fox owl cat octopus frog panda penguin lion ghost pirate ninja flame lightning
                     crystal mushroom cactus""".split())
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
# Players: accounts (a name and a password), the browsers they're logged in on (sessions),
# and their ratings. Passwords are kept as scrypt hashes; session keys and recovery codes,
# being long and random, as SHA-256 hashes.

PASSWORD_MIN, PASSWORD_MAX = 8, 128
SESSION_DAYS = 180  # a browser that hasn't been back for this long is logged out


def hash_password(password):
    salt = secrets.token_bytes(16)
    h = hashlib.scrypt(password.encode(), salt=salt, n=2 ** 14, r=8, p=1, dklen=32)
    return f"scrypt$16384$8$1${salt.hex()}${h.hex()}"


def check_password(password, stored):
    try:
        _, n, r, p, salt, h = stored.split("$")
        got = hashlib.scrypt(password.encode(), salt=bytes.fromhex(salt), n=int(n), r=int(r), p=int(p), dklen=32)
        return secrets.compare_digest(got.hex(), h)
    except (ValueError, AttributeError):
        return False


def sha(text):
    return hashlib.sha256(text.encode()).hexdigest()


def new_recovery_code():
    return "-".join("".join(secrets.choice(CODE_ALPHABET) for _ in range(4)) for _ in range(4))


def normal_code(code):  # (however the player typed it)
    return "".join(ch for ch in str(code).upper() if ch.isalnum())


class Player:
    def __init__(self, row):
        self.id, self.name, self.rating, self.games, self.wins, self.draws, self.losses, self.icon, self.haspw = row
        self.haspw = bool(self.haspw)

    def public(self):
        return {"name": self.name, "rating": self.rating, "icon": self.icon}

    def brief(self):  # (in the lobby: the picture, the rating and the record of rated games)
        return {"name": self.name, "icon": self.icon, "rating": self.rating, "games": self.games,
                "wins": self.wins, "draws": self.draws, "losses": self.losses}

    def me(self):
        return dict(self.brief(), haspw=self.haspw)


class Players:
    def __init__(self, path):
        self.path = path
        self.db = sqlite3.connect(path)
        self.db.execute("PRAGMA journal_mode=WAL")
        self.db.execute("PRAGMA synchronous=NORMAL")  # (safe with WAL; far fewer waits for the disk)
        self._schema()
        self.cache = {}  # id -> Player: one object per player, whatever pages they have open
        self.cards = {}  # id -> (when, card): players' statistics, as profile() worked them out

    def _schema(self):
        db = self.db
        version = db.execute("PRAGMA user_version").fetchone()[0]
        old = db.execute("SELECT name FROM sqlite_master WHERE type = 'table' AND name = 'players'").fetchone()
        with db:
            if old and version < 2:
                # from before passwords: each player's browser key becomes a session (they stay
                # logged in there, and choose a password the first time they need one)
                db.executescript("""
                    CREATE TABLE sessions (token TEXT PRIMARY KEY, player INTEGER NOT NULL,
                                           created INTEGER NOT NULL, used INTEGER NOT NULL);
                    INSERT INTO sessions (token, player, created, used) SELECT secret_hash, id, created, seen FROM players;
                    CREATE TABLE players_new (
                      id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE COLLATE NOCASE, password TEXT, recovery TEXT,
                      rating INTEGER NOT NULL, games INTEGER NOT NULL DEFAULT 0, wins INTEGER NOT NULL DEFAULT 0,
                      draws INTEGER NOT NULL DEFAULT 0, losses INTEGER NOT NULL DEFAULT 0,
                      created INTEGER NOT NULL, seen INTEGER NOT NULL);
                    INSERT INTO players_new (id, name, rating, games, wins, draws, losses, created, seen)
                      SELECT id, name, rating, games, wins, draws, losses, created, seen FROM players;
                    DROP TABLE players;
                    ALTER TABLE players_new RENAME TO players;
                    ALTER TABLE games ADD COLUMN rated INTEGER NOT NULL DEFAULT 1;
                    ALTER TABLE games ADD COLUMN started INTEGER;
                """)
            db.executescript("""
                CREATE TABLE IF NOT EXISTS players (
                  id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE COLLATE NOCASE, password TEXT, recovery TEXT,
                  rating INTEGER NOT NULL, games INTEGER NOT NULL DEFAULT 0, wins INTEGER NOT NULL DEFAULT 0,
                  draws INTEGER NOT NULL DEFAULT 0, losses INTEGER NOT NULL DEFAULT 0,
                  created INTEGER NOT NULL, seen INTEGER NOT NULL);
                CREATE INDEX IF NOT EXISTS players_by_rating ON players (rating DESC);
                CREATE TABLE IF NOT EXISTS sessions (token TEXT PRIMARY KEY, player INTEGER NOT NULL,
                                                     created INTEGER NOT NULL, used INTEGER NOT NULL);
                CREATE INDEX IF NOT EXISTS sessions_by_player ON sessions (player);
                CREATE TABLE IF NOT EXISTS games (
                  id INTEGER PRIMARY KEY, white INTEGER, black INTEGER, result TEXT, why TEXT, moves TEXT,
                  white_before INTEGER, white_after INTEGER, black_before INTEGER, black_after INTEGER,
                  ended INTEGER NOT NULL, rated INTEGER NOT NULL DEFAULT 1, started INTEGER);
                CREATE INDEX IF NOT EXISTS games_by_end ON games (ended);
                CREATE INDEX IF NOT EXISTS games_by_white ON games (white);  -- (each player's games,
                CREATE INDEX IF NOT EXISTS games_by_black ON games (black);  --  for the admin app)
            """)
            # version 3: the picture each player chooses (web/avatars.js), none at first
            if "icon" not in [r[1] for r in db.execute("PRAGMA table_info(players)")]:
                db.execute("ALTER TABLE players ADD COLUMN icon TEXT")
            db.execute("PRAGMA user_version = 3")

    # a Player's columns (by_session() puts its own in place of the last)
    COLUMNS = "id, name, rating, games, wins, draws, losses, icon, password IS NOT NULL"

    def _player(self, row):
        p = self.cache.get(row[0])
        if p is None:
            p = self.cache[row[0]] = Player(row)
        p.haspw = bool(row[8])
        return p

    def _by_name(self, name):
        return self.db.execute(f"SELECT {self.COLUMNS}, password, recovery FROM players WHERE name = ?",
                               (name,)).fetchone()

    def _session(self, player_id):
        key = secrets.token_urlsafe(24)
        now = int(time.time())
        self.db.execute("INSERT INTO sessions (token, player, created, used) VALUES (?, ?, ?, ?)",
                        (sha(key), player_id, now, now))
        return key

    def by_session(self, key):
        now = int(time.time())
        row = self.db.execute(f"SELECT {', '.join('p.' + c for c in self.COLUMNS.split(', ')[:-1])}, "
                              f"p.password IS NOT NULL FROM sessions s JOIN players p ON p.id = s.player "
                              f"WHERE s.token = ?", (sha(key),)).fetchone()
        if row is None:
            return None
        with self.db:
            self.db.execute("UPDATE sessions SET used = ? WHERE token = ?", (now, sha(key)))
            self.db.execute("UPDATE players SET seen = ? WHERE id = ?", (now, row[0]))
        return self._player(row)

    def name_taken(self, name):
        return self._by_name(name) is not None

    def signup(self, name, password_hash):
        """A new player: (player, session key, recovery code), or None if the name is taken."""
        code = new_recovery_code()
        now = int(time.time())
        try:
            with self.db:
                cur = self.db.execute("INSERT INTO players (name, password, recovery, rating, created, seen) "
                                      "VALUES (?, ?, ?, ?, ?, ?)",
                                      (name, password_hash, sha(normal_code(code)), START_RATING, now, now))
                key = self._session(cur.lastrowid)
        except sqlite3.IntegrityError:
            return None
        row = self.db.execute(f"SELECT {self.COLUMNS} FROM players WHERE id = ?", (cur.lastrowid,)).fetchone()
        return self._player(row), key, code

    def stored_password(self, name):
        """(player id, password hash or None) for a name, or None."""
        row = self._by_name(name)
        return (row[0], row[9]) if row else None

    def login(self, player_id):
        """A new session for a player whose password checked out."""
        with self.db:
            key = self._session(player_id)
            self.db.execute("UPDATE players SET seen = ? WHERE id = ?", (int(time.time()), player_id))
        row = self.db.execute(f"SELECT {self.COLUMNS} FROM players WHERE id = ?", (player_id,)).fetchone()
        return self._player(row), key

    def logout(self, key):
        with self.db:
            self.db.execute("DELETE FROM sessions WHERE token = ?", (sha(key),))

    def password_of(self, player):
        return self.db.execute("SELECT password FROM players WHERE id = ?", (player.id,)).fetchone()[0]

    def set_password(self, player, password_hash, keep_key):
        """A new password; the player's other browsers are logged out."""
        with self.db:
            self.db.execute("UPDATE players SET password = ? WHERE id = ?", (password_hash, player.id))
            self.db.execute("DELETE FROM sessions WHERE player = ? AND token != ?", (player.id, sha(keep_key or "")))
        player.haspw = True

    def check_recovery(self, name, code):
        row = self._by_name(name)
        if row is None or not row[10]:
            return None
        return row[0] if secrets.compare_digest(row[10], sha(normal_code(code))) else None

    def recover(self, player_id, password_hash):
        """A forgotten password replaced: every browser logged out, a new session and a new recovery code."""
        code = new_recovery_code()
        with self.db:
            self.db.execute("UPDATE players SET password = ?, recovery = ? WHERE id = ?",
                            (password_hash, sha(normal_code(code)), player_id))
            self.db.execute("DELETE FROM sessions WHERE player = ?", (player_id,))
            key = self._session(player_id)
        row = self.db.execute(f"SELECT {self.COLUMNS} FROM players WHERE id = ?", (player_id,)).fetchone()
        return self._player(row), key, code

    def new_code(self, player_id):
        code = new_recovery_code()
        with self.db:
            self.db.execute("UPDATE players SET recovery = ? WHERE id = ?", (sha(normal_code(code)), player_id))
        return code

    def delete(self, player_id):
        """A player deleted (the admin app): their sessions with them. Their games stay, without
        them, so that their opponents keep theirs."""
        with self.db:
            self.db.execute("UPDATE games SET white = NULL WHERE white = ?", (player_id,))
            self.db.execute("UPDATE games SET black = NULL WHERE black = ?", (player_id,))
            self.db.execute("DELETE FROM sessions WHERE player = ?", (player_id,))
            gone = self.db.execute("DELETE FROM players WHERE id = ?", (player_id,)).rowcount
        # (a new player may be given the same id: nothing of this one may stay behind)
        self.cache.pop(player_id, None)
        self.cards.clear()  # (their opponents' cards name them in their latest games)
        return gone > 0

    def set_icon(self, player, icon):
        with self.db:
            self.db.execute("UPDATE players SET icon = ? WHERE id = ?", (icon, player.id))
        player.icon = icon
        self.cards.clear()  # (cards show their opponents' pictures too)

    def profile(self, player_id):
        """A player's card in the lobby: their rating and record, and what their latest games say
        about them (form, streaks, checkmates, a favourite first move ...). None if there's no such player."""
        now = time.monotonic()
        kept = self.cards.get(player_id)
        if kept and now - kept[0] < PROFILE_SECONDS:
            return kept[1]
        row = self.db.execute("SELECT name, icon, rating, games, wins, draws, losses, created FROM players "
                              "WHERE id = ?", (player_id,)).fetchone()
        if row is None:
            return None
        name, icon, rating, rated, wins, draws, losses, created = row
        finished = "result IN ('1-0', '0-1', '1/2-1/2')"  # (not aborted)
        played, played_rated = self.db.execute(
            f"SELECT COUNT(*), TOTAL(rated) FROM games WHERE (white = ? OR black = ?) AND {finished}",
            (player_id, player_id)).fetchone()
        games = self.db.execute(
            "SELECT white, black, result, why, SUBSTR(moves, 1, 4), "
            "CASE WHEN moves = '' THEN 0 ELSE LENGTH(moves) - LENGTH(REPLACE(moves, ' ', '')) + 1 END, "
            f"rated, ended, white_before, white_after, black_before, black_after FROM games "
            f"WHERE (white = ? OR black = ?) AND {finished} ORDER BY ended DESC, id DESC LIMIT ?",
            (player_id, player_id, PROFILE_GAMES)).fetchall()
        results, recent, lengths, openings = [], [], [], {}
        mates, quickest, peak = 0, None, rating
        for w, b, result, why, first, plies, is_rated, ended, wb, wa, bb, ba in games:
            white = w == player_id
            score = {"1-0": 1.0, "0-1": 0.0}.get(result, 0.5)
            if not white:
                score = 1 - score
            r = "W" if score == 1 else "L" if score == 0 else "D"
            results.append(r)
            moves = (plies + 1) // 2
            if moves:
                lengths.append(moves)
            if r == "W" and why == "checkmate":
                mates += 1
                quickest = moves if quickest is None else min(quickest, moves)
            if white and len(first) == 4:
                openings[first] = openings.get(first, 0) + 1
            if is_rated:
                peak = max(peak, (wa if white else ba) or 0)
            if len(recent) < 5:
                change = ((wa - wb) if white else (ba - bb)) if is_rated else None
                recent.append({"opp": b if white else w, "r": r, "why": why, "moves": moves, "rated": bool(is_rated),
                               "change": change, "ended": ended, "white": white})
        streak = 0  # the latest results alike: 3 wins in a row ...
        while streak < len(results) and results[streak] == results[0]:
            streak += 1
        best = run = 0  # the most wins in a row
        for r in reversed(results):
            run = run + 1 if r == "W" else 0
            best = max(best, run)
        favourite = None  # the first move they play most with White (from 3 games on)
        if openings:
            uci, times = max(openings.items(), key=lambda kv: kv[1])
            if times >= 3:
                as_white = sum(openings.values())
                san = ("N" if uci[1] == "1" else "") + uci[2:]  # (a first move is a pawn's or a knight's)
                favourite = {"move": san, "times": times, "of": as_white}
        opponents = {o["opp"] for o in recent if o["opp"] is not None}
        named = {}
        if opponents:
            marks = ", ".join("?" * len(opponents))
            for pid, pname, picon, prating in self.db.execute(
                    f"SELECT id, name, icon, rating FROM players WHERE id IN ({marks})", tuple(opponents)):
                named[pid] = {"name": pname, "icon": picon, "rating": prating}
        for o in recent:
            o["opp"] = named.get(o["opp"])  # (None: a deleted player)
        card = {"name": name, "icon": icon, "rating": rating, "peak": peak, "games": rated, "wins": wins,
                "draws": draws, "losses": losses, "rank": self.rank_of(rating, rated), "since": created,
                "played": played, "friendly": played - int(played_rated), "form": "".join(results[:10]),
                "streak": [results[0], streak] if results else None, "best_streak": best, "mates": mates,
                "quickest": quickest, "opening": favourite,
                "avg_moves": round(sum(lengths) / len(lengths)) if lengths else None, "recent": recent}
        if len(self.cards) > 2000:
            self.cards.clear()
        self.cards[player_id] = (now, card)
        return card

    def head_to_head(self, player_id, other_id):
        """[won, drawn, lost]: player_id's games against other_id (aborted ones aside)."""
        out = [0, 0, 0]
        for white, result in self.db.execute(
                "SELECT white, result FROM games WHERE ((white = ? AND black = ?) OR (white = ? AND black = ?)) "
                "AND result IN ('1-0', '0-1', '1/2-1/2')", (player_id, other_id, other_id, player_id)):
            if result == "1/2-1/2":
                out[1] += 1
            else:
                out[0 if (result == "1-0") == (white == player_id) else 2] += 1
        return out

    def forget_old_sessions(self):
        with self.db:
            self.db.execute("DELETE FROM sessions WHERE used < ?", (int(time.time()) - SESSION_DAYS * 86400,))

    def top(self):
        rows = self.db.execute("SELECT name, rating, games FROM players WHERE games >= ? "
                               "ORDER BY rating DESC, games DESC, id LIMIT ?", (TOP_MIN_GAMES, TOP_SIZE)).fetchall()
        return [list(r) for r in rows]

    def rank(self, p):
        return self.rank_of(p.rating, p.games)

    def rank_of(self, rating, games):  # (0: not on the leaderboard yet)
        if games < TOP_MIN_GAMES:
            return 0
        above = self.db.execute("SELECT COUNT(*) FROM players WHERE games >= ? AND rating > ?",
                                (TOP_MIN_GAMES, rating)).fetchone()[0]
        return above + 1

    def record(self, room, result, why, changes):
        """A finished game: rated ones change both players' ratings and records."""
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
                "black_after, ended, rated, started) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                (w.id, b.id, result, why, " ".join(room.moves),
                 changes["w"][0] if changes else w.rating, changes["w"][1] if changes else w.rating,
                 changes["b"][0] if changes else b.rating, changes["b"][1] if changes else b.rating,
                 int(time.time()), 1 if room.rated else 0, int(room.created)))
        for p in (w, b):
            self.cards.pop(p.id, None)  # (their cards, with this game)


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
        self.created = time.time()

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
        self.session = None  # the key it logged in with
        self.room = self.seat = None
        self.seeking = None  # since when
        self.challenge = None  # the invitation to play this page has sent, waiting for an answer


rooms = {}
queue = {}           # Conn -> since: the players waiting for an opponent
lobby = {}           # Conn -> since: the pages with the lobby open (Play someone online)
lobby_changed = False  # (its list is sent again to everyone there)
challenges = {}      # id -> Challenge: invitations to play, waiting for an answer
in_game = {}         # player id -> Room: the players in a rated game going on
recent = {}          # (kind, ip) -> times: new games and names from each address
conns_by_ip = {}
connections = 0
all_conns = set()


def address_key(addr):
    """Where a connection comes from, as the limits count it: an IPv4 address as it is, an IPv6
    one by its /64 network (each home or machine has a whole /64, any address in it its own)."""
    try:
        a = ipaddress.ip_address(addr.split("%")[0])
    except ValueError:
        return addr
    if a.version == 6:
        if a.ipv4_mapped:
            return str(a.ipv4_mapped)
        return str(ipaddress.ip_network(f"{a}/64", strict=False))
    return str(a)


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
    await send_text(ws, json.dumps(msg))


async def send_text(ws, text):
    if ws is not None:
        try:
            await ws.send(text)
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
    if room.rated:
        for p in room.players.values():
            if p is not None and in_game.get(p.id) is room:
                del in_game[p.id]
        lobby_change()  # (they're free again, with their new ratings)
    if players and room.players["w"] and room.players["b"]:
        if room.rated and result != "*":
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
    if not room.rated and c.player:  # (friend games are recorded too, with both players logged in)
        room.players[seat] = c.player
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
    """A rated game between two pages: from the queue, a player joining someone who waits, or an
    invitation accepted. Both leave the lobby, and any other invitations to or from them end."""
    waited = {c: queue.get(c) for c in (a, b)}
    for c in (a, b):
        await detach(c)  # (out of the queue, and out of a game with a friend)
        if lobby.pop(c, None) is not None:
            lobby_change()
    for p in (a.player, b.player):
        for ch in [ch for ch in challenges.values() if p is not None and p.id in (ch.asker.id, ch.dst.id)]:
            await drop_challenge(ch, "playing")
    if not all(c in all_conns and c.player for c in (a, b)):  # (one left meanwhile: the other waits on)
        for c in (a, b):
            if c in all_conns and c.player and waited[c] is not None:
                queue[c] = c.seeking = waited[c]
        return
    if random.random() < 0.5:
        a, b = b, a
    room = Room(new_code(), rated=True)
    rooms[room.code] = room
    room.players = {"w": a.player, "b": b.player}
    in_game[a.player.id] = in_game[b.player.id] = room
    lobby_change()
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
                if a.player.id in in_game or b.player.id in in_game:  # (playing on another page)
                    continue
                if a not in queue or b not in queue:  # (left while others were being paired)
                    continue
                if abs(a.player.rating - b.player.rating) <= max(reach(a), reach(b)):
                    del waiting[j]
                    await start_match(a, b)
                    break
            i += 1


# ---------------------------------------------------------------------------
# The lobby (Play someone online): the players there, with their pictures and records, those
# waiting for a game (anyone can join them; the matchmaker pairs them too), and the games going
# on. A player asks another to play (an invitation, answered yes or no within 30 seconds), or
# joins someone who is waiting. Everyone there gets the list again whenever it changes (at most
# once a second).

class Challenge:
    """An invitation to play: from a page in the lobby (src, its player the asker) to a player
    there (dst), who answers from any of their pages in the lobby."""

    def __init__(self, src, dst):
        self.id = secrets.token_urlsafe(9)
        self.src, self.asker, self.dst = src, src.player, dst
        self.timer = None

    def offer(self):  # what the player asked sees
        return {"t": "challenged", "id": self.id, "from": self.asker.brief(), "seconds": CHALLENGE_SECONDS}


def lobby_change():
    global lobby_changed
    lobby_changed = True


def pages_of(player, where):
    return [c for c in where if c.player is not None and c.player.id == player.id]


def lobby_list():
    """The lobby as everyone there sees it: waiting players first (the longest waiting first),
    then the others by rating; and the rated games going on."""
    now = time.monotonic()
    playing = []
    for r in rooms.values():
        if r.rated and r.result is None and r.players["w"] and r.players["b"] and len(playing) < LOBBY_GAMES:
            playing.append({"w": r.players["w"].brief(), "b": r.players["b"].brief(), "moves": (len(r.moves) + 1) // 2})
    here = {}
    for c in list(lobby) + list(queue):
        p = c.player
        if p is None or p.id in in_game:
            continue
        e = here.get(p.id)
        if e is None:
            e = here[p.id] = dict(p.brief(), waiting=None)
        if c in queue:
            e["waiting"] = max(e["waiting"] or 0, int(now - queue[c]))
    listed = sorted(here.values(), key=lambda e: (e["waiting"] is None, -(e["waiting"] or 0), -e["rating"], e["name"]))
    online = len({c.player.id for c in all_conns if c.player})
    return {"t": "lobby", "players": listed[:LOBBY_SIZE], "more": max(0, len(listed) - LOBBY_SIZE), "games": playing,
            "playing": len(in_game) // 2, "online": online, "clock_ms": CLOCK_MS, "inc_ms": INCREMENT_MS}


async def lobby_loop():
    """The list again to everyone in the lobby when it has changed, and every 10 seconds anyway
    (the waiting times, the games' moves)."""
    global lobby_changed
    quiet = 0
    while True:
        await asyncio.sleep(1)
        quiet += 1
        if not lobby or not (lobby_changed or quiet >= 10):
            continue
        lobby_changed, quiet = False, 0
        text = json.dumps(lobby_list())
        for c in list(lobby):
            await send_text(c.ws, text)


async def enter_lobby(c):
    if not players or not rules:
        return await send(c.ws, {"t": "lobby_error", "why": "Games against other players aren't available here."})
    if c.player is None:
        return await send(c.ws, {"t": "need_login"})
    if c not in lobby:
        lobby[c] = time.monotonic()
        lobby_change()
    await send(c.ws, lobby_list())
    for ch in list(challenges.values()):  # (invitations that came while this page was away)
        if ch.dst.id == c.player.id:
            await send(c.ws, ch.offer())


async def leave_lobby(c, p=None):
    """The page closed the lobby (or went away, or its player p logged out there): it stops
    waiting for a game, and its invitation ends, as do those to its player if they have no other
    page in the lobby."""
    p = p or c.player
    if c in queue:
        del queue[c]
        c.seeking = None
        lobby_change()
    if lobby.pop(c, None) is not None:
        lobby_change()
    await drop_challenge(c.challenge, "left")
    for ch in [ch for ch in challenges.values() if ch.src is c]:
        await drop_challenge(ch, "left")
    if p is not None and not pages_of(p, lobby):
        for ch in [ch for ch in challenges.values() if ch.dst.id == p.id]:
            await drop_challenge(ch, "left")


async def drop_challenge(ch, why):
    """An invitation ends: declined, expired, taken back, accepted, or one of them left or is playing."""
    if ch is None or challenges.get(ch.id) is not ch:
        return
    del challenges[ch.id]
    if ch.timer:
        ch.timer.cancel()
    if ch.src.challenge is ch:
        ch.src.challenge = None
    msg = {"t": "challenge_gone", "id": ch.id, "why": why, "name": ch.dst.name, "from": ch.asker.name}
    await send(ch.src.ws, msg)
    for o in pages_of(ch.dst, lobby):
        await send(o.ws, msg)


async def challenge(c, name):
    """c asks the player called name to play. If they are waiting for a game, it starts at once."""
    async def no(why):
        await send(c.ws, {"t": "lobby_error", "why": why})

    if not players or not rules:
        return await no("Games against other players aren't available here.")
    me = c.player
    if me is None:
        return await send(c.ws, {"t": "need_login"})
    if me.id in in_game:
        return await no("Finish your game first.")
    name = name.lower()
    theirs = [o for o in list(queue) + list(lobby) if o.player is not None and o.player.name.lower() == name]
    if not theirs:
        playing = next((p for r in in_game.values() for p in r.players.values() if p and p.name.lower() == name), None)
        if playing:
            return await no(f"{playing.name} is playing a game right now.")
        return await no("That player isn't in the lobby any more.")
    them = theirs[0].player
    if them.id == me.id:
        return await no("That's you!")
    if them.id in in_game:
        return await no(f"{them.name} is playing a game right now.")
    reachable = [o for o in theirs if o.ip != c.ip or allow_same_ip]
    if not reachable:
        return await no(f"You and {them.name} are on the same internet connection, so you can't play each other "
                        "in a rated game. Play a friend online instead.")
    waiting = [o for o in reachable if o in queue]
    if waiting:  # (they're waiting for anyone: the game starts now)
        return await start_match(c, waiting[0])
    asked = next((ch for ch in challenges.values() if ch.asker.id == them.id and ch.dst.id == me.id), None)
    if asked and asked.src in lobby and (asked.src.ip != c.ip or allow_same_ip):
        # (they asked this player already: that's a yes from both)
        await drop_challenge(asked, "accepted")
        return await start_match(asked.src, c)
    if c.challenge and c.challenge.dst.id == them.id:  # (asked already: still waiting for their answer)
        return await send(c.ws, {"t": "challenge_sent", "id": c.challenge.id, "to": them.brief(),
                                 "seconds": max(1, round(c.challenge.timer.when() - asyncio.get_running_loop().time()))})
    if failures("declined", (me.id, them.id), ASK_AGAIN_SECONDS):
        return await no(f"{them.name} said no thanks a moment ago. You can ask again in a minute.")
    if limited("challenge", c.ip, CHALLENGES_PER_IP, 600):
        return await no("That's a lot of invitations. Wait a few minutes, then ask again.")
    await drop_challenge(c.challenge, "cancelled")  # (one at a time: asking someone else takes back the last)
    ch = c.challenge = Challenge(c, them)
    challenges[ch.id] = ch
    ch.timer = asyncio.get_running_loop().call_later(
        CHALLENGE_SECONDS, lambda: asyncio.ensure_future(drop_challenge(ch, "expired")))
    await send(c.ws, {"t": "challenge_sent", "id": ch.id, "to": them.brief(), "seconds": CHALLENGE_SECONDS})
    for o in pages_of(them, lobby):
        await send(o.ws, ch.offer())


async def profile(c, name):
    """A player's card, for the page that asked: their statistics, where they are now, and how
    the asker has done against them."""
    async def no(why):
        await send(c.ws, {"t": "lobby_error", "why": why})

    if not players:
        return await no("This server keeps no players.")
    if c.player is None:
        return await send(c.ws, {"t": "need_login"})
    if limited("profile", c.ip, PROFILES_PER_IP, 60):
        return await no("That's a lot of players looked at. Wait a moment.")
    row = players._by_name(name)
    if row is None:
        return await no(f"There's no player called {name}.")
    pid = row[0]
    card = players.profile(pid)
    if card is None:
        return await no(f"There's no player called {name}.")
    pages = [o for o in all_conns if o.player is not None and o.player.id == pid]
    where = ("playing" if pid in in_game else "waiting" if any(o in queue for o in pages)
             else "lobby" if any(o in lobby for o in pages) else "online" if pages else "away")
    out = dict(card, t="profile", where=where)
    if pid != c.player.id:
        out["vs"] = players.head_to_head(c.player.id, pid)  # (the asker's wins, draws and losses against them)
    await send(c.ws, out)


async def answer(c, cid, yes):
    """The player asked says yes (the game starts) or no thanks."""
    ch = challenges.get(cid)
    if ch is None or c.player is None or ch.dst.id != c.player.id:
        return await send(c.ws, {"t": "challenge_gone", "id": cid, "why": "gone"})
    if not yes:
        failures("declined", (ch.asker.id, ch.dst.id), ASK_AGAIN_SECONDS).append(time.monotonic())
        return await drop_challenge(ch, "declined")
    src = ch.src
    if src not in lobby or src.player is None or src.player.id != ch.asker.id:
        return await drop_challenge(ch, "left")
    if ch.asker.id in in_game or ch.dst.id in in_game:
        return await drop_challenge(ch, "playing")
    if src.ip == c.ip and not allow_same_ip:
        await send(c.ws, {"t": "lobby_error", "why": f"You and {ch.asker.name} are on the same internet connection, "
                                                     "so you can't play each other in a rated game."})
        return await drop_challenge(ch, "left")
    await drop_challenge(ch, "accepted")
    await start_match(src, c)


# ---------------------------------------------------------------------------
# Accounts: the front page's forms, and the Account window in the game

def failures(kind, key, seconds):
    now = time.monotonic()
    times = [t for t in recent.get((kind, key), []) if now - t < seconds]
    recent[(kind, key)] = times
    return times


# Wrong passwords and recovery codes are counted from each address, for each name from each
# address, and for each name from everywhere. The last limit is far higher than the others, so
# that nobody can keep a player out of their account by guessing wrong at their name.

def too_many_fails(kind, name, ip):
    return (len(failures("fail", ip, 600)) >= LOGIN_FAILS_PER_IP or
            len(failures(kind, (name, ip), 600)) >= LOGIN_FAILS_PER_NAME or
            len(failures(kind + "-all", name, 600)) >= LOGIN_FAILS_PER_NAME_ALL)


def count_fail(kind, name, ip):
    now = time.monotonic()
    for key in (("fail", ip), (kind, (name, ip)), (kind + "-all", name)):
        failures(*key, 600).append(now)


def log_out_elsewhere(player, here, keep_session=None):
    """After a new password, or a recovery code used: the player's other pages are logged out
    (their sessions are gone already) - all but 'here', and those logged in with keep_session."""
    for o in list(all_conns):
        if o is here or o.player is None or o.player.id != player.id:
            continue
        if keep_session is not None and o.session == keep_session:
            continue  # (the same browser, in another tab)
        asyncio.ensure_future(leave_lobby(o, o.player))
        o.player = o.session = None
        queue.pop(o, None)
        o.seeking = None
        asyncio.ensure_future(o.ws.close(4001, "logged out"))


async def account(c, kind, msg):
    async def no(why):
        await send(c.ws, {"t": "auth_error", "for": kind, "why": why})

    if not players:
        return await no("This server keeps no accounts.")
    name = str(msg.get("name", "")).strip()[:32]
    password = str(msg.get("password", ""))[:PASSWORD_MAX + 1]

    def password_ok(pw):
        return PASSWORD_MIN <= len(pw) <= PASSWORD_MAX

    # wrong passwords and recovery codes: limits from each address, and for each name
    if kind in ("login", "recover") and too_many_fails(kind, name.lower(), c.ip):
        return await no("Too many wrong tries. Wait 10 minutes and try again.")

    if kind == "signup":
        if not NAME.match(name):
            return await no("A name is 3 to 16 letters, digits, _ or -.")
        if not password_ok(password):
            return await no(f"A password is {PASSWORD_MIN} to {PASSWORD_MAX} characters.")
        if players.name_taken(name):
            return await no(f"The name {name} is taken. Try another.")
        if limited("name", c.ip, NAMES_PER_IP_PER_DAY, 86400):
            return await no("Too many new accounts from here today. Try again tomorrow.")
        made = players.signup(name, await asyncio.to_thread(hash_password, password))
        if made is None:
            return await no(f"The name {name} is taken. Try another.")
        p, key, code = made
        c.player, c.session = p, key
        log.info("player %s signed up", p.name)
        await send(c.ws, dict({"t": "account", "session": key, "recovery": code}, **p.me()))

    elif kind == "login":
        stored = players.stored_password(name)
        ok = stored is not None and stored[1] is not None and await asyncio.to_thread(check_password, password, stored[1])
        if not ok:
            count_fail("login", name.lower(), c.ip)
            if stored is not None and stored[1] is None:
                return await no("This account has no password yet: open the game in the browser you played on, "
                                "and choose one under Account.")
            return await no("That name and password don't match.")
        p, key = players.login(stored[0])
        c.player, c.session = p, key
        await send(c.ws, dict({"t": "account", "session": key}, **p.me()))

    elif kind == "recover":  # a forgotten password: the recovery code, and a new password
        if not password_ok(password):
            return await no(f"A password is {PASSWORD_MIN} to {PASSWORD_MAX} characters.")
        player_id = players.check_recovery(name, str(msg.get("code", ""))[:64])
        if player_id is None:
            count_fail("recover", name.lower(), c.ip)
            return await no("That name and recovery code don't match.")
        p, key, code = players.recover(player_id, await asyncio.to_thread(hash_password, password))
        for k in ("login", "recover"):  # (it's theirs: wrong guesses at the name no longer count)
            recent.pop((k, (name.lower(), c.ip)), None)
            recent.pop((k + "-all", name.lower()), None)
        log_out_elsewhere(p, c)
        c.player, c.session = p, key
        log.info("player %s recovered their account", p.name)
        await send(c.ws, dict({"t": "account", "session": key, "recovery": code}, **p.me()))

    elif c.player is None:
        return await no("Log in first.")

    elif kind == "passwd":  # a new password (the current one first, if there is one)
        new = str(msg.get("new", ""))[:PASSWORD_MAX + 1]
        if not password_ok(new):
            return await no(f"A password is {PASSWORD_MIN} to {PASSWORD_MAX} characters.")
        if len(failures("fail", c.ip, 600)) >= LOGIN_FAILS_PER_IP:
            return await no("Too many wrong tries. Wait 10 minutes and try again.")
        p = c.player
        current = players.password_of(p)
        if current is not None and not await asyncio.to_thread(check_password, password, current):
            failures("fail", c.ip, 600).append(time.monotonic())
            return await no("Your current password isn't right.")
        new_hash = await asyncio.to_thread(hash_password, new)
        if c.player is not p:  # (logged out meanwhile, from another page)
            return await no("Log in first.")
        players.set_password(p, new_hash, c.session)
        log_out_elsewhere(p, c, c.session)
        await send(c.ws, {"t": "done", "what": "passwd"})

    elif kind == "newcode":  # a new recovery code (the password first)
        p = c.player
        current = players.password_of(p)
        if current is None:
            return await no("Choose a password first.")
        if not await asyncio.to_thread(check_password, password, current):
            failures("fail", c.ip, 600).append(time.monotonic())
            return await no("That password isn't right.")
        if c.player is not p:
            return await no("Log in first.")
        await send(c.ws, {"t": "code", "recovery": players.new_code(p.id)})

    elif kind == "logout":
        if c.session:
            players.logout(c.session)
        await leave_lobby(c)
        queue.pop(c, None)
        c.seeking = None
        c.player = c.session = None
        await send(c.ws, {"t": "me"})


# ---------------------------------------------------------------------------

async def handle(c, msg):
    kind = msg.get("t")
    room, seat = c.room, c.seat

    if kind in ("signup", "login", "recover") and (c in lobby or c in queue or c.challenge):
        await leave_lobby(c)  # (the page may be someone else's from here)

    if kind == "hello":  # the browser's session key, if it is logged in
        key = str(msg.get("secret", ""))[:64]
        p = players.by_session(key) if players and key else None
        if c.player is not None and (p is None or p.id != c.player.id):
            await leave_lobby(c)  # (someone else's page from here)
        c.player, c.session = p, (key if p else None)
        await send(c.ws, dict({"t": "me"}, **(p.me() if p else {})))

    elif kind in ("signup", "login", "recover", "passwd", "newcode", "logout"):
        await account(c, kind, msg)

    elif kind == "seek":
        if not rules or not players:
            return await send(c.ws, {"t": "notice", "why": "Games against other players aren't available here."})
        if c.player is None:
            return await send(c.ws, {"t": "need_login"})
        if c.player.id in in_game or (room and room.rated and room.result is None):
            return await send(c.ws, {"t": "notice", "why": "Finish this game first."})
        # (a game with a friend stays open on this page until the rated game starts: start_match)
        for other_c in [o for o in queue if o.player is c.player]:  # (the same player in another page)
            queue.pop(other_c, None)
            other_c.seeking = None
        c.seeking = time.monotonic()
        queue[c] = c.seeking
        lobby_change()
        await send(c.ws, {"t": "seeking", "waiting": len(queue)})

    elif kind == "unseek":
        if queue.pop(c, None) is not None:
            lobby_change()
        c.seeking = None

    elif kind == "lobby":
        await enter_lobby(c)

    elif kind == "unlobby":
        await leave_lobby(c)

    elif kind == "challenge":  # ask a player in the lobby to play (or join them, if they are waiting)
        await challenge(c, str(msg.get("name", "")).strip()[:32])

    elif kind == "unchallenge":
        await drop_challenge(c.challenge, "cancelled")

    elif kind == "answer":
        await answer(c, str(msg.get("id", ""))[:32], msg.get("yes") is True)

    elif kind == "profile":  # a player's card: their record and statistics
        await profile(c, str(msg.get("name", "")).strip()[:32])

    elif kind == "icon":  # the player's picture (None: none)
        icon = msg.get("icon")
        if c.player is None:
            return await send(c.ws, {"t": "need_login"})
        if icon is not None and (not isinstance(icon, str) or icon not in ICONS):
            return await send(c.ws, {"t": "lobby_error", "why": "There's no such picture."})
        if limited("icon", c.ip, 60, 600):
            return await send(c.ws, {"t": "lobby_error", "why": "That's a lot of new pictures. Try again in a while."})
        players.set_icon(c.player, icon)
        lobby_change()
        for o in pages_of(c.player, all_conns):  # (every page of theirs shows it)
            await send(o.ws, dict({"t": "me"}, **c.player.me()))

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
        if len(failures("join", c.ip, 600)) >= JOIN_FAILS_PER_IP:  # (guessing at games' links)
            return await send(c.ws, {"t": "error", "why": "Too many links to games that aren't there. "
                                                          "Wait 10 minutes and try again."})
        r = rooms.get(code)
        if r is None:
            failures("join", c.ip, 600).append(time.monotonic())
            return await send(c.ws, {"t": "error", "why": "There is no game at this link (unfinished games end "
                                                          "after a day without moves)."})
        s = next((s for s in "wb" if same_token(r.tokens[s], token)), None)
        if s is None:
            if r.tokens["b"] is not None:
                return await send(c.ws, {"t": "error", "why": "This game already has two players."})
            s = "b"
            r.tokens["b"] = secrets.token_urlsafe(18)
        elif r.rated and r.players[s] and (c.player is None or c.player.id != r.players[s].id):
            # a rated game's side is its player's: a page logged out (a new password) can't come back
            return await send(c.ws, {"t": "error", "why": f"Log in as {r.players[s].name} to come back to this game."})
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
            room.created = time.time()
            await to_seat(room, other(seat), {"t": "new"})


def request_headers(ws):
    request = getattr(ws, "request", None)  # (websockets 14 and later)
    return request.headers if request is not None else ws.request_headers


async def player(ws, path=None):
    global connections
    ip = address_key(request_headers(ws).get("X-Real-IP") or (ws.remote_address or ("?",))[0])
    if connections >= MAX_CONNECTIONS or conns_by_ip.get(ip, 0) >= MAX_CONNECTIONS_PER_IP:
        await ws.close(4002, "busy")
        return
    connections += 1
    conns_by_ip[ip] = conns_by_ip.get(ip, 0) + 1
    c = Conn(ws, ip)
    all_conns.add(c)
    allowance, last = MESSAGES_BURST, time.monotonic()  # (how many messages the page may send now)
    try:
        async for raw in ws:
            now = time.monotonic()
            allowance = min(MESSAGES_BURST, allowance + (now - last) * MESSAGES_PER_SECOND)
            last = now
            if allowance < 1:  # (no more of its messages are read; the close needn't be waited for)
                asyncio.ensure_future(ws.close(4003, "too many messages"))
                await asyncio.sleep(0)  # (the close starts here, before the handler ends)
                break
            allowance -= 1
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
        all_conns.discard(c)
        await leave_lobby(c)
        await detach(c)


async def sweep():
    rounds = 0
    while True:
        await asyncio.sleep(60)
        rounds += 1
        if players and rounds % 360 == 1:  # (every 6 hours)
            players.forget_old_sessions()
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


async def delete_player(name):
    """The admin app deletes a player: their game in progress ends (aborted, unrated), their pages
    are logged out, and they leave the database."""
    row = players._by_name(name) if players else None
    if row is None:
        return {"ok": False, "why": f"There is no player called {name}."}
    pid, real = row[0], row[1]
    p = players.cache.get(pid)
    if p is not None:
        for r in list(rooms.values()):
            for seat in "wb":
                if r.players[seat] is not p:
                    continue
                if r.rated and r.result is None:
                    await end_game(r, "*", "aborted")  # (recorded first: the deletion takes them out of it)
                if not r.rated:
                    r.players[seat] = None  # (a game with a friend: not recorded with them)
    for o in list(all_conns):
        if o.player is not None and o.player.id == pid:
            await leave_lobby(o)
            o.player = o.session = None
            queue.pop(o, None)
            o.seeking = None
            asyncio.ensure_future(o.ws.close(4004, "account deleted"))
    for ch in [ch for ch in challenges.values() if pid in (ch.asker.id, ch.dst.id)]:
        await drop_challenge(ch, "left")
    players.delete(pid)
    log.info("player %s deleted (admin app)", real)
    return {"ok": True, "why": f"{real} is deleted."}


def requests_folder(db_path):
    return os.path.join(os.path.dirname(os.path.abspath(db_path)), "admin-requests")


async def admin_requests(folder):
    """Requests from the admin app (web/admin.py, run as root): a file each in the folder,
    NAME.request, answered in NAME.answer. Only root and this server can write there."""
    os.makedirs(folder, exist_ok=True)
    while True:
        await asyncio.sleep(1)
        try:
            names = sorted(f for f in os.listdir(folder) if f.endswith(".request"))
        except OSError:
            continue
        for f in names:
            path = os.path.join(folder, f)
            try:
                with open(path) as fh:
                    req = json.load(fh)
            except (OSError, ValueError):
                req = {}
            try:
                os.remove(path)
            except OSError:
                pass
            if req.get("t") == "delete":
                answer = await delete_player(str(req.get("name", "")).strip())
            else:
                answer = {"ok": False, "why": "The game server doesn't know that request."}
            out = path[:-len(".request")] + ".answer"
            try:
                with open(out + ".tmp", "w") as fh:
                    json.dump(answer, fh)
                os.replace(out + ".tmp", out)
            except OSError as e:
                log.warning("admin request: %s", e)


async def delete_from_command_line(db_path, status_path, name):
    """--delete-player: the running game server does it (it knows who is online and playing);
    without one, the database is changed here. True if the player was deleted."""
    def server_running():
        try:
            with open(status_path) as f:
                return time.time() - json.load(f).get("time", 0) < 10
        except (OSError, ValueError):
            return False

    folder = requests_folder(db_path)
    if server_running() and os.path.isdir(folder):
        key = secrets.token_hex(8)
        req, ans = os.path.join(folder, key + ".request"), os.path.join(folder, key + ".answer")
        with open(req + ".tmp", "w") as f:
            json.dump({"t": "delete", "name": name}, f)
        os.chmod(req + ".tmp", 0o644)  # (the server, another user, reads it)
        os.replace(req + ".tmp", req)
        for _ in range(200):  # (20 seconds)
            await asyncio.sleep(0.1)
            if os.path.exists(ans):
                with open(ans) as f:
                    answer = json.load(f)
                os.remove(ans)
                print(answer["why"])
                return answer["ok"]
        try:
            os.remove(req)
        except OSError:
            pass
        if server_running():
            print("The game server didn't answer. Is it busy? Try again in a moment.")
            return False
    global players
    players = Players(db_path)
    row = players._by_name(name)
    if row is None:
        print(f"There is no player called {name}.")
        ok = False
    else:
        ok = players.delete(row[0])
        print(f"{row[1]} is deleted.")
    players.db.close()
    keep_owner(db_path)
    return ok


async def write_status(path):
    """For the admin app (web/admin.py): who is here, who is waiting and the games going on,
    written to a file every 2 seconds."""
    started = time.time()
    while True:
        await asyncio.sleep(2)

        def who(r, seat):
            p = r.players[seat]
            return {"name": p.name if p else None, "rating": p.rating if p else None, "here": r.conns[seat] is not None}

        going = [r for r in rooms.values() if r.result is None and (r.moves or (r.conns["w"] and r.conns["b"]))]
        going.sort(key=lambda r: -r.created)
        status = {
            "time": int(time.time()), "started": int(started), "connections": len(all_conns),
            "online": sorted({c.player.name for c in all_conns if c.player})[:5000],
            "seeking": [{"name": c.player.name, "rating": c.player.rating, "waiting": int(time.monotonic() - since)}
                        for c, since in list(queue.items()) if c.player],
            "lobby": sorted({c.player.name for c in lobby if c.player})[:5000],
            "games_going": len(going),
            "games": [{"code": r.code, "rated": r.rated, "w": who(r, "w"), "b": who(r, "b"), "moves": len(r.moves),
                       "since": int(r.created), "clock": r.clock_now()} for r in going[:500]],
        }
        try:
            tmp = path + ".tmp"
            with open(tmp, "w") as f:
                json.dump(status, f)
            os.replace(tmp, path)
        except OSError as e:
            log.warning("status file: %s", e)
            await asyncio.sleep(60)


def keep_owner(path):
    """Run as root (an admin command), the database's files stay the game server's."""
    if os.geteuid() != 0:
        return
    d = os.stat(os.path.dirname(os.path.abspath(path)))
    for f in (path, path + "-wal", path + "-shm"):
        if os.path.exists(f):
            os.chown(f, d.st_uid, d.st_gid)


def static_files(root):
    """For local tests: serves the page itself, with the headers the game's threads need."""
    root = os.path.realpath(root)
    types = {".wasm": "application/wasm", ".js": "text/javascript", ".data": "application/octet-stream"}

    def respond(path):
        """(status, headers, body) for a file, or None for the WebSocket."""
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

    if NEW_WEBSOCKETS:
        from websockets.datastructures import Headers
        from websockets.http11 import Response

        def process_request(connection, request):
            r = respond(request.path)
            if r is None:
                return None
            status, headers, body = r
            headers += [("Content-Length", str(len(body))), ("Connection", "close")]
            return Response(status.value, status.phrase, Headers(headers), body)
    else:
        async def process_request(path, headers):
            return respond(path)

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
    ap.add_argument("--status", help="the status file for the admin app (default: status.json beside the database)")
    ap.add_argument("--reset-code", metavar="NAME",
                    help="give a player a new recovery code (when they've lost theirs), print it and stop")
    ap.add_argument("--delete-player", metavar="NAME",
                    help="delete a player (logged out, their games kept without them), print what happened and stop")
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
    if args.reset_code:
        players = Players(args.db)
        row = players._by_name(args.reset_code)
        if row is None:
            print(f"There is no player called {args.reset_code}.")
        else:
            code = players.new_code(row[0])
            print(f"A new recovery code for {row[1]}: {code}")
            print("They can use it with Forgot password on the front page, to choose a new password.")
        players.db.close()
        keep_owner(args.db)
        return
    status = args.status or os.path.join(os.path.dirname(os.path.abspath(args.db)), "status.json")
    if args.delete_player:
        if not await delete_from_command_line(args.db, status, args.delete_player.strip()):
            raise SystemExit(1)
        return
    if args.db != "none":
        players = Players(args.db)
        asyncio.ensure_future(write_status(status))
        asyncio.ensure_future(admin_requests(requests_folder(args.db)))
    asyncio.ensure_future(sweep())
    asyncio.ensure_future(matchmaker())
    asyncio.ensure_future(lobby_loop())
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
