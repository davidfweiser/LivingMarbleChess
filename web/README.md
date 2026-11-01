# Living Marble on a web page

The C++ version in `src/` also builds for the browser with
[Emscripten](https://emscripten.org): WebAssembly with threads, WebGL 2 and Web
Audio. It plays, looks and sounds like the Windows and Linux versions; like the
iPhone and iPad it starts at one sculpting level less (`quality=2`) and *High*
graphics. The whole page is about 1.3 MB to download (gzipped): the game,
plus the DejaVu fonts for its text. On the web you can also play a friend, or
anyone else who is looking for a game, with names, ratings and a leaderboard
(see [Online games](#online-games)).

It needs a current Chrome, Edge, Firefox or Safari (WebGL 2 with
`EXT_color_buffer_float`), and it must be served with two headers, over HTTPS
(or from `localhost`), or the browser won't give it threads:

    Cross-Origin-Opener-Policy: same-origin
    Cross-Origin-Embedder-Policy: require-corp

## Put it on a server (Linode, or any Ubuntu 24.04 machine)

Point a domain name at the server (an `A` record with its IP address), then on
the server:

```sh
curl -fsSLO https://raw.githubusercontent.com/davidfweiser/LivingMarbleChess/main/web/linode-setup.sh
sudo bash linode-setup.sh chess.example.com you@example.com
```

[`linode-setup.sh`](linode-setup.sh) installs nginx, certbot and Emscripten,
builds the page from GitHub, serves it with the headers above, gets a free
Let's Encrypt certificate (it renews itself) and runs the game server for
online games. Run it again to update the page to the latest version. The
smallest plan is enough for a start: once loaded, the game runs entirely in the
player's browser, and the game server only checks and passes moves along (see
[How many players](#how-many-players)). (On plans with less than 3 GB of memory
the script adds swap for the build.)

The name has to be one you control: Let's Encrypt won't certify Linode's own
names for its servers. Without a domain, a free name from a dynamic DNS service
such as [DuckDNS](https://www.duckdns.org) (`yourname.duckdns.org`) works.

## Build it yourself

With Emscripten installed (Ubuntu 24.04: `sudo apt install emscripten`, or the
[emsdk](https://emscripten.org/docs/getting_started/downloads.html)):

```sh
emcmake cmake -B build-web -DCMAKE_BUILD_TYPE=Release
cmake --build build-web -j
python3 web/serve.py build-web     # then open http://localhost:8080/
```

`serve.py` serves only the page. To try online games on your own computer, the
game server can serve the page as well (it needs `python3-websockets`, and the
chess rules built as a library):

```sh
mkdir -p build-rules
g++ -O2 -std=c++17 -shared -fPIC -Isrc -o build-rules/liblmrules.so src/chess.cpp web/chess_rules.cpp
python3 web/relay.py --static build-web --rules build-rules/liblmrules.so --allow-same-ip
# then open http://localhost:8765/
```

Open the game in a private window (or another browser) to play the other side:
each browser remembers its player and its side. (`--allow-same-ip` lets two
players from one computer be paired, which the real server doesn't allow.)

The page is `build-web/index.html` with `living_marble.js`, `.wasm`,
`.worker.js` and `.data` (the fonts) beside it.

## Options in the address

The command line's options, as in `?pieces=medieval&quality=3&graphics=3`:

| | |
|---|---|
| `pieces=marble`, `medieval`, `alien` | which army to start with |
| `quality=0` .. `3` | sculpting detail (default 2; 3 is the desktop's, slower to load) |
| `graphics=0` .. `3` | Low, Medium, High (default), Ultra |
| `demo` | watch the AI play itself |
| `level=0` .. `4` | the computer's strength |
| `mute`, `no-music`, `dof` | no sound, no music, depth of field |
| `fen=...` | start from a position |

## Online games

Two kinds, both under *Mode*:

**Play a friend online.** The game shows a link (and puts it in the address
bar): send it to a friend. You play White, your friend Black, and the game
starts when they open it. These games are untimed and unrated; *New game*
starts a new one for both.

**Play someone online.** The server pairs you with another player looking for
a game, of a similar rating at first and anyone after a while; colours are
random. The first time, you choose a name for the leaderboard (3 to 16
letters, digits, `_` or `-`): there are no passwords, the browser keeps a
secret key for the player. These games are rated and timed, 10 minutes each
plus 5 seconds a move:

- Losing on time, resigning, or staying away for 90 seconds loses the game;
  a draw can be offered and accepted. A game that ends before both players have
  moved is aborted, unrated.
- Ratings are Elo, starting at 1200 (a new player's moves faster for the first
  30 games). *Leaderboard* shows the best 20, from 5 rated games on.
- Against cheating: the server checks every move with the game's own rules
  ([`chess_rules.cpp`](chess_rules.cpp)) and decides how games end, the players
  can't choose each other, and two players from the same internet address are
  never paired. It can't tell whether someone uses a chess engine.

In both, reloading the page or a dropped connection comes back to the same game
and side; *Undo* and *Hint* are off.

### The game server

[`relay.py`](relay.py), run by `linode-setup.sh` as the service
`living-marble-relay` behind nginx (at `/ws`):

- Its log: `journalctl -u living-marble-relay`.
- The players and their games: `/var/lib/living-marble/players.db` (SQLite).
  Back it up now and then, e.g. `sqlite3 /var/lib/living-marble/players.db
  ".backup players-backup.db"` (`apt install sqlite3`).
- Games in progress live in its memory: restarting it ends them, so
  `linode-setup.sh` restarts it only when it has changed. Unfinished games are
  forgotten after a day without moves, finished ones 10 minutes after both
  players have left.
- Settings (in the service's `ExecStart` line, then `systemctl daemon-reload`
  and `systemctl restart living-marble-relay`): `--clock` minutes and
  `--increment` seconds for rated games, `--away` seconds, `--top-min` games
  for the leaderboard, `--per-ip` connections and `--names-per-ip` new names a
  day from one address (40 and 20). `python3 relay.py --help` lists them all.

### How many players

The game server is one Python process. In a load test on a 4-core machine,
simulated players registered, looked for opponents and played real games
through it, a move every 1 to 3 seconds (several times faster than people):

| Players at once | Moves a second | Server CPU (one core) | Memory | A move answered in (median / 95% / 99%) |
|---|---|---|---|---|
| 1,000 | 260 | 17% | 99 MB | 1 ms / 2 ms / 6 ms |
| 3,000 | 780 | 40-46% | 241 MB | 1 ms / 8 ms / 100 ms |
| 6,000 | 1,560 | 60-68% | 451 MB | 1 ms / 120 ms / 330 ms |

So one small server holds several thousand people playing at once; people move
far less often than the test's players, which leaves room to spare. The script
raises the limits that would otherwise stop it at a few hundred (nginx's
connections and the open files allowed to each program). Beyond about 10,000 at
once, the game server would have to be split over several processes or
machines, which it doesn't do yet. The page itself is about 1.3 MB to download
per new visitor (browsers keep it after that).

## What is different in the browser

- Sound starts with the first click or key (browsers keep pages quiet until then).
- *Full screen* uses the browser's (F11 works too); F12 screenshots and Ctrl+Q
  are left to the browser.
- The sculpting uses up to 8 threads.
