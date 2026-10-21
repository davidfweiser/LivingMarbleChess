# Living Marble — 3D Battle Chess

A photoreal 3D chess game in C++ where carved marble Staunton pieces come alive:
they hop, lean, wobble and turn to face each other — and every capture is a
fight. Each piece type knows three attacks and picks one at random, and the loser breaks into
real fracture chunks that tumble across a lacquered wooden board floating in
mid-air — drag to turn it and look at the battle from any side, even underneath.

Everything is procedural. There are **no model, texture or sound files**:
the pieces are sculpted from signed-distance fields at startup, the board's
inlaid wood is baked on the fly, and every sound effect is synthesised.

On the Mac the game is a native app written in Swift: Metal graphics, an
AppKit/SwiftUI window, menus and interface, and AVAudioEngine sound (see
[`macos/`](macos)). The same Swift code runs on iPhone and iPad with touch
controls (see [`ios/`](ios)). Windows and Linux use the C++/OpenGL version in
`src/`. All versions play, look and sound the same.

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

### macOS (native app)

You need Xcode 16 or later. The app runs on macOS 14 Sonoma or later. There
are no dependencies to download.

```sh
./build.sh                      # or: macos/build-app.sh [--universal]
open "macos/build/Living Marble.app"
```

`--universal` builds for both Apple silicon and Intel Macs. To work on the
code in Xcode, open `macos/Package.swift`.

### iPhone and iPad

You need Xcode 16 or later. The app runs on iOS / iPadOS 17 or later. Open
`ios/LivingMarble.xcodeproj`, pick a simulator or your device and press Run.
To run on a device, first choose your team under *Signing & Capabilities*
(a free Apple ID works).

```sh
xcodebuild -project ios/LivingMarble.xcodeproj -scheme LivingMarble \
           -destination 'generic/platform=iOS Simulator' build
```

The iOS app builds the Mac app's sources in `macos/Sources/LivingMarble`
(all but the AppKit window, menu and mouse files) together with the touch
shell in `ios/App`, so the two never drift apart.

### Windows and Linux (C++)

You need CMake 3.18+, a C++17 compiler and git. The first configure downloads
GLFW, GLM, Dear ImGui and miniaudio automatically, so an internet connection is
required once.

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
| Mouse wheel (Mac: also two-finger scroll or pinch) | Zoom |
| N (Mac: also ⌘N) | New game |
| U / Ctrl+Z (Mac: ⌘Z) | Undo |
| F | Flip the view |
| R | Reset the view |
| B | Toggle battle animations (quick captures when off) |
| C | Toggle the cinematic battle camera |
| H | Hide / show the interface |
| F1 (Mac: also ? or ⌘?) | Help |
| F12 (Mac: also ⇧⌘S) | Save a screenshot (PNG; on the Mac it goes to Pictures › Living Marble) |
| Ctrl+Q (Mac: ⌘Q) | Quit |

On the Mac every action is also in the menu bar, including the mode, the
opponent's strength and the graphics quality. ⌃⌘F switches to full screen.

On iPhone and iPad:

| Touch | Action |
|---|---|
| Tap | Select a piece, then tap a glowing square to move |
| Drag a piece | Carry it onto a glowing square to move it there |
| Drag anywhere else | Turn the floating board |
| Pinch | Zoom |
| Twist two fingers | Turn the board |
| Slide two fingers up or down | Tilt the board |

The toolbar has New game, Undo, Flip, Reset view, the move list (on iPhone;
iPad shows it beside the board) and Settings, which holds the mode, opponent,
battle and camera switches, volume, graphics quality, *Hide Interface* and
*Save Screenshot* (the board without the interface, saved to the Files app).
The board fills the screen in portrait as well as landscape. On iPad a
trackpad or mouse also hovers and scrolls to zoom, and a keyboard takes the
same keys as on the Mac (including ⌘Z and ⌘N).

Modes (in the panel): play White or Black against the AI, two players on one
screen (the board turns each move), or watch the AI play itself. The AI has
five strengths, from *Squire* (forgiving) to *Grandmaster* (about 6 seconds of
search per move).

## Command-line options

The Mac app takes the same options when started from Terminal:
`"macos/build/Living Marble.app/Contents/MacOS/LivingMarble" --demo`. There,
`--size` is in points, so screenshots on a Retina screen come out at twice that
many pixels, and screenshots show the 3D scene without the interface panels.

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

The native Mac version in `macos/Sources/LivingMarble` is a Swift port of the
same design:

| File | What it does |
|---|---|
| `Chess.swift`, `AI.swift` | Rules and the alpha-beta engine (as `chess.*`, `ai.*`) |
| `SDF.swift`, `MeshGen.swift`, `Textures.swift` | Procedural sculpting, fracture and the baked board (as `sdf.*`, `meshgen.*`, `textures.*`) |
| `Shaders.swift` | All Metal Shading Language, compiled at startup |
| `Renderer.swift` | The same render passes on Metal |
| `Animation.swift`, `Particles.swift` | The living-piece rig, debris, props, lights, camera and particles |
| `BattleCore.swift`, `Battles*.swift` | Moves, castling, promotion, checkmate and all 18 battle styles |
| `Audio.swift` | Procedural sounds mixed in real time through AVAudioEngine |
| `GameController.swift`, `GameView.swift` | Game flow, mouse/trackpad/keyboard input, picking, capture tools |
| `Interface.swift`, `AppDelegate.swift` | Native SwiftUI interface, window and menu bar |

The iPhone and iPad app in `ios/App` adds only the touch shell around those
sources (`main.swift`, `AppDelegate.swift` and `GameView.swift` are Mac only):

| File | What it does |
|---|---|
| `GameView.swift` | Touch input: taps, piece drags, orbit, pinch / twist / tilt; iPad pointer, scrolling and keyboard |
| `TouchInterface.swift` | Touch layout for phones (portrait and landscape) and tablets, settings, move list and help sheets |
| `LivingMarbleApp.swift` | App lifecycle: full-screen scene, pausing in the background, audio resume |
