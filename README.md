# Living Marble — 3D Battle Chess

A photoreal 3D chess game in C++ where carved marble Staunton pieces come alive:
they hop, lean, wobble and turn to face each other — and every capture is a
fight. Each piece type knows three attacks and picks one at random, and the loser breaks into
real fracture chunks that tumble across a lacquered wooden board floating in
mid-air — drag to turn it and look at the battle from any side, even underneath.

There is a second army to play with: **the medieval set** (*Pieces* in the
panel, or `--pieces medieval`). Men-at-arms, knights on caparisoned horses,
bishops and royals on their thrones and a gatehouse with a drawbridge — and a
dragon living inside it. Its figures walk, ride, stand up from their thrones
and fight with their own battles, sounds and music.

And a third: **the aliens** (`--pieces alien`). Little green men with ray
guns, flying saucers, big-brained psychics floating in glass jars, tripod war
machines, an alien queen and an overlord on a hovering egg-chair throne — with
their own (slightly ridiculous) battles and B-movie sound effects.

In every army, **the fallen come back as ghosts**: a captured piece slowly
takes shape again, translucent, beside the board, and watches the rest of the
game — cheering its side on.

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

## The ghosts

A captured piece doesn't stay gone. A moment after it dies, its ghost slowly
and silently takes shape out of rising wisps and joins its army's row of
ghosts floating at the side of the board — White's along the a-file, Black's
along the h-file, each row starting from its own end. They are the pieces
themselves in their own colours, smaller and see-through (you can see the
board through them), fading away towards their feet. This works the same for
the marble, medieval and alien armies (switching armies switches the ghosts
too).

The ghosts bob in the air and turn to watch whoever is on the move. When their
side captures a piece, gives check or mates, they jump for joy: arms and
spears in the air, mouths open, antennae up, the knight's horse rears, the
saucer's lights whirl, marble ghosts hop and wiggle. The only sound they make
is a soft, far-off cheer when one of their teammates takes out an opponent.
When their side loses a piece they hang their heads, and now and then one
gives a little wave to urge the others on. Taking a move back brings the
captured piece back to life and its ghost disappears.

## The medieval army

Switch armies with *Pieces* in the panel (on iPhone and iPad: Settings; on the
Mac also *Game › Pieces*). The pieces on the board swap in place; the first
time, the medieval figures are sculpted in the background (a few seconds)
while the game goes on. `--pieces medieval` starts with them.

- **Pawn** — a man-at-arms in a kettle hat and gambeson, with spear and kite shield
- **Knight** — a knight in a great helm on a caparisoned horse, with a lance
- **Bishop** — a bishop in mitre and cope with his crozier, enthroned on a gothic cathedra
- **Rook** — a gatehouse with battlements, a portcullis and a drawbridge on
  chains. It glides over the board — or stands up on stone legs and walks
- **Queen** and **King** — crowned, on their thrones (the king bearded, in an
  ermine mantle, with a sword; the queen with a sceptre). They glide across
  the board on their thrones, or get up, walk, fight and sit back down
- A **dragon** lives in each gatehouse

Every figure is a painted, multi-material mesh (stone, wood, steel, chain
mail, gold, cloth, skin, fur, horse coat, dragon scales, ...) skinned to a
small skeleton and animated on the GPU: walk cycles, the horse's walk, canter
and gallop, rearing and leaping, standing up from a throne, the drawbridge and
portcullis, the dragon's neck, jaw, wings and tail. White's army wears ivory
and azure over bright steel, Black's sable and gules over blackened steel.

| Attacker | Attacks |
|---|---|
| Pawn   | **Spear thrust** — marches up, levels the spear and lunges · **Shield bash** — raises the shield and charges, knocking the victim flying · **Javelin** — hurls the spear from afar, then walks over to pull it out of the rubble |
| Knight | **Joust** — couches the lance and charges; the lance shatters on impact · **Trample** — the horse rears over the victim and crashes down on it · **Sword sweep** — the rider draws his sword and cuts the victim in two at a gallop |
| Bishop | **Wrath of heaven** — the organ plays the *Dies irae* with a choir, a storm cloud gathers over the victim and lightning strikes it · **Brimstone** — a dark organ chord, a red cloud and burning stones raining down · **Pillar of salt** — a bell tolls three times and the victim turns to salt and crumbles |
| Rook   | **Siege stomp** — the gatehouse stands up on stone legs, walks over and stamps on the victim · **Dragon fire** — the drawbridge drops, a dragon crawls out of the gate and breathes fire · **Dragon's jaws** — the dragon lunges and bites the victim apart |
| Queen  | **Throne charge** — the throne rockets across the board and sends the victim flying · **Sceptre smite** — she gets up, walks over and brings the sceptre down in a burst of gold · **Sceptre sweep** — a backhanded sweep that knocks the victim off its feet |
| King   | **Royal cleave** — he rises, draws his sword overhead and cleaves the victim in two · **Throne ram** — the throne slams into the victim and smashes it · **Sweeping blow** — a level sweep of the sword |

Royals who get up to fight leave their throne behind; it glides after them and
they sit back down. Men-at-arms march, knights walk, canter and leap (the
knight's move is a jump), thrones and gatehouses glide with grinding stone;
when castling, the king's throne swings out in an arc while the gatehouse
slides past. A promoted man-at-arms
becomes a queen on her throne in a pillar of light; a mated king rises,
cracks, topples forward and shatters with his throne.

Sounds of the medieval army: a synthesised pipe organ and choir, thunder,
storm wind, the dragon's roar and fire, drawbridge chains, grinding stone,
giant footsteps, hooves and a whinny, a church bell, splintering lances and
armoured steps.

## The aliens

The third army (*Pieces › Aliens*, or `--pieces alien`) is sculpted the same
way, also in the background the first time.

- **Pawn** — a little green man in a silver flight suit, with antennae, big
  glossy black eyes and a ray gun. He waddles
- **Knight** — a flying saucer with a little pilot under a glass canopy, a
  ring of running lights and landing legs. It takes off, flies its move and
  lands (the knight's move is a hop)
- **Bishop** — a psychic with an enormous brain in a glass jar, floating
  cross-legged on a hover disc, with a third eye
- **Rook** — a tripod war machine: a domed pod with a glowing eye on three
  long legs, with tentacles. It strides across the board
- **Queen** — the alien queen: a crested head, a shell bodice, a long gown and
  a tail with a spade on the end
- **King** — the overlord: an enormous veined head with a monocle and a tiny
  crown, a royal robe and medals, on a hovering egg-chair throne with a big
  red button

The heads, eyes, glass, chrome and lights get their own materials (glossy
eyes, see-through glass canopies and jars, glowing running lights). White's
aliens are green in silver suits with cyan lights, Black's purple in black
suits with magenta lights.

| Attacker | Attacks |
|---|---|
| Pawn   | **Ray gun** — two wild misses (the victim hops out of the way), then a hit that disintegrates it; he blows the smoke off the muzzle · **Goo grenade** — a blob of slime lobbed at the victim, which melts into a puddle · **Tongue** — it shoots out, wraps the victim, reels it in — gulp — and he burps up the pieces |
| Knight | **Abduction** — the saucer beams the victim up, there are noises inside, and out it comes again in pieces · **Crop circle** — a laser carves a glowing ring into the board around the victim, then cuts it in two from above · **Cow drop** — a cow falls out of the saucer onto the victim, bounces, moos and is beamed back up |
| Bishop | **Mind blast** — the victim swells under the strain and pops · **Hypnosis** — the victim dances in a daze, then blasts off into the sky and twinkles · **Telekinetic crush** — lifted into the air and squeezed until it bursts |
| Rook   | **Heat ray** — the tripod rises up tall, sounds its horn and burns the victim to a cinder · **Stomp** — it raises a foot over the victim, which hops aside; a miss, so it tries again · **Tentacle toss** — the tentacles grab the victim, lift it, give it a shake and fling it away |
| Queen  | **Acid spit** — she rears back and spits a glob of acid; the victim sizzles and melts · **Brood** — she lays an egg, which hops over to the victim and hatches a swarm of hungry little critters · **Tail strike** — she turns her back and her tail lashes round, swatting the victim away |
| King   | **Shrink ray** — zap: the victim shrinks to a squeaking speck, and the throne floats over and sits on it · **The mothership** — he summons it; it fills the sky, beams the victim to dust and leaves · **Disintegrator** — it sputters; he gives it a tap, and it fires far too hard, blowing the victim apart and his throne backwards |

Waiting aliens fidget: they look about with their antennae swivelling, hop
nervously and yawn; saucers bob and whirl their light rings, tripods shift
their weight and peer about. When castling, the tripod strides past and the
overlord's throne swings round in an arc behind it. A promoted little green
man becomes a queen in a pillar of light; a mated overlord's throne sputters,
drops to the board and shatters.

Sounds of the aliens: ray-gun pews, a theremin, the saucer's hum and its
tractor beam, gibbering, slurps and burps, boings, splats, a moo, the tripod's
horn and heat ray, the shrink ray, sizzling acid, a hatching egg, the
mothership's drone and a sputtering disintegrator.

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

To install it with a launcher entry and icon (the game is a single
self-contained binary):

```sh
cmake --install build --prefix ~/.local   # just for you, no sudo
sudo cmake --install build                # or for everyone, into /usr/local
```

Then start *Living Marble* from the app menu, or run `living_marble`.

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
opponent's strength, the army and the graphics quality. ⌃⌘F switches to full screen.

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
the army (*Pieces*), battle and camera switches, volume, graphics quality, *Hide Interface* and
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
--pieces marble|medieval|alien  which army to play with (default marble)
--mute                  no sound
--selftest              verify the move generator (perft)
```

Capture tools (handy for making videos): `--shot file.png [--frames N]`,
`--record folder` (writes a PNG per frame at a fixed 30 fps),
`--battle K` (stages one battle: 0 pawn … 5 king), `--style S` (forces battle
style 0–2 instead of a random one), `--shots t1,t2,...` (screenshots at those
times, with `--battle K` or with `--fen "<FEN>" --move e2e4` for any move),
`--camera yaw,pitch,dist[,tx,ty,tz]`, `--no-cinematic` (keeps that camera still
through the battles), `--scenario 4` (a gallery of ghosts on both sides),
`--no-ui`. All of them work with `--pieces medieval` and `--pieces alien`.

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
| `medieval.h`, `medieval_mesh.cpp` | The medieval army: figures built from blended, painted SDF parts, each bound to a bone; skeletons and skin weights |
| `medieval_pose.cpp` | Turns animation controls into bone matrices: walking, riding, standing up, drawbridge, dragon |
| `alien.h`, `alien_mesh.cpp`, `alien_pose.cpp` | The aliens: their figures, skeletons and poses (waddling, hovering, the saucer's flight, the tripod's legs, tongues, tails) |
| `sculpt.h`, `pose_kit.h` | The sculpting and posing kit shared by the medieval army and the aliens |
| `textures.*` | Bakes the inlaid wooden board (grain, pores, seams, brass, lettering) and 3D noise |
| `shaders.cpp` | All GLSL: PBR, PCSS, marble, lacquer, IBL generation, SSAO, DoF, bloom, tone mapping |
| `renderer.*` | Render passes: shadow → reflection → prepass → SSAO → MSAA HDR → DoF → bloom → composite |
| `animation.*` | The living-piece rig, rigid-body debris, props, lights, cinematic camera, the ghosts |
| `battle.*`, `battle_kit.h` | Choreography for moves, castling, promotion, checkmate and the marble battles |
| `battle_medieval.cpp` | The medieval army's moves, castling, checkmate and its 18 battles |
| `battle_alien.cpp` | The aliens' moves, castling, checkmate and their 18 battles |
| `particles.*` | Dust, smoke, sparks, embers, magic, lightning |
| `audio.*` | Procedural sound synthesis (including a pipe organ and choir) and a small real-time mixer (miniaudio) |
| `app.*`, `ui.cpp` | Window, game flow, input/picking, Dear ImGui interface |

The native Mac version in `macos/Sources/LivingMarble` is a Swift port of the
same design:

| File | What it does |
|---|---|
| `Chess.swift`, `AI.swift` | Rules and the alpha-beta engine (as `chess.*`, `ai.*`) |
| `SDF.swift`, `MeshGen.swift`, `Textures.swift` | Procedural sculpting, fracture and the baked board (as `sdf.*`, `meshgen.*`, `textures.*`) |
| `Medieval.swift`, `MedievalMesh.swift`, `MedievalPose.swift` | The medieval army, its skeletons and poses (as `medieval*`) |
| `Alien.swift`, `AlienMesh.swift`, `AlienPose.swift` | The aliens, their skeletons and poses (as `alien*`) |
| `Sculpt.swift`, `PoseKit.swift` | The shared sculpting and posing kit (as `sculpt.h`, `pose_kit.h`) |
| `Shaders.swift` | All Metal Shading Language, compiled at startup |
| `Renderer.swift` | The same render passes on Metal |
| `Animation.swift`, `Particles.swift` | The living-piece rig, debris, props, lights, camera, particles and the ghosts |
| `BattleCore.swift`, `Battles*.swift` | Moves, castling, promotion, checkmate and all 18 battle styles |
| `BattlesMedieval*.swift` | The medieval army's choreography and battles (as `battle_medieval.cpp`) |
| `BattlesAlien*.swift` | The aliens' choreography and battles (as `battle_alien.cpp`) |
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
