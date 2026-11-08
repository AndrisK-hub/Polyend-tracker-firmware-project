# Changelog

All notable changes to the **Polyend Tracker Community Edition (CE)** firmware across **Original Tracker** (`MK66`), **Tracker+** and **Tracker Mini** (`IMX`) are documented in this file.

---

## [Unreleased]

### Added

1. **Longer patterns: up to 512 steps (Tracker Mini only)**
   * A pattern can now be up to 512 steps long (was 128). Original Tracker and Tracker+ stay at 128 steps.
   * The length can be set with the encoder, with the length jump values (which now continue 192, 256, 320, 384, 512) or with the pads (pads still reach 191 steps).
   * Pattern files (`.mtp`) use a new format (**Version 7**) with 512 steps for each of the 16 tracks, so a saved pattern is about 64 KB instead of 16 KB.
   * **Compatibility:** existing projects (stock, CE and earlier builds of this firmware) open normally; their patterns are expanded to 512 steps in memory. Once you save, the pattern files are Version 7 and **cannot be read by stock/CE firmware**. Back up your `Projects/` folder first.
   * About 2 MB of the sample pool (`SAMPLE_MEMORY_SIZE`) is used for the bigger pattern buffers. Samples of 256 KB and more are streamed from the SD card, so this is not noticeable in normal use.
   * The maximum is a single setting (`PATTERN_MAX_STEPS`, in `core/source/mtStructs.h`); 128, 512 and 1024 have their own file versions.
   * Known limits: the custom performance play orders still cover only the first 128 steps, and in hexadecimal numbering mode the horizontal pattern views show only the last two digits of the step number.

2. **Note length when recording and when entering notes (All 3 Devices)**
   * New `Config > General > Rec. note length` option (`On` / `Off`, default `On`).
   * `On`: while the sequencer is playing, live recording (pads and MIDI) stores how long each note was held, in any `Recording options` mode. A note held for less than a step gets a `Gate length` FX on its step. A note held for a step or longer gets an `OFF` step where it was released, with `Micro-move` for the position inside that step.
   * `On` also applies when you enter notes in edit mode (red cursor) with the pads, the Mini middle button or a MIDI keyboard, with the sequencer stopped or running. There is no sequencer clock to count, so the time the key is held is measured with the real clock and converted with the project tempo (a step lasts as long as the tempo says). The result is written the same way. Entering a note again on the same step replaces the `Gate length` of the earlier take.
   * `Off`: the original behavior. Only the note is stored and, when recording live, an `OFF` is placed in the step following it.
   * During the pre-roll and in Performance mode the original behavior is always used.

3. **Pass conditions for the `Chance` FX: `A/B`, `1ST`, `!1S` (All 3 Devices)**
   * Three more `Chance` (`C`) modes after the existing ones. They count how many times the track has played its pattern (a pass), like the existing `Play N / Skip M` cycles, and are restarted by the same events (play start, pattern change).
     * **Pass A of B (`C 1/2`–`C 8/8`)**: Plays only on pass `A` of every `B` passes (`B` in `2..8`, `A` in `1..B`). `C 2/4` plays on the 2nd, 6th, 10th... pass.
     * **First pass (`C 1ST`)**: Plays only on the first pass after the sequencer starts or the pattern changes.
     * **Not first pass (`C !1S`)**: Plays on every pass except the first.
   * Like the other conditions, they set the "last" state used by `C GL` and `C !L`.
   * They follow `Skip N / Play M` in the value list, so `+1` / `-1` walk through them and `+10` / `-10` jump to the next group (next `B`).
   * No file format change: the modes are new values of the existing `Chance` FX value byte. A firmware that does not know them (stock/CE) plays such a step on every pass.

4. **Drum patterns for Fill: `Where?` > `Drums` (All 3 Devices)**
   * New entry **Drums** at the end of the `Where?` list of the Fill popup (after `Euclidean`), in the Note, Instrument, Micro-move, Volume and Fx fills. It chooses the steps of the selection by a drum pattern, the same way `Each`, `Random` and `Euclidean` choose them. Everything else in the popup (Scale, Fill Type, From / To) works as before, so the hits get the note or value you set there.
   * The `Density` slot becomes **Pattern**. Step through it with `+` / `-` (or the pads, the first pad is `Random`). The first entry is `Random`, then 65 patterns:
     * **24 beats** (names start with `B`): one entry writes a whole kit onto the selected tracks, in order. The first selected track gets the kick, the next one the snare/clap, then the closed hat, the open hat (and percussion for the beats that have it); tracks after the last lane are left alone. House, Techno, Boom bap, Lo-fi, Trap (half-time and double-time snare), Drill, UK Drill, 2-Step, Dembow, One Drop, Rockers, Steppers, Rock, Rock 2 and Half-time, and a synth-pop group: SynPop (sparse drum-machine groove), NewWave, Italo (four on the floor with an extra kick on the last offbeat, claps, open hat after the last beat), Disco (sixteenth hats that leave out the snare steps), Stomp (stomp, stomp, clap), Indust (industrial, with 3-3-3-3-4 metal hits and rim), Dark (half-time) and Pulse (eighth-note kick, sixteenth hats). These are original grooves written in those styles, not copies of any recording.
     * **41 single rhythms** (names start with `K` kick, `S` snare, `H` hat, `P` percussion): one rhythm that every selected track gets. Kicks, snares and claps, hats, and the clave and bell patterns (bossa nova, son, rumba, shiko, soukous, gahu, tresillo, cinquillo, habanera), plus Italo, Stomp and Pulse kicks, a snare turnaround, disco and tucked-in hats, and metal-hit, rim and pickup percussion.
   * **One instrument per lane (beats).** When a beat fills the `Note` column, each lane takes its own instrument from the instrument list: the first selected track gets the instrument you used last, the second track the next one in the list, and so on. A kit loaded into consecutive instrument slots (kick, snare, closed hat, open hat, ...) therefore plays right away. Choose the kick instrument in the editor first. Only empty steps get an instrument (steps that already have a note keep theirs), only sample tracks step through the list (a MIDI track keeps one instrument), the list stops at the last sample instrument, and single rhythms give every track the same instrument as before. Only as many lanes are written as tracks are selected.
   * **PERC drum synth.** When the last used instrument is a synth running the built-in `PERC` drum engine, a Drums fill of the `Note` column keeps every lane on that one instrument and writes the drum as the note: a beat uses kick 60, snare/clap 62, closed hat 64, open hat 66 and percussion 70 (lanes in the same order as above), a single rhythm the note of its letter (`K` kick, `S` snare, `H` closed hat, `H Offbeat` open hat, `P` percussion). These are the first notes of the PERC sections as marked by the pad lighting of the instrument editor. The note and scale chosen in the popup are not used for these hits. A step that already has a note on another instrument keeps that instrument and gets the popup's note, as before. If the hits cut each other off, raise the polyphony of the synth part.
   * The pattern starts at the first selected step and repeats over the selection. Only the hit steps are written; the other steps are left alone. Filling again with another pattern therefore adds to what is there, which is how patterns are combined.
   * `Random` picks a new pattern on every fill (one pattern for the whole selection).
   * No file format change.

5. **Games: NES emulator (Tracker Mini only, beta)**
   * New `Config` group **Games** (first in the list) with **Play game**. It lists the `.nes` files in the `Games/` folder of the SD card (the folder is created if it is missing). Select one and press `Play`.
   * **Games is also a box in the Menu grid** (hold `Menu`, the last free box, after Sample Recorder). Choosing it opens the Config screen straight on the list of games. It can also be assigned to an Fn key in `Config > Fn buttons binding` (the list has one more entry, `Games`; the default bindings are unchanged).
   * Controls: arrows = D-pad, `Fn1` = Select, `Fn2` = Start, `Fn3` = B, `Fn4` = A, middle key = Start. Several directions can be held at once (diagonals). **Hold `Menu`** to quit and return to Config.
   * The picture is shown 2x in the middle of the screen. While a game runs the sequencer is stopped and the rest of the interface is paused.
   * The emulator is new and was written for this firmware (6502 CPU with the undocumented opcodes, cycle-timed picture chip with sprite-0 and mid-frame scroll effects, NTSC and PAL timing, and the full sound chip: two pulse channels, triangle, noise and the sample channel). Boards (iNES mapper numbers): 0, 1, 2, 3, 4, 7, 9, 10, 11, 33, 34, 48, 66, 71, 79, 87, 118, 119, 140, 180, 206. These include NROM, MMC1, UxROM, CNROM, MMC3 (with its scanline IRQ), AxROM, MMC2/MMC4 and most of the common smaller boards. iNES and NES 2.0 headers are read.
   * Sound goes out through the Tracker's normal audio output (the usual output volume applies). The game is paced by the audio clock, so picture and sound stay together; PAL games run at 50 frames per second.
   * **This is a test build.** It has not been run on a real Tracker Mini yet, so picture, speed and sound are still to be confirmed. There is no saving of battery RAM, and boards that are not in the list above (for example MMC5, VRC2/4/6/7, FME-7, Namco 163) are refused with a message. A game that uses an unsupported board, or is larger than about 690 KB, shows the reason instead of starting.
   * **Sound test.** While a game runs, a short **tap on `Menu`** plays a test beep through the game's sound path and shows a small panel of four lines of hex numbers in the top left corner of the picture for about fifteen seconds. Line 1: `B` audio blocks asked for, `F` samples waiting, `A` stream state (3 = connected and on). Line 2: `C` samples the sound chip made for the last picture (about 2DE), `E` its peak level now, `D` milliseconds one frame took. Line 3: `M` loudest chip level since the game started, `W` writes the game made to the sound chip registers, `T` loudest sample sent to the mixer since the tap. Line 4: `P` the game's processor position, `J` 1 if the processor has stopped, `S` peak of a built-in test note played through the same chip code on the device before the game starts (about 14CB when the chip works). Line 5: `L` the longest gap in ms between two turns of the main loop since the tap, `K` how many of the last 32 pictures were left out to keep the sound going. If a game is silent, tap `Menu` during the game and send a photo of the panel together with whether you heard the beep. The sound level of the games was also raised by about 5 dB.
   * **Keeping the sound going on a slow frame.** A frame took about 16 ms on the device, nearly the whole 16.7 ms of an NTSC frame, so the sound buffer kept running empty: the sound was cut into short pieces with gaps (in a model of the timing, 17 to 36 % of the time silent). The game now keeps about 46 ms of sound buffered (was 25 ms). When the buffer drops below about 23 ms, the next frame is run without drawing its picture (about 60 % of the time of a drawn frame; at most 3 pictures in a row are left out), and if the buffer is still low a second frame follows straight away. Leaving out a picture changes nothing the game can see: sprite-0 hits, sprite overflow and the tile fetches the cartridge boards count all still happen (checked on 47 test ROMs and games, identical results).
   * The game runs in the 768 KB scratch memory that is otherwise used for previews and the Sample Editor, so it uses no memory when you are not playing. Pattern and project data are not touched.

6. **Generative songs, drums, ambient, bass and lead for Fill: `Gen Song`, `Algo Song`, `Gen Drum`, `Gen Ambi`, `Gen Bass`, `Gen Lead` (Tracker Mini only)**
   * Six more entries at the end of the `Where?` list of the Fill popup, only in the **Note** fill. Each one writes a whole part that **plays through the selection like a small song** instead of repeating one pattern: a sparse intro, a build, the groove, a breakdown (for selections of 21 bars and more), the peak and an outro, in phrases of four bars (one bar is 16 steps). **The music never stops: every part plays through the whole selection, so a full 512-step pattern is filled from the first step to the last.** The breakdown thins the music out to the level of the intro (kick and hats, the first pad voice, the bass and a hushed lead) instead of falling silent. Every phrase ends with a fill, and a cymbal marks the start of a phrase where the energy rises. Short selections (up to 4 bars) are a steady groove; 5 to 20 bars build up to the peak without a breakdown.
   * **Style** (the `Density` slot): `Random` or one of 13 styles written for this firmware: SynthPop, NewWave, Italo, Disco, House, Techno, EBM, Electro, Breaks, HipHop, Trap, DnB and Ambient. The style sets the groove of the drums, the bassline and the chord progression of the bass and the pads. `Random` picks a style for each fill.
   * **Evolve** (the `Fill Type` slot, which has its own list here): `Gentle` (the groove changes every 8 bars, few extra hits, the chords change at half the speed), `Normal` (a new variation every phrase) and `Wild` (a new bar every time, more fills and surprises, the arrangement itself moves more).
   * **Tracks.** Like the drum beats, every selected track gets one voice, in order: **Gen Drum** the kick, snare, closed hat, open hat, percussion and cymbal; **Gen Ambi** up to four pad voices (a chord root, its fifth, and two upper voices with sparse notes that come in as the energy rises); **Gen Bass** one bassline on the first track; **Gen Lead** one lead line on the first track. Tracks after the last voice are left alone.
   * **`Gen Song` and `Algo Song`** (the first two of the six) fill a whole song with one entry. The selected tracks get, in order: the **lead on instrument 1**, the **bassline on instrument 2**, then the drums: kick, snare, closed hat, open hat, percussion and cymbal, **on instruments 3 to 8** (the next instrument for each lane on sample tracks; if the last used instrument is a PERC drum synth, all drums play on it with the right drum as the note; on MIDI tracks the instrument last used is kept). Fewer tracks give fewer voices (one track: the lead; two: lead and bass), more tracks than eight keep the rest as they are. All voices share one song, so they fit together, and follow the chosen **Style**, **Scale**, key and **Evolve**. The `Register` is the register of the lead; the bass sits two octaves below it (72 and 48 by default); sample drums play at their own pitch.
     * **`Gen Song`** uses the written grooves, basslines, lead rhythms and chords of the classic songs. Its `Song` values are `Random`, `Last` and `Song 1`..`Song 99`.
     * **`Algo Song`** uses **no preset**: every groove, the bass and lead rhythms and the chords are made up from a random seed inside the rules of the style, and **`Evolve` sets how far it goes** (`Gentle` stays close to the style, `Wild` re-rolls most of it and may use every chord of the scale). Its `Song` values are `New Algo` (a new piece on every fill), `Last` and `Algo 1`..`Algo 99` (the same piece every time).
   * **Note and scale.** `Scale` and the key of the pad board keep the bass and the pads in tune (`Chromatic` is replaced by the style's own scale). The `From` slot is the **Note** of the sample drums (a PERC drum synth always gets the right drum as the note) and the **Register** of the bass and the pads (a very low value is raised to a playable one when you pick the part: 48 for the bass, 60 for the pads, 72 for the lead). `Gen Drum` has no scale.
   * **Song** (the last slot, where `To` was) decides which piece is written, and whether parts filled one after the other fit together. Its value is written under the label (`Random`, `New Algo`, `Last`, `Song 12`, `Algo 12`; earlier builds drew only the bar). For the single parts the values are, in the order of the encoder (the first pads jump to the first values):
     * **`Song 1`..`Song 99`** are the *classic* songs: the grooves, bass and lead rhythms and the chord progression of the style, with the variations, fills and arrangement of that song. A song number always gives the same piece. **Parts filled with the same style and the same song number fit together**: the drums, the bassline, the pads and the lead use one chord progression (the same chords in the same bars), the same energy arc, the same breakdown and the same phrase ends, and the lead and the pads move around the chord the bass is playing. The default is song 1.
     * **`Random`** picks one of the classic songs at random (a different one on every fill).
     * **`New Algo`** makes up a **new piece from the style on the spot** (a different one on every fill): instead of the written grooves it generates the kick, snare, hat, open hat and percussion patterns, the bass and lead rhythms and the chord progression (and how fast the chords change) from a random seed, inside the rules of the style. The core of the style stays so that it is still that style: the kick on the beat, the backbeat, the first main note of the bass and the first two of the lead, and a sparse style stays sparse. Everything else is thrown away or re-rolled, and the skeleton changes now and then during the piece (every 16, 8 or 4 bars) so that it does not loop one bar.
     * **`Algo 1`..`Algo 99`** are numbered algo songs: the same algo piece every time, so that you can come back to one you like and so that parts fit together.
     * **`Last`** repeats the song of the previous Gen fill, whatever it was (also a `Random` or `Algo` one). Fill the drums with `Algo`, then the bass and the lead with `Last`, and they belong to the drums. A style chosen in the popup still wins over the style of the last song (the seed stays).
     * **Evolve also sets how far an algo song goes**: `Gentle` re-rolls fewer steps, plays sparser, keeps to the chords of the style and holds them twice as long; `Normal` re-rolls more and adds the fourth and fifth chord; `Wild` re-rolls most of the grooves, plays busier and may use every chord of the scale.
     * For parts that must line up, use the same **Style**, **Evolve**, **Scale**, key and selection (same first step and length) and the same song (or `Last`). With the style on `Random`, a numbered song also picks one style, the same for every part, and `Last` repeats the style.
   * **Note lengths.** The bassline, the pads and the lead no longer play every note at the same length. The style decides the mix: staccato and tied bass notes, short and long lead notes (with the odd held note and a few phrase endings), long pad chords. A note shorter than a step is written as a `Gate length` fx, a longer one gets a note-off step where it ends (with a `Micro-move` when it ends between two steps), and a note that lasts until the next note needs neither. Where the step already has two other fx there is no room for a `Gate length`, and the note then simply lasts until the next one. `Gen Drum` is unchanged (drum hits are short).
   * The steps of the voice tracks inside the selection are **rewritten**: the notes that do not belong to the part are removed, volumes are set from the generated velocities, and the instruments follow the Drums rule (the instrument you used last, the next ones for the next lanes on sample tracks, one PERC synth for all lanes). Other fx of the steps are kept; a `Micro-move` and a `Gate length` that an earlier take left on the bass, pad and lead steps are removed (the note lengths are written again). Pad voices that drop out of the arrangement (into the breakdown, or when the energy falls) fade out with a note-off.
   * **The lead is a melody now** (`Gen Lead`, and the lead of `Gen Song` / `Algo Song`). It used to be a rhythm with notes picked for each hit, which sounded like beats. Now every 4-bar phrase has a **contour** (an arch, a climb, a fall or a wave) and a **hook** that comes back:
     * Bar 1 states the hook, **bar 2 plays it again** (same rhythm, same ups and downs, with the same notes when the chord stays) and then turns to a **half cadence** on the third or the fifth of the chord; bar 3 goes on; **bar 4 closes on the root of the chord with a held note** and a short rest. A phrase run into the next phrase (where the energy rises) ends in a rising or falling run of neighbouring notes instead.
     * **Strong beats (steps 1 and 9 of a bar) always rest on a chord tone** (root, third or fifth of the chord the bass is playing); between them the line **walks by step** (passing and neighbour notes, a note right before the next beat) instead of jumping from chord tone to chord tone. Jumps of a sixth or more are rare (about 3% of the moves), the same note twice in a row is about 11% (it was 24%).
     * `Evolve` decides how the hook returns: `Gentle` plays hook A A B B (the same hook for eight bars), `Normal` A A B A (the main hook comes back), `Wild` a new hook every other phrase and a new contour in every bar. An `Algo` song also takes more liberties with the hooks and turns some contours upside down.
     * The notes are mostly **legato** (a note sounds for most of the time to the next one, about 84% on average; Techno is the most detached, Trap, HipHop and Ambient hold longest), a main note now and then comes a sixteenth early (syncopation; more in `Wild`, never the first note of the hook). The intro and the breakdown play a plainer line (half of the notes between the beats rest, softer), the peak the full line.
     * A short selection (1 to 3 bars) still gives a hook and closes on the root at its last bar.
     * Check done on the generator (not on the device): about 4.3 million automatic checks (chord tones on strong beats, hook repeat 100% in the first half of bar 2 in `Normal` and `Gentle`, all 936 phrase ends on the root, all 1872 half cadences on the third or fifth, runs, range of about an octave per phrase, no empty breakdown bars apart from one in a thousand), and mutation checks on the new code (23 of 24 deliberate breakages are caught). It has **not** been listened to on a Tracker Mini yet; how melodic it sounds is a matter of taste, tell me what to change (the range of the lead is the `Register` setting, 72 by default).
   * Original Tracker and Tracker+ do not get these entries. No file format change.
   * Check done: the real fill code was run on simulated patterns (8,000+ fills of bass, pads and lead across all styles, Evolve modes, classic, Random, Algo and numbered algo songs, with empty and with messy patterns): every written note length plays back exactly as the generator asked, nothing outside the selection changes and fx on other tracks and steps are kept. It has **not** been listened to on a Tracker Mini yet.

7. **Clear the whole pattern: hold `Undo` (All 3 Devices)**
   * In the Pattern Editor, in edit mode, **hold the `Undo` button for a second** and a question appears: *Clear the whole pattern?* A short press still undoes the last change as before.
   * **Yes is selected from the start** (it has the frame). `Yes` / `No` are the last two buttons under the screen; the arrow keys move the frame between them and `Insert` (the middle key on the Mini) answers the selected one. Everything else is blocked until you answer.
   * Yes empties **every step of every track**: notes, instruments, volumes, fx and micro-moves. The length of the pattern and of the tracks, the swing and the name stay as they are.
   * The pattern is saved to the undo history first, so **`Undo` brings it back**.
   * No file format change.

8. **Select to the end of the pattern in one press: `Shift` + middle button (Tracker Mini)**
   * In the Pattern Editor, **`Shift` + the middle button** (the round one inside the cursor keys) extends the selection to the full length of the pattern, so there is no need to scroll down to the last step first. Each press takes the next stage:
     * with only the cursor: the whole current track;
     * with a part of some tracks selected (for example with `Shift` + `Left` / `Right` / `Down`): all steps of **those tracks**, whatever the cursor track is;
     * with whole tracks selected: all tracks.
   * Same selection as the existing `Shift` + `Up` on the first step (which selects the track, and then all tracks), from any step. `Shift` + `Left` / `Right` still widens the selection by whole tracks afterwards. Copy, cut, delete, Fill and the other selection functions work on it as on any other selection; a plain cursor key clears it.
   * The middle button does nothing else while `Shift` is held (no note entry, no step preview). Without `Shift` it works as before.
   * In preview mode (green cursor) the selection is only changed while the sequencer is stopped, like the other cursor keys.
   * No file format change.

9. **Generative drone for Fill: `Gen Drone` (Tracker Mini only)**
   * One more entry at the end of the `Where?` list of the Fill popup (Note fill): a **drone of long, held notes** that change slowly. It has four voices, one per selected track in order (the instrument you used last, like `Gen Ambi`): the **ground** (the key note), the **fifth**, a **colour tone** (a second, a third, a seventh... as the style likes it) an octave up, and a quiet high **shimmer**. Tracks after the fourth are left alone.
   * **Long notes of different lengths.** Every voice plays its own schedule of notes of 1 to 16 bars, so the voices change at different bars and the drone never changes all at once. The ground comes in at the first step, the other voices one after the other. The ground holds longest and the shimmer is the quickest; a note lets go a little early now and then (a breath before the next one) and the colour tone and the shimmer rest for a note here and there. A note never runs past the end of the selection. The notes are written like the lengths of `Gen Bass` and `Gen Lead`: a note-off step where a note ends, nothing where it lasts until the next one.
   * **Intensity.** Every note comes in at its own volume: a slow wave (a different length for each voice) makes the voices swell and fade, with a little chance on top. The ground is the loudest voice and the shimmer the softest; a loud style (EBM, Techno, Trap) is louder than a soft one (Ambient).
   * **Scale, key and Register.** Every note is snapped to the chosen `Scale` (and the key of the pad board), so the scale is what the drone is made of; `Chromatic` uses the scale of the style. The `Register` setting is where the ground sits (48 by default); the colour tone sits above the octave of the ground and the shimmer about two octaves above it.
   * **Style** (the `Density` slot) sets the character: how fast the notes change (`Ambient` the slowest, then `Trap`, the middle styles, and `Techno`, `EBM`, `Breaks` and `DnB` the quickest), which colour tones it uses (open seconds and fourths, thirds, sevenths, dark flat seconds and tritones, airy sixths and ninths, bright major sevenths), and how loud it is.
   * **Evolve** sets how much it moves: `Gentle` is a pedal on the key note with slow changes of volume and rare changes of note; `Normal` also moves the ground to other degrees of the style's progression for a few bars (the fourth and fifth too) and comes back; `Wild` changes faster, moves the ground often, swings the volume widely and rests more.
   * **Song.** The same `Song` setting as the other parts: a numbered song, `Algo` (a new piece on every fill) or `Last`. An algo drone also changes its pace from block to block. The key note stays the pedal, so a drone of the same style fits a `Gen Song` or the other parts.
   * Check done on the generator and the fill code (not on the device): about 19 million automatic checks (notes only on bar starts, in the scale and the range, never past the next note or the end of the selection, voices entering in order, lengths and intensity by Evolve, style and voice, rests, root drift per Evolve), the real fill code on simulated patterns (all voices, styles, Evolve modes, songs, with empty and messy patterns: every written length plays back exactly), and 20 deliberate breakages of the drone code that the checks catch. It has **not** been listened to on a Tracker Mini yet; the note lengths, the pace per style and the colour tones are a first guess, tell me what to change.
   * Original Tracker and Tracker+ do not get this entry. No file format change.

10. **Change the instrument of a whole track: `Shift` + `Inst` + track button (Tracker Mini only)**
   * In the Pattern Editor, hold **`Shift`** and **`Inst`** and press one of the **eight track buttons** under the screen (the ones that mute a track while `Shift` is held): every **note** of that track gets the instrument you have selected, the one shown in the `Inst` list (the instrument you used or picked last). Empty steps and note-off steps are left alone, and so are the volume, the FX and the notes of all other tracks.
   * Pick the instrument first: hold `Inst` and choose it with the encoder or `+1` / `-1` / `+10` / `-10`, then (still holding `Inst`) press `Shift` and then the track button. Holding `Shift` first and then `Inst` works too, with the instrument chosen earlier.
   * It needs **edit mode** (like paste or delete); otherwise a short message says so. The track buttons show the tracks of the group the cursor is in (sample tracks, or MIDI tracks), like the mute buttons.
   * The notes of the **visible length of that track** change (steps past the pattern length are left alone). It is **one `Undo` step**, and nothing is stored when every note already uses that instrument.
   * A **sample instrument cannot go on a MIDI track**; a short message says so and nothing is changed (synth and MIDI instruments can go on any track, sample instruments only on the sample tracks).
   * Without `Inst`, `Shift` + a track button mutes the track as before. Original Tracker and Tracker+ do not get this entry. No file format change.
   * Check done on the real function cut from the source (not on the device): every track with every instrument on simulated patterns against an independent model (only real notes inside the track length change, nothing else, one undo step, none when nothing changes, refusals leave the pattern untouched), and a check that deliberately broken versions of the function fail the tests.

### Fixed

1. **`Gate length` FX is no longer ignored on steps that also have `Micro-move`.** The gate length is now carried along with the step until the delayed note actually starts. Patterns that combine both on one step will now play their gate; before, the note kept ringing.

2. **Patterns of 512 steps were not saved completely (Tracker Mini).** A pattern file is about 64 KB, but a file is written in chunks of at most 32 KB and the pattern save made only one call, so the rest was never written and the interrupted write stayed open. The next file written (for example `project.mt` when creating a new project) could then be lost, which showed up as a freeze on *Create new project* and a hang on *Opening last project* at the next start. Pattern saves now write the whole file before they return (`saveMemoryToFileWhole`). Affects the builds with 512-step patterns only; **patterns saved by those builds may be incomplete**, so check them or restore them from a backup of `Projects/`.

3. **Games had no sound (Tracker Mini).** The sound stream of the games was a global object of its own source file, and the audio engine connected it to the mixer while the engine itself was being constructed at start-up. Which of the two is constructed first is not defined, and in this build the engine came first: the constructor of an audio stream then cleared the connection, the stream stayed switched off, and the audio engine never asked it for sound, so the picture ran but everything was silent (and the test beep of the `Menu` tap was silent too). The engine now creates the sound stream itself right before it connects it, so the order cannot go wrong. Checked on the host with the real audio stream and connection code: the old order gives a stream that is never asked for sound, the new one delivers its samples to the mixer input. The first device report after this fix showed the stream running (blocks counted, sound buffer in use) but still no sound heard, so the sound test panel was extended (see Games) to show where the sound is lost.

---

## [1.0.0] - Initial Community Edition Release

> **⚠️ Important :: Project Compatibility Notice:**
> To support dedicated per-step **Micro-move (`m`)** and **Volume (`V`)** columns alongside **`Fx1`** and **`Fx2`**, pattern files (`.mtp`) have been updated to a new format (**Version 6**).
> 
> - **Loading old projects works automatically**: Existing projects created on official/older firmwares load seamlessly and are upgraded in memory.
>   
> - **Saving updates the project to Version 6 (not backward-compatible with older firmware)**: Once you save a project on Community Edition firmware, its saved `.mtp` pattern files cannot be read by older firmwares. **Please make a backup of your `Projects/` folder on your SD card or use `Save As` to save under a new project name, if you ever plan to roll back to an older firmware.**

### Added

1. **Open Synth SDK & New `CHIP` + `DIRT` Synthesizer Engines (Tracker+ & Tracker Mini)**
   * Added a new Open Synth SDK allowing the community to add new synthesizer engines. `CHIP` is the first implementation of this open architecture.
   * Added **`DIRT`** (wave-shaping/distortion synth).
   * See [`docs/custom_synth_sdk.md`](docs/custom_synth_sdk.md) to learn more about developing your own engines.

2. **Modular Sample Editor Effect & Generator Plugins (All 3 Devices)**
   * Added hot-swappable DSP plugins loaded from `/Effects/*.ptfx` on the SD card. 
   * Supports up to **12 parameters** (across 3 pages) and up to **128 installed plugins**.
   * Added the complete `.ptfx` SDK, `ptfx_builder.py`, and 6 reference effects/generators (`Add Silence`, `Analog Kick Gen`, `Chiptune Voice Gen`, `Multi-Mode Filter`, `Stereo Delay`, `Stereo to Mono`) in [`editor-fx-sdk/`](editor-fx-sdk/).
   * See [`docs/modular_effects_system.md`](docs/modular_effects_system.md) to learn more about developing your own plugins.

3. **Wavetable Envelopes (`WT Env`), Wavetable LFOs (`WT LFO`) & Step FX (`w` / `y`) (All 3 Devices)**
   * Added `WT Env` and `WT LFO` modulation types across all 7 instrument parameters (`Volume`, `Panning`, `Cutoff`, `Wavetable Position`, `Granular Position`, `Granular Length`, `Finetune`) with **128 Factory (`F 001`–`F 128`)** and **128 User (`U 001`–`U 128`)** morphable contours and dual-axis `Warp X/Y` shaping.
   * Load your own wavetables to overwrite the 128 User shapes, by uploading them to `/Envelopes/*.wav`
   * Added `w` (WT Volume Env) and `y` (WT Filter Env) pattern step FX for per-step envelope duration/warp/ADSR shaping.
   * See [`docs/wavetable_envelopes.md`](docs/wavetable_envelopes.md).

4. **Custom Color Themes & Font Packages (All 3 Devices)**
   * You can now create your own themes and load them from `/Themes/*.json` on your SD Card (up to 29 custom themes alongside the 3 built-in themes) and custom antialiased font packages from `/Fonts/*.ptfont` (up to 30 custom fonts) selectable in `Config > General > Theme / Font`.
   * Create your own with the [Web Theme & Font Editor](https://polyend.github.io/tracker-theme-editor/) ([`polyend/tracker-theme-editor`](https://github.com/polyend/tracker-theme-editor)). For more information see: [`docs/custom_themes_and_fonts.md`](docs/custom_themes_and_fonts.md).

5. **Performance Mode Enhancement (All 3 Devices)**
   * Added page toggling between Sample Tracks and MIDI/Synth Tracks in Performance Mode. Just keep pressing `[Perform]` to toggle between the views.
   * Oh, for OG Tracker users, there's a small surprise that goes along well with this change.

6. **Hybrid SD Card Sample Streaming (All 3 Devices)**
   * Long samples  (`256 KB` and larger) now keep only their initial attack in sample memory while streaming the rest of the audio directly from the SD card in the background. This lets you load a massive amount of stems, loops, and recordings into a single project while using only a fraction of your sample RAM.
     
   * **What Streams on Each Device**:
     * **Tracker+ & Tracker Mini**: Streams **`One-Shot`**, **`Forward Loop`**, **`Backward Loop`**, **`Ping-Pong Loop`**, **`Slice`**, and **`Beat Slice`** instruments (`Wavetable` and `Granular` instruments remain fully in RAM).
       
     * **Original Tracker**: Streams **`One-Shot`**, **`Forward Loop`**, **`Backward Loop`**, and **`Ping-Pong Loop`** instruments, while keeping **`Slice`** and **`Beat Slice`** instruments (alongside `Wavetable` and `Granular`) fully in RAM to keep performance solid across multiple tracks.
       
   * **Works Transparently with the Sample Editor**: You can still edit streamed samples in the Sample Editor or switch play modes at any time. The Tracker automatically loads the full sample into RAM while you edit it and returns to streaming once your edits are applied. This excludes very long sample however, due to RAM restrictions. If you plan to stream multi-minutes samples, please prepare them outside of the device.
   * See [`docs/developer_guide.md`](docs/developer_guide.md#3-copy-on-write-workspaceinstruments-incremental-project-saver--hybrid-sd-streaming) for technical details.

7. **`Slice` & `Beat Slice` Instruments can now `Play Thru` (All 3 Devices)**
   * Added a per-instrument **`Play Thru`** (`Off` / `On`) setting on the **`More`** page of the **Sample Playback** screen for both **`Slice`** and **`Beat Slice`** modes.
   * When **`Play Thru`** is **`On`**, triggering any slice starts at that slice marker and continues playing through subsequent slices until the note is released (`Note Off` / `CUT` / `FAD`) or the end of the sample is reached.

8. **Dedicated Micro-move (`m`) & Volume (`V`) Step View (All 3 Devices)**
   * Each pattern step now stores dedicated **Micro-move (`m`)** (`0..100`) and **Volume / Velocity (`V`)** (`0..100`) values in addition to its two custom step FX slots (**`Fx1`** and **`Fx2`**), freeing both FX slots for creative use.
   * Pressing **`[Note]`** in Pattern View while the cursor is already on the **`Note`** column toggles the two effect columns between the standard **`[Fx1 | Fx2]`** view and the new **`[m | V]`** view.
   * Unquantized live recording (**Record Micro-move**) and velocity recording (**Record Velocity**) now write directly into the dedicated **`m`** and **`V`** step fields instead of occupying **`Fx1`** and **`Fx2`**.
   * Full support in **`[m | V]`** view for Jog-wheel / `±` editing, Pad entry, `Delete`, `Copy` / `Paste`, `Invert`, `Undo` / `Redo`, and the **`Fill`** tool (`Constant`, `From-To`, `Random`, `Euclidean`).

9. **Granular Length Automation & Step FX (`z`) (All 3 Devices)**
   * Added **`Granular Length`** as a modulatable parameter in **Instrument Automation**, allowing you to animate grain size over time using an Envelope, LFO and the new Wavetable Envelope or LFOs.
   * Added the **`z`** step FX (`0`–`100%`) to control grain length on a per-step basis (`0–50%` gives fine `1.0–100.0 ms` control for tight micro-grains, while `50–100%` sweeps `100.0–1000.0 ms` for lush clouds).
     
10. **More Chance Conditions (All 3 Devices)**
   * Spiced up the **`Chance` (`C`)** step FX beyond percentage probability (`C  0`–`C100`) with conditional and cycle-based modes inspired by the Polyend Play:
     * **Group with Last (`C GL`)**: Plays only if the previous conditional step evaluated on the same track played.
     * **Not Last (`C !L`)**: Plays only if the previous conditional step evaluated on the same track was skipped.
     * **Play N / Skip M (`C1:1`–`C6:6`)**: Plays the step for `N` consecutive pattern playthroughs, then skips it for `M` playthroughs (`N, M` in `1..6`).
     * **Skip N / Play M (`CS11`–`CS66`)**: Skips the step for `N` consecutive pattern playthroughs, then plays it for `M` playthroughs (`N, M` in `1..6`).
     
11. **Reactivated Multi-Folder SD Card Image Viewer (All 3 Devices)**
   * Reactivated and expanded the JPEG image viewer under `Config > Info` with three user folders (`Reference`, `Notes`, and `Custom` in `/Images/<Folder>/1.jpg` … `48.jpg`), supporting Left/Right, Jog-wheel, and direct Pad `1–48` selection.
   * See [`docs/image_viewer.md`](docs/image_viewer.md).

### Changed & Fixed

 * **Live Recording Improvements (All 3 Devices)**:
	* You can now play much tigther notes on single-armed tracks. As in: If you play a second note before releasing the first, it won't get lost.
	* Audible first note after preroll, and configurable first-note recording timing threshold (if you tend to play early).
	  
* **Exact `44100 Hz` Offline Audio Export (All 3 Devices)**:
	* Fixed sample rate drift during offline master and stem export so rendered `.wav` files match real-time `44100 Hz` playback pitch and timing.
	* Also removed excessive silence at end of renders.
	  
* **Removed clicks on rendered samples and editor effects (All 3 devices)**
	* No more clicks at the end of your samples.
	  
* **MIDI Overhaul (All 3 devices)**
	* Fixed USB MIDI enumeration for Windows 11 (March 2026 update).
	* Fixed long-term tempo drift and overhauled how MIDI Clock is sent out. Timing is now ~15× tighter and stays locked over hours of playback.
	* Fixed an issue where the Tracker could start slightly late (by one clock tick) when pressing Play on an external master clock or DAW. Step 1 now fires dead-on the downbeat.
	* The Tracker now responds to MIDI `Continue` and `Song Position Pointer` messages.
	* Improved how the Tracker follows incoming MIDI Clock when your computer or master device sends slightly uneven clock bursts, and fixed the `Clock Sync Delay` setting so fine-tuning your setup's latency works predictably.
	* Fixed low-level USB and TRS MIDI buffer issues that could occasionally drop clocks, notes, or CC automation during busy patterns.
	
* **Faster Project Loading & Saving (All 3 Devices)**:
	* Opening a project no longer duplicates all of its audio samples into a temporary folder on the SD card before loading them into memory. Instead, samples are loaded directly from your project folder, making even large, sample-heavy projects open in a fraction of the time while drastically reducing SD card wear.
	* Saving changes to your current project now only writes out samples whose audio was actually edited or newly added, while pattern edits and instrument parameter tweaks save almost instantly. (`Save As` still saves a complete, standalone copy of your project and all its samples).
	  
* **Major Internal Memory, SDRAM & Audio Engine Optimization (All 3 Devices)**:
	* Reclaimed internal SRAM and SDRAM across all three devices to power Hybrid SD Card Sample Streaming while keeping 100% of the original sample RAM pool intact (`11.25 MB` on Original Tracker / `60 MB` on Tracker+ & Tracker Mini), leaving generous internal RAM headroom (`~128 KB` free on Original Tracker, `~22.5 KB` `SRAM_ITC` + `~21.5 KB` `SRAM_DTC` + `~9.3 KB` `SRAM_OC` on Tracker+ & Tracker Mini) and up to `~29 MB` (`IMX`) / `~5.3 MB` (`MK66`) of reclaimable SDRAM for future community ideas.
	* Optimized real-time audio rendering for cleaner playback on heavy multi-track projects.
	  
* **Open-Source Build Consolidation (All 3 Devices)**:
	* Unified `core/`, `platforms/mk66/`, `platforms/imx/`, `synth/`, `util/`, and `editor-fx-sdk/` into a single monorepo built via a top-level [`Makefile`](Makefile) and [`version.mk`](version.mk).
	* Removed the proprietary NES emulator and repurposed its `EXTERNAL_RAM` region (`externalRamScratchBuff`, `768 KB`) as the shared scratch pool for `.ptfx` plugins, offline audio export, SD `.wav` preview, and modal file browsers. (Sorry gamers).
