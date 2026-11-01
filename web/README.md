# Living Marble on a web page

The C++ version in `src/` also builds for the browser with
[Emscripten](https://emscripten.org): WebAssembly with threads, WebGL 2 and Web
Audio. It plays, looks and sounds like the Windows and Linux versions; like the
iPhone and iPad it starts at one sculpting level less (`quality=2`) and *High*
graphics. The whole page is about 1.3 MB to download (gzipped): the game,
plus the DejaVu fonts for its text.

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
builds the page from GitHub, serves it with the headers above and gets a free
Let's Encrypt certificate (it renews itself). Run it again to update the page
to the latest version. The smallest plan is enough to serve it: once loaded,
the game runs entirely in the player's browser. (On plans with less than 3 GB
of memory the script adds swap for the build.)

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

## What is different in the browser

- Sound starts with the first click or key (browsers keep pages quiet until then).
- *Full screen* uses the browser's (F11 works too); F12 screenshots and Ctrl+Q
  are left to the browser.
- The sculpting uses up to 8 threads.
