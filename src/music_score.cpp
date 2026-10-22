// The background music's score: the instruments and three songs for each piece
// (the notation is explained at the top). The Mac and iOS version has the same
// text in MusicScore.swift; keep the two identical.
#include "music.h"

namespace music {

extern const char* const kScore;
const char* const kScore =
R"SCORE(# ===========================================================================
# The instruments
#   patch <name> synth <saw|sqr|tri|sin|organ|organ2|flute> | fm | ks | drum <type>  settings...
#   a d s r: envelope (attack s, decay and release s to -60 dB, sustain level)   cut: filter at cut x the note (hz: plus Hz)
#   fenv fatk fdec: filter envelope (octaves, s)   vib: rate,semitones,delay   gain pan rev del
# ===========================================================================
# drums
patch kick    drum membrane f=50 pamt=2.2 pdec=0.03 d=0.7 snap=0.3 nf=2500 ndec=0.006 drive=1.3 gain=0.9 rev=0.02
patch boom    drum membrane f=45 pamt=2.5 pdec=0.05 d=1.5 snap=0.2 nf=1500 ndec=0.008 drive=1.6 gain=0.8 rev=0.05
patch snare   drum snare f=190 nf=1500 d=0.35 tone=0.5 snap=0.8 gain=0.45 rev=0.18
patch clap    drum clap nf=1150 d=0.35 gain=0.7 rev=0.22
patch hat     drum metal nf=7000 d=0.1 metal=0.5 gain=0.5 rev=0.03
patch ohat    drum metal nf=6500 d=0.5 metal=0.5 gain=0.28 rev=0.05
patch crash   drum metal nf=4000 d=3.0 metal=0.35 a=0.002 gain=0.24 rev=0.3
patch shaker  drum shaker nf=6000 a=0.012 d=0.12 gain=0.35 rev=0.05
patch tamb    drum metal f=260 nf=7500 d=0.3 metal=0.65 gain=0.4 rev=0.08
patch tom     drum membrane f=110 pamt=0.6 pdec=0.06 d=0.7 snap=0.4 nf=900 ndec=0.02 gain=0.65 rev=0.2
patch timp    drum membrane f=98 pamt=0.06 pdec=0.04 d=2.2 p2=1.5 p2amp=0.4 snap=0.35 nf=500 ndec=0.025 gain=0.4 rev=0.35
patch taiko   drum membrane f=60 pamt=0.6 pdec=0.06 d=1.4 snap=1.0 nf=380 ndec=0.07 drive=1.5 gain=0.45 rev=0.35
patch block   drum membrane f=880 pamt=0.15 pdec=0.008 d=0.12 p2=2.7 p2amp=0.35 snap=0.25 nf=4000 ndec=0.004 gain=0.4 rev=0.12
# basses
patch bass    synth saw sub=0.5 a=0.003 d=0.8 s=0.6 r=0.2 cut=3 res=0.15 fenv=2.5 fdec=0.1 gain=0.42 rev=0.02
patch fatbass synth saw uni=2 det=10 sub=0.6 a=0.003 d=1.0 s=0.7 r=0.2 cut=2.5 res=0.25 fenv=2 fdec=0.15 drive=1.5 gain=0.36 rev=0.02
patch slap    synth sqr pw=0.4 sub=0.4 a=0.002 d=0.6 s=0.35 r=0.15 cut=2 res=0.35 fenv=3.5 fdec=0.06 gain=0.42 rev=0.03
patch tuba    synth saw sub=0.3 a=0.025 d=0.7 s=0.75 r=0.25 cut=1.5 res=0.1 fenv=1.6 fatk=0.03 fdec=0.15 gain=0.5 rev=0.12
patch pizz    ks bright=0.45 damp=0.5 decay=0.6 r=0.2 gain=0.75 rev=0.2
# plucked strings, keys and mallets
patch harp    ks bright=0.7 damp=0.3 decay=2.2 r=0.8 gain=0.5 rev=0.35 del=0.12
patch harpsi  ks bright=1.0 damp=0.12 decay=1.2 r=0.25 gain=0.42 rev=0.22
patch guitar  ks bright=0.85 damp=0.2 decay=1.6 r=0.3 drive=1.2 gain=0.7 rev=0.18 del=0.3
patch marimba fm ratio=4 index=1.3 idec=0.025 p2=10 p2amp=0.08 p2dec=0.02 a=0.001 d=0.9 s=0 r=0.3 gain=0.55 rev=0.15
patch glock   fm ratio=3.5 index=0.7 idec=0.15 p2=2.76 p2amp=0.25 p2dec=0.25 a=0.001 d=2.5 s=0 r=1.2 gain=0.3 rev=0.3 del=0.18
patch bell    fm ratio=1.4 index=2.4 idec=0.5 a=0.001 d=3.5 s=0 r=1.8 gain=0.42 rev=0.35 del=0.2
patch celesta fm ratio=1 index=1.1 idec=0.2 p2=4 p2amp=0.15 p2dec=0.12 a=0.001 d=2.2 s=0 r=0.9 gain=0.38 rev=0.3
patch clav    synth sqr pw=0.25 a=0.001 d=0.5 s=0.2 r=0.12 cut=4 res=0.4 fenv=2 fdec=0.05 gain=0.22 rev=0.1
patch organ   synth organ a=0.006 d=0.3 s=1 r=0.2 trem=6.5,0.2 gain=0.18 rev=0.25
patch church  synth organ2 sub=0.25 a=0.03 d=0.3 s=1 r=0.5 gain=0.24 rev=0.45
# leads
patch chip    synth sqr pw=0.5 a=0.004 d=0.5 s=0.7 r=0.2 vib=5.5,0.15,0.2 gain=0.35 rev=0.12 del=0.15
patch fife    synth flute noise=0.05 a=0.02 d=0.3 s=0.85 r=0.15 vib=6,0.18,0.15 cut=6 gain=0.48 rev=0.15
patch whistle synth sin noise=0.015 a=0.02 d=0.3 s=0.9 r=0.2 vib=5.5,0.25,0.12 gain=0.4 rev=0.2 del=0.15
patch lead    synth saw uni=2 det=10 a=0.005 d=1.0 s=0.7 r=0.4 cut=5 res=0.2 fenv=1.5 fdec=0.25 vib=5.5,0.15,0.3 gain=0.36 rev=0.15 del=0.25
patch supersaw synth saw uni=3 det=18 a=0.008 d=1.0 s=0.75 r=0.6 cut=6 res=0.1 fenv=1 fdec=0.3 gain=0.3 rev=0.25 del=0.25
patch dist    synth saw uni=2 det=8 a=0.003 d=1.4 s=0.8 r=0.25 drive=4 hz=2800 res=0.1 gain=0.16 rev=0.12
# brass and strings
patch brass   synth saw uni=2 det=7 a=0.02 d=0.9 s=0.75 r=0.35 cut=2 res=0.15 fenv=2.2 fatk=0.04 fdec=0.25 vib=5,0.12,0.35 scoop=0.6 gain=0.42 rev=0.22
patch trumpet synth saw a=0.012 d=0.7 s=0.8 r=0.25 cut=2.5 res=0.2 fenv=2.5 fatk=0.03 fdec=0.2 vib=5.5,0.14,0.3 scoop=0.8 drive=1.3 gain=0.45 rev=0.25
patch horn    synth saw uni=2 det=5 a=0.05 d=1.0 s=0.8 r=0.5 cut=1.3 res=0.1 fenv=1.5 fatk=0.08 fdec=0.3 scoop=0.4 gain=0.45 rev=0.3
patch stab    synth saw uni=2 det=8 a=0.004 d=0.6 s=0 r=0.25 cut=3 res=0.2 fenv=2.5 fdec=0.1 gain=0.3 rev=0.2
patch strings synth saw uni=3 det=14 a=0.12 d=1.4 s=0.85 r=1.2 cut=4 res=0.05 vib=5,0.08,0.4 gain=0.2 rev=0.35
patch spicc   synth saw uni=2 det=10 a=0.004 d=0.45 s=0 r=0.2 cut=4 res=0.1 fenv=1 fdec=0.08 gain=0.3 rev=0.25
patch disco   synth saw uni=3 det=12 a=0.01 d=0.9 s=0.4 r=0.5 cut=4 res=0.1 fenv=1 fdec=0.15 gain=0.2 rev=0.3
patch violin  synth saw a=0.05 d=1.0 s=0.85 r=0.5 cut=4 res=0.15 vib=5.5,0.2,0.2 gain=0.42 rev=0.3
patch pad     synth saw uni=3 det=16 a=0.35 d=1.7 s=0.8 r=2.0 cut=2 res=0.1 gain=0.14 rev=0.45
patch choir   synth saw uni=3 det=14 filt=vox a=0.15 d=1.4 s=0.85 r=1.4 vib=5.2,0.1,0.3 gain=0.22 rev=0.45

# ===========================================================================
# The songs, three for each piece
#   song piece= bpm= beat=<steps a beat, 4> bar=<steps a bar, 16> bars= swing= echo=<steps> gain= title=
#   chords: one a bar unless given a length (C:8 G = C for 8 steps, G the rest of the bar)
#   drum <instrument> [settings]: x hit, X accent, o soft, . rest - one character a step
#   pat <instrument> [settings]: chord tones, one a step - 1 3 5 7 8 10 12 (and 2 4 6 9 ...),
#        c the chord, p a power chord, ' up and , down an octave, - hold, . rest
#   mel <instrument> [settings]: notes with lengths in steps (C5:4, the length carries on),
#        r rest, _ hold, ! accent, ? soft; "same" repeats the part before on this instrument
#   settings: vol pan rev del gate tr (semitones) lo (lowest note of a pat) bars=1-8,13
#   a bar can end with *N to play it N times; every bar is checked for its length
# ===========================================================================

# ---------------------------------------------------------------- the pawn: bouncy, eager, humble
song piece=pawn bpm=132 bars=16 gain=0.86 title=Hop_Along
chords: C | G | Am | F | C | G | F:8 G:8 | C | F | G | Em | Am | Dm | G | C | G7
drum kick: x.....x.x...x...
drum clap: ....x.......x...
drum hat: ..x...x...x...x.
drum shaker: .ooo.ooo.ooo.ooo
drum crash bars=1,9: x...............
mel tom bars=8,16: r:8 G3:1 G3 E3 E3 C3:2 C3:2
pat pizz lo=C2: 1 . 8 . 5 . 8 . 1 . 8 . 5 . 8 .
pat marimba lo=E4 vol=0.6: . . c . . . c . . . c . . . c .
mel chip: | E5:2 E5 G5 E5 C5 D5 E5:4 | D5:2 D5 F5 D5 B4 C5 D5:4 | C5:2 E5 A5 G5 E5 C5 A4 C5
  | A4:2 C5 F5:4 E5:2 D5 C5:4 | E5:2 E5 G5 E5 C5 D5 E5:4 | D5:2 B4 G4 B4 D5 F5 G5:4
  | A5:2 G5 F5 E5 D5 E5 F5 D5 | C5:4 G4:2 C5 r:8
  | A5:3 G5:1 F5:2 C5 A5:3 G5:1 F5:4 | B5:3 A5:1 G5:2 D5 B5:3 A5:1 G5:4 | G5:2 E5 B4 E5 G5 B5 G5 E5
  | A5:4 E5:2 C5 A4:4 r:4 | F5:2 F5 A5 F5 D5 E5 F5:4 | G5:2 G5 B5 G5 D5 E5 F5:4
  | E5:2 G5 C6:4 B5:2 A5 G5:4 | F5:2 E5 D5 B4 G4:4 r:4 |
mel marimba tr=-12 vol=0.5: same
mel glock bars=9-16 vol=0.5: same

song piece=pawn bpm=116 bars=16 gain=1.08 title=Fife_and_Drum
chords: G | G | D | D | G | C | D | G | C | G | D | G | C | G | D7 | G
drum kick: x.......x.......
drum snare: X.o.x.o.X.o.x.oo *3 | X.o.x.o.xxxxXxXX
drum crash bars=1,9: x...............
pat tuba lo=G1: 1 - . . 5, - . . 1 - . . 5, - . .
pat horn lo=B3 vol=0.45 gate=0.5: . . c . . . c . . . c . . . c .
mel fife: | D5:2 G5 G5 A5 B5:4 G5 | B5:2 C6 D6:4 B5 G5 | A5:2 B5 A5 G5 F#5:4 D5 | E5:2 F#5 G5 A5 F#5:4 r
  | D5:2 G5 G5 A5 B5:4 G5 | C6:2 B5 C6 E6 D6:4 C6 | B5:2 A5 G5 F#5 A5:4 F#5 | G5:4 D5:2 B4 G4:4 r
  | E6:4 D6:2 C6 G5:4 E5 | D6:4 C6:2 B5 G5:4 D5 | F#5:2 G5 A5 B5 C6 B5 A5 F#5 | G5:4 B5 D6 r
  | E6:3 D6:1 C6:2 E6 G6:4 E6 | D6:3 C6:1 B5:2 D6 G6:4 D6 | C6:2 B5 A5 G5 F#5 G5 A5 C6 | B5:4 G5 G5:2 G5 G5:4 |
mel glock bars=9-16 vol=0.45: same

song piece=pawn bpm=108 swing=0.22 bars=16 gain=0.93 title=Pawn_Shuffle
chords: Dm7 | G7 | Dm7 | G7 | Bbmaj7 | Am7 | Gm7 | A7 | Dm7 | G7 | Dm7 | G7 | Bbmaj7 | C7 | Dm7 | A7
drum kick: x..x....x.x..x..
drum snare: ....x..o.o..x...
drum hat: x.x.x.x.x.x.x.xo
drum tamb: ....x.......x...
drum crash bars=1,9: x...............
pat slap lo=D1: 1 . . 8 . . 1 . 5 . 7 . 8 . 5 .
pat clav lo=F3 gate=0.4: . . c . . c . . . . c . . c . c
pat organ lo=F3 gate=1 vol=0.7 bars=9-16: c - - - - - - - - - - - - - - -
mel brass: | r:2 D5:1 F5 r G5 A5:2 r C6 A5 G5 | r:2 B4:1 D5 r F5 G5:2 r F5:1 E5 D5:4
  | r:2 D5:1 F5 r G5 A5:2 r D6 C6 A5 | B5:2 G5 F5 D5 F5:1 G5 r:6
  | F5:3 D5 Bb4:2 A5:3 F5 D5:2 | E5:3 C5 A4:2 G5:3 E5 C5:2 | D5:2 F5 G5 Bb5 A5 G5 F5 D5 | C#5:2 E5 G5 A5 C#6:4 r
  | A5:4 G5:2 F5 D5:4 r:2 C5 | D5:2 F5 D5 B4 G4:4 r | A5:4 C6:2 A5 D6:4 r:2 C6 | B5:2 A5 G5 F5 D5:4 r
  | D6:2 C6 Bb5 A5 F5:4 D5 | E5:2 G5 Bb5 C6 E6:4 r | F6:2 E6 D6 C6 A5 G5 F5 D5 | E5:2 C#5 A4 C#5 E5 G5 A5:4 |
mel brass tr=-12 vol=0.5: same

# ---------------------------------------------------------------- the knight: galloping, leaping, heroic
song piece=knight bpm=152 bars=16 gain=1.02 title=Gallop
chords: Em | Em | C | D | Em | Em | C | B7 | G | D | Em | C | G | D | C:8 B7:8 | Em
drum block vol=0.8: xxX.xxX.xxX.xxX.
drum kick: x.......x.......
drum snare: ....x.......x...
drum hat: ..x...x...x...x.
drum crash bars=1,9: x...............
mel tom bars=8,16: r:8 B3:1 B3 G3 G3 E3:2 E3:2
pat bass lo=E1: 1 1 8 . 1 1 8 . 1 1 8 . 5 5 8 .
pat spicc lo=E3 vol=0.6: c c c . c c c . c c c . c c c .
mel trumpet: | E5:4 B4:2 E5 G5:4 B5 | A5:2 G5 F#5 G5 E5:8 | E5:4 C5:2 E5 G5:4 C6 | B5:2 A5 G5 A5 F#5:8
  | E5:4 B4:2 E5 G5:4 B5 | E6:4 D6:2 B5 G5:4 E5 | C6:2 B5 A5 G5 A5 B5 C6:4 | B5:4 A5:2 F#5 D#5:8
  | D5:2 G5 B5:4 D6 B5 | A5:2 F#5 D5:4 A5 F#5 | G5:2 F#5 E5 F#5 G5:4 B5 | C6:4 B5:2 A5 G5:4 E5
  | D6:3 D6:1 D6:2 B5 G5:4 D6 | E6:3 E6:1 E6:2 D6 A5:4 F#5 | C6:2 B5 A5 G5 F#5 G5 A5 B5 | E6:4 B5 E5 r |
mel strings tr=-12 vol=0.7 bars=9-16: same

song piece=knight bpm=168 bars=16 gain=1.08 title=Wild_West_Leap
chords: Am | Am | G | G | F | F | E | E | Dm | Am | Dm | Am | F | G | E | E
drum kick: x.......x.......
drum snare: o.o.X.o.o.o.X.o.
drum hat: x.x.x.x.x.x.x.x.
drum block vol=0.6: x..x..x.x..x..x.
drum crash bars=1,9: x...............
pat bass lo=E1: 1 . 1 . 5 . 5 . 1 . 1 . 5 . 8 .
pat guitar lo=A3 vol=0.4 gate=0.3: . . c . . . c . . . c . . . c .
mel guitar: | A4:2 C5 E5 A5:4 G5:2 E5:4 | A5:2 G5 E5 C5 D5:4 E5 | G4:2 B4 D5 G5:4 F5:2 D5:4 | G5:2 F5 D5 B4 D5:4 B4
  | F4:2 A4 C5 F5:4 E5:2 C5:4 | F5:2 E5 C5 A4 C5:4 A4 | B4:2 E5 G#5:4 B5 G#5 | E5:4 D5:2 C5 B4:4 G#4
  | A5:4 D6 C6:2 A5 F5:4 | E5:4 A5 G5:2 E5 C5:4 | A5:4 D6 E6:2 F6 D6:4 | C6:4 B5:2 A5 E5:8
  | F5:2 G5 A5 C6 A5 G5 F5:4 | G5:2 A5 B5 D6 B5 A5 G5:4 | G#5:2 A5 B5:4 E6 D6 | B5:4 G#5 E5:8 |
mel whistle bars=9-16 vol=0.7: same

song piece=knight bpm=140 bars=16 gain=0.97 title=The_Joust
chords: D | D | G | A | D | Bm | G | A | Bm | G | D | A | Bm | G | Em | A7
drum kick: x.......x.......
drum snare: ....x.......x.xx
drum hat: x.x.x.x.x.x.x.x.
drum crash bars=1,5,9,13: x...............
pat timp lo=F2: 1 . . . . . . . 1 . . . 5, . 1 .
pat spicc lo=D3 vol=0.7: 1 5 8 5 10 5 8 5 1 5 8 5 10 5 8 5
pat strings lo=A3 gate=1 vol=0.5: c - - - - - - - - - - - - - - -
pat bass lo=D1: 1 . . 1 . . 1 . 1 . . 1 . . 5 .
mel brass: | A4:4 D5 F#5:6 E5:1 D5 | E5:4 F#5:2 G5 A5:8 | B5:4 A5:2 G5 D5:4 G5 | A5:6 G5:1 F#5 E5:8
  | A4:4 D5 F#5:6 E5:1 D5 | F#5:4 B5 D6:6 C#6:1 B5 | B5:4 G5:2 B5 D6:4 B5 | C#6:4 B5:2 C#6 A5:8
  | D6:4 C#6:2 B5 F#5:8 | G5:4 A5:2 B5 D5:8 | F#5:4 G5:2 A5 D6:6 C#6:1 B5 | A5:4 B5:2 C#6 E6:8
  | F#6:4 E6:2 D6 B5:8 | D6:4 B5:2 A5 G5:8 | G5:4 B5 E6 D6:2 B5 | C#6:4 B5:2 G5 A5:8 |
mel horn tr=-12 vol=0.6: same

# ---------------------------------------------------------------- the bishop: shining, gliding, heavenly
song piece=bishop bpm=122 bars=16 echo=3 gain=0.94 title=Glide
chords: Fmaj7 | C | Dm7 | Bbmaj7 | Fmaj7 | C | Bbmaj7 | C | Dm7 | Am7 | Bbmaj7 | F | Gm7 | Am7 | Bbmaj7 | C7
drum kick: x...x...x...x...
drum clap: ....x.......x...
drum ohat: ..x...x...x...x.
drum shaker: .ooo.ooo.ooo.ooo
drum crash bars=1,9: x...............
pat bass lo=F1: . . 1 . . . 1 . . . 1 . . . 1 .
pat harp lo=F4 vol=0.4 gate=1.5: 1 5 8 10 12 10 8 5 1 5 8 10 12 10 8 5
pat choir lo=A3 gate=1 vol=0.6: c - - - - - - - - - - - - - - -
mel bell: | C6:3 A5 F5:2 E5:4 F5 | G5:3 E5 C5:2 D5:4 E5 | F5:3 D5 A4:2 C5:4 D5 | D5:6 C5:2 A4:8
  | C6:3 A5 F5:2 E5:4 F5 | G5:3 E5 C6:2 D6:4 E6 | F6:4 D6 A5 Bb5 | G5:8 r
  | A5:2 C6 D6:4 C6:2 A5 F5:4 | E5:2 G5 A5:4 G5:2 E5 C5:4 | D5:2 F5 A5:4 Bb5:2 A5 F5:4 | C6:8 A5:4 F5
  | Bb5:2 A5 G5 F5 D5:4 F5 | E5:2 G5 A5 C6 E6:4 C6 | D6:4 C6:2 Bb5 A5:4 F5 | E5:4 G5 Bb5 C6 |
mel celesta tr=12 vol=0.4 bars=9-16: same

song piece=bishop bpm=112 beat=2 bar=8 swing=0.33 bars=16 gain=1.07 title=Glory_Glory
chords: Bb | Eb | Bb | F7 | Gm | Eb | F | Bb | Eb | Bb | Eb | F | Gm | Cm7 | F7 | Bb
drum kick: x...x...
drum clap: ..x...x.
drum tamb: oxoxoxox
drum crash bars=1,9: x.......
pat bass lo=Bb1: 1 . 5 . 8 . 5 6
pat organ lo=F3 gate=0.6: . c . c . c . c
pat choir lo=F3 gate=1 vol=0.5: c - - - - - - -
mel church vol=1.4: | F5:2 D5:1 F5 G5:2 F5 | G5:2 Bb5:1 G5 Eb5:4 | D5:1 F5 Bb5:2 A5:1 G5 F5:2 | Eb5:2 C5:1 A4 F5:4
  | G5:2 Bb5:1 G5 D5:2 G5 | Bb5:2 C6:1 Bb5 G5:2 Eb5 | A5:1 Bb5 C6:2 A5:1 F5 Eb5:2 | D5:4 Bb4
  | G5:2 G5:1 Bb5 Eb6:2 D6:1 C6 | D6:2 C6:1 Bb5 F5:4 | G5:2 G5:1 Bb5 Eb6:2 F6:1 Eb6 | C6:2 A5:1 F5 C6:4
  | Bb5:2 A5:1 G5 D5:2 G5 | Eb5:1 G5 Bb5:2 C6 Bb5:1 G5 | A5:1 C6 Eb6:2 D6:1 C6 A5:2 | Bb5:4 F5:2 D5 |
mel choir tr=-12 vol=0.6: same

song piece=bishop bpm=130 bars=16 echo=3 gain=0.97 title=Seraph_Dance
chords: Dm | Bb | F | C | Dm | Bb | Gm | A | Bb | C | Dm | Dm | Bb | C | A | A7
drum kick: x...x...x...x...
drum clap: ....x.......x...
drum ohat: ..x...x...x...x.
drum hat: .o.o.o.o.o.o.o.o
drum crash bars=1,9: x...............
pat bass lo=D1: . 1 1 1 . 1 1 1 . 1 1 1 . 1 1 1
pat harp lo=D4 vol=0.4 gate=1.5: 1 5 8 10 12 10 8 5 1 5 8 10 12 10 8 5
pat pad lo=A3 gate=1: c - - - - - - - - - - - - - - -
mel supersaw vol=1.3: | D6:6 A5:2 F5:4 A5 | D6:4 F6 E6:2 D6 C6 Bb5 | C6:6 A5:2 F5:4 C6 | E6:4 D6:2 C6 G5:8
  | D6:6 A5:2 F5:4 A5 | F6:4 D6 Bb5 D6 | Bb5:4 D6 G6 F6:2 D6 | E6:4 C#6 A5:8
  | F5:2 Bb5 D6 F6 E6 D6 C6 Bb5 | E5:2 G5 C6 E6 D6 C6 Bb5 G5 | F5:2 A5 D6 F6 E6 D6 C6 A5 | D6:12 r:4
  | F6:4 E6:2 D6 F6:4 Bb5 | G6:4 F6:2 E6 C6:8 | E6:4 D6:2 C#6 A5:8 | G5:4 A5:2 C#6 E6:8 |
mel bell bars=9-16 vol=0.6: same

# ---------------------------------------------------------------- the rook: heavy, stomping, a fortress
song piece=rook bpm=108 bars=16 gain=0.87 title=Castle_Stomp
chords: E5 | E5 | C5:8 D5:8 | E5 | E5 | E5 | G5:8 A5:8 | B5 | C5 | D5 | E5 | E5 | C5 | D5 | B5 | B5
drum boom: x.x.....x.x.....
drum clap: ....x.......x...
drum snare: ....x.......x...
drum hat: x.x.x.x.x.x.x.x.
drum crash bars=1,5,9,13: x...............
mel tom bars=8,16: r:8 E3:2 C3 A2 E2
)SCORE"
R"SCORE(pat fatbass lo=E1: 1 . 1 . 1 . 1 . 1 . 1 . 1 . 1 .
pat dist lo=E2: p - . p - . p - p - . p - . p .
mel brass: | E5:4 G5 A5 B5 | D6:2 B5 A5 G5 E5:8 | G5:4 E5 A5 F#5 | G5:2 A5 B5:4 E5:8
  | E5:4 G5 A5 B5 | D6:2 E6 D6 B5 A5:4 G5 | B5:4 G5 C6 A5 | B5:6 A5:1 G5 F#5:8
  | G5:4 E5 C6:8 | A5:4 F#5 D6:8 | B5:4 G5 E6:8 | E6:2 D6 B5 A5 G5 A5 B5:4
  | C6:4 B5:2 A5 G5:4 E5 | D6:4 C6:2 B5 A5:4 F#5 | B5:4 A5:2 G5 F#5:4 B4 | F#5:4 A5 B5:8 |
mel horn tr=-12 vol=0.6: same

song piece=rook bpm=126 bars=16 gain=0.98 title=Siege_Engine
chords: Dm | Dm | Bb | C | Dm | Dm | Bb | A | Gm | Dm | Bb | A | Gm | Dm | Bb:8 C:8 | A
drum kick: x...x...x...x...
drum snare: ....X.......X...
drum hat: xoxoxoxoxoxoxoxo
drum taiko bars=1,5,9,13: x.......x.......
drum crash bars=1,9: x...............
pat fatbass lo=D1: 1 8 1 8 1 8 1 8 1 8 1 8 1 8 1 8
pat stab lo=A3 gate=0.5: c . . c . . c . . . c . c . . .
mel brass: | D5:3 D5 F5:2 A5:3 G5 F5:2 | E5:3 F5 E5:2 D5:8 | D5:3 D5 F5:2 Bb5:3 A5 F5:2 | G5:3 A5 G5:2 E5:8
  | D5:3 D5 F5:2 A5:3 G5 F5:2 | A5:3 C6 A5:2 D6:8 | D6:3 C6 Bb5:2 F5:3 G5 A5:2 | A5:8 E5:4 C#5
  | Bb5:4 A5:2 G5 D5:4 G5 | A5:4 G5:2 F5 D5:8 | F5:2 G5 A5 Bb5 D6:4 C6:2 Bb5 | C#6:4 A5 E5 C#5
  | G5:2 Bb5 D6:4 C6:2 Bb5 G5:4 | F5:2 A5 D6:4 C6:2 A5 F5:4 | F5:4 Bb5 E5 G5 | A5:8 E5:4 C#5 |
mel supersaw tr=-12 vol=0.5 bars=9-16: same

song piece=rook bpm=120 bars=16 gain=0.88 title=Fortress
chords: Cm | G:8 Cm | Ab | Bb | Cm | Cm | Ab | G | Ab | Bb | Cm | Cm | Fm | G | Ab | G
drum taiko: X...x.x.X...x.x.
drum kick: x.......x.......
drum snare: ....x.......x...
drum hat: ..x...x...x...x.
drum crash bars=1,9: x...............
pat spicc lo=C3 vol=0.7: 1 1 5 1 8 1 5 1 1 1 5 1 8 1 5 1
pat bass lo=C1: 1 . . 1 . . 1 . 1 . . 1 . . 1 .
pat strings lo=G3 gate=1 vol=0.5: c - - - - - - - - - - - - - - -
mel horn: | C5:4 Eb5 G5:6 F5:1 Eb5 | D5:4 B4 C5:8 | C5:4 Eb5 Ab5:6 G5:1 F5 | G5:4 F5 D5:8
  | C5:4 Eb5 G5:6 F5:1 Eb5 | G5:4 Bb5 C6:8 | Ab5:4 G5:2 F5 Eb5:4 C5 | D5:4 B4 G4:8
  | Eb5:2 Ab5 C6:4 Bb5:2 Ab5 Eb5:4 | F5:2 Bb5 D6:4 C6:2 Bb5 F5:4 | G5:2 C6 Eb6:4 D6:2 C6 G5:4 | C6:8 G5
  | Ab5:4 G5:2 F5 C6:4 Ab5 | B5:4 D6 G5:8 | C6:4 Bb5:2 Ab5 Eb5:4 F5 | G5:8 D5:4 B4 |
mel brass tr=12 vol=0.4 bars=9-16: same

# ---------------------------------------------------------------- the queen: elegant, twirling, powerful
song piece=queen bpm=176 beat=2 bar=6 bars=32 gain=1.45 title=Twirl
chords: Am | Am | E7 | E7 | E7 | E7 | Am | Am | Am | Am | Dm | Dm | Am | E7 | Am | Am
  | C | C | G7 | G7 | G7 | G7 | C | C | F | F | C | A7 | Dm | G7 | C | E7
drum kick: x.....
drum shaker: ..x.x.
drum tamb bars=17-32: ..x.x.
drum crash bars=1,17: x.....
pat pizz lo=A1: 1 . . . . . | 5, . . . . .
pat spicc lo=E3 vol=0.6: . . c . c .
pat harpsi lo=A4 vol=0.35 bars=17-32: 1 5 8 5 10 5
mel violin: | E5:2 A5 B5 | C6:4 B5:1 A5 | G#5:2 B5 E6 | D6:4 C6:1 B5 | D6:2 B5 G#5 | E5:2 G#5 B5 | C6:4 B5:1 A5 | A5:6
  | E5:2 A5 B5 | C6:4 D6:1 E6 | F6:4 E6:1 D6 | D6:2 A5 F5 | E6:4 C6:2 | B5:2 D6 G#5 | A5:2 C6 E6 | A5:4 r:2
  | E5:2 G5 C6 | E6:4 D6:1 C6 | B5:2 D6 F6 | F6:4 E6:1 D6 | D6:2 B5 G5 | F5:2 G5 B5 | C6:4 D6:1 E6 | C6:4 r:2
  | A5:2 C6 F6 | F6:4 E6:1 D6 | E6:2 C6 G5 | A5:2 C#6 E6 | F6:4 E6:1 D6 | D6:2 B5 G5 | C6:2 G5 E5 | B5:2 G#5 E5 |
mel glock bars=17-32 vol=0.45: same

song piece=queen bpm=120 bars=16 gain=0.98 title=Sovereign
chords: Gm7 | Cm7 | F | Bbmaj7 | Ebmaj7 | Am7b5 | D7 | D7 | Ebmaj7 | F | Dm7 | Gm7 | Cm7 | F | D7 | D7
drum kick: x...x...x...x...
drum clap: ....x.......x...
drum ohat: ..x...x...x...x.
drum hat: x.x.x.x.x.x.x.x.
drum crash bars=1,9: x...............
pat bass lo=G1: 1 . 8 . 1 . 8 . 1 . 8 . 1 . 8 .
pat disco lo=Bb3 gate=0.5: c - . . . . c . . . c - . . . .
pat clav lo=G3 gate=0.3 vol=0.6: . c . c . . c . . c . c . . c .
pat strings lo=D4 gate=1 vol=0.5 bars=9-16: c - - - - - - - - - - - - - - -
mel lead vol=1.2: | D5:2 F5 G5 Bb5 A5:4 G5 | G5:2 Bb5 C6 Eb6 D6:4 C6 | A5:2 C6 F6:4 Eb6:2 D6 C6:4 | D6:4 C6:2 Bb5 A5:8
  | G5:2 Bb5 D6:4 C6:2 Bb5 G5:4 | C6:4 Eb6 D6:2 C6 A5:4 | F#5:2 A5 C6:4 Bb5:2 A5 F#5:4 | D5:2 F#5 A5 C6 D6:8
  | Bb5:4 G5 D6 Bb5 | C6:4 A5 F6 C6 | A5:2 C6 D6:4 F6 D6 | Bb5:4 A5:2 G5 D5:8
  | Eb6:2 D6 C6 Bb5 G5:4 C6 | A5:2 C6 F6 Eb6 D6:4 C6 | A5:4 F#5 C6 A5 | D6:2 C6 A5 F#5 D5:8 |
mel violin tr=-12 vol=0.5: same

song piece=queen bpm=128 bars=16 gain=1.14 title=Royal_Toccata
chords: Dm | A7 | Dm | A7 | Dm | Gm | A7 | Dm | F | C | Dm | A | Bb | Gm | A7 | A7
drum kick: x.....x.x.......
drum snare: ....x.......x...
drum hat: x.x.x.x.x.x.x.x.
drum crash bars=1,9: x...............
pat harpsi lo=D4 vol=0.5: 1 5 8 5 10 8 5 8 1 5 8 5 10 8 5 8
pat bass lo=D1: 1 . 1 . 8 . 1 . 1 . 1 . 8 . 5 .
pat strings lo=A3 gate=1 vol=0.5: c - - - - - - - - - - - - - - -
mel violin: | A5:4 D6 F6 E6:2 D6 | C#6:4 E6 A5:8 | F5:4 A5 D6 C6:2 Bb5 | A5:4 G5:2 F5 E5:8
  | D6:2 E6 F6 D6 A5:4 F5 | G5:2 A5 Bb5 G5 D6:4 Bb5 | C#6:2 D6 E6 C#6 A5:4 G5 | F5:4 E5 D5:8
  | C6:4 A5:2 C6 F6:4 C6 | E6:4 C6:2 E6 G6:4 E6 | F6:2 E6 D6 C6 D6:4 A5 | C#6:2 B5 A5 B5 C#6:4 E6
  | D6:2 C6 Bb5 A5 Bb5:4 F5 | Bb5:2 A5 G5 F5 G5:4 D5 | E5:2 F5 G5 A5 C#6:4 E6 | D6:2 C#6 B5 C#6 A5:8 |
mel church tr=-12 vol=0.5: same

# ---------------------------------------------------------------- the king: regal, triumphant
song piece=king bpm=116 bars=16 gain=0.94 title=Royal_Fanfare
chords: Bb | Bb | Eb | F | Bb | Gm | Eb:8 F:8 | Bb | Eb | Bb | Cm | F | Eb | Bb | F7 | Bb
drum kick: x.......x.......
drum snare: X.o.x.ooX.o.x.ox
drum crash bars=1,5,9,13: x...............
pat timp lo=F2: 1 . . . . . . . 1 . . . 5, . 1 .
pat tuba lo=Bb1: 1 - . . 5, - . . 1 - . . 5, - . .
pat horn lo=D4 gate=0.5 vol=0.5: . . c . . . c . . . c . . . c .
mel trumpet: | F4:2 Bb4 D5 F5:6 D5:2 F5 | Bb5:8 F5:4 D5 | Eb5:2 G5 Bb5:4 G5 Eb5 | F5:3 F5:1 F5:2 A5 C6:8
  | D6:3 C6:1 Bb5:2 F5 D6:3 C6:1 Bb5:4 | Bb5:3 A5:1 G5:2 D5 G5:4 Bb5 | G5:4 Eb5 A5 C6 | Bb5:4 F5:2 D5 Bb4:8
  | G5:3 G5:1 G5:2 Bb5 Eb6:8 | D6:3 D6:1 D6:2 C6 Bb5:8 | C6:3 Bb5:1 G5:2 Eb5 G5:4 C6 | A5:4 C6 F6:8
  | Eb6:4 D6:2 C6 Bb5:4 G5 | F5:4 Bb5 D6:8 | C6:3 C6:1 C6:2 A5 Eb6:4 C6 | Bb5:4 F5 Bb5:8 |
mel brass tr=-12 vol=0.5: same
mel glock bars=9-16 vol=0.4: same

song piece=king bpm=112 bars=16 gain=1.01 title=Coronation
chords: D | A | Bm | G | D | A | G | A | G | D | A | Bm | G | D | Em7 | A7
drum kick: x.....x.x.....x.
drum snare: ....x.......x...
drum hat: x.x.x.x.x.x.x.x.
drum crash bars=1,5,9,13: x...............
mel tom bars=8,16: r:8 D3:2 B2 G2 D2
pat bass lo=D1: 1 . 1 . 1 . 1 . 1 . 1 . 1 . 5 .
pat organ lo=F#3 gate=0.9 vol=0.8: c - . c - . c - c - . c - . c .
pat choir lo=A3 gate=1 vol=0.5 bars=9-16: c - - - - - - - - - - - - - - -
mel trumpet: | F#5:4 A5 D6:6 C#6:1 B5 | C#6:4 B5:2 A5 E5:8 | D5:4 F#5 B5:6 A5:1 G5 | A5:4 G5:2 F#5 D5:8
  | F#5:4 A5 D6:6 E6:1 F#6 | E6:4 C#6 A5:8 | B5:4 D6 G6 F#6:2 E6 | C#6:4 E6 A5:8
  | B5:4 A5:2 G5 D6:8 | A5:4 F#5:2 A5 D6:8 | C#6:4 B5:2 A5 E6:8 | D6:4 C#6:2 B5 F#5:8
  | G5:4 B5 D6 G6 | F#6:4 E6:2 D6 A5:8 | G5:4 B5 D6 E6 | C#6:4 B5:2 A5 G5:4 E5 |
mel church tr=-12 vol=0.5: same

song piece=king bpm=126 swing=0.12 bars=16 gain=1.11 title=The_King's_Parade
chords: F | Bb | C | F | Dm | Bb | Gm | C | Bb | C | Am | Dm | Gm | C | F | C7
drum kick: x.......x.......
drum snare: X.o.x.o.X.o.x.o.
drum clap: ....x.......x...
drum tamb: x.x.x.x.x.x.x.x.
drum crash bars=1,9: x...............
pat pizz lo=F1: 1 . . . 5 . . . 1 . 3 . 5 . . .
pat strings lo=A3 gate=1 vol=0.5: c - - - - - - - - - - - - - - -
pat marimba lo=C4 vol=0.5: . . c . . . c . . . c . . . c .
mel trumpet: | C5:2 F5 A5 C6 A5 F5 A5:4 | Bb5:4 D6 C6:2 Bb5 F5:4 | G5:2 C6 E6:4 D6:2 C6 G5:4 | A5:4 F5 C5:8
  | D5:2 F5 A5 D6 C6 A5 F5:4 | Bb5:2 A5 Bb5 D6 F6:4 D6 | G5:2 Bb5 D6:4 C6:2 Bb5 G5:4 | E5:4 G5 C6:8
  | D6:3 C6:1 Bb5:2 F5 D6:4 F6 | E6:3 D6:1 C6:2 G5 C6:4 E6 | C6:3 B5:1 A5:2 E5 A5:4 C6 | D6:3 C6:1 A5:2 F5 D5:8
  | Bb5:2 A5 G5 Bb5 D6:4 Bb5 | C6:2 Bb5 A5 G5 E5:4 G5 | A5:4 C6 F6:8 | E6:2 D6 C6 Bb5 G5:4 E5 |
mel glock tr=12 vol=0.35: same
)SCORE";

}  // namespace music
