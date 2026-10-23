// The background music's score: the instruments and, for each army, three songs
// for each piece (the notation is explained at the top). The same text as
// music_score.cpp in the C++ version; keep the two identical.

let musicScoreText = """
# ===========================================================================
# The instruments
#   patch <name> synth <saw|sqr|tri|sin|organ|organ2|flute> | fm | ks | drum <type>  settings...
#   a d s r: envelope (attack s, decay and release s to -60 dB, sustain level)   cut: filter at cut x the note (hz: plus Hz)
#   fenv fatk fdec: filter envelope (octaves, s)   vib: rate,semitones,delay   gain pan rev del
#   glide: slides into each note from the instrument's last one (s)   bend: semitones,s (each note starts that far off)
#   wah: rate,octaves (the filter sways)
#
# The songs: for each army (marble, medieval, alien) three for each piece
#   song army= piece= bpm= beat=<steps a beat, 4> bar=<steps a bar, 16> bars= swing= echo=<steps> gain= title=
#   chords: one a bar unless given a length (C:8 G = C for 8 steps, G the rest of the bar)
#   drum <instrument> [settings]: x hit, X accent, o soft, . rest - one character a step
#   pat <instrument> [settings]: chord tones, one a step - 1 3 5 7 8 10 12 (and 2 4 6 9 ...),
#        c the chord, p a power chord, ' up and , down an octave, - hold, . rest
#   mel <instrument> [settings]: notes with lengths in steps (C5:4, the length carries on),
#        r rest, _ hold, ! accent, ? soft; "same" repeats the part before on this instrument
#   settings: vol pan rev del gate tr (semitones) lo (lowest note of a pat) bars=1-8,13
#   a bar can end with *N to play it N times; every bar is checked for its length
# ===========================================================================
# drums
patch kick    drum membrane f=50 pamt=2.2 pdec=0.03 d=0.7 snap=0.3 nf=2500 ndec=0.006 drive=1.3 gain=0.9 rev=0.02
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
patch tuba    synth saw sub=0.3 a=0.025 d=0.7 s=0.75 r=0.25 cut=1.5 res=0.1 fenv=1.6 fatk=0.03 fdec=0.15 gain=0.5 rev=0.12
patch pizz    ks bright=0.45 damp=0.5 decay=0.6 r=0.2 gain=0.75 rev=0.2
# plucked strings, keys and mallets
patch harp    ks bright=0.7 damp=0.3 decay=2.2 r=0.8 gain=0.5 rev=0.35 del=0.12
patch harpsi  ks bright=1.0 damp=0.12 decay=1.2 r=0.25 gain=0.42 rev=0.22
patch guitar  ks bright=0.85 damp=0.2 decay=1.6 r=0.3 drive=1.2 gain=0.7 rev=0.18 del=0.3
patch glock   fm ratio=3.5 index=0.7 idec=0.15 p2=2.76 p2amp=0.25 p2dec=0.25 a=0.001 d=2.5 s=0 r=1.2 gain=0.3 rev=0.3 del=0.18
patch bell    fm ratio=1.4 index=2.4 idec=0.5 a=0.001 d=3.5 s=0 r=1.8 gain=0.42 rev=0.35 del=0.2
patch celesta fm ratio=1 index=1.1 idec=0.2 p2=4 p2amp=0.15 p2dec=0.12 a=0.001 d=2.2 s=0 r=0.9 gain=0.38 rev=0.3
patch organ   synth organ a=0.006 d=0.3 s=1 r=0.2 trem=6.5,0.2 gain=0.18 rev=0.25
patch church  synth organ2 sub=0.25 a=0.03 d=0.3 s=1 r=0.5 gain=0.24 rev=0.45
# winds
patch fife    synth flute noise=0.05 a=0.02 d=0.3 s=0.85 r=0.15 vib=6,0.18,0.15 cut=6 gain=0.48 rev=0.15
patch whistle synth sin noise=0.015 a=0.02 d=0.3 s=0.9 r=0.2 vib=5.5,0.25,0.12 gain=0.4 rev=0.2 del=0.15
# brass and strings
patch brass   synth saw uni=2 det=7 a=0.02 d=0.9 s=0.75 r=0.35 cut=2 res=0.15 fenv=2.2 fatk=0.04 fdec=0.25 vib=5,0.12,0.35 scoop=0.6 gain=0.42 rev=0.22
patch trumpet synth saw a=0.012 d=0.7 s=0.8 r=0.25 cut=2.5 res=0.2 fenv=2.5 fatk=0.03 fdec=0.2 vib=5.5,0.14,0.3 scoop=0.8 drive=1.3 gain=0.45 rev=0.25
patch horn    synth saw uni=2 det=5 a=0.05 d=1.0 s=0.8 r=0.5 cut=1.3 res=0.1 fenv=1.5 fatk=0.08 fdec=0.3 scoop=0.4 gain=0.45 rev=0.3
patch strings synth saw uni=3 det=14 a=0.12 d=1.4 s=0.85 r=1.2 cut=4 res=0.05 vib=5,0.08,0.4 gain=0.2 rev=0.35
patch spicc   synth saw uni=2 det=10 a=0.004 d=0.45 s=0 r=0.2 cut=4 res=0.1 fenv=1 fdec=0.08 gain=0.3 rev=0.25
patch violin  synth saw a=0.05 d=1.0 s=0.85 r=0.5 cut=4 res=0.15 vib=5.5,0.2,0.2 gain=0.42 rev=0.3
patch pad     synth saw uni=3 det=16 a=0.35 d=1.7 s=0.8 r=2.0 cut=2 res=0.1 gain=0.14 rev=0.45
patch choir   synth saw uni=3 det=14 filt=vox a=0.15 d=1.4 s=0.85 r=1.4 vib=5.2,0.1,0.3 gain=0.22 rev=0.45

# ===========================================================================
# The Living Marble army: a concert hall - string quartet, orchestra, harpsichord
# ===========================================================================
patch viola    synth saw a=0.05 d=1.0 s=0.85 r=0.45 cut=3.2 res=0.12 vib=5.2,0.18,0.2 gain=0.42 rev=0.3
patch cello    synth saw a=0.06 d=1.2 s=0.85 r=0.5 cut=2.8 res=0.12 vib=5,0.16,0.25 gain=0.48 rev=0.3
patch dbass    synth saw sub=0.3 a=0.04 d=1.0 s=0.8 r=0.3 cut=2 res=0.1 gain=0.5 rev=0.2
patch flute    synth flute noise=0.03 a=0.04 d=0.4 s=0.85 r=0.2 vib=5,0.12,0.25 cut=5 gain=0.42 rev=0.3 del=0.1
patch oboe     synth sqr pw=0.2 a=0.03 d=0.6 s=0.85 r=0.15 cut=4 res=0.3 vib=5.5,0.12,0.3 gain=0.3 rev=0.25
patch clarinet synth sqr pw=0.5 a=0.03 d=0.6 s=0.85 r=0.15 cut=3 res=0.1 vib=5,0.06,0.4 gain=0.32 rev=0.25
patch bassoon  synth sqr pw=0.3 a=0.03 d=0.6 s=0.85 r=0.15 cut=2.2 res=0.2 gain=0.42 rev=0.2
patch trombone synth saw uni=2 det=5 a=0.03 d=0.9 s=0.8 r=0.3 cut=1.6 res=0.1 fenv=1.8 fatk=0.04 fdec=0.25 scoop=0.3 gain=0.45 rev=0.25
patch bassdrum drum membrane f=48 pamt=0.4 pdec=0.05 d=1.6 snap=0.25 nf=400 ndec=0.04 gain=0.55 rev=0.35
patch sidedrum drum snare f=220 nf=2000 d=0.25 tone=0.35 snap=0.9 gain=0.4 rev=0.25
patch triangle drum metal f=700 nf=7000 d=1.8 metal=0.85 gain=0.22 rev=0.3

# ---------------------------------------------------------------- the pawn: humble, eager, hopping
song army=marble piece=pawn bpm=138 beat=4 bar=12 bars=24 gain=1.40 title=Pawn's_Minuet
chords: G | D7 | C | D7 | G | A7 | G:6 D7:6 | G
  | Em | B7 | Em | B7 | Am | D7 | C:6 D7:6 | D7
  | G | D7 | C | D7 | G | A7 | G:6 D7:6 | G
pat viola lo=G3 gate=0.5 vol=0.4 bars=1-16: . . . . c . . . c . . .
pat viola lo=G3 gate=0.8 vol=0.35 bars=17-24: 1 . 5 . 8 . 5 . 8 . 5 .
mel cello: | G2:4 r:8 | F#2:4 r:8 | E2:4 r:8 | F#2:4 r:8 | G2:4 r:8 | C#3:4 r:8 | D3:4 r:2 D2:4 r:2 | G2:4 G3 r
  | E3:4 r:8 | D#3:4 r:8 | E3:4 r:8 | B2:4 r:8 | A2:4 r:8 | F#2:4 r:8 | C3:4 r:2 D3:4 r:2 | D2:4 D3 C3
  | G2:4 B2 D3 | F#2:4 A2 C3 | E2:4 G2 C3 | F#2:4 A2 D3 | G2:4 B2 D3 | C#3:4 E3 A2 | D3:4 r:2 D2:4 r:2 | G2:4 D2 G2 |
mel violin: | D5:2 G5 G5:4 B5 | A5:2 G5 F#5 G5 A5:4 | E5:2 C6 C6:4 E6 | D6:2 C6 B5 C6 A5:4
  | D5:2 G5 G5:4 B5 | A5:2 G5 F#5 G5 C#6:4 | D6:2 B5 G5 C6 A5 F#5 | G5:4 G4 r
  | B5:2 C6 B5 A#5 B5:4 | F#6:4 D#6:2 E6 F#6 A5 | G5:2 F#5 E5 F#5 G5:4 | F#5:2 A5 D#6 F#6 B5:4
  | A5:2 B5 C6 A5 E6:4 | D6:2 C6 B5 A5 F#5:4 | G5:2 C6 E6 D6 C6 A5 | F#5:2 A5 C6 B5 A5 F#5
  | D5:2 G5 G5:4 B5 | A5:2 G5 F#5 G5 A5:4 | E5:2 C6 C6:4 E6 | D6:2 C6 B5 C6 A5:4
  | D5:2 G5 G5:4 B5 | A5:2 G5 F#5 G5 C#6:4 | D6:2 B5 G5 C6 A5 F#5 | G5:4 D5:2 B4 G4:4 |
mel flute bars=17-24 vol=0.5: | D5:2 G5 G5:4 B5 | A5:2 G5 F#5 G5 A5:4 | E5:2 C6 C6:4 E6 | D6:2 C6 B5 C6 A5:4
  | D5:2 G5 G5:4 B5 | A5:2 G5 F#5 G5 C#6:4 | D6:2 B5 G5 C6 A5 F#5 | G5:4 D5:2 B4 G4:4 |

song army=marble piece=pawn bpm=112 bar=8 bars=32 gain=0.92 title=Tin_Soldier_March
chords: D | D | A7 | D | D | G | D:4 A7:4 | D
  | D | D | A7 | D | D | Em | A7 | D
  | G | D7 | G | D | C | G | D7 | G:4 A7:4
  | D | D | A7 | D | D | Em | A7 | D
drum sidedrum: X.o.x.oo
drum bassdrum: x.......
drum triangle bars=17-24: x.......
drum crash bars=1,17,25: x.......
pat bassoon lo=D2 gate=0.6: 1 . . . 5, . . .
pat pizz lo=F#3 vol=1.1: . . c . . . c .
mel fife: | D5:3 F#5:1 A5:2 A5 | B5:3 A5:1 F#5:2 D5 | E5:3 F#5:1 G5:2 E5 | F#5:2 D5 A4:4
  | D5:3 F#5:1 A5:2 D6 | B5:3 D6:1 B5:2 G5 | F#5:2 A5 E5 G5 | D5:4 r
  | D5:3 F#5:1 A5:2 A5 | B5:3 A5:1 F#5:2 D5 | E5:3 F#5:1 G5:2 E5 | F#5:2 D5 A4:4
  | D5:3 F#5:1 A5:2 D6 | E6:3 D6:1 B5:2 G5 | A5:2 C#6 E6 C#6 | D6:4 r
  | B5:2 C6 D6:4 | C6:2 B5 A5:4 | B5:2 A5 G5 B5 | A5:6 r:2
  | E6:2 D6 C6 E6 | D6:2 C6 B5 D6 | C6:2 A5 F#5 A5 | G5:4 E5:2 C#5
  | D5:3 F#5:1 A5:2 A5 | B5:3 A5:1 F#5:2 D5 | E5:3 F#5:1 G5:2 E5 | F#5:2 D5 A4:4
  | D5:3 F#5:1 A5:2 D6 | E6:3 D6:1 B5:2 G5 | A5:2 C#6 E6 C#6 | D6:4 r |
mel glock bars=17-24 vol=0.45: | B5:2 C6 D6:4 | C6:2 B5 A5:4 | B5:2 A5 G5 B5 | A5:6 r:2
  | E6:2 D6 C6 E6 | D6:2 C6 B5 D6 | C6:2 A5 F#5 A5 | G5:4 E5:2 C#5 |

song army=marble piece=pawn bpm=96 beat=6 bar=12 bars=24 gain=1.08 title=Little_Serenade
chords: F | C7 | F | C | F | Bb | F:6 C7:6 | F
  | C | G7 | C | G | C | F | G7 | C7
  | F | C7 | F | C | F | Bb | F:6 C7:6 | F
mel oboe bars=1-8,17-24: | C5:4 F5:2 A5:4 F5:2 | Bb5:4 G5:2 E5:4 C5:2 | A5:2 Bb5 C6 D6:4 C6:2 | Bb5:4 A5:2 G5:6
  | C5:4 F5:2 A5:4 F5:2 | D6:4 Bb5:2 F5:4 D5:2 | A5:2 C6 F5 G5 Bb5 E5 | F5:6 r |
mel flute bars=17-24 vol=0.45: same
mel bassoon bars=9-16: | G3:2 C4 E4 G4:4 E4:2 | F4:4 D4:2 B3:4 G3:2 | E4:2 F4 G4 A4:4 G4:2 | F4:4 E4:2 D4:6
  | G3:2 C4 E4 G4:4 E4:2 | A4:4 F4:2 C4:4 A3:2 | G3:2 B3 D4 F4:4 D4:2 | C4:2 E4 G4 Bb4:6 |
pat bassoon lo=F2 gate=0.7 bars=1-8,17-24: 1 - - . . . 5, - - . . .
pat clarinet lo=F3 vol=0.55 bars=1-8,17-24: 1 . 5 . 8 . 10 . 8 . 5 .
pat clarinet lo=G4 vol=0.45 bars=9-16: 1 . 5 . 8 . 5 . 8 . 5 .
pat horn lo=C3 gate=0.9 vol=0.5 bars=9-16: 1 - - - - - 5, - - - - -
pat horn lo=A3 gate=1 vol=0.35 bars=17-24: c - - - - - c - - - - -

# ---------------------------------------------------------------- the knight: leaping, galloping, jousting
song army=marble piece=knight bpm=144 bar=8 bars=32 gain=1.04 title=Grand_Galop
chords: D | D | A7 | D | D | G | A7 | D
  | G | A7 | D | E7 | A | Bm:4 E7:4 | A | A7
  | D | D | A7 | D | D | G | A7 | D
  | D | G | D | A7 | D | A7 | D:4 A7:4 | D
drum crash bars=1,17,25: x.......
drum triangle bars=17-32: x.......
pat timp lo=A2: 1 . . . 5, . . .
pat dbass lo=D2 gate=0.6: 1 . . . 5, . . .
pat spicc lo=A3 gate=0.5 vol=0.8: c c c . c c c .
mel violin: | A5:2 A5:1 A5 D6:2 A5 | F#5:2 F#5:1 F#5 A5:2 F#5 | E5:2 F#5 G5 A5 | F#5:4 D5
  | A5:2 A5:1 A5 D6:2 A5 | B5:2 B5:1 B5 D6:2 B5 | A5:2 G5 E5 C#5 | D5:4 r
  | D5:2 G5 B5 D6 | C#6:4 A5 | D6:2 A5 F#5 D5 | E5:2 G#5 B5 D6 | C#6:4 A5 | B5:2 D6 B5 G#5 | E5:2 A5 C#6 E6 | G5:2 A5 B5 C#6
  | A5:2 A5:1 A5 D6:2 A5 | F#5:2 F#5:1 F#5 A5:2 F#5 | E5:2 F#5 G5 A5 | F#5:4 D5
  | A5:2 A5:1 A5 D6:2 A5 | B5:2 B5:1 B5 D6:2 B5 | A5:2 G5 E5 C#5 | D5:4 r
  | D6:2 C#6:1 D6 A5:2 F#5 | G5:2 F#5:1 G5 B5:2 D6 | F#5:2 E5:1 F#5 A5:2 D6 | C#6:2 B5:1 C#6 E6:2 A5
  | D6:1 C#6 D6 E6 F#6:2 D6 | E6:1 D6 C#6 B5 A5:2 G5 | F#5:2 D5 E5 C#5 | D5:2 r D4 r |
mel trumpet bars=1-8,17-24 vol=0.55: | A5:2 A5:1 A5 D6:2 A5 | F#5:2 F#5:1 F#5 A5:2 F#5 | E5:2 F#5 G5 A5 | F#5:4 D5
  | A5:2 A5:1 A5 D6:2 A5 | B5:2 B5:1 B5 D6:2 B5 | A5:2 G5 E5 C#5 | D5:4 r |
mel glock bars=17-24 vol=0.35: same

song army=marble piece=knight bpm=126 beat=6 bar=12 bars=32 gain=1.51 title=Tarantella
chords: Am | Am | E7 | Am | Dm | Am | E7 | Am
  | C | G7 | C | G | F | C | Dm:6 E7:6 | E7
  | Am | Am | E7 | Am | Dm | Am | E7 | Am
  | Am | E7 | Am | E7 | Dm | Am:6 E7:6 | Am:6 E7:6 | Am
drum tamb: X.o.o.X.o.o.
drum triangle bars=17-32: x.....x.....
pat pizz lo=A2 vol=1.3: 1 . . . . . 5, . . . . .
pat spicc lo=A3 gate=0.5 vol=0.8: . . c . c . . . c . c .
mel violin bars=1-8,17-24: | E5:2 A5 B5 C6 B5 A5 | E5:2 A5 B5 C6:4 A5:2 | D6:2 C6 B5 G#5 B5 E5 | C6:2 B5 A5 A5:4 r:2
  | F5:2 A5 D6 F6 E6 D6 | C6:2 B5 A5 E6:4 C6:2 | B5:2 C6 D6 C6 B5 G#5 | A5:4 E5:2 A5:4 r:2 |
mel flute bars=17-24 vol=0.5: same
mel violin bars=9-16: | G5:4 C6:2 E6:4 C6:2 | D6:4 B5:2 G5:4 B5:2 | C6:2 D6 E6 F6 E6 D6 | D6:4 B5:2 G5:6
  | A5:4 C6:2 F6:4 C6:2 | G5:4 C6:2 E6:4 C6:2 | F6:2 E6 D6 B5 C6 D6 | E6:2 D6 C6 B5 A5 G#5 |
mel violin bars=25-32: | E5:2 A5 E5 A5 C6 A5 | E5:2 G#5 E5 G#5 B5 G#5 | E5:2 A5 C6 E6 C6 A5 | G#5:2 B5 D6 E6 D6 B5
  | D6:2 F6 E6 D6 C6 B5 | A5:2 C6 E6 E6 D6 B5 | A5:2 E5 C5 B4 D5 G#5 | A5:2 r:4 A4:2 r:4 |
mel flute bars=25-32 vol=0.5: same

song army=marble piece=knight bpm=100 beat=6 bar=12 bars=24 gain=1.08 title=The_Hunt
chords: F | C | F | C | F | Bb | F:6 C7:6 | F
  | Dm | A7 | Dm | A7 | Bb | F | Gm:6 C7:6 | C7
  | F | C | F | C | F | Bb | F:6 C7:6 | F
mel horn bars=1-8,17-24: | F4:2 A4 C5 F5:4 C5:2 | G4:4 C5:2 E5:4 C5:2 | F5:4 E5:2 D5:4 C5:2 | D5:4 E5:2 C5:6
  | F4:2 A4 C5 F5:4 C5:2 | D5:2 C5 Bb4 F5:4 D5:2 | A4:2 C5 F5 E5:2 C5 Bb4 | A4:4 G4:2 F4:6 |
mel violin bars=17-24 tr=12 vol=0.5: same
mel horn bars=1-8,17-24 vol=0.75: | C4:2 F4 A4 C5:4 A4:2 | E4:4 G4:2 C5:4 G4:2 | A4:4 C5:2 Bb4:4 A4:2 | Bb4:4 G4:2 E4:6
  | C4:2 F4 A4 C5:4 A4:2 | Bb4:2 A4 F4 D5:4 Bb4:2 | F4:2 A4 C5 C5:2 G4 G4 | C4:4 Bb3:2 A3:6 |
mel violin bars=9-16: | A5:4 D6:2 F6:4 D6:2 | E6:4 C#6:2 A5:6 | F6:2 E6 D6 E6 F6 G6 | E6:4 A5:2 C#6:6
  | D6:4 F6:2 Bb5:4 D6:2 | C6:4 A5:2 F5:4 A5:2 | Bb5:2 D6 G6 E6 C6 Bb5 | C6:2 Bb5 A5 G5 E5 C5 |
pat horn lo=F3 gate=1 vol=0.4 bars=9-16: c - - - - - c - - - - -
pat spicc lo=A3 gate=0.6 vol=0.8: c . . . c . c . . . c .
pat cello lo=F2 gate=0.8: 1 . . . . . 5, . . . . .
pat timp lo=C2 bars=1-8,17-24: 1 . . . . . 1 . . . . .

# ---------------------------------------------------------------- the bishop: gliding, shining, holy
song army=marble piece=bishop bpm=66 beat=6 bar=24 bars=10 gain=1.27 title=Chorale_Prelude
chords: G | G:6 D:6 Em:6 C:6 | Am:6 G:6 D:12 | G:6 Am:6 G:6 C:6 | G:6 D7:6 Em:12
  | Bm:6 A7:6 D:6 Em:6 | A7:6 D:18 | Em:6 D:6 C:6 G:6 | Am:6 D7:6 G:12 | C:12 G:12
pat violin lo=D4 vol=0.35: 3 . 5 . 8 . 10 . 8 . 5 . 3 . 5 . 8 . 10 . 8 . 5 .
pat harpsi lo=D4 vol=0.8: same
pat cello lo=G2 gate=0.85: 1 - - - - . 1 - - - - . 1 - - - - . 1 - - - - .
pat church lo=G3 gate=1 vol=0.3: c - - - - - c - - - - - c - - - - - c - - - - -
mel oboe: | r:24 | G4:6 A4 B4 G4 | C5:6 B4 A4:12 | B4:6 C5 D5 E5 | D5:6 C5 B4:12
  | D5:6 E5 F#5 G5 | E5:6 F#5 D5:12 | G5:6 F#5 E5 D5 | C5:6 A4 G4:12 | G4:24 |
mel choir vol=0.45: same

song army=marble piece=bishop bpm=69 beat=6 bar=12 bars=24 gain=1.06 title=Barcarolle
chords: D | D | G | A7 | D | Bm | Em7:6 A7:6 | D
  | G | F#7 | Bm | E7 | A | D | G:6 Gm:6 | A7
  | D | D | G | A7 | D | Bm | Em7:6 A7:6 | D
mel flute: | F#5:4 G5:2 A5:6 | A5:4 B5:2 A5:4 F#5:2 | B5:4 A5:2 G5:4 D5:2 | G5:4 F#5:2 E5:6
  | F#5:4 G5:2 A5:6 | D6:4 C#6:2 B5:6 | G5:4 F#5:2 E5:4 C#5:2 | D5:12
  | B5:4 C#6:2 D6:6 | C#6:4 A#5:2 F#5:6 | D6:4 C#6:2 B5:4 F#5:2 | G#5:4 A5:2 B5:6
  | C#6:4 B5:2 A5:6 | D6:4 E6:2 F#6:6 | B5:4 A5:2 Bb5:4 G5:2 | A5:4 G5:2 E5:6
  | F#5:4 G5:2 A5:6 | A5:4 B5:2 A5:4 F#5:2 | B5:4 A5:2 G5:4 D5:2 | G5:4 F#5:2 E5:6
  | F#5:4 G5:2 A5:6 | D6:4 C#6:2 B5:6 | G5:4 F#5:2 E5:4 C#5:2 | D5:12 |
mel violin bars=17-24 tr=-12 vol=0.45: | F#5:4 G5:2 A5:6 | A5:4 B5:2 A5:4 F#5:2 | B5:4 A5:2 G5:4 D5:2 | G5:4 F#5:2 E5:6
  | F#5:4 G5:2 A5:6 | D6:4 C#6:2 B5:6 | G5:4 F#5:2 E5:4 C#5:2 | D5:12 |
mel celesta bars=17-24 tr=12 vol=0.3: same
pat harp lo=D3 vol=0.8: 1 . 5 . 8 . 10 . 8 . 5 .
pat cello lo=D2 gate=0.9 vol=0.7: 1 - - - - - 5 - - - - -
pat strings lo=F#3 gate=1 vol=0.35: c - - - - - c - - - - -

song army=marble piece=bishop bpm=120 bars=16 gain=1.08 title=Gloria
chords: D | A7 | Bm:8 G:8 | A7:8 D:8 | D | A7 | Bm:8 G:8 | A7:8 D:8
  | Bm | Em | A | A7 | D | A7 | Bm:8 G:8 | A7:8 D:8
mel violin bars=1-4,13-16: | D5:4 F#5:2 A5 D6:4 A5 | C#6:2 B5 A5 G5 F#5:4 E5 | F#5:2 B5 D6 B5 G5 B5 D6 B5 | E6:2 C#6 A5 C#6 D6:8 |
mel trumpet bars=5-8,13-16 vol=0.7: same
mel violin bars=9-12: | F#5:2 B5 A#5 B5 D6:4 B5 | G5:2 E6 D#6 E6 B5:4 G5 | E5:2 A5 G#5 A5 C#6:4 A5 | E6:2 D6 C#6 B5 A5:4 G5 |
pat violin lo=D4 vol=0.35: 1 . 8 . 1 . 8 . 1 . 8 . 1 . 8 .
pat cello lo=D2 gate=0.7: 1 . 1 . 1 . 1 . 1 . 1 . 1 . 1 .
pat harpsi lo=F#3 vol=0.7: c . . . c . . . c . . . c . . .
pat choir lo=A3 gate=1 vol=0.6 bars=5-8,13-16: c - - - - - - - c - - - - - - -
pat timp lo=A2 bars=5-8,13-16: 1 . . . . . . . 1 . . . 1 . 1 .
drum crash bars=5,13: x...............

# ---------------------------------------------------------------- the rook: a heavy tower, cannon and earthquake
song army=marble piece=rook bpm=104 bars=16 gain=0.96 title=Fireworks_Overture
chords: D | D | G:8 A7:8 | D | D | Bm | E7 | A
  | A7 | D | G:8 Em:8 | A7 | D | G | D:8 A7:8 | D
mel trumpet: | D5:4 D5:3 D5:1 F#5:4 A5 | D6:6 C#6:1 B5 A5:8 | B5:3 A5:1 G5:2 B5 E5:3 F#5:1 G5:2 E5 | F#5:4 D5 A4:8
  | F#5:4 F#5:3 F#5:1 A5:4 D6 | D6:4 C#6:2 B5 F#5:8 | E5:3 F#5:1 G#5:2 A5 B5:4 E5 | A5:4 E5 C#5 A4
  | E5:4 E5:3 E5:1 A5:4 C#6 | D6:8 A5:4 F#5 | G5:3 A5:1 B5:2 G5 E5:3 F#5:1 G5:2 E5 | A5:4 G5 F#5 E5
  | D5:4 D5:3 D5:1 F#5:4 A5 | B5:4 B5:3 B5:1 D6:4 B5 | A5:3 B5:1 A5:2 F#5 E5:3 F#5:1 E5:2 C#5 | D5:12 r:4 |
mel oboe bars=9-16 vol=0.5: | E5:4 E5:3 E5:1 A5:4 C#6 | D6:8 A5:4 F#5 | G5:3 A5:1 B5:2 G5 E5:3 F#5:1 G5:2 E5 | A5:4 G5 F#5 E5
  | D5:4 D5:3 D5:1 F#5:4 A5 | B5:4 B5:3 B5:1 D6:4 B5 | A5:3 B5:1 A5:2 F#5 E5:3 F#5:1 E5:2 C#5 | D5:12 r:4 |
pat horn lo=F#3 gate=0.9 vol=0.45: c - - - - - - - c - - - - - - -
pat spicc lo=A3 gate=0.5 vol=0.7: c . . c c . . . c . . c c . . .
pat cello lo=D2 gate=0.7: 1 . 1 . 1 . 1 . 1 . 1 . 1 . 1 .
pat timp lo=A2: 1 . . . . . . . 1 . . 1 1 . . .
drum bassdrum bars=8: ........X.......
drum bassdrum bars=16: X...............
drum crash bars=9: x...............

song army=marble piece=rook bpm=132 bars=16 gain=1.11 title=Storm_on_the_Ramparts
chords: Cm | G7 | Cm | G | Ab | Fm | Dm7b5:8 G7:8 | Cm
  | Cm | Bb | Ab | G7 | Cm | Ab | Fm:8 G7:8 | Cm
mel violin: | G5:4 C6 Eb6:6 D6:1 C6 | B5:4 D6 F6:6 Eb6:1 D6 | Eb6:4 D6:2 C6 G5:4 Eb5 | D5:4 G5 B5:8
  | C6:4 Eb6 Ab6:6 G6:1 F6 | F6:4 Eb6:2 D6 C6:4 Ab5 | D6:4 F6 B5 D6 | C6:12 r:4
  | r:16 | r:16 | r:16 | r:16
  | G5:4 C6 Eb6:6 D6:1 C6 | C6:4 Eb6 Ab6:6 G6:1 F6 | F6:2 Eb6 D6 C6 B5 D6 F6 D6 | C6:8 r:8 |
mel dbass: | C2:1 D2 Eb2 F2 G2:2 G2 C2:4 C2 | G1:1 A1 B1 C2 D2:2 D2 G1:4 G1 | C2:1 D2 Eb2 F2 G2:2 G2 C2:4 C2 | G1:1 A1 B1 C2 D2:4 G1:8
  | Ab1:1 Bb1 C2 Db2 Eb2:2 Eb2 Ab1:4 Ab1 | F1:1 G1 Ab1 Bb1 C2:2 C2 F1:4 F1 | D2:4 D2 G1 G1 | C2:1 D2 Eb2 F2 G2 Ab2 B2 C3 C2:8
  | C2:16 | Bb1:16 | Ab1:16 | G1:16
  | C2:1 D2 Eb2 F2 G2:2 G2 C2:4 C2 | Ab1:1 Bb1 C2 Db2 Eb2:2 Eb2 Ab1:4 Ab1 | F1:4 Ab1 G1 G1 | C2:8 r:8 |
mel cello bars=9-12: | C3:4 Eb3 G3:6 F3:1 Eb3 | D3:4 F3 Bb3:6 Ab3:1 G3 | Ab3:4 G3:2 F3 Eb3:4 C3 | B2:4 D3 F3 G3 |
pat spicc lo=G3 gate=0.6 vol=0.5: c c c c c c c c c c c c c c c c
pat trombone lo=C3 vol=0.55 bars=1-8,13-16: p - - - . . . . . . . . p - . .
pat horn lo=G3 gate=1 vol=0.45 bars=9-12: c - - - - - - - - - - - - - - -
pat timp lo=G2: 1 . . . . . . . . . . . . . . .
pat timp lo=G2 bars=4,8,12,15: . . . . . . . . 1 1 1 1 1 1 1 1
drum bassdrum bars=8,12,16: X...............

song army=marble piece=rook bpm=100 beat=4 bar=12 bars=21 gain=1.21 title=Passacaglia
chords: Gm | F | Eb | D | Gm | F | Eb | D | Gm | F | Eb | D | Gm | F | Eb | D | Gm | F | Eb | D | G
mel cello: | G2:12 | F2 | Eb2 | D2
  | G2:12 | F2 | Eb2 | D2
  | G2:4 Bb2 D3 | F2:4 A2 C3 | Eb2:4 G2 Bb2 | D2:4 F#2 A2
  | G2:12 | F2 | Eb2 | D2
  | G2:4 G2 G2 | F2:4 F2 F2 | Eb2:4 Eb2 Eb2 | D2:4 D2 D2 | G2:12 |
mel dbass vol=0.6 tr=-12: same
pat harpsi lo=Bb3 vol=0.9 bars=1-16: . . . . c - - - . . . .
mel violin bars=5-12: | G4:4 D5:6 C5:2 | A4:4 C5:6 Bb4:2 | G4:4 Bb4:6 A4:2 | F#4:8 D4:4
  | D5:2 G5 F5 G5 Bb5 G5 | C5:2 F5 Eb5 F5 A5 F5 | Bb4:2 Eb5 D5 Eb5 G5 Eb5 | A4:2 D5 C5 D5 F#5 D5 |
mel oboe bars=13-16: | Bb5:8 A5:2 G5 | A5:8 G5:2 F5 | G5:4 Bb5 Eb5 | F#5:8 A5:4 |
pat violin lo=G4 vol=0.35 bars=13-16: 1 3 5 8 5 3 1 3 5 8 5 3
mel trumpet bars=17-21: | G5:4 D6:6 C6:2 | A5:4 C6:6 Bb5:2 | G5:4 Bb5:6 A5:2 | F#5:8 D5:4 | G5:12 |
mel horn bars=17-21 tr=-12 vol=0.6: same
pat church lo=G3 gate=1 vol=0.4 bars=17-21: c - - - - - - - - - - -
pat timp lo=D2 bars=17-21: 1 . . . . . . . . . . .

# ---------------------------------------------------------------- the queen: elegant, twirling, powerful
song army=marble piece=queen bpm=168 beat=2 bar=6 bars=32 gain=1.43 title=Queen's_Waltz
chords: F | F | F | C7 | C7 | C7 | C7 | F
  | F | F | F | Bb | Bb | F | C7 | F
  | Bb | Bb | F7 | F7 | F7 | Bb | C7 | C7
  | F | F | F | Bb | Bb | F | C7 | F
drum triangle bars=17-32: x.....
pat cello lo=F2 gate=0.45: 1 . . . . . | 5, . . . . .
pat spicc lo=A3 gate=0.5 vol=0.9: . . c . c .
pat horn lo=F3 gate=1 vol=0.35 bars=17-24: c - - - - -
pat harp lo=F3 vol=0.45 bars=25-32: 1 5 8 10 12 15
mel violin: | C5:2 F5 A5 | C6:4 B5:2 | C6:2 A5 F5 | G5:6 | Bb4:2 E5 G5 | Bb5:4 A5:2 | Bb5:2 G5 E5 | F5:6
  | C5:2 F5 A5 | C6:4 B5:2 | C6:2 A5 F5 | D6:6 | D6:2 C6 Bb5 | A5:4 F5:2 | G5:4 E5:2 | F5:6
  | F5:2 D5 F5 | Bb5:4 A5:2 | Eb5:2 F5 A5 | C6:4 Bb5:2 | C6:2 A5 F5 | Bb5:4 D6:2 | C6:2 Bb5 G5 | Bb5:2 G5 E5
  | C5:2 F5 A5 | C6:4 B5:2 | C6:2 A5 F5 | D6:6 | D6:2 C6 Bb5 | A5:4 F5:2 | G5:4 E5:2 | F5:6 |
mel flute bars=17-24 vol=0.5: | F5:2 D5 F5 | Bb5:4 A5:2 | Eb5:2 F5 A5 | C6:4 Bb5:2 | C6:2 A5 F5 | Bb5:4 D6:2 | C6:2 Bb5 G5 | Bb5:2 G5 E5 |
mel glock bars=25-32 tr=12 vol=0.3: | C5:2 F5 A5 | C6:4 B5:2 | C6:2 A5 F5 | D6:6 | D6:2 C6 Bb5 | A5:4 F5:2 | G5:4 E5:2 | F5:6 |

song army=marble piece=queen bpm=120 bars=16 gain=1.42 title=Gavotte
chords: A | A:8 E:8 | A | Bm:8 E7:8 | A | A:8 E:8 | A:8 D:8 | E:8 A:8
  | E | B7 | E | F#m:8 C#7:8 | F#m:8 Bm:8 | E7 | A:8 D:8 | E7:8 A:8
mel violin: | r:8 E5:4 A5 | C#6:4 B5:2 A5 G#5:4 A5:2 B5 | C#6:4 A5:2 C#6 E6:4 C#6 | D6:4 B5 A5:2 G#5 F#5 G#5
  | A5:8 E5:4 A5 | C#6:4 B5:2 A5 G#5:4 A5:2 B5 | C#6:2 D6 E6 C#6 D6:4 F#5 | E5:4 B5 A5 E5
  | B5:4 G#5:2 A5 B5:4 E6 | D#6:4 C#6:2 B5 F#5:4 B5 | E6:4 B5:2 G#5 E5:4 G#5 | A5:4 F#5 E#5 G#5
  | F#5:4 A5 B5 D6 | B5:2 C#6 D6 B5 G#5:4 E5 | A5:4 C#6:2 E6 D6:4 F#5 | E5:4 G#5 A5:8 |
mel oboe bars=9-16 vol=0.5: | B5:4 G#5:2 A5 B5:4 E6 | D#6:4 C#6:2 B5 F#5:4 B5 | E6:4 B5:2 G#5 E5:4 G#5 | A5:4 F#5 E#5 G#5
  | F#5:4 A5 B5 D6 | B5:2 C#6 D6 B5 G#5:4 E5 | A5:4 C#6:2 E6 D6:4 F#5 | E5:4 G#5 A5:8 |
pat harpsi lo=C#4 vol=0.9: c . . . c . . . c . . . c . . .
pat cello lo=A2 gate=0.7: 1 . . . 5 . . . 1 . . . 5 . . .
pat viola lo=E3 gate=0.6 vol=0.3 bars=9-16: . . c . . . c . . . c . . . c .

song army=marble piece=queen bpm=96 beat=4 bar=12 bars=16 gain=1.10 title=Polonaise
chords: Eb | Bb7 | Eb | Bb | Eb | Ab | Bb7 | Eb
  | Cm | G7 | Cm | G | Ab | Eb | Ab:6 Bb7:6 | Eb
mel violin: | Bb4:2 Eb5:1 F5 G5:2 Bb5 Eb6:4 | D6:2 C6:1 Bb5 Ab5:2 F5 D5:4 | G5:2 Bb5:1 Ab5 G5:2 F5 Eb5:4 | F5:6 D5:2 Bb4:4
  | Bb4:2 Eb5:1 F5 G5:2 Bb5 Eb6:4 | C6:2 Eb6:1 D6 C6:2 Ab5 Eb5:4 | D6:2 F6:1 Eb6 D6:2 C6 Bb5 Ab5 | G5:4 Eb5 r
  | G5:2 C6:1 D6 Eb6:2 D6 C6:4 | B5:2 D6:1 C6 B5:2 G5 F5:4 | Eb5:2 G5:1 F5 Eb5:2 D5 C5:4 | D5:6 B4:2 G4:4
  | C6:2 Eb6:1 D6 C6:2 Bb5 Ab5:4 | G5:2 Bb5:1 Ab5 G5:2 F5 Eb5:4 | C6:2 Ab5:1 Bb5 C6:2 D6 F5 Ab5 | Eb6:6 Bb5:2 Eb5:4 |
mel trumpet bars=9-16 vol=0.55: | G5:2 C6:1 D6 Eb6:2 D6 C6:4 | B5:2 D6:1 C6 B5:2 G5 F5:4 | Eb5:2 G5:1 F5 Eb5:2 D5 C5:4 | D5:6 B4:2 G4:4
  | C6:2 Eb6:1 D6 C6:2 Bb5 Ab5:4 | G5:2 Bb5:1 Ab5 G5:2 F5 Eb5:4 | C6:2 Ab5:1 Bb5 C6:2 D6 F5 Ab5 | Eb6:6 Bb5:2 Eb5:4 |
pat spicc lo=G3 gate=0.5 vol=0.9: c . c c c . c . c . c .
pat cello lo=Eb2 gate=0.6: 1 . . . 1 . . . 1 . . .
pat timp lo=Bb1 gate=0.8: 1 . 1 1 1 . . . . . . .
drum sidedrum bars=9-16 vol=0.6: x.xxx.......
drum crash bars=9,16: x...........

# ---------------------------------------------------------------- the king: majestic, ceremonial
song army=marble piece=king bpm=112 bars=16 gain=1.17 title=Ceremonial_March
chords: C | C | F:8 G7:8 | C | C | Am | Dm:8 G7:8 | C7
  | F | Bb | F:8 Dm:8 | Gm:8 C7:8 | F | Bb:8 Gm:8 | F:8 C7:8 | F
mel trumpet bars=1-8: | G4:3 G4:1 C5:4 E5:3 E5:1 G5:4 | C6:6 B5:2 A5:4 G5 | A5:3 A5:1 F5:4 G5:3 G5:1 D5:4 | E5:4 C5 G4:8
  | G4:3 G4:1 C5:4 E5:3 E5:1 G5:4 | A5:6 G5:2 E5:4 C5 | F5:3 E5:1 D5:4 B4:3 C5:1 D5:4 | C5:8 r |
mel violin bars=1-8 vol=0.6: same
mel violin bars=9-16: | C5:8 A5:4 G5 | F5:8 D5:4 Bb4 | C5:4 F5 A5 D6 | D6:4 Bb5 C6:8 | C6:8 A5:4 F5 | D6:8 Bb5:4 G5 | A5:4 F5 G5 E5 | F5:16 |
mel horn bars=9-16 tr=-12 vol=0.7: same
mel trumpet bars=13-16 vol=0.6: | C6:8 A5:4 F5 | D6:8 Bb5:4 G5 | A5:4 F5 G5 E5 | F5:16 |
pat tuba lo=C2 gate=0.6: 1 . . . . . . . 5, . . . . . . .
pat spicc lo=G3 gate=0.5 vol=0.8 bars=1-8: . . . . c . . . . . . . c . . .
pat strings lo=A3 gate=1 vol=0.45 bars=9-16: c - - - - - - - c - - - - - - -
pat timp lo=C2 bars=9-16: 1 . . . . . . . 5, . . . . . . .
drum sidedrum bars=1-8: X.o.x.o.X.o.x.ox
drum bassdrum: x.......x.......
drum crash bars=9,13,16: x...............

song army=marble piece=king bpm=104 bars=16 gain=1.15 title=Trumpet_Voluntary
chords: D | D | G:8 A:8 | D | D | Em:8 A:8 | D:8 A7:8 | D
  | A | E7 | A:8 D:8 | A7 | D | G | D:8 A7:8 | D
mel trumpet tr=-2: | A4:4 D5 D5:3 E5:1 F#5:4 | E5:2 F#5 G5 E5 F#5:4 D5 | B5:4 A5:2 G5 A5:4 E5 | F#5:3 E5:1 D5:4 A4:8
  | A4:4 D5 F#5:3 G5:1 A5:4 | B5:4 G5:2 E5 C#6:4 A5 | F#5:2 G5 A5 F#5 E5:3 F#5:1 G5:2 E5 | D5:8 r:4 A4
  | E5:4 A5 A5:3 B5:1 C#6:4 | B5:2 C#6 D6 B5 G#5:4 E5 | C#6:4 A5:2 C#6 D6:4 A5 | B5:3 A5:1 G5:4 E5:8
  | F#5:4 A5 D6:3 C#6:1 D6:4 | B5:4 G5:2 B5 D6:4 B5 | A5:3 B5:1 A5:2 F#5 E5:3 F#5:1 E5:2 C#5 | D5:12 r:4 |
pat church lo=F#3 gate=1 vol=0.45 tr=-2: c - - - - - - - c - - - - - - -
pat cello lo=D2 gate=0.8 tr=-2: 1 . . . 5, . . . 1 . . . 5, . . .
pat timp lo=A2 tr=-2: 1 . . . . . . . 1 . . . . . . .
pat harpsi lo=A3 vol=0.6 tr=-2 bars=9-16: c . c . c . c . c . c . c . c .

song army=marble piece=king bpm=116 bars=16 gain=1.05 title=Coronation_Anthem
chords: G | C | G | D7 | Em | Am | D | D7
  | G | C | D | G | Em:8 C:8 | D | C:8 D7:8 | G
pat violin lo=G4 vol=0.4 bars=1-4: 1 3 5 8 1 3 5 8 1 3 5 8 1 3 5 8
pat violin lo=G4 vol=0.55 bars=5-16: 1 3 5 8 1 3 5 8 1 3 5 8 1 3 5 8
pat viola lo=D4 vol=0.5: . 1 3 5 . 1 3 5 . 1 3 5 . 1 3 5
pat cello lo=G2 gate=0.6: 1 . 1 . 1 . 1 . 1 . 1 . 1 . 1 .
mel oboe bars=5-8: | B4:16 | C5:16 | D5:16 | F#5:8 A5:8 |
pat choir lo=B3 gate=1 vol=0.7 bars=9-15: c - - - - - c - c - - - c - - -
pat choir lo=B3 gate=1 vol=0.7 bars=16: c - - - - - - - - - - - - - - -
mel trumpet bars=9-16: | G4:4 B4 D5 G5 | E5:8 G5:4 E5 | F#5:4 A5 D6:6 C6:1 B5 | B5:8 G5 | G5:4 E5 E5 G5 | F#5:4 A5 D5:8 | E5:4 G5 F#5 A5 | G5:16 |
pat church lo=G3 gate=1 vol=0.4 bars=9-16: c - - - - - - - c - - - - - - -
pat timp lo=D2 bars=9-16: 1 . . . . . 1 . 1 . . . 5, . . .
drum crash bars=9,16: x...............

# ===========================================================================
# The medieval army: early music - recorders, shawms, bagpipes, hurdy-gurdy, lute, tabor
# ===========================================================================
patch recorder  synth flute noise=0.035 a=0.03 d=0.3 s=0.9 r=0.1 vib=5,0.08,0.35 cut=5 gain=0.4 rev=0.25 del=0.1
patch shawm     synth sqr pw=0.22 a=0.02 d=0.5 s=0.9 r=0.1 cut=4 res=0.3 drive=1.4 vib=5.5,0.1,0.25 gain=0.45 rev=0.2
patch crumhorn  synth sqr pw=0.5 a=0.015 d=0.4 s=0.9 r=0.08 hz=900 cut=1 res=0.35 gain=0.3 rev=0.2
patch chanter   synth sqr pw=0.3 a=0.01 d=0.3 s=1 r=0.05 cut=6 res=0.2 drive=2 gain=0.3 rev=0.2
patch drone     synth saw uni=2 det=4 a=0.25 d=1 s=1 r=0.6 cut=2.5 res=0.1 drive=1.2 gain=0.22 rev=0.25
patch hurdy     synth saw a=0.02 d=0.6 s=0.9 r=0.12 cut=3.5 res=0.2 drive=1.6 vib=6,0.1,0.2 trem=7,0.12 gain=0.42 rev=0.2
patch fiddle    synth saw a=0.04 d=1 s=0.85 r=0.25 cut=3 res=0.2 drive=1.2 vib=5.5,0.18,0.15 gain=0.48 rev=0.25
patch lute      ks bright=0.55 damp=0.35 decay=1.4 r=0.3 gain=0.7 rev=0.3
patch psaltery  ks bright=0.9 damp=0.1 decay=2.5 r=0.6 gain=0.55 rev=0.35 del=0.1
patch sackbut   synth saw uni=2 det=4 a=0.04 d=1 s=0.8 r=0.3 cut=1.3 res=0.1 fenv=1.2 fatk=0.05 fdec=0.3 gain=0.55 rev=0.3
patch cornett   synth saw a=0.025 d=0.8 s=0.8 r=0.2 cut=1.8 res=0.15 fenv=1 fatk=0.03 fdec=0.2 vib=5,0.08,0.3 gain=0.55 rev=0.3
patch portative synth flute uni=2 det=3 a=0.01 d=0.2 s=1 r=0.08 gain=0.32 rev=0.3
patch tabor     drum snare f=160 nf=1300 d=0.22 tone=0.7 snap=0.45 gain=0.6 rev=0.2
patch frame     drum membrane f=85 pamt=0.3 pdec=0.04 d=0.5 snap=0.45 nf=700 ndec=0.03 gain=0.7 rev=0.25
patch naker     drum membrane f=150 pamt=0.25 pdec=0.03 d=0.35 snap=0.5 nf=1600 ndec=0.015 gain=0.6 rev=0.2
patch zills     drum metal f=650 nf=8000 d=1.0 metal=0.9 gain=0.6 rev=0.25
patch bourdon   synth saw uni=2 det=6 sub=0.4 a=0.3 d=1 s=1 r=0.6 cut=1.6 res=0.3 drive=2 wah=0.25,0.6 gain=0.22 rev=0.3

# ---------------------------------------------------------------- the pawn: a man-at-arms - marching, humble, eager
song army=medieval piece=pawn bpm=116 bars=16 gain=0.80 title=Men-at-Arms
chords: Dm | C | Dm | Am | F | C | Dm:8 Am:4 C:4 | Dm
drum tabor: X..x x.x. X..x x.x. *7 | X.x.x.x. xxxx XxXx
drum frame bars=9-16: X... .... x... ....
drum zills bars=1,9: x... .... .... ....
pat crumhorn lo=D3 vol=0.6: 1 - - . 5 - - . 1 - - . 5 - 1 .
pat lute lo=D4 bars=9-16 gate=0.5: . . c . . . c . . . c . . . c .
mel recorder: | D5:4 D5:2 E5 F5:4 A5 | G5:4 E5:2 C5 E5:4 G5 | A5:3 G5:1 F5:2 E5 D5:4 F5 | E5:2 D5 C5 E5 A4:8
  | F5:4 A5:2 C6 A5:4 F5 | G5:2 A5 G5 E5 C5:4 E5 | D5:2 E5 F5 G5 A5:4 C5:2 E5 | D5:4 A4 D5:8 |
mel shawm tr=-12 bars=9-16 vol=0.8: same

song army=medieval piece=pawn bpm=132 bars=16 gain=0.98 title=Kettle_Hat_Bransle
chords: G | F | G | D | G | C | F | G
drum tamb vol=2.5: X.o. x.o. X.o. x.oo
drum frame: X... ..x. x... ....
mel drone gate=1: G2+D3:16 | _:16 | _:16 | _:16 |
mel chanter: | G4:2 B4 D5 B4 G4:4 D5 | C5:2 F5 E5 D5 C5:4 A4 | B4:2 D5 G5 F5 E5 D5 B4:4 | A4:2 B4 C5 B4 A4:8
  | G5:4 F5:2 E5 D5:4 G5 | E5:2 F5 G5 E5 C5:4 E5 | F5:2 E5 D5 C5 A4:4 C5 | B4:2 A4 G4:12 |
mel recorder tr=12 bars=9-16 vol=0.6: same
drum tabor bars=9-16: x.x. x.xx x.x. x.xx

song army=medieval piece=pawn bpm=100 beat=6 bar=12 bars=32 gain=0.98 title=A_Flagon_for_the_Spearmen
chords: Am | G | Am | Em | Am | G | F:6 G:6 | Am | C | G | Am | Em | F | C | Dm:6 E:6 | Am
drum frame: X..... x.....
drum tamb vol=2.5: X.o.o. x.o.o.
pat lute lo=A3 vol=0.75: 1 . 8 . 5 . 1 . 8 . 5 .
mel recorder: | A4:4 C5:2 E5:4 A5:2 | G5:4 E5:2 D5:4 B4:2 | C5:4 E5:2 A5:4 C6:2 | B5:6 G5:4 E5:2
  | A5:4 G5:2 E5:4 C5:2 | D5:4 E5:2 G5:4 D5:2 | C5:2 D5 E5 D5:4 B4:2 | A4:6 r:4 E5:2
  | G5:4 C6:2 G5:4 E5:2 | D5:4 G5:2 B5:4 G5:2 | A5:4 C6:2 B5:4 A5:2 | G5:6 E5:6
  | F5:4 A5:2 C6:4 A5:2 | G5:4 E5:2 C5:4 E5:2 | D5:2 F5 A5 B5:4 G#5:2 | A5:6 r:6 |
mel fiddle tr=-12 bars=17-32 vol=0.9: same
pat sackbut lo=A2 bars=17-32: 1 - - - - . 5, - - - - .
drum zills bars=17,25: x..... ......

# ---------------------------------------------------------------- the knight: canter, gallop and the joust
song army=medieval piece=knight bpm=120 bars=16 gain=0.96 title=Lists_and_Lances
chords: D | D | G | A | D | C | G:8 A:8 | D
drum naker note=D3: X..x ..x. X..x ..x.
drum naker note=A2: .... x... .... x.xx
drum tabor bars=9-16: x.xx x.x. x.xx x.xx
pat sackbut lo=D3 vol=0.6: 1 - - 1 5 - - 5 1 - - 1 8 - 5 .
mel cornett: | D5:2 D5 F#5 A5 D6:4 A5 | F#5:2 A5 F#5 D5 A4:8 | B4:2 D5 G5 B5 A5:4 G5 | A5:6 G5:2 F#5:4 E5
  | D6:4 A5:2 F#5 D5:4 F#5 | E5:2 G5 C6 G5 E5:4 C5 | D5:2 G5 B5 G5 A5:4 C#6 | D6:8 r:8 |
mel sackbut tr=-12 bars=9-16 vol=0.8: same
drum zills bars=8,16: .... .... x... ....

song army=medieval piece=knight bpm=112 beat=6 bar=12 bars=32 gain=0.96 title=Saltarello_of_the_Spurs
chords: Dm | Dm | C | Dm | Dm | C | Dm:6 C:6 | Dm | F | C | Dm | Am | F | G | Am:6 C:6 | Dm
drum tabor: X...x. X...x.
drum frame: X..... ......
mel drone gate=1: D3+A3:12 | _:12 | _:12 | _:12 |
mel shawm: | D5:4 E5:2 F5:4 G5:2 | A5:4 G5:2 F5:4 E5:2 | G5:4 E5:2 C5:4 E5:2 | F5:4 E5:2 D5:6
  | A5:4 A5:2 C6:4 A5:2 | G5:4 F5:2 E5:4 C5:2 | D5:2 E5 F5 E5:4 C5:2 | D5:6 r:6
  | F5:4 A5:2 C6:4 A5:2 | G5:4 E5:2 C6:4 G5:2 | A5:4 F5:2 D5:4 F5:2 | E5:6 A5:6
  | C6:4 A5:2 F5:4 A5:2 | B5:4 G5:2 D5:4 G5:2 | A5:2 G5 F5 E5:4 C5:2 | D5:12 |
mel hurdy tr=-12 bars=17-32 vol=0.9: same
drum tamb bars=17-32 vol=2.5: x.o.o. x.o.o.
drum naker note=D3 bars=17-32: ...... x.x.x.

song army=medieval piece=knight bpm=152 beat=4 bar=12 bars=24 gain=1.34 title=Estampie_of_the_Great_Helm
chords: Am | G | Am | Em | D | Em | Am | G | Am | Em | G | Am | C | G | Am | D | C | D | C | G | Am | D | G | Am
drum naker note=A2: X....... x...
drum naker note=E3: .... x.x. ..x.
drum tamb bars=13-24 vol=2.5: x.o. x.o. x.o.
pat psaltery lo=E4 vol=0.5: 1 . 5 . 8 . 5 . 3 . 5 .
pat harp lo=A2: 1 . . . . . . . 5 . . .
mel fiddle: | A4:4 C5 E5 | D5:2 E5 D5 C5 B4:4 | C5:4 E5:2 G5 A5:4 | G5:2 E5 B4:8
  | A4:2 B4 C5 D5 E5:4 | B4:4 E5:8
  | A4:4 C5 E5 | D5:2 E5 D5 C5 B4:4 | C5:4 E5:2 G5 A5:4 | G5:2 E5 B4:8
  | D5:2 C5 B4 G4 B4:4 | A4:12
  | E5:4 G5 C6 | B5:2 A5 G5 D5 G5:4 | A5:4 E5:2 C5 E5:4 | F#5:2 E5 D5 E5 F#5:4
  | G5:2 E5 C5 E5 G5:4 | A5:4 D5:8
  | E5:4 G5 C6 | B5:2 A5 G5 D5 G5:4 | A5:4 E5:2 C5 E5:4 | F#5:2 E5 D5 E5 F#5:4
  | B4:2 D5 C5 B4 G4:4 | A4:12 |
mel recorder bars=13-24 vol=0.8: | E5:4 G5 C6 | B5:2 A5 G5 D5 G5:4 | A5:4 E5:2 C5 E5:4 | F#5:2 E5 D5 E5 F#5:4
  | G5:2 E5 C5 E5 G5:4 | A5:4 D5:8
  | E5:4 G5 C6 | B5:2 A5 G5 D5 G5:4 | A5:4 E5:2 C5 E5:4 | F#5:2 E5 D5 E5 F#5:4
  | B4:2 D5 C5 B4 G4:4 | A4:12 |

# ---------------------------------------------------------------- the bishop: mitre and crozier, cathedral, organ and choir
song army=medieval piece=bishop bpm=104 bars=16 gain=1.16 title=Organum_of_the_Crozier
chords: D5
mel church gate=1 vol=0.7: D3+A3:16 | _:16 | _:16 | _:16 |
pat bell lo=D4 vol=0.6: 1 . . . . . . . 5 . . . . . . .
drum frame bars=9-16: x... .... x... ....
mel choir vol=1.4: | D4:4 F4 E4 G4 | F4:4 G4 A4:8 | C5:4 B4 A4 G4 | A4:12 G4:4
  | F4:4 A4 G4 F4 | E4:4 G4 F4 E4 | D4:4 F4 E4 C4 | D4:16 |
mel choir tr=-5 bars=9-16 vol=0.8: same
mel recorder tr=12 bars=9-16 vol=0.5: same

song army=medieval piece=bishop bpm=116 bars=16 gain=0.93 title=Procession_of_the_Mitre
chords: F | C | F | Bb | F | C | Bb:8 C:8 | F
drum tabor: X... x.x. X... x...
drum tamb bars=9-16 vol=2.5: ..x. ..x. ..x. ..x.
pat bell lo=F4 vol=0.5: 1 . . . . . . . . . . . . . . .
pat sackbut lo=F2: 1 - - - 5 - - - 1 - - - 5 - - -
mel portative: | F4:2 G4 A4 C5 F5:4 C5 | E5:2 D5 C5 G4 C5:8 | A4:2 C5 F5 G5 A5:4 F5 | G5:2 F5 D5 Bb4 D5:8
  | C5:2 A4 C5 F5 A5:4 G5 | E5:2 G5 E5 C5 G4:8 | D5:2 F5 Bb5 F5 E5:4 C5 | F5:12 r:4 |
mel choir tr=-12 bars=9-16: same
mel recorder tr=12 bars=9-16 vol=0.5: same

song army=medieval piece=bishop bpm=152 beat=4 bar=12 bars=32 gain=1.06 title=Laudate_Saltando
chords: G | C | G | F | G | F | C:4 F:8 | G | G | F | C | G | G | Dm | F | G
drum tamb vol=2.5: X... x... x.x.
drum frame: x... .... x...
pat portative lo=G3 gate=0.6 vol=0.8: 1 . . . c . . . c . . .
mel recorder: | G4:4 B4 D5 | E5:4 D5:2 C5 E5:4 | D5:4 B4 G4 | A4:4 C5:8
  | B4:4 D5 G5 | F5:4 E5:2 D5 C5:4 | E5:4 C5:2 A4 F4:4 | G4:12
  | D5:2 E5 G5:4 B5 | A5:4 F5 C5 | G5:2 A5 G5 E5 C5:4 | D5:4 B4:8
  | D5:2 E5 G5:4 B5 | A5:4 F5:2 E5 D5:4 | C6:4 A5:2 F5 A5:4 | G5:12 |
mel fiddle tr=-12 bars=17-32 vol=0.8: same
mel choir tr=-12 bars=17-32 vol=0.9: same
drum zills bars=9,17,25: x... .... ....

# ---------------------------------------------------------------- the rook: a stone gatehouse with a drawbridge, and a dragon inside
song army=medieval piece=rook bpm=100 bars=16 gain=1.12 title=The_Gatehouse_Stands
chords: Dm | Dm | Bb | C | Dm | Bb | C:8 A:8 | Dm
drum frame: X... .... X... ....
drum tabor: .... x... .... x.xx
drum naker note=D3 bars=9-16: x.x. x.x. x.x. x.x.
mel drone gate=1: D2+A2:16 | _:16 | _:16 | _:16 |
mel sackbut: | D3:6 D3:2 F3:4 A3 | D4:6 C4:2 A3:8 | Bb3:6 A3:2 F3:4 D3 | E3:6 F3:2 G3:8
  | A3:6 A3:2 D4:4 F4 | F4:6 E4:2 D4:4 Bb3 | G3:4 E3 A3 C#4 | D4:8 A3:4 D3 |
mel crumhorn tr=12 bars=9-16 vol=0.8: same

song army=medieval piece=rook bpm=132 bars=16 gain=0.90 title=Portcullis_Ductia
chords: Gm | F | Gm | C:8 Dm:8 | Gm | F | Gm | F:8 Gm:8 | Bb | F | Gm | C:8 Dm:8 | Bb | F | Gm | F:8 Gm:8
drum tabor: X..x ..x. X..x ..x.
drum frame bars=9-16: X... .... x... x...
mel bourdon gate=1: G2+D3:16 | _:16 | _:16 | _:16 |
mel crumhorn: | G4:2 A4 Bb4 G4 D5:4 Bb4 | A4:2 Bb4 C5 A4 F4:4 A4 | Bb4:2 A4 G4 F4 G4:4 D4 | E4:2 F4 E4 C4 D4:8
  | G4:2 A4 Bb4 G4 D5:4 Bb4 | A4:2 Bb4 C5 A4 F4:4 A4 | Bb4:2 A4 G4 F4 G4:4 D4 | A4:2 Bb4 C5 A4 G4:8
  | D5:2 C5 Bb4 C5 D5:4 F4 | C5:2 Bb4 A4 Bb4 C5:4 A4 | Bb4:2 C5 D5 C5 Bb4:4 G4 | C5:2 Bb4 A4 G4 A4:8
  | D5:2 C5 Bb4 C5 D5:4 F4 | C5:2 Bb4 A4 Bb4 C5:4 A4 | Bb4:2 C5 D5 C5 Bb4:4 G4 | A4:2 G4 F4 A4 G4:8 |
mel crumhorn tr=-5 vol=0.6: same
mel shawm tr=12 bars=9-16 vol=0.6: | D5:2 C5 Bb4 C5 D5:4 F4 | C5:2 Bb4 A4 Bb4 C5:4 A4 | Bb4:2 C5 D5 C5 Bb4:4 G4 | C5:2 Bb4 A4 G4 A4:8
  | D5:2 C5 Bb4 C5 D5:4 F4 | C5:2 Bb4 A4 Bb4 C5:4 A4 | Bb4:2 C5 D5 C5 Bb4:4 G4 | A4:2 G4 F4 A4 G4:8 |

song army=medieval piece=rook bpm=108 bars=16 gain=0.90 title=The_Dragon_in_the_Gate
chords: Em | F | Em | Dm:8 Em:8 | Am | F | G:8 F:8 | Em
drum frame: X.x. .... X.x. ....
drum naker note=E3 bars=9-16: .... ..x. .... x.xx
mel bourdon gate=1: E2+B2:16 | _:16 | _:16 | _:16 |
mel crumhorn vol=0.7: E3:2 E3 F3 E3 B2:4 E3:2 F3 |
mel shawm: | E4:4 G4 B4:6 A4:2 | C5:6 B4:2 A4:4 F4 | G4:4 F4 E4:8 | D4:4 F4 E4:8
  | A4:4 C5 E5:6 D5:2 | C5:4 A4 F5:8 | D5:4 B4 C5 A4 | G4:4 F4 E4:8 |
mel sackbut tr=-12 bars=9-16 vol=0.8: same
mel bell bars=9-16 vol=0.7: E4:16 |

# ---------------------------------------------------------------- the queen: enthroned with her sceptre - graceful and powerful
song army=medieval piece=queen bpm=138 beat=4 bar=12 bars=32 gain=0.91 title=Basse_Danse_of_the_Sceptre
chords: F | Bb | F | C | Dm | Gm | C | F | Bb | F | Gm | Dm | Bb | C | Bb:6 C:6 | F
pat lute lo=F3 vol=0.75: 1 . 5 . 8 . 10 . 8 . 5 .
pat harp lo=F2: 1 . . . . . . . 5 . . .
drum tabor bars=17-32 vol=0.8: x... .... x.x.
mel recorder: | C5:4 F5:6 E5:2 | D5:4 F5 Bb5 | A5:6 G5:2 F5:4 | E5:4 G5:8
  | F5:4 A5 D6 | Bb5:6 A5:2 G5:4 | G5:4 E5 C5 | F5:8 E5:2 D5
  | D5:4 F5:6 D5:2 | C5:4 F5 A5 | G5:4 Bb5:6 A5:2 | F5:4 D5:8
  | F5:4 D5 Bb4 | C5:2 D5 E5:4 G5 | F5:4 D5:2 E5:6 | F5:12 |
mel fiddle tr=-12 bars=17-32 vol=0.75: same
mel psaltery bars=17-32 vol=0.7: same

song army=medieval piece=queen bpm=104 bars=16 gain=1.01 title=The_Queen's_Pavane
chords: Gm | F | Bb | D | Eb:8 Bb:8 | Cm:8 D:8 | Gm:8 D:8 | Gm
drum tabor: X... .... x... x...
pat sackbut lo=G2 vol=0.8: 1 - - - - - - - 5 - - - 1 - - -
mel cornett: | D5:8 Bb4:4 D5 | C5:8 A4:4 C5 | D5:8 F5:4 D5 | A5:8 F#5:4 D5
  | G5:8 F5:4 D5 | C5:4 Eb5 D5 A4 | Bb4:4 G4 A4 F#4 | G4:16 |
mel fiddle tr=-12 bars=9-16 vol=0.8: same
mel recorder tr=12 bars=9-16 vol=0.45: same
pat lute lo=G3 bars=9-16: 1 . 5 . 8 . 5 . 1 . 5 . 8 . 5 .

song army=medieval piece=queen bpm=168 beat=4 bar=12 bars=32 gain=1.13 title=Galliard_of_the_Five_Steps
chords: D | G | D | A | Bm | G | G:8 A:4 | A:4 D:8 | D | G | A | Bm | G | Em | A | D
drum tamb vol=2.5: X... x.x. x...
drum tabor bars=17-32: x... .... x.x.
drum zills bars=1,9,17,25: x... .... ....
pat lute lo=D3 vol=0.6: 1 . 5 . c . . . c . 5 .
mel recorder: | D5:4 F#5 A5 | B5:8 A5:4 | F#5:4 A5:2 F#5 D5:4 | E5:8 C#5:4
  | D5:4 F#5 B5 | B5:4 A5:2 G5 D5:4 | B5:8 C#6:4 | _:4 D6:8
  | A5:4 F#5 D5 | G5:4 B5:8 | A5:4 E5:2 F#5 G5:4 | F#5:8 D5:4
  | G5:4 B5 D6 | E6:8 B5:4 | C#6:4 B5:2 A5 E5:4 | D6:8 r:4 |
mel fiddle tr=-12 bars=17-32 vol=0.8: same

# ---------------------------------------------------------------- the king: bearded, in ermine, with his sword - regal
song army=medieval piece=king bpm=112 bars=16 gain=0.94 title=The_King's_Procession
chords: C | F | C | G | C | Am | F:8 G:8 | C
drum naker note=C3: X... x.x. X... x.x.
drum naker note=G2: .... x... .... x...
drum tamb bars=9-16 vol=2.5: ..x. ..x. ..x. ..xx
pat sackbut lo=C3 vol=0.75: 1 - - . 1 - 5 - 1 - - . 5 - - .
pat bell lo=C4 vol=0.6: 1 . . . . . . . 5 . . . . . . .
mel shawm: | C5:4 E5:2 G5 C6:6 G5:2 | A5:4 F5:2 A5 C6:8 | G5:4 E5:2 C5 G5:4 E5 | D5:6 E5:2 D5:4 B4
  | E5:4 G5:2 C6 C6:6 B5:2 | A5:4 G5:2 E5 A4:8 | F5:2 E5 D5 C5 B4:4 D5 | C5:8 r:8 |
mel shawm tr=-12 bars=9-16 vol=0.8: same

song army=medieval piece=king bpm=108 beat=6 bar=12 bars=32 gain=0.97 title=Feast_in_the_Great_Hall
chords: D | C | D | G | D | C | Am:6 C:6 | D | G | D | C | D | G | Em | C:6 D:6 | D
drum frame: X..... x.....
drum tamb vol=2.5: X.o.o. x.o.o.
mel drone gate=1: D3+A3:12 | _:12 | _:12 | _:12 |
mel chanter: | D5:2 F#5 A5 F#5:4 D5:2 | E5:2 G5 E5 C5:4 E5:2 | F#5:4 A5:2 B5:4 A5:2 | G5:4 D5:2 B4:6
  | A4:2 D5 F#5 A5:4 F#5:2 | G5:4 E5:2 C5:4 E5:2 | A4:2 C5 E5 G5:4 E5:2 | D5:6 F#5:4 A5:2
  | B5:4 A5:2 G5:4 B5:2 | A5:4 F#5:2 D5:4 F#5:2 | G5:4 E5:2 C5:4 G5:2 | F#5:6 A5:6
  | B5:2 A5 G5 A5:4 B5:2 | G5:4 E5:2 B4:4 E5:2 | E5:2 G5 E5 F#5:4 A5:2 | D5:6 r:6 |
mel hurdy tr=-12 bars=17-32 vol=0.8: same
mel psaltery bars=17-32 vol=0.7: same
drum zills bars=17-32: ...... x.....

song army=medieval piece=king bpm=104 bars=16 gain=0.87 title=Long_Live_the_King
chords: D | G | D | A | Bm | G | Em:8 A:8 | D
drum naker note=D3: X... .... x... ....
drum naker note=A2 bars=8,16: x.x. x.x. xxxx XXXX
pat church lo=D3 gate=1 vol=0.7: c - - - - - - - c - - - - - - -
pat sackbut lo=D2: 1 - - - - - - - 5 - - - 8 - - -
mel choir vol=1.4: | D4:4 F#4 A4 D5 | B4:8 A4:4 G4 | F#4:4 A4 D5:6 C#5:2 | B4:4 C#5 A4:8
  | D5:4 F#5:8 E5:4 | D5:6 B4:2 G4:8 | G4:4 B4 A4 C#5 | D5:16 |
mel cornett bars=9-16 vol=0.8: same
pat bell lo=D4 bars=9-16 vol=0.45: 1 . . . . . . . 8 . . . . . . .

# ===========================================================================
# The aliens: 1950s science fiction - theremin, vibraphone, bongos, bleeps
# ===========================================================================
patch theremin synth sin glide=0.07 a=0.08 d=0.5 s=0.95 r=0.35 vib=6,0.35,0.15 gain=0.3 rev=0.4 del=0.25
patch ondes    synth tri glide=0.04 a=0.04 d=0.6 s=0.9 r=0.3 vib=5.5,0.2,0.2 gain=0.34 rev=0.35 del=0.2
patch vibes    fm ratio=4 index=0.8 idec=0.08 a=0.001 d=3 s=0 r=0.9 trem=5,0.3 gain=0.38 rev=0.35
patch upright  ks bright=0.32 damp=0.6 decay=1.6 r=0.15 gain=0.75 rev=0.08
patch muted    synth saw a=0.015 d=0.7 s=0.7 r=0.15 filt=bp cut=3 res=0.5 vib=5,0.1,0.3 gain=0.8 rev=0.25
patch analog   synth saw a=0.005 d=0.4 s=0.5 r=0.2 cut=2 res=0.6 fenv=3 fdec=0.12 gain=0.3 rev=0.2 del=0.25
patch saucer   synth saw uni=2 det=18 a=0.4 d=1.5 s=0.8 r=1.2 cut=1.5 res=0.55 wah=0.4,1.5 gain=0.11 rev=0.5
patch warble   synth sin a=0.03 d=0.5 s=0.9 r=0.3 vib=7,1.0,0 gain=0.35 rev=0.35 del=0.2
patch bleep    synth sqr pw=0.5 a=0.001 d=0.18 s=0 r=0.05 bend=12,0.03 gain=0.3 rev=0.2 del=0.3
patch blip     synth sin a=0.001 d=0.25 s=0 r=0.05 bend=-7,0.04 gain=0.35 rev=0.25 del=0.3
patch ringmod  fm ratio=1.41 index=3 idec=0.25 a=0.002 d=0.8 s=0 r=0.3 gain=0.3 rev=0.3 del=0.2
patch rocksax  synth saw a=0.02 d=0.8 s=0.75 r=0.15 cut=2.2 res=0.3 fenv=1.5 fatk=0.02 fdec=0.15 vib=5.5,0.15,0.3 scoop=0.6 drive=2 gain=0.36 rev=0.18
patch jukebox  fm ratio=1 index=2 idec=0.25 p2=3 p2amp=0.2 p2dec=0.3 a=0.002 d=1.8 s=0 r=0.3 gain=0.42 rev=0.15
patch tripod   synth saw uni=2 det=12 a=0.05 d=1 s=0.9 r=0.4 cut=1.5 res=0.3 fenv=1.5 fatk=0.1 fdec=0.3 bend=-3,0.15 drive=2 gain=0.32 rev=0.35
patch rocket   synth sin noise=1 a=0.8 d=2.5 s=0.4 r=0.8 hz=150 fenv=5.5 fatk=1.6 fdec=0.8 gain=0.3 rev=0.4
patch bongo    drum membrane f=400 pamt=0.2 pdec=0.01 d=0.16 snap=0.3 nf=3000 ndec=0.004 gain=0.42 rev=0.12
patch conga    drum membrane f=210 pamt=0.15 pdec=0.015 d=0.3 snap=0.3 nf=2000 ndec=0.006 gain=0.45 rev=0.12
patch brush    drum shaker nf=4500 a=0.02 d=0.18 gain=0.3 rev=0.1
patch ride     drum metal f=120 nf=6000 d=1.4 metal=0.6 gain=0.22 rev=0.15
patch snap     drum clap nf=2200 d=0.1 gain=0.45 rev=0.2
patch clank    drum metal f=180 nf=2500 d=0.25 metal=0.8 gain=0.6 rev=0.25

# ---------------------------------------------------------------- the pawn: a little green man - waddling, with a ray gun
song army=alien piece=pawn bpm=116 bars=16 gain=1.27 title=Little_Green_March
chords: F | C7 | F | C7 | Bb | F | G7 | C7 | F | C7 | F | D7 | Gm | C7 | F:8 C7:8 | F
drum kick: x.......x.......
drum snare: X.o.x.o.X.o.x.oo
drum block vol=0.5: x...x...x...x...
drum crash bars=1,9: x...............
drum tamb bars=9-16: ..x...x...x...x.
pat tuba lo=A1: 1 - . . 5, - . . 1 - . . 5, - . .
pat vibes lo=A3 gate=0.4 vol=0.5: . . c . . . c . . . c . . . c .
mel muted: | C5:3 C#5:1 D5:2 C5:2 A4:4 F4:4 | G4:3 G#4:1 A4:2 G4:2 E4:4 C4:4 | F4:2 A4 C5 F5 E5:3 D5:1 C5:4 | Bb4:2 A4 G4 C5 E4:4 r:4
  | D5:3 D#5:1 E5:2 F5:2 D5:4 Bb4:4 | C5:3 C#5:1 D5:2 C5:2 A4:8 | B4:2 D5 G5 F5 E5:2 D5:2 B4:4 | C5:2 r:2 G4:2 r:2 C4:4 r:4
  | C5:3 C#5:1 D5:2 C5:2 A4:4 F4:4 | G4:3 G#4:1 A4:2 G4:2 E4:4 C4:4 | F4:2 A4 C5 F5 E5:3 D5:1 C5:4 | F#4:2 A4 C5 D5 F#5:4 A5:4
  | Bb5:3 A5:1 G5:2 F5:2 D5:4 Bb4:4 | G5:3 F5:1 E5:2 C5:2 Bb4:4 G4:4 | A4:2 C5 F5 A5 G5:2 E5 C5 Bb4 | A4:4 F4:4 F5:4 r:4 |
mel glock tr=12 vol=0.3 bars=9-16: | C5:3 C#5:1 D5:2 C5:2 A4:4 F4:4 | G4:3 G#4:1 A4:2 G4:2 E4:4 C4:4 | F4:2 A4 C5 F5 E5:3 D5:1 C5:4 | F#4:2 A4 C5 D5 F#5:4 A5:4
  | Bb5:3 A5:1 G5:2 F5:2 D5:4 Bb4:4 | G5:3 F5:1 E5:2 C5:2 Bb4:4 G4:4 | A4:2 C5 F5 A5 G5:2 E5 C5 Bb4 | A4:4 F4:4 F5:4 r:4 |
mel bleep: | r:16 *3 | r:12 G6:1 r C7 r | r:16 *3 | r:2 G6:1 r:1 r:2 E6:1 r:1 r:4 C7:1 r C7 r | r:16 *7 | r:2 G6:1 r:1 r:2 E6:1 r:1 r:4 C7:1 r C7 r |

song army=alien piece=pawn bpm=120 bars=16 gain=1.13 title=Martian_Cha-Cha
chords: Am | Bb7 | E7 | Am | Am | Dm7 | Bm7b5:8 E7:8 | Am | Fmaj7 | G7 | Cmaj7 | Am | Dm7 | Bb7 | E7 | Am
drum conga: ....x.......x.x.
drum bongo vol=0.7: x.o.x.o.x.o.x.oo
drum block vol=0.45: x...x...x...x...
drum shaker: x.xxx.xxx.xxx.xx
pat upright lo=E1: 1 . . . . . . . 5 . . . 3 . 5 .
pat vibes lo=A3 gate=0.5 vol=0.5: . . c . . c . . . . c . c . c .
pat celesta lo=A5 vol=0.3 bars=9-16: . 1 . 5 . 8 . 5 . 1 . 5 . 8 . 5
mel theremin: | E5:6 A5:2 C6:4 B5:4 | Bb5:8 Ab5:4 F5:4 | G#5:6 B5:2 D6:4 C6:2 B5:2 | A5:12 r:4
  | E5:6 A5:2 C6:4 E6:4 | F6:6 E6:2 D6:4 C6:4 | D6:4 B5:4 G#5:4 E5:4 | A5:12 r:4
  | A5:6 C6:2 E6:4 C6:4 | B5:6 D6:2 F6:4 D6:4 | E6:6 D6:2 C6:4 B5:4 | A5:6 G5:2 E5:8
  | F5:6 A5:2 D6:4 C6:4 | D6:6 C6:2 Bb5:4 Ab5:4 | G#5:4 B5:4 D6:4 F6:4 | A5:12 r:4 |
mel bleep vol=0.8: | r:16 *3 | r:12 A6:2 A6:2 |

song army=alien piece=pawn bpm=168 beat=2 bar=8 swing=0.3 bars=24 gain=1.02 title=Ray_Gun_Boogie
chords: C7 | F7 | C7 | C7 | F7 | F7 | C7 | C7 | G7 | F7 | C7 | G7
drum kick: x...x...
drum snare: ..x...x.
drum hat: xoxoxoxo
drum crash bars=1,13: x.......
pat upright lo=C2 vol=0.8: 1 3 5 6 7 6 5 3
pat jukebox lo=G4 gate=0.5 vol=0.45: . c . c . c . c
mel rocksax: | G4:1 Bb4 C5:2 Eb5:1 E5 G5:2 | A5:1 G5 Eb5:2 C5:4 | G4:1 Bb4 C5:2 Eb5:1 E5 G5:2 | Bb5:2 G5 C6:4
  | C5:1 Eb5 F5:2 Ab5:1 A5 C6:2 | Eb6:2 C6 A5 F5 | G5:1 Bb5 C6:2 Eb6:1 E6 C6:2 | G5:4 r:4
  | D6:2 B5 G5:1 F5 D5:2 | C6:2 A5 F5:1 Eb5 C5:2 | E5:1 G5 C6:2 Bb5:1 G5 E5:2 | D5:2 F5 G5 B5 |
mel guitar bars=13-24 tr=-12 vol=0.55: same
mel bleep: | r:8 *7 | r:4 C7:1 G6 E6 C6 | r:8 *4 |

# ---------------------------------------------------------------- the knight: a flying saucer - taking off, swooping, landing
song army=alien piece=knight bpm=132 bars=16 echo=3 gain=1.00 title=Saucer_Flight
chords: Em | C | Am | B7 | Em | Caug | Am6 | B7 | C | D | Bm | Em | C | Am | F#m7b5:8 B7:8 | Em
drum kick: x...x...x...x...
drum snare: ....x.......x...
drum hat: xoxoxoxoxoxoxoxo
drum ohat bars=9-16: ..x...x...x...x.
drum crash bars=1,9: x...............
mel tom bars=8,16: r:8 B3:2 G3 E3 B2
pat analog lo=E1 del=0: 1 1 8 1 1 8 1 1 8 1 1 8 1 8 1 8
pat saucer lo=E3 gate=1: c - - - - - - - - - - - - - - -
pat blip lo=E5 vol=0.5 bars=9-16: 8 . 5 . 10 . 5 . 8 . 5 . 12 . 5 .
mel theremin: | E5:4 B5:12 | C6:6 B5:2 G5:8 | A5:4 E6:12 | D#6:6 C6:2 B5:8
  | E5:4 B5:4 E6:8 | G#5:6 E6:2 C6:8 | F#5:6 A5:2 C6:8 | B5:16
  | G5:4 C6:4 E6:8 | F#6:6 E6:2 D6:8 | D6:4 B5:4 F#5:8 | G5:6 F#5:2 E5:8
  | E5:4 G5:4 C6:8 | E6:6 D6:2 C6:8 | A5:8 D#6:8 | E6:12 r:4 |

song army=alien piece=knight bpm=168 beat=4 bar=12 bars=32 gain=1.22 title=Crop_Circle_Waltz
chords: Dm | Dm | A7 | A7 | A7 | A7 | Dm | Dm | Dm | Dm7 | Gm | Gm | Bb | A7 | Dm | Dm
  | F | F | C7 | C7 | C7 | C7 | F | F | Bb | Bbm | F | D7 | Gm | A7 | Dm | Dm
drum brush: x...o...o...
drum ride bars=17-32: ....x...x...
pat upright lo=D2: 1 . . . . . . . . . . . | 5, . . . . . . . . . . .
pat vibes lo=A3 gate=0.5 vol=0.55: . . . . c . . . c . . .
pat organ lo=D4 gate=1 vol=0.35 bars=17-32: c - - - - - - - - - - -
pat celesta lo=A5 vol=0.3 bars=17-32: . . . . 5 . . . 8 . . .
mel theremin: | D5:4 F5:4 A5:4 | D6:8 C#6:4 | E6:4 Bb5:4 G5:4 | A5:12 | C#5:4 E5:4 G5:4 | Bb5:8 A5:4 | F5:4 E5:4 D5:4 | A5:12
  | D5:4 F5:4 A5:4 | C6:8 D6:4 | Bb5:4 D6:4 G6:4 | G6:8 F6:4 | F6:4 D6:4 Bb5:4 | G5:4 C#6:4 E6:4 | D6:12 | r:12
  | C5:4 F5:4 A5:4 | C6:8 B5:4 | Bb5:4 G5:4 E5:4 | G5:12 | E5:4 G5:4 Bb5:4 | D6:8 C6:4 | A5:4 G5:4 F5:4 | C6:12
  | D6:4 Bb5:4 F5:4 | Db6:8 C6:4 | A5:4 C6:4 F6:4 | F#6:4 D6:4 C6:4 | Bb5:8 G5:4 | E5:4 G5:4 C#6:4 | D6:12 | r:12 |
mel harp: | r:12 *15 | D5:1 F5 A5 D6 F6 A6 D7:6 | r:12 *15 | D5:1 F5 A5 D6 F6 A6 D7:6 |

song army=alien piece=knight bpm=152 bars=20 gain=1.08 title=Countdown_to_Liftoff
chords: Dsus4 | D | D | A7 | D | D | C | D | Bb | C | D | A7 | G | D | E7 | A | Bb | C | A7 | D
drum timp bars=3-4: xoxoxoxoxoxoxoxo
drum snare bars=4: ooooooooxxxxXXXX
drum crash bars=5,13: x...............
drum kick bars=5-20: x...x...x...x...
drum snare bars=5-20: ....x.......x...
drum hat bars=5-20: x.x.x.x.x.x.x.x.
pat saucer lo=A3 gate=1 bars=1-4: c - - - - - - - - - - - - - - -
pat spicc lo=D3 vol=0.6 bars=5-20: 1 1 5 . 1 1 5 . 1 1 5 . 1 1 5 .
pat upright lo=D2 bars=5-20: 1 . . 1 . . 1 . 1 . . 1 . . 5 .
pat blip lo=D6 vol=0.4 bars=13-20: 1 . 5 . 8 . 5 . 1 . 5 . 8 . 5 .
mel blip bars=1-2: D6:2 r:2 C#6:2 r B5 r A5 r | G5:2 r F#5 r E5 r D5 r |
mel rocket bars=3-4: D2:16 | _:16 |
mel trumpet: | r:16 *4 | D5:2 D5 D5 A4 D5:4 F#5:4 | A5:6 F#5:2 A5:8 | G5:2 G5 G5 E5 G5:4 C6:4 | A5:12 r:4
  | F5:2 F5 F5 D5 F5:4 Bb5:4 | C6:6 G5:2 E6:8 | D6:4 C#6:2 B5 A5:4 F#5:4 | E5:6 G5:2 A5:8
  | B5:2 B5 B5 G5 B5:4 D6:4 | F#6:6 E6:2 D6:8 | E5:2 G#5 B5 D6 E6:8 | C#6:12 r:4
  | Bb5:4 D6:4 F6:8 | G6:4 E6:4 C6:8 | E6:6 C#6:2 A5:8 | D6:12 r:4 |
mel horn tr=-12 vol=0.5: same

# ---------------------------------------------------------------- the bishop: a brain in a jar - hypnotic, telepathic, floating
song army=alien piece=bishop bpm=66 beat=6 bar=12 bars=16 echo=3 gain=1.16 title=The_Third_Eye
chords: Em | Fmaj7 | Em | Fmaj7 | Am | G | Fmaj7 | B7 | Em | Fmaj7 | Em | Fmaj7 | Am | Caug | F | B7
drum bongo vol=0.7: x..o.ox..o.o
drum conga: ......x.....
drum shaker vol=0.7: .o.o.o.o.o.o
pat saucer lo=E3 gate=1: c - - - - - - - - - - -
pat vibes lo=E4 vol=0.45: 1 5 8 10 8 5 1 5 8 10 8 5
pat upright lo=E2: 1 . . . . . 5, . . . . .
pat harp lo=E3 vol=0.35 bars=9-16: 1 . 5 . 8 . 10 . 8 . 5 .
mel ondes: | B5:6 G5:4 E5:2 | F5:6 A5:4 C6:2 | B5:4 C6:2 B5:4 G5:2 | A5:12
  | C6:6 E6:4 A5:2 | B5:6 D6:4 G5:2 | A5:4 C6:2 E6:4 F6:2 | D#6:12
  | E6:6 B5:4 G5:2 | F5:6 C6:4 A5:2 | G5:4 F#5:2 E5:4 B4:2 | C5:12
  | E5:4 A5:2 C6:4 E6:2 | E6:6 C6:4 G#5:2 | A5:4 C6:2 F6:4 E6:2 | D#6:6 B5:6 |

song army=alien piece=bishop bpm=100 bars=16 echo=3 gain=1.31 title=Telepathy
chords: Ebmaj7 | Emaj7 | Ebmaj7 | Emaj7 | Abmaj7 | Gm7 | Fm9 | Bb7 | Ebmaj7 | Emaj7 | Ebmaj7 | Cm9 | Fm9 | Bb7 | Ebmaj7 | Ebmaj7
drum bongo vol=0.6: x.ox..o.x.ox..o.
drum conga vol=0.8: ......x.......xx
drum shaker vol=0.6: xoooxoooxoooxooo
pat upright lo=Eb2: 1 . . . . . 5, . 1 . . . . . 5, .
pat choir lo=G3 gate=1 vol=0.3: c - - - - - - - - - - - - - - -
pat harp lo=G4 vol=0.35 bars=9-16: . . 1 . 3 . 5 . 7 . . . . . . .
mel vibes del=0.2 vol=1.3: | G5:4 Bb5:2 D6:6 C6:4 | G#5:4 B5:2 D#6:6 C#6:4 | Bb5:4 G5:2 Eb5:6 F5:4 | G#5:16
  | C6:4 Eb6:2 G6:6 F6:4 | F6:4 D6:2 Bb5:6 G5:4 | Ab5:4 C6:2 Eb6:6 G6:4 | F6:6 D6:2 Ab5:8
  | G5:4 Bb5:2 D6:6 C6:4 | G#5:4 B5:2 D#6:6 C#6:4 | Bb5:4 G5:2 Eb5:6 F5:4 | G5:4 Bb5:2 D6:6 Eb6:4
  | C6:4 Ab5:2 F5:6 G5:4 | Ab5:4 F5:2 D5:6 Bb4:4 | Eb5:16 | r:16 |
mel whistle vol=0.5: | r:16 *3 | r:6 D#7:1 B6 D#7 B6 r:2 F#7:2 r:2 | r:16 *3 | r:6 D7:1 Bb6 D7 Bb6 r:2 F7:2 r:2 | r:16 *7 | r:6 Bb6:1 G6 Bb6 G6 r:2 D7:2 r:2 |

song army=alien piece=bishop bpm=138 bar=20 bars=16 gain=1.12 title=Mind_over_Matter
chords: Caug | Daug | Caug | Daug | Bbaug | Caug | F#7 | C7 | Fm | Db7 | Fm | Db7 | Bbm | Gb7 | C7 | Caug
drum ride: x...x.x.x...x.x.x...
drum snap: ............x.......
drum kick vol=0.6: x.....x.......x.....
pat upright lo=C2: 1 . . . . . 5 . . . . . 8 . . . 5 . . .
pat organ lo=E4 gate=0.6 vol=0.45: c . . . . . c . . . . . c . . . c . . .
pat celesta lo=C5 vol=0.3: . . . . . . . . . . . . 1 3 5 8 10 12 15 .
pat saucer lo=F3 gate=1 bars=9-16: c - - - - - - - - - - - - - - - - - - -
mel theremin: | C5:6 E5:6 G#5:8 | A#5:6 F#5:6 D6:8 | E6:6 C6:6 G#5:8 | F#5:12 D5:8
  | D5:6 F#5:6 A#5:8 | C6:6 E6:6 G#6:8 | F#6:6 E6:6 C#6:8 | E6:6 Bb5:6 G5:8
  | Ab5:6 C6:6 F6:8 | F6:6 B5:6 Ab5:8 | G5:6 Ab5:6 C6:8 | Db6:12 B5:8
  | Bb5:6 Db6:6 F6:8 | E6:6 Db6:6 Bb5:8 | G5:6 Bb5:6 E6:8 | C6:20 |
mel ringmod bars=8,16: r:12 C6:4 F#6:4

# ---------------------------------------------------------------- the rook: a tripod war machine - three legs, a horn and a heat ray
song army=alien piece=rook bpm=112 beat=4 bar=12 bars=24 gain=1.13 title=Three-Legged_March
chords: Cm | Cm | Ab | G | Cm | Cm | Fm | G | Cm | Cm | Ab | G | Ab | Ab | Fm | G | Ab | Ab | Db | G | Cm | Fm | G7 | Cm
drum taiko: X...x...x...
drum kick: x...........
drum clank: ......x...x.
drum crash bars=1,13: x...........
pat tuba lo=G1: 1 - . . 5 - . . 1 - . .
pat spicc lo=C3 vol=0.55 bars=13-24: 1 . 1 . 5 . 1 . 8 . 5 .
mel brass: | C5:4 C5 Eb5 | G5:8 F5:2 Eb5:2 | Eb5:4 C5:4 Ab4:4 | B4:8 D5:4 | C5:4 C5 Eb5 | G5:8 Ab5:2 G5:2 | F5:4 Ab5:4 C6:4 | B5:12
  | C6:4 G5 Eb5 | C6:8 D6:2 Eb6:2 | C6:4 Ab5:4 Eb5:4 | D5:8 B4:4
  | Eb5:4 Ab5 C6 | Eb6:8 D6:2 C6:2 | Ab5:4 F5 C5 | D5:8 G5:4 | C6:4 Ab5 Eb5 | C6:8 Db6:2 C6:2 | Ab5:4 F5:4 Db5:4 | B4:4 D5:4 G5:4
  | G5:4 Eb5 C5 | Ab5:4 C6:4 F6:4 | F6:4 D6:4 B5:4 | C6:12 |
mel horn tr=-12 vol=0.5: same
mel tripod: | r:12 *7 | G2+D3:8 r:4 | r:12 *7 | G2+D3:8 r:4 | r:12 *7 | C3+G3:8 r:4 |

song army=alien piece=rook bpm=132 bars=16 gain=1.02 title=Heat_Ray
chords: Fm | Fm | Db | C | Fm | Fm | Bbm | C | Db | Eb | Fm | Fm | Db | Bbm | Db:8 C:8 | C7
drum kick: x...x...x...x...
drum snare: ....x.......x..x
drum hat: x.x.x.x.x.x.x.x.
drum taiko bars=1,5,9,13: x.......x.......
pat analog lo=F1 del=0: 1 1 3 1 5 1 3 4 1 1 3 1 5 1 3 4
pat ringmod lo=F4 vol=0.35: . . . . c . . . . . . . c . . .
pat brass lo=F4 gate=0.3 vol=0.45: . . . c . . c . . . . . . . . .
pat saucer lo=F3 gate=1 bars=9-16: c - - - - - - - - - - - - - - -
mel theremin: | F5:4 Ab5:4 C6:8 | Db6:6 C6:2 Ab5:8 | F5:4 Ab5:4 Db6:8 | E6:6 Db6:2 C6:8
  | F6:8 Eb6:4 C6:4 | Ab5:16 | Bb5:4 Db6:4 F6:8 | E6:12 r:4
  | F6:6 Eb6:2 Db6:8 | G5:4 Bb5:4 Eb6:8 | C6:6 Ab5:2 F5:8 | G5:6 Ab5:2 C6:8
  | Ab5:4 Db6:4 F6:8 | F6:6 Db6:2 Bb5:8 | Ab5:8 G5:8 | E6:12 r:4 |
mel bleep bars=8,16: r:12 C7:1 C7 C7 C7

song army=alien piece=rook bpm=116 bars=16 gain=0.99 title=Invasion_Stomp
chords: Em | Em | C | B | Em | Em | Am | B7 | C | D | Em | Em | Am | B7 | C:8 B7:8 | Em
drum kick: x.x.....x.x.....
drum clap: ....x.......x...
drum hat: x.x.x.x.x.x.x.x.
drum crash bars=1,9: x...............
drum tamb bars=9-16: ..x...x...x...x.
pat timp lo=E2: 1 . . . . . . . 1 . . . . . . .
pat tuba lo=E1: 1 . . . 1 . . . 1 . . . 5, . 1 .
pat guitar lo=E2 vol=0.45 gate=0.5: p . p . p . . p . p p . p . . .
mel brass: | E5:4 E5:2 G5:2 B5:4 E5:4 | D5:2 E5 G5 A5 B5:8 | C6:4 B5:2 A5:2 G5:4 E5:4 | F#5:12 D#5:4
  | E5:4 E5:2 G5:2 B5:4 E6:4 | D6:2 B5 A5 G5 E5:8 | A5:4 C6:4 E6:4 D6:2 C6:2 | B5:12 r:4
  | G5:2 G5 C6:4 E6:4 C6:4 | F#5:2 F#5 A5:4 D6:4 A5:4 | G5:2 A5 B5 D6 E6:8 | E6:4 D6:2 B5:2 G5:8
  | C6:4 A5:2 C6:2 E6:8 | D#6:4 B5:4 F#5:8 | E5:4 G5:4 F#5:4 D#5:4 | E5:12 r:4 |
mel horn tr=-12 vol=0.5: same
mel warble bars=8,16: r:12 B5:4

# ---------------------------------------------------------------- the queen: the alien queen - slinky, glamorous, dramatic
song army=alien piece=queen bpm=118 bars=16 gain=1.06 title=Spade-Tail_Tango
chords: Dm | Dm | A7 | A7 | A7 | A7 | Dm | Dm | Gm | Gm | Dm | Dm | Bb7 | A7 | Dm | A7
drum kick vol=0.7: x...x...x...x...
drum snap vol=0.6: ....x.......x...
drum block vol=0.4 bars=9-16: x..x..x.x..x..x.
pat upright lo=D2 vol=0.85: 1 . . 5, 3 . 5 . 1 . . 5, 3 . 5 .
pat vibes lo=A3 gate=0.3 vol=0.5: c . . . c . . . c . . . c . c .
pat strings lo=A3 gate=1 vol=0.4 bars=9-16: c - - - - - - - - - - - - - - -
mel theremin: | A5:6 Bb5:2 A5:4 F5:4 | D6:8 E6:2 F6:2 E6:2 D6:2 | C#6:6 D6:2 C#6:4 A5:4 | E6:12 r:4
  | G5:6 A5:2 G5:4 E5:4 | Bb5:8 A5:2 G5:2 F5:2 E5:2 | F5:6 E5:2 D5:4 A4:4 | D5:12 r:4
  | D6:6 Eb6:2 D6:4 Bb5:4 | G6:8 F6:2 Eb6:2 D6:2 C6:2 | A5:6 Bb5:2 A5:4 F5:4 | D6:12 r:4
  | F6:6 D6:2 Bb5:4 Ab5:4 | G5:6 Bb5:2 C#6:4 E6:4 | F6:6 E6:2 D6:8 | A5:4 C#6:4 E6:4 G6:4 |
mel harp: | r:16 *7 | r:8 D5:1 F5 A5 D6 F6 A6 D7:2 | r:16 *7 | r:8 A4:1 C#5 E5 G5 A5 C#6 E6:2 |

song army=alien piece=queen bpm=132 bars=16 gain=1.11 title=Shell_Bodice_Bossa
chords: Fmaj7 | Gm7:8 C7:8 | Fmaj7 | Abmaj7 | Gm7 | C7 | Am7:8 D7:8 | Gm7:8 C7:8 | Fmaj7 | Bbmaj7 | Am7 | Dm7 | Gm7 | C7 | Fmaj7:8 Dbmaj7:8 | Gm7:8 C7:8
drum kick vol=0.5: x.....x.x.....x.
drum block vol=0.55: x..x..x...x..x..
drum shaker vol=0.6: xoooxoooxoooxooo
pat upright lo=E1: 1 . . . . . 5, . 1 . . . . . 5, .
pat vibes lo=A3 gate=0.6 vol=0.5: c . . c . . c . . . c . . c . .
pat pad lo=A3 gate=1 vol=0.6 bars=9-16: c - - - - - - - c - - - - - - -
pat harp lo=F4 vol=0.3 bars=9-16: 1 . 5 . 8 . 10 . 12 . 10 . 8 . 5 .
mel theremin: | C6:12 A5:4 | Bb5:8 E5:8 | A5:6 G5:2 F5:8 | Eb6:12 C6:4
  | D6:6 C6:2 Bb5:8 | E6:6 D6:2 Bb5:4 G5:4 | E6:6 C6:2 F#5:8 | G5:6 Bb5:2 E6:8
  | F6:6 E6:2 C6:8 | D6:6 C6:2 A5:8 | G5:6 A5:2 C6:8 | F6:12 r:4
  | F6:6 D6:2 Bb5:8 | G5:6 Bb5:2 E6:8 | A5:8 Ab5:8 | G5:8 Bb5:4 C6:4 |

song army=alien piece=queen bpm=150 beat=4 bar=12 bars=32 gain=1.37 title=Royal_Hive_Waltz
chords: Gm | Gm | Cm | Cm | D7 | D7 | Gm | Gm | Eb | Eb | Cm | Cm | Am7b5 | D7 | Gm | D7
  | Bb | Bb | F7 | F7 | Bb | Bb | Gm | Gm | Eb | Ebm | Bb | G7 | Cm | D7 | Gm | Gm
drum brush: x...o...o...
drum ride bars=17-32: x...x.x.x...
pat upright lo=G1: 1 . . . . . . . . . . . | 5, . . . . . . . . . . .
pat vibes lo=Bb3 gate=0.5 vol=0.5: . . . . c . . . c . . .
pat strings lo=D4 gate=1 vol=0.4 bars=17-32: c - - - - - - - - - - -
mel ondes: | D5:4 G5:4 Bb5:4 | D6:8 C6:2 Bb5:2 | Eb6:8 D6:2 C6:2 | G5:12 | F#5:4 A5:4 C6:4 | Eb6:8 D6:4 | Bb5:4 A5:4 G5:4 | D5:12
  | G5:4 Bb5:4 Eb6:4 | G6:8 F6:2 Eb6:2 | C6:4 Eb6:4 G6:4 | G6:8 F6:2 Eb6:2 | Eb6:4 C6:4 A5:4 | F#5:4 A5:4 D6:4 | G6:12 | F#6:4 D6:4 A5:4
  | D5:4 F5:4 Bb5:4 | D6:8 C6:2 Bb5:2 | Eb6:8 C6:4 | A5:12 | Bb5:4 D6:4 F6:4 | F6:8 Eb6:2 D6:2 | D6:4 Bb5:4 G5:4 | D6:12
  | Bb5:4 Eb6:4 G6:4 | Gb6:8 F6:2 Eb6:2 | D6:8 Bb5:4 | B5:4 D6:4 F6:4 | Eb6:8 D6:2 C6:2 | A5:4 C6:4 F#6:4 | G6:12 | r:12 |
mel harp: | r:12 *15 | A4:1 C5 D5 F#5 A5 C6 D6 F#6 A6:4 | r:12 *15 | G4:1 Bb4 D5 G5 Bb5 D6 G6 Bb6 D7:4 |

# ---------------------------------------------------------------- the king: the overlord - pompous, swaggering, ridiculous
song army=alien piece=king bpm=96 bars=16 gain=0.92 title=Hail_the_Overlord
chords: Bb | Eb | Bb | F | Bb | Gb | Eb:8 F:8 | Bb | Eb | Bb | Gb | Db | Eb | Bb | F7 | Bb
drum snare: X...x.o.X...x.oo
drum kick: x.......x.......
drum crash bars=1,5,9,13: x...............
pat timp lo=F2: 1 . . . . . . . 1 . . . 5, . 1 .
pat tuba lo=Bb1: 1 . . . 5, . . . 1 . . . 5, . . .
pat church lo=F3 gate=1 vol=0.45: c - - - - - - - c - - - - - - -
pat choir lo=Bb3 gate=1 vol=0.5 bars=9-16: c - - - - - - - c - - - - - - -
mel trumpet: | F4:4 Bb4:6 C5:2 D5:4 | Eb5:6 D5:2 C5:4 Bb4:4 | D5:4 F5:6 Eb5:2 D5:4 | C5:12 r:4
  | F5:4 Bb5:6 A5:2 Bb5:4 | Bb5:6 Db6:2 Bb5:4 Gb5:4 | G5:4 Bb5:4 A5:4 C6:4 | Bb5:12 r:4
  | G5:4 Bb5:4 Eb6:8 | D6:6 C6:2 Bb5:8 | Db6:4 Bb5:4 Gb5:8 | F5:6 Ab5:2 Db6:8
  | G5:4 Bb5:4 Eb6:8 | D6:6 F6:2 D6:4 Bb5:4 | C6:6 A5:2 Eb6:8 | D6:12 r:4 |
mel horn tr=-12 vol=0.55: same
mel theremin bars=9-16 vol=0.6: | G6:16 | F6:16 | Gb6:16 | F6:16 | G6:16 | F6:16 | Eb6:16 | F6:12 r:4 |

song army=alien piece=king bpm=100 bars=16 gain=0.97 title=Mothership_Arrival
chords: C | Ab | Bb | C | C | Ab | Bb | G | F | C | Ab | Eb | F | Fm | G | C
drum crash bars=1,5,9,13: x...............
drum kick bars=5-16: x.......x.......
drum snare bars=5-16: ....x.......x...
mel tom bars=8,16: r:8 G3:2 E3 C3 G2
pat timp lo=F2: 1 . . . . . . . 1 . . 1 5, . 1 1
pat tuba lo=E1: 1 - - - - - - - 5, - - - 1 - - -
pat saucer lo=G3 gate=1: c - - - - - - - - - - - - - - -
pat blip lo=C6 vol=0.45 bars=1-8: 1 . . . 5 . . . 8 . . . 5 . . .
pat choir lo=G3 gate=1 vol=0.55 bars=9-16: c - - - - - - - - - - - - - - -
mel ringmod bars=1-4: C3:16 | Ab2:16 | Bb2:16 | C3:16 |
mel trumpet: | C5:4 G5:12 | C6:8 Eb6:8 | D6:12 F6:4 | E6:16
  | C5:3 C5:1 G5:4 C6:4 E6:4 | Eb6:8 C6:4 Ab5:4 | F6:6 D6:2 Bb5:8 | B5:8 D6:8
  | C6:6 A5:2 F5:8 | G5:6 C6:2 E6:8 | Eb6:6 C6:2 Ab5:8 | G5:6 Bb5:2 Eb6:8
  | F6:6 C6:2 A5:8 | Ab5:6 C6:2 F6:8 | G5:4 B5:4 D6:4 F6:4 | E6:12 r:4 |
mel horn tr=-12 vol=0.55: same

song army=alien piece=king bpm=176 beat=2 bar=8 swing=0.33 bars=32 gain=1.12 title=Big_Red_Button_Swing
chords: Bb6 | G7 | Cm7 | F7 | Bb6 | G7 | Cm7:4 F7:4 | Bb6 | Bb6 | G7 | Cm7 | F7 | Bb6 | G7 | Cm7:4 F7:4 | Bb6
  | D7 | D7 | G7 | G7 | C7 | C7 | F7 | F7 | Bb6 | G7 | Cm7 | F7 | Bb6 | G7 | Cm7:4 F7:4 | Bb6
drum ride: x.xxx.xx
drum hat: ..x...x.
drum kick vol=0.4: x...x...
drum snare vol=0.35: ......o.
drum crash bars=1,17,25: x.......
pat upright lo=F1: 1 . 3 . 5 . 6 . | 8 . 7 . 5 . 3 .
pat vibes lo=D4 gate=0.4 vol=0.4: c . . c . . . .
mel trumpet: | D5:1 F5 G5 Bb5:3 G5:2 | B5:2 D6:1 F6 r:4 | Eb5:1 G5 Bb5 C6:3 Bb5:2 | A5:2 C6:1 Eb6 r:4
  | D6:1 C6 Bb5 G5:2 F5:3 | F5:1 G5 B5 D6:3 B5:2 | C6:2 Bb5 A5 F5 | Bb5:6 r:2
  | D5:1 F5 G5 Bb5:3 G5:2 | B5:2 D6:1 F6 r:4 | Eb5:1 G5 Bb5 C6:3 Bb5:2 | A5:2 C6:1 Eb6 r:4
  | D6:1 C6 Bb5 G5:2 F5:3 | F5:1 G5 B5 D6:3 B5:2 | C6:2 Bb5 A5 F5 | Bb5:6 r:2
  | F#5:3 A5:1 D6:4 | C6:2 A5 F#5 D5 | B5:3 D6:1 F6:4 | F6:2 D6 B5 G5 | E5:3 G5:1 C6:4 | Bb5:2 G5 E5 C5 | A5:3 C6:1 Eb6:4 | Eb6:2 C6 A5:1 G5 F5:2
  | D5:1 F5 G5 Bb5:3 G5:2 | B5:2 D6:1 F6 r:4 | Eb5:1 G5 Bb5 C6:3 Bb5:2 | A5:2 C6:1 Eb6 r:4
  | D6:1 C6 Bb5 G5:2 F5:3 | F5:1 G5 B5 D6:3 B5:2 | C6:2 Bb5 A5 F5 | Bb5:6 r:2 |
mel rocksax tr=-12 vol=0.6: same
mel bleep bars=8,16,32: r:6 F6:1 Bb5:1
"""
