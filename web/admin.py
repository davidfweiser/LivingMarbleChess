#!/usr/bin/env python3
"""Living Marble - the admin app: who is playing, the players and their games, at a glance.

A text-screen app for the server (over SSH), reading the game server's database (read-only)
and the status file it writes every 2 seconds:

    sudo living-marble-admin          (installed by linode-setup.sh)
    sudo living-marble-admin --once   (the summary as plain text, and stop)

Keys: 1-4 or Tab for the screens (Now, Players, Games, Stats), arrows and Page Up/Down to
scroll, / to search for a player, Enter for a player's games (Esc or Backspace: back),
d to delete a player (their name typed again first), s to sort, q to quit. Esc leaves a
question at the bottom (the search, or the deletion) without changing anything.

Deleting goes through the game server (relay.py --delete-player), which logs the player out
everywhere and ends their game; without a server running it changes the database itself.
"""
import argparse
import curses
import datetime
import json
import os
import sqlite3
import subprocess
import sys
import time

DEFAULT_DB = "/var/lib/living-marble/players.db"
RELAY = os.path.join(os.path.dirname(os.path.abspath(__file__)), "relay.py")  # (beside this file)
SCREENS = ["Now", "Players", "Games", "Stats"]
REASONS = {"checkmate": "checkmate", "resignation": "resigned", "timeout": "on time", "abandoned": "left",
           "agreement": "agreed", "stalemate": "stalemate", "fifty": "50 moves", "repetition": "repetition",
           "material": "material", "aborted": "aborted"}
SORTS = [("rating", "rating DESC, name"), ("name", "name COLLATE NOCASE"), ("games", "played DESC, name"),
         ("last seen", "seen DESC"), ("joined", "created DESC")]


# ---------------------------------------------------------------------------
# The data

class Data:
    def __init__(self, db_path, status_path):
        self.db_path, self.status_path = db_path, status_path
        self.db = None
        self.problem = ""

    def connect(self):
        if self.db is None:
            if not os.path.exists(self.db_path):
                self.problem = f"No database at {self.db_path} yet (the game server makes it when it starts)."
                return False
            try:
                self.db = sqlite3.connect(f"file:{self.db_path}?mode=ro", uri=True, timeout=2)
            except sqlite3.Error as e:
                self.problem = f"Can't open {self.db_path}: {e} (run it with sudo)"
                return False
        self.problem = ""
        return True

    def q(self, sql, args=()):
        try:
            return self.db.execute(sql, args).fetchall()
        except sqlite3.Error as e:
            self.problem = f"Database: {e} (run it with sudo?)"
            self.db = None
            return []

    def status(self):
        """The game server's status file, and how old it is (seconds); (None, None) without one."""
        try:
            with open(self.status_path) as f:
                st = json.load(f)
            return st, time.time() - st.get("time", 0)
        except (OSError, ValueError):
            return None, None

    def players(self, sort, search):
        where, args = "", []
        if search:
            where, args = "WHERE p.name LIKE ? ESCAPE '\\'", ["%" + search.replace("%", "\\%").replace("_", "\\_") + "%"]
        order = dict(SORTS)[sort]
        return self.q(f"""
            SELECT p.name, p.rating, p.games, p.wins, p.draws, p.losses,
                   (SELECT COUNT(*) FROM games g WHERE g.white = p.id OR g.black = p.id) AS played,
                   p.seen, p.created, p.password IS NOT NULL, p.id
            FROM players p {where} ORDER BY {order}""", args)

    def games(self, limit=500, player_id=None):
        where, args = "", []
        if player_id is not None:
            where, args = "WHERE g.white = ? OR g.black = ?", [player_id, player_id]
        return self.q(f"""
            SELECT g.ended, g.rated, w.name, g.white_before, g.white_after, b.name, g.black_before, g.black_after,
                   g.result, g.why, g.moves, g.started
            FROM games g LEFT JOIN players w ON w.id = g.white LEFT JOIN players b ON b.id = g.black
            {where} ORDER BY g.ended DESC LIMIT ?""", args + [limit])

    def stats(self):
        now = int(time.time())
        day, week, month = now - 86400, now - 7 * 86400, now - 30 * 86400
        one = lambda sql, *a: (self.q(sql, a) or [[0]])[0][0] or 0  # noqa: E731
        s = {
            "players": one("SELECT COUNT(*) FROM players"),
            "new_day": one("SELECT COUNT(*) FROM players WHERE created >= ?", day),
            "new_week": one("SELECT COUNT(*) FROM players WHERE created >= ?", week),
            "new_month": one("SELECT COUNT(*) FROM players WHERE created >= ?", month),
            "seen_day": one("SELECT COUNT(*) FROM players WHERE seen >= ?", day),
            "seen_week": one("SELECT COUNT(*) FROM players WHERE seen >= ?", week),
            "games": one("SELECT COUNT(*) FROM games"),
            "rated": one("SELECT COUNT(*) FROM games WHERE rated = 1"),
            "games_day": one("SELECT COUNT(*) FROM games WHERE ended >= ?", day),
            "games_week": one("SELECT COUNT(*) FROM games WHERE ended >= ?", week),
            "white": one("SELECT COUNT(*) FROM games WHERE result = '1-0'"),
            "black": one("SELECT COUNT(*) FROM games WHERE result = '0-1'"),
            "draws": one("SELECT COUNT(*) FROM games WHERE result = '1/2-1/2'"),
            "avg_moves": one("SELECT AVG(LENGTH(moves) - LENGTH(REPLACE(moves, ' ', '')) + 1) FROM games "
                             "WHERE moves != ''"),
        }
        days = []  # the last 14 days: games, new players
        today = datetime.date.today()
        for i in range(13, -1, -1):
            d = today - datetime.timedelta(days=i)
            start = int(time.mktime(d.timetuple()))
            end = start + 86400
            days.append((d, one("SELECT COUNT(*) FROM games WHERE ended >= ? AND ended < ?", start, end),
                         one("SELECT COUNT(*) FROM players WHERE created >= ? AND created < ?", start, end)))
        s["days"] = days
        s["top"] = self.q("SELECT name, rating, games FROM players WHERE games > 0 ORDER BY rating DESC LIMIT 10")
        return s


# ---------------------------------------------------------------------------
# Words and numbers

def ago(t, now=None):
    if not t:
        return "-"
    d = int((now or time.time()) - t)
    if d < 60:
        return "now" if d < 10 else f"{d}s ago"
    if d < 3600:
        return f"{d // 60}m ago"
    if d < 86400:
        return f"{d // 3600}h ago"
    if d < 30 * 86400:
        return f"{d // 86400}d ago"
    return datetime.date.fromtimestamp(t).isoformat()


def span(seconds):
    seconds = int(seconds)
    if seconds < 3600:
        return f"{seconds // 60}:{seconds % 60:02d}"
    if seconds < 86400:
        return f"{seconds // 3600}h {seconds % 3600 // 60:02d}m"
    return f"{seconds // 86400}d {seconds % 86400 // 3600}h"


def clock(ms):
    s = max(0, int(ms) // 1000)
    return f"{s // 60}:{s % 60:02d}"


def moves_in(text):
    return len(text.split()) if text else 0


def result_words(result, why):
    r = {"1-0": "1-0", "0-1": "0-1", "1/2-1/2": "½-½", "*": "-"}.get(result, result or "")
    return f"{r} {REASONS.get(why, why or '')}".strip()


def who(p):
    if not p or not p.get("name"):
        return "?"
    return f"{p['name']} ({p['rating']})" + ("" if p.get("here") else " away")


# ---------------------------------------------------------------------------
# The screens, as lines: (text, style) where style is one of the colour pairs below

TITLE, GOLD, DIM, GOOD, BAD, HEAD, BAR, SEL = range(1, 9)


def now_lines(data, width):
    st, age = data.status()
    out = []
    if st is None:
        out.append(("The game server's status file isn't there yet: is the game server running?", BAD))
        out.append((f"  ({data.status_path})", DIM))
        return out
    if age > 15:
        out.append((f"The game server hasn't written its status for {span(age)}: is it running?  "
                    "(systemctl status living-marble-relay)", BAD))
    games = st.get("games", [])
    playing = sum(1 for g in games for s in "wb" if g[s].get("here"))
    out.append((f"Online now: {len(st.get('online', []))} players   Playing: {playing} in "
                f"{st.get('games_going', len(games))} games   In the lobby: {len(st.get('lobby', []))}   "
                f"Waiting for an opponent: {len(st.get('seeking', []))}   Connections: {st.get('connections', 0)}", GOLD))
    out.append(("", 0))
    out.append(("GAMES GOING ON", HEAD))
    if not games:
        out.append(("  none", DIM))
    else:
        out.append((f"  {'':7} {'White':<26} {'Black':<26} {'Moves':>5}  {'Clocks (*: to move)':<20} {'Playing for':>11}", DIM))
        for g in games:
            c = g.get("clock")
            clocks = (f"{clock(c['w'])}{'*' if c.get('run') == 'w' else ' '} {clock(c['b'])}"
                      f"{'*' if c.get('run') == 'b' else ''}") if c else ""
            kind = "unrated" if g.get("unrated") else "rated" if g.get("rated") else "friend"
            out.append((f"  {kind:<7} {who(g['w']):<26.26} {who(g['b']):<26.26} {g.get('moves', 0):>5}  {clocks:<20} "
                        f"{span(time.time() - g.get('since', time.time())):>11}", 0))
    out.append(("", 0))
    out.append(("WAITING FOR AN OPPONENT", HEAD))
    seek = st.get("seeking", [])
    out.append(("  none", DIM) if not seek else
               ("  " + ",  ".join(f"{s['name']} ({s['rating']}, {s['waiting']}s)" for s in seek), 0))
    out.append(("", 0))
    busy = {g[s]["name"] for g in games for s in "wb" if g[s].get("name")}
    idle = [n for n in st.get("online", []) if n not in busy and n not in {s["name"] for s in seek}]
    out.append((f"ONLINE, NOT PLAYING ({len(idle)})", HEAD))
    line = "  "
    for n in idle:
        if len(line) + len(n) + 2 > width - 2:
            out.append((line, 0))
            line = "  "
        line += n + "  "
    out.append((line if idle else "  none", 0 if idle else DIM))
    return out


def player_lines(rows, online, playing):
    out = [(f"  {'Name':<17} {'Rating':>6} {'Rated':>5} {'Won':>4} {'Drawn':>5} {'Lost':>4} {'All':>5}  "
            f"{'Last seen':<12} {'Joined':<12} Now", DIM)]
    now = time.time()
    for name, rating, games, wins, draws, losses, played, seen, created, haspw, _ in rows:
        state = "playing" if name in playing else "online" if name in online else ""
        out.append((f"  {name:<17} {rating:>6} {games:>5} {wins:>4} {draws:>5} {losses:>4} {played:>5}  "
                    f"{ago(seen, now):<12} {ago(created, now):<12} {state}" + ("" if haspw else "  (no password yet)"),
                    GOOD if state else 0))
    return out


def game_lines(rows):
    out = [(f"  {'Ended':<12} {'Type':<7} {'White':<24} {'Black':<24} {'Result':<16} {'Moves':>5}", DIM)]
    now = time.time()
    for ended, rated, w, wb, wa, b, bb, ba, result, why, moves, started in rows:
        def side(name, before, after):
            name = name or "(deleted)"  # (a player deleted from the admin app)
            if not rated or after is None or before is None or result == "*":
                return name
            return f"{name} {after - before:+d}"
        out.append((f"  {ago(ended, now):<12} {'rated' if rated else 'unrated':<7} {side(w, wb, wa):<24.24} "
                    f"{side(b, bb, ba):<24.24} {result_words(result, why):<16.16} {(moves_in(moves) + 1) // 2:>5}", 0))
    return out


def stats_lines(s, width):
    out = [("PLAYERS", HEAD),
           (f"  {s['players']} account{'' if s['players'] == 1 else 's'}   new: {s['new_day']} today, {s['new_week']} this week, "
            f"{s['new_month']} this month   seen: {s['seen_day']} today, {s['seen_week']} this week", 0),
           ("", 0), ("GAMES", HEAD)]
    total = s["games"] or 1
    out.append((f"  {s['games']} played ({s['rated']} rated, {s['games'] - s['rated']} unrated)   "
                f"{s['games_day']} today, {s['games_week']} this week   "
                f"about {int((s['avg_moves'] or 0) + 1) // 2} moves a game", 0))
    out.append((f"  White won {100 * s['white'] // total}%   Black won {100 * s['black'] // total}%   "
                f"drawn {100 * s['draws'] // total}%", 0))
    out.append(("", 0))
    days = s["days"]
    for label, k in (("GAMES A DAY, THE LAST 14 DAYS", 1), ("NEW PLAYERS A DAY, THE LAST 14 DAYS", 2)):
        out.append((label, HEAD))
        most = max(1, max(d[k] for d in days))
        room = max(10, width - 22)
        for d in days:
            n = d[k]
            out.append((f"  {d[0].strftime('%a %d %b')}  {n:>5} " + "█" * round(room * n / most), BAR))
        out.append(("", 0))
    out.append(("TOP 10", HEAD))
    for i, (name, rating, games) in enumerate(s["top"], 1):
        out.append((f"  {i:>2}. {name:<17} {rating:>5}   {games} rated game{'' if games == 1 else 's'}", 0))
    if not s["top"]:
        out.append(("  nobody has played a rated game yet", DIM))
    return out


# ---------------------------------------------------------------------------

def put(scr, y, x, text, room, attr=0):
    """Writes text at (y, x), at most room characters, and never fails: a control character
    shows as two (^[) and would push the line on, and a full bottom line ends in the screen's
    last corner, which curses calls an error."""
    if room <= 0:
        return
    text = "".join(c if c.isprintable() else " " for c in text)
    try:
        scr.addnstr(y, x, text, room, attr)
    except curses.error:
        pass


def once(data):
    """The summary as plain text."""
    if not data.connect():
        print(data.problem)
        return 1
    for text, _ in now_lines(data, 100):
        print(text)
    print()
    for text, _ in stats_lines(data.stats(), 60):
        print(text)
    return 0


class App:
    def __init__(self, data):
        self.data = data
        self.screen = 0
        self.scroll = [0, 0, 0, 0]
        self.sort = 0
        self.search = ""
        self.picked = None  # (name, id): a player's games
        self.cursor = 0
        self.lines = []
        self.loaded = 0
        self.message, self.message_until = "", 0  # (in the footer for a while: what a deletion did)

    def load(self, w):
        d = self.data
        if not d.connect():
            self.lines = [(d.problem, BAD)]
            return
        st, _ = d.status()
        online = set(st.get("online", [])) if st else set()
        playing = {g[s]["name"] for g in (st or {}).get("games", []) for s in "wb" if g[s].get("name")}
        if self.screen == 0:
            self.lines = now_lines(d, w)
        elif self.screen == 1:
            if self.picked:
                rows = d.games(200, self.picked[1])
                self.lines = [(f"{self.picked[0]}: the last {len(rows)} games  (Esc or Backspace: back)", GOLD)] + game_lines(rows)
            else:
                self.rows = d.players(SORTS[self.sort][0], self.search)
                self.lines = player_lines(self.rows, online, playing)
        elif self.screen == 2:
            self.lines = game_lines(d.games())
        else:
            self.lines = stats_lines(d.stats(), w)
        self.loaded = time.time()

    def draw(self, scr):
        h, w = scr.getmaxyx()
        scr.erase()
        if h < 8 or w < 40:
            put(scr, 0, 0, "Make the window bigger", w - 1)
            scr.refresh()
            return
        title = " Living Marble - admin "
        put(scr, 0, 0, title.ljust(w), w - 1, curses.color_pair(TITLE) | curses.A_BOLD)
        st, _ = self.data.status()
        up = f"game server up {span(time.time() - st['started'])}   " if st and st.get("started") else ""
        stamp = up + time.strftime("%Y-%m-%d %H:%M:%S ")
        sx = max(len(title) + 1, w - len(stamp) - 1)
        put(scr, 0, sx, stamp, w - sx - 1, curses.color_pair(TITLE))
        x = 1
        for i, name in enumerate(SCREENS):
            label = f" {i + 1} {name} "
            put(scr, 1, x, label, w - x - 1, curses.color_pair(SEL if i == self.screen else DIM) | (curses.A_BOLD if i == self.screen else 0))
            x += len(label) + 1
        info = ""
        if self.screen == 1 and not self.picked:
            info = f"sorted by {SORTS[self.sort][0]} (s)   " + (f"search: {self.search} (/)" if self.search else "/ search   Enter: games") + "   d: delete"
        put(scr, 1, min(x + 2, w - 1), info, w - x - 3, curses.color_pair(DIM))
        body = h - 3
        first = self.scroll[self.screen]
        lines = self.lines
        heads = 1 if self.screen in (1, 2) and lines else 0  # (the column titles stay put)
        if heads:
            text, style = lines[0]
            put(scr, 2, 0, text, w - 1, curses.color_pair(style))
        rows = lines[heads:]
        first = max(0, min(first, max(0, len(rows) - (body - heads))))
        self.scroll[self.screen] = first
        for i, (text, style) in enumerate(rows[first:first + body - heads]):
            attr = curses.color_pair(style)
            if self.screen == 1 and not self.picked and first + i == self.cursor:
                attr = curses.color_pair(SEL) | curses.A_BOLD
            put(scr, 2 + heads + i, 0, text, w - 1, attr)
        foot = " q quit  1-4/Tab screens  arrows/PgUp/PgDn scroll  r refresh "
        if len(rows) > body - heads:
            foot += f"  {first + 1}-{min(len(rows), first + body - heads)} of {len(rows)}"
        if self.message and time.time() < self.message_until:
            foot = " " + self.message + " "
        put(scr, h - 1, 0, foot.ljust(w), w - 1, curses.color_pair(TITLE))
        scr.refresh()

    def ask(self, scr, prompt, most=20):
        """A line typed on the bottom line: Enter ends it, Esc leaves it (None)."""
        curses.curs_set(1)
        scr.timeout(-1)  # (wait for the typing: the screen's refresh timer would cut it short)
        text = ""
        try:
            while True:
                h, w = scr.getmaxyx()
                shown = prompt[:max(1, w - most - 2)]  # (room left for the typing, in a narrow window)
                put(scr, h - 1, 0, (shown + text).ljust(w), w - 1, curses.color_pair(TITLE))
                scr.move(h - 1, min(len(shown) + len(text), w - 1))
                scr.refresh()
                try:
                    k = scr.get_wch()
                except curses.error:
                    continue
                if k in ("\n", "\r", curses.KEY_ENTER):
                    return text.strip()
                if k == "\x1b":  # Esc
                    return None
                if k in ("\x7f", "\b", curses.KEY_BACKSPACE, curses.KEY_LEFT):
                    text = text[:-1]
                elif k == "\x15":  # Ctrl-U: all of it
                    text = ""
                elif isinstance(k, str) and k.isprintable() and len(text) < most:
                    text += k
        finally:
            curses.curs_set(0)
            scr.timeout(500)

    def delete(self, scr, name):
        """Deletes a player, once their name is typed again: the game server does it."""
        typed = self.ask(scr, f" Delete {name} for good? Type the name to delete (Enter or Esc: keep): ")
        if not typed or typed.lower() != name.lower():
            self.message = f"{name} is kept." if not typed else f"{typed} isn't {name}: nothing deleted."
        else:
            try:
                done = subprocess.run([sys.executable, RELAY, "--db", self.data.db_path, "--delete-player", name],
                                      capture_output=True, text=True, timeout=40)
                said = (done.stdout.strip() or done.stderr.strip() or "?").splitlines()
                self.message = said[-1]
            except (OSError, subprocess.TimeoutExpired) as e:
                self.message = f"Couldn't delete {name}: {e}"
            self.data.db = None  # (opened again: the database changed)
        self.message_until = time.time() + 8

    def run(self, scr):
        curses.curs_set(0)
        curses.start_color()
        curses.use_default_colors()
        for pair, fg, bg in ((TITLE, curses.COLOR_BLACK, curses.COLOR_YELLOW), (GOLD, curses.COLOR_YELLOW, -1),
                             (DIM, 8 if curses.COLORS > 8 else curses.COLOR_WHITE, -1), (GOOD, curses.COLOR_GREEN, -1),
                             (BAD, curses.COLOR_RED, -1), (HEAD, curses.COLOR_YELLOW, -1),
                             (BAR, curses.COLOR_YELLOW, -1), (SEL, curses.COLOR_BLACK, curses.COLOR_YELLOW)):
            curses.init_pair(pair, fg, bg)
        scr.timeout(500)
        while True:
            h, w = scr.getmaxyx()
            if time.time() - self.loaded > 2:
                self.load(w)
            self.draw(scr)
            k = scr.getch()
            if k == -1:
                continue
            page = max(1, h - 5)
            if k in (ord("q"), ord("Q")):
                return
            if k in (ord("1"), ord("2"), ord("3"), ord("4")):
                self.screen, self.picked = k - ord("1"), None
            elif k == 9:  # Tab
                self.screen, self.picked = (self.screen + 1) % 4, None
            elif k == curses.KEY_BTAB:
                self.screen, self.picked = (self.screen - 1) % 4, None
            elif self.picked and k in (27, curses.KEY_BACKSPACE, curses.KEY_LEFT, 127, 8):  # Esc, Backspace, <-
                self.picked = None
            elif self.screen == 1 and not self.picked and k in (curses.KEY_UP, curses.KEY_DOWN, curses.KEY_NPAGE,
                                                                 curses.KEY_PPAGE, curses.KEY_HOME, curses.KEY_END):
                n = len(getattr(self, "rows", []))
                step = {curses.KEY_UP: -1, curses.KEY_DOWN: 1, curses.KEY_NPAGE: page, curses.KEY_PPAGE: -page,
                        curses.KEY_HOME: -n, curses.KEY_END: n}[k]
                self.cursor = max(0, min(n - 1, self.cursor + step))
                first = self.scroll[1]
                if self.cursor < first:
                    self.scroll[1] = self.cursor
                elif self.cursor >= first + (h - 4):
                    self.scroll[1] = self.cursor - (h - 5)
                self.draw(scr)
                continue
            elif k in (curses.KEY_UP, curses.KEY_DOWN, curses.KEY_NPAGE, curses.KEY_PPAGE, curses.KEY_HOME, curses.KEY_END):
                step = {curses.KEY_UP: -1, curses.KEY_DOWN: 1, curses.KEY_NPAGE: page, curses.KEY_PPAGE: -page,
                        curses.KEY_HOME: -10 ** 9, curses.KEY_END: 10 ** 9}[k]
                self.scroll[self.screen] = max(0, self.scroll[self.screen] + step)
                self.draw(scr)
                continue
            elif k in (ord("s"), ord("S")) and self.screen == 1:
                self.sort = (self.sort + 1) % len(SORTS)
                self.cursor = self.scroll[1] = 0
            elif k == ord("/") and self.screen == 1:
                typed = self.ask(scr, " Search for a player (Enter for everyone, Esc: as it was): ")
                if typed is not None:
                    self.search = typed
                    self.cursor = self.scroll[1] = 0
                    self.picked = None
            elif k in (ord("d"), ord("D"), curses.KEY_DC) and self.screen == 1 and not self.picked:
                rows = getattr(self, "rows", [])
                if rows:
                    self.delete(scr, rows[min(self.cursor, len(rows) - 1)][0])
                    self.cursor = min(self.cursor, max(0, len(rows) - 2))
            elif k in (10, 13, curses.KEY_ENTER) and self.screen == 1 and not self.picked:
                rows = getattr(self, "rows", [])
                if rows:
                    r = rows[min(self.cursor, len(rows) - 1)]
                    self.picked = (r[0], r[10])
            elif k == curses.KEY_RESIZE:
                pass
            self.loaded = 0  # (show it now)


def main():
    ap = argparse.ArgumentParser(description="Living Marble's admin app: the players, their games, who is playing.")
    ap.add_argument("--db", default=DEFAULT_DB, help=f"the game server's database (default {DEFAULT_DB})")
    ap.add_argument("--status", help="its status file (default: status.json beside the database)")
    ap.add_argument("--once", action="store_true", help="print the summary as plain text and stop")
    args = ap.parse_args()
    status = args.status or os.path.join(os.path.dirname(os.path.abspath(args.db)), "status.json")
    data = Data(args.db, status)
    if args.once or not sys.stdout.isatty():
        return once(data)
    os.environ.setdefault("ESCDELAY", "25")  # (Esc answers at once)
    curses.wrapper(App(data).run)
    return 0


if __name__ == "__main__":
    sys.exit(main())
