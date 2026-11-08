// Generative parts for Fill > Where?: drums, ambient pads, a bassline and a lead that evolve over
// the selected steps (Tracker Mini).
//
// Everything here is a pure function of (setup, lane, step): no state is kept, so the same setup
// always gives the same music and every part of one song agrees on the form (intro, build, break,
// peak, outro) and on the chords, because they all derive them from the same seed. The seed is
// made from the style and the song number (songSeed()), so a drum part, a bassline and a lead
// that are filled one after the other with the same style and song belong together.
//
// Time: a step is a sixteenth note, a bar is 16 steps and a phrase is 4 bars. The selection is
// laid out as an arrangement of phrases. Each phrase has an energy from 0 (break) to 4 (peak):
// short selections just get a steady groove, longer ones build up from a sparse intro, hold,
// drop out for a break about 55% of the way through, come back at the peak and end calmer. The
// last bar of every phrase is a fill (a snare roll, an open hat, a kick drop, a bass pickup).
#ifndef MT_GENERATIVE_H_
#define MT_GENERATIVE_H_

#include <stdint.h>

namespace gen
{
enum Mode : uint8_t
{
    kDrums = 0,
    kAmbient,
    kBass,
    kLead,
    kModeCount
};

// How much the arrangement changes: Gentle repeats a groove for 8 bars and moves little between
// phrases, Normal changes every phrase, Wild changes every bar and wanders between energies.
enum Evolve : uint8_t
{
    kGentle = 0,
    kNormal,
    kWild,
    kEvolveCount
};

constexpr int kStyleCount = 13;
// lanes: kick, snare/clap, closed hat, open hat, percussion, cymbal (the order of the Drums beats)
constexpr int kDrumLanes = 6;
constexpr int kAmbientVoices = 4;
constexpr int kBassVoices = 1;
constexpr int kLeadVoices = 1;
constexpr int kMicroStepsPerStep = 48; // the unit of Event::length
constexpr int kSongCount = 99;         // songs 1..99 per style; 0 means a new random one every time

struct Setup
{
    uint8_t mode = kDrums;
    uint8_t style = 0;       // 0 .. kStyleCount - 1
    uint8_t evolve = kNormal;
    uint16_t seed = 1;       // a different seed gives a different arrangement of the same style
    uint16_t length = 64;    // steps in the selection
    uint8_t root = 0;        // pitch class of the key (0 = C)
    uint8_t scale = 0;       // index into scaleDef; Chromatic uses the style's own scale
    int16_t anchor = 60;     // melodic parts: the note the lowest voice is built on
};

struct Event
{
    bool on = false;        // a note starts here
    bool off = false;       // the voice fades out here (a break)
    uint8_t velocity = 0;   // 1..127 when on
    int16_t note = 0;       // melodic parts: the note
    // melodic parts: how long the note sounds, in 1/48 of a step. 0 = until the next note.
    uint16_t length = 0;
};

const char* styleName(int style);
// number of parts a mode can write: drum lanes, pad voices or 1 for the bassline
int laneCount(uint8_t mode);
// note that plays a lane on the PERC drum synth
int16_t drumNote(uint8_t lane);

Event stepEvent(const Setup& setup, uint8_t lane, uint16_t offset);

// The seed of a song: the same style and song number always give the same seed.
uint16_t songSeed(int style, int song);
// The style a 'Random' style stands for in a given song (so that every part of the song agrees).
int songStyle(int song);
// The chord (the scale degree of its root, 0 = the key note) that the bassline, the pads and the
// lead all follow in this bar.
int chordRoot(const Setup& setup, int bar);
// bars a chord lasts in this style (and evolve setting)
int chordLength(const Setup& setup);

// helpers that the tests and the editor share
int barEnergy(const Setup& setup, int bar); // 0 (break) .. 4 (peak)
bool isFillBar(const Setup& setup, int bar);
} // namespace gen

#endif /* MT_GENERATIVE_H_ */
