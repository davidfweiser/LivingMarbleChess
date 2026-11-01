#!/usr/bin/env python3
"""Living Marble - the relay for online games on the web page.

Two players' browsers can't reach each other directly, so each connects here
(a WebSocket at /ws, behind nginx) and the relay passes each move on to the
other. A game is a room with a code: the player who creates it plays White and
sends the link (https://.../?join=CODE) to a friend, who plays Black. Each seat
has a secret token, kept in the browser, so a player who reloads the page or
loses the connection comes back to the same seat; the room keeps the moves, so
they come back to the same position. The browsers check every move against
the rules; the relay checks only whose turn it is.

    python3 relay.py                          (127.0.0.1:8765, for nginx)
    python3 relay.py --static build-web       (also serves the page: a local test
                                               server, http://localhost:8765/)

Needs the websockets package (Ubuntu: apt install python3-websockets).
"""
import argparse
import asyncio
import http
import json
import logging
import mimetypes
import os
import re
import secrets
import time

import websockets

CODE_ALPHABET = "ABCDEFGHJKMNPQRSTUVWXYZ23456789"  # no 0/O, 1/I/L
CODE_LENGTH = 6
MAX_ROOMS = 5000
ROOM_IDLE_SECONDS = 24 * 3600     # a room nobody has touched for this long goes
CREATES_PER_IP_PER_HOUR = 30
MAX_PLIES = 1200
MAX_MESSAGE = 2048
UCI = re.compile(r"^[a-h][1-8][a-h][1-8][nbrq]?$")

log = logging.getLogger("relay")


class Room:
    def __init__(self, code):
        self.code = code
        self.tokens = {"w": secrets.token_urlsafe(18), "b": None}
        self.conns = {"w": None, "b": None}
        self.moves = []
        self.touched = time.monotonic()


rooms = {}
recent_creates = {}  # ip -> times of the rooms it created in the last hour


def other(seat):
    return "b" if seat == "w" else "w"


def new_code():
    while True:
        code = "".join(secrets.choice(CODE_ALPHABET) for _ in range(CODE_LENGTH))
        if code not in rooms:
            return code


def same_token(a, b):
    return bool(a) and bool(b) and secrets.compare_digest(a.encode(), b.encode())


def room_message(room, seat):
    """What a player gets on (re)joining: their seat and the game so far."""
    return {"t": "room", "room": room.code, "seat": seat, "token": room.tokens[seat], "moves": room.moves,
            "full": room.tokens["b"] is not None, "peer": room.conns[other(seat)] is not None}


async def send(ws, msg):
    if ws is not None:
        try:
            await ws.send(json.dumps(msg))
        except websockets.ConnectionClosed:
            pass


async def refuse(ws, why):
    await send(ws, {"t": "error", "why": why})
    await ws.close(4001, "refused")


def may_create(ip):
    now = time.monotonic()
    times = [t for t in recent_creates.get(ip, []) if now - t < 3600]
    recent_creates[ip] = times
    if len(times) >= CREATES_PER_IP_PER_HOUR:
        return False
    times.append(now)
    return True


async def player(ws, path=None):
    ip = ws.request_headers.get("X-Real-IP") or (ws.remote_address or ("?",))[0]
    room = seat = None
    try:
        async for raw in ws:
            try:
                msg = json.loads(raw)
                kind = msg.get("t")
            except (ValueError, AttributeError):
                return
            if room is None:
                if kind == "create":
                    if len(rooms) >= MAX_ROOMS:
                        return await refuse(ws, "The server has too many games right now. Try again later.")
                    if not may_create(ip):
                        return await refuse(ws, "Too many new games from here. Try again in an hour.")
                    room = Room(new_code())
                    rooms[room.code] = room
                    seat = "w"
                    log.info("room %s created (%d rooms)", room.code, len(rooms))
                elif kind == "join":
                    code = str(msg.get("room", ""))[:16].upper()
                    token = str(msg.get("token", ""))[:64]
                    found = rooms.get(code)
                    if found is None:
                        return await refuse(ws, "There is no game at this link (games end after a day without moves).")
                    seat = next((s for s in "wb" if same_token(found.tokens[s], token)), None)
                    if seat is None:
                        if found.tokens["b"] is not None:
                            return await refuse(ws, "This game already has two players.")
                        seat = "b"
                        found.tokens["b"] = secrets.token_urlsafe(18)
                        log.info("room %s joined", code)
                    room = found
                else:
                    return
                old = room.conns[seat]
                room.conns[seat] = ws
                room.touched = time.monotonic()
                if old is not None and old is not ws:
                    await old.close(4000, "opened elsewhere")  # the same player, from another page
                await send(ws, room_message(room, seat))
                await send(room.conns[other(seat)], {"t": "peer", "here": True})
                continue

            room.touched = time.monotonic()
            if kind == "move":
                uci, ply = msg.get("uci"), msg.get("ply")
                if not isinstance(uci, str) or not UCI.match(uci) or not isinstance(ply, int):
                    continue
                to_move = "w" if len(room.moves) % 2 == 0 else "b"
                if ply != len(room.moves) or seat != to_move or len(room.moves) >= MAX_PLIES:
                    await send(ws, room_message(room, seat))  # out of step: here is the game as it stands
                    continue
                room.moves.append(uci)
                await send(room.conns[other(seat)], {"t": "move", "uci": uci, "ply": ply})
            elif kind == "new":
                room.moves = []
                await send(room.conns[other(seat)], {"t": "new"})
            elif kind == "join":  # asked again: the game as it stands
                await send(ws, room_message(room, seat))
    except websockets.ConnectionClosed:
        pass
    finally:
        if room is not None and room.conns.get(seat) is ws:
            room.conns[seat] = None
            room.touched = time.monotonic()
            await send(room.conns[other(seat)], {"t": "peer", "here": False})


async def sweep():
    while True:
        await asyncio.sleep(600)
        now = time.monotonic()
        for code in [c for c, r in rooms.items()
                     if now - r.touched > ROOM_IDLE_SECONDS and r.conns["w"] is None and r.conns["b"] is None]:
            del rooms[code]
        for ip in [i for i, t in recent_creates.items() if not t or now - t[-1] > 3600]:
            del recent_creates[ip]


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
    ap = argparse.ArgumentParser(description="The relay for Living Marble's online games.")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--static", metavar="FOLDER", help="also serve the web page from FOLDER (local tests)")
    args = ap.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    logging.getLogger("websockets").setLevel(logging.WARNING)  # (not every connection)
    asyncio.ensure_future(sweep())
    async with websockets.serve(player, args.host, args.port, max_size=MAX_MESSAGE, max_queue=16,
                                process_request=static_files(args.static) if args.static else None):
        log.info("Living Marble relay on %s:%d%s", args.host, args.port,
                 f", serving {args.static}" if args.static else "")
        await asyncio.Future()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
