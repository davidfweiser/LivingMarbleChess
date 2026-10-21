# Living Marble — 3D Battle Chess

A photoreal 3D chess game in C++ where carved marble Staunton pieces come alive:
they hop, lean, wobble and turn to face each other — and every capture is a
fight. Each piece type knows three attacks and picks one at random, and the loser breaks into
real fracture chunks that tumble across a lacquered wooden board floating in
mid-air — drag to turn it and look at the battle from any side, even underneath.

Everything is procedural. There are **no model, texture or sound files**:
the pieces are sculpted from signed-distance fields at startup, the board's
inlaid wood is baked on the fly, and every sound effect is synthesised.

## The battles

Every capture picks one of the attacker's three styles at random (never the
same one twice in a row for a piece type).

| Attacker | Attacks |
|---|---|
| Pawn   | **Head-butt** — hops up, winds back and butts; the victim topples and bursts · **Bull charge** — paws the ground, charges and sends the victim flying · **Whirlwind** — spins up like a top and grinds the victim apart in a shower of sparks |
| Knight | **Trample** — leaps high and lands on the victim · **Buck kick** — turns its back and kicks the victim sky-high · **Joust** — summons a golden lance and gallops straight through |
| Bishop | **Holy bolt** — a bolt of light; the victim cracks with light and explodes · **Judgment beam** — a pillar of light from above dissolves the victim · **Seraph orbs** — five orbs circle the victim, then strike as one |
| Rook   | **Cannon** — muzzle flash, iron ball, fireball · **Timber!** — the tower topples onto the victim, then springs back up · **Earthquake** — slams down; a shockwave races across the board and erupts under the victim |
| Queen  | **Telekinesis** — lifts the victim in a lightning storm and crushes it · **Frost nova** — freezes the victim solid, then shatters it into glowing ice · **Vortex** — spins the victim down to a point, then it bursts back out |
| King   | **Royal blade** — a golden sword cleaves the victim in two · **Royal decree** — the victim is made to bow, then crumbles to dust · **Rain of blades** — three golden swords plunge in from above |

Quiet moves have personality too: pawns hop eagerly, rooks stomp (with a little
camera shake), knights leap in arcs, bishops glide, the queen twirls and the
king walks with dignity. Selected pieces bounce impatiently, threatened pieces
tremble, a king in check shakes and glows red, and a mated king cracks and
falls. Castling makes the rook vault over the king; promotion turns the pawn
into its new self inside a pillar of light.

## Graphics

- Physically based shading (GGX / Smith / Schlick) with image-based lighting
  from a procedurally generated studio environment (irradiance + prefiltered
  specular + BRDF LUT)
- Polished Carrara and Nero Marquina marble with domain-warped veins and
  subsurface translucency (light visibly bleeds through thin marble)
- Soft PCSS shadows (contact-hardening) from a 4096² shadow map
- Inlaid maple and walnut board with a lacquer clear-coat, **planar
  reflections** of the pieces, brass inlay and gold coordinate lettering
- SSAO, baked per-vertex ambient occlusion and analytic contact shadows
- 4× MSAA HDR pipeline, physically based bloom, bokeh depth of field
  (strong during cinematic battle shots), ACES filmic tone mapping, vignette
  and film grain
- Voronoi fracture: every piece is pre-shattered into 10–16 real chunks
  (plus a two-piece slice for the king's sword) with rigid-body physics —
  chunks bounce, roll, spin, collide with standing pieces and can tumble off
  the edge of the floating board
- Particles: dust, smoke, sparks, embers, magic, lightning, shockwave rings
- A spring-driven squash / bend / twist rig deforms every piece in the vertex
  shader, with automatic inertia (pieces lean against acceleration and
  overshoot when they stop)

## Building

You need CMake 3.18+, a C++17 compiler and git. The first configure downloads
GLFW, GLM, Dear ImGui and miniaudio automatically, so an internet connection is
required once.

### macOS

```sh
xcode-select --install          # compiler, if you don't have it
brew install cmake              # or download CMake from cmake.org
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/living_marble
```

macOS still ships OpenGL 4.1 (deprecated but fully working); you will see
deprecation warnings while compiling, which are harmless.

### Windows (Visual Studio 2022)

```bat
cmake -S . -B build
cmake --build build --config Release
build\Release\living_marble.exe
```

Or open the folder in Visual Studio ("Open a local folder") and run the
`living_marble` target.

### Linux

```sh
sudo apt install build-essential cmake git libx11-dev libxrandr-dev \
     libxinerama-dev libxcursor-dev libxi-dev libgl1-mesa-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/living_marble
```

(Fedora: `sudo dnf install cmake gcc-c++ libX11-devel libXrandr-devel
libXinerama-devel libXcursor-devel libXi-devel mesa-libGL-devel`.)
Sound uses ALSA/PulseAudio/PipeWire at runtime; no audio dev packages needed.

On startup the pieces are sculpted and fractured procedurally — this takes a
few seconds (shown on the loading screen).

## Controls

| Input | Action |
|---|---|
| Left-click | Select a piece, then click a glowing square to move |
| Drag (any button), arrow keys | Turn the floating board — from above, the side or underneath |
| Mouse wheel | Zoom |
| N | New game |
| U / Ctrl+Z | Undo |
| F | Flip the view |
| R | Reset the view |
| B | Toggle battle animations (quick captures when off) |
| C | Toggle the cinematic battle camera |
| H | Hide / show the interface |
| F1 | Help |
| F12 | Save a screenshot (PNG) |
| Ctrl+Q | Quit |

Modes (in the panel): play White or Black against the AI, two players on one
screen (the board turns each move), or watch the AI play itself. The AI has
five strengths, from *Squire* (forgiving) to *Grandmaster* (about 6 seconds of
search per move).

## Command-line options

```
--fullscreen            run fullscreen
--size WxH              window size (default 1600x900)
--graphics 0..3         Low / Medium / High / Ultra (default High)
--quality 0|1|2         sculpting & board texture detail (default 1)
--demo                  watch the AI play itself
--level 0..4            AI strength
--fen "<FEN>"           start from a position
--mute                  no sound
--selftest              verify the move generator (perft)
```

Capture tools (handy for making videos): `--shot file.png [--frames N]`,
`--record folder` (writes a PNG per frame at a fixed 30 fps),
`--battle K` (stages one battle: 0 pawn … 5 king), `--style S` (forces battle
style 0–2 instead of a random one), `--camera yaw,pitch,dist`,
`--no-ui`.

If the frame rate is low, open *Graphics* in the panel and pick *Medium* or
*Low* (turns off depth of field, reflections and SSAO and lowers MSAA and the
shadow map size).

## How it is built

| File | What it does |
|---|---|
| `chess.*` | Rules: legal move generation (perft-verified), SAN, FEN, draws |
| `ai.*` | Alpha-beta engine (PVS, transposition table, null-move, LMR, quiescence) on a background thread |
| `sdf.*` | SDF primitives, lathe profiles, sparse Surface-Nets mesher with baked AO |
| `meshgen.*` | Sculpts the six pieces, Voronoi fracture chunks, slice halves, the sword, the board |
| `textures.*` | Bakes the inlaid wooden board (grain, pores, seams, brass, lettering) and 3D noise |
| `shaders.cpp` | All GLSL: PBR, PCSS, marble, lacquer, IBL generation, SSAO, DoF, bloom, tone mapping |
| `renderer.*` | Render passes: shadow → reflection → prepass → SSAO → MSAA HDR → DoF → bloom → composite |
| `animation.*` | The living-piece rig, rigid-body debris, props, lights, cinematic camera |
| `battle.*` | Choreography for moves, castling, promotion, checkmate and the six battles |
| `particles.*` | Dust, smoke, sparks, embers, magic, lightning |
| `audio.*` | Procedural sound synthesis and a small real-time mixer (miniaudio) |
| `app.*`, `ui.cpp` | Window, game flow, input/picking, Dear ImGui interface |
