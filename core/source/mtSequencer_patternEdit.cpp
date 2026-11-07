#include "core/interfaceDefs.h"
#include "fileManager.h"
#include "mtAudioEngine.h"
#include "mtSequencer.h"
#include "mtStructs.h"
#include "patternEditor/patternEditor.h"
#include "patternFileValidation.h"

#include "ctype.h"
#include "util_math.h"
#include <cr_section_macros.h>

extern Sequencer sequencer;

__NOINIT(EXTERNAL_RAM) Sequencer::strPattern copyPastePattern;

void fromToSwap(int16_t& from, int16_t& to)
{
    if (to < from)
    {
        int16_t buff;
        buff = from;
        from = to;
        to = buff;
    }
}

// The value the step rules get for one track. For Drums it holds the pattern and the lane (the
// place of the track inside the selection); the other rules take 'density' as it is.
static inline int16_t fillRuleForTrack(int16_t fillStep,
                                       int16_t density,
                                       uint8_t track,
                                       uint8_t firstTrack)
{
    return fillStep == Sequencer::fillStepDrums
             ? Sequencer::makeDrumRule(density, static_cast<uint8_t>(track - firstTrack))
             : density;
}

// How far the instrument of a new note moves from the last used one. A drum beat uses one
// instrument per lane, in the order of the instrument list (kick on the last used instrument, the
// snare on the next one, ...), so a kit loaded in that order plays right away. Only sample tracks
// do this: a MIDI track has one instrument per channel and keeps the last used one.
static inline int16_t fillInstrumentStep(int16_t fillStep, int16_t rule, uint8_t track)
{
    // one PERC drum synth plays every drum by note: all lanes stay on it
    return fillStep == Sequencer::fillStepDrums && isSampleTrack(track) &&
               !isDrumSynthInstrument(mtProject.values.lastUsedInstrument)
             ? Sequencer::drumRuleInstrumentStep(rule)
             : 0;
}

// The note of a Drums hit on a PERC drum synth (the drum of the lane or rhythm), -1 for the other
// rules. The note chosen in the popup is for samples and other synths; on PERC the note is the drum.
static inline int16_t fillDrumSynthNote(int16_t fillStep, int16_t rule)
{
    return fillStep == Sequencer::fillStepDrums ? Sequencer::drumRuleSynthNote(rule) : -1;
}

void Sequencer::fillRandomNotes(int16_t fillStep,
                                int16_t scale,
                                int16_t from,
                                int16_t to,
                                int16_t density)
{
    fromToSwap(from, to);

    strSelection* sel = &selection;
    if (!isSelectionCorrect(sel))
        return;
    strPattern::strTrack::strStep* step;
    uint8_t root = mtProject.values.padBoardRootNote;

    for (uint8_t t = sel->firstTrack; t <= sel->lastTrack; t++)
    {
        const int16_t rule = fillRuleForTrack(fillStep, density, t, sel->firstTrack);
        const int16_t instrumentStep = fillInstrumentStep(fillStep, rule, t);
        const int16_t drumNote = fillDrumSynthNote(fillStep, rule);
        for (uint16_t s = sel->firstStep, offset = 0; s <= sel->lastStep; s++, offset++)
        {
            step = &currentPattern->track[t].step[s];
            if (isStepToFillNote(step, offset, fillStep, rule))
            {
                if (step->note == STEP_NOTE_EMPTY)
                {
                    step->instrument = changeInstrumentInSpecificOrder(
                      mtProject.values.lastUsedInstrument, instrumentStep, !isSampleTrack(t));
                }
                if (drumNote >= 0 && isDrumSynthInstrument(step->instrument))
                {
                    step->note = static_cast<int8_t>(drumNote); // PERC: the note is the drum
                    continue;
                }
                for (uint8_t a = 0; a <= 100; a -= -1)
                {
                    step->note = utilMathRandom::random(from, to + 1);
                    if (isInScale(step->note, root, scale))
                        break;
                }
            }
        }
    }
}
void Sequencer::fillLinearNotes(int16_t fillStep,
                                int16_t scale,
                                int16_t from,
                                int16_t to,
                                int16_t density)
{
    strSelection* sel = &selection;
    if (!isSelectionCorrect(sel))
        return;
    strPattern::strTrack::strStep* step;
    //	uint8_t scale = mtProject.values.padBoardScale;
    uint8_t root = mtProject.values.padBoardRootNote;

    for (uint8_t t = sel->firstTrack; t <= sel->lastTrack; t++)
    {
        const int16_t rule = fillRuleForTrack(fillStep, density, t, sel->firstTrack);
        const int16_t instrumentStep = fillInstrumentStep(fillStep, rule, t);
        const int16_t drumNote = fillDrumSynthNote(fillStep, rule);
        for (uint16_t s = sel->firstStep, offset = 0; s <= sel->lastStep; s++, offset++)
        {
            step = &currentPattern->track[t].step[s];
            if (isStepToFillNote(step, offset, fillStep, rule))
            {
                if (drumNote >= 0) // PERC: the note is the drum, whatever the scale
                {
                    const int instrument =
                      step->isNoteEmpty() ? changeInstrumentInSpecificOrder(
                                              mtProject.values.lastUsedInstrument,
                                              instrumentStep,
                                              !isSampleTrack(t))
                                          : step->instrument;
                    if (isDrumSynthInstrument(instrument))
                    {
                        step->instrument = static_cast<uint8_t>(instrument);
                        step->note = static_cast<int8_t>(drumNote);
                        continue;
                    }
                }
                int newNote = round(mapClamped<float>(
                  (float)offset + sel->firstStep, sel->firstStep, sel->lastStep, from, to));

                for (uint8_t a = 0; a < 12; a++)
                {
                    if (isInScale(newNote, root, scale))
                        break;
                    newNote++;
                }
                newNote = clamp<int>(newNote, STEP_NOTE_MIN, STEP_NOTE_MAX);

                if (isInScale(newNote, root, scale))
                {
                    if (step->isNoteEmpty())
                    {
                        step->instrument = changeInstrumentInSpecificOrder(
                          mtProject.values.lastUsedInstrument, instrumentStep, !isSampleTrack(t));
                    }
                    step->note = newNote;
                }
            }
        }
    }
}
void Sequencer::fillLinearInstruments(int16_t fillStep, int16_t from, int16_t to, int16_t density)
{

    strSelection* sel = &selection;
    if (!isSelectionCorrect(sel))
        return;
    strPattern::strTrack::strStep* step;

    for (uint8_t t = sel->firstTrack; t <= sel->lastTrack; t++)
    {
        const int16_t rule = fillRuleForTrack(fillStep, density, t, sel->firstTrack);
        for (uint16_t s = sel->firstStep, offset = 0; s <= sel->lastStep; s++, offset++)
        {
            step = &currentPattern->track[t].step[s];
            if (isStepToFillNote(step, offset, fillStep, rule))
            {
                int newInstrument =
                  mapClamped<int>(offset + sel->firstStep, sel->firstStep, sel->lastStep, from, to);

                step->instrument =
                  changeInstrumentInSpecificOrder(newInstrument, 0, !isSampleTrack(t));

                if (step->note == STEP_NOTE_EMPTY && isSampleInstrument(step->instrument))
                {
                    step->note = STEP_NOTE_DEFAULT;
                }
            }
        }
    }
}

void Sequencer::fillLinearFx(int16_t fxIndex,
                             int16_t fillStep,
                             int16_t fxName,
                             int16_t fromVal,
                             int16_t toVal,
                             int16_t density)
{
    strSelection* sel = &selection;
    if (!isSelectionCorrect(sel))
        return;
    if (fxIndex > FX_SLOTS_MAX)
        fxIndex = 0;

    int16_t fxType = interfaceGlobals.fxNameToId(fxName);

    //	if(fromVal < 0 && toVal <= 0) toVal++;
    //	else if(fromVal >= 0 && toVal > 0) toVal++;

    strPattern::strTrack::strStep* step;

    for (uint8_t t = sel->firstTrack; t <= sel->lastTrack; t++)
    {
        const int16_t rule = fillRuleForTrack(fillStep, density, t, sel->firstTrack);
        for (uint16_t s = sel->firstStep, offset = 0; s <= sel->lastStep; s++, offset++)
        {
            step = &currentPattern->track[t].step[s];

            if (isStepToFillFx(step, offset, fxIndex, fillStep, rule))
            {
                step->fx[fxIndex].type = (fxType >= 0) ? fxType : generateRandomFx();

                float tempFloat = fromVal;
                if (sel->firstStep != sel->lastStep)
                {
                    tempFloat = mapClamped<float>((float)(offset + sel->firstStep),
                                                  sel->firstStep,
                                                  sel->lastStep,
                                                  fromVal,
                                                  toVal);
                }

                step->fx[fxIndex].value = clamp<int>((uint8_t)tempFloat,
                                                     getFxMin(step->fx[fxIndex].type),
                                                     getFxMax(step->fx[fxIndex].type));
            }
        }
    }
}

uint8_t Sequencer::generateRandomFx()
{
    uint8_t retVal;
    for (uint8_t a = 0; a < 100; a++)
    {
        retVal = utilMathRandom::random(fx.FX_TYPE_OFF + 1, FX_COUNT - FX_COUNT_HIDDEN_FXes + 1);
        if (retVal != fx.FX_TYPE_TEMPO && retVal != fx.FX_TYPE_BREAK_PATTERN // breaka nie chcemy
        )
            break;
    }
    return retVal;
}

void Sequencer::fillRandomFx(int16_t fxIndex,
                             int16_t fillStep,
                             int16_t fxName,
                             int16_t fromVal,
                             int16_t toVal,
                             int16_t density)
{
    fromToSwap(fromVal, toVal);

    strSelection* sel = &selection;
    if (!isSelectionCorrect(sel))
        return;
    if (fxIndex > FX_SLOTS_MAX)
        fxIndex = 0;

    int16_t fxType = interfaceGlobals.fxNameToId(fxName);

    strPattern::strTrack::strStep* step;

    for (uint8_t t = sel->firstTrack; t <= sel->lastTrack; t++)
    {
        const int16_t rule = fillRuleForTrack(fillStep, density, t, sel->firstTrack);
        for (uint16_t s = sel->firstStep, offset = 0; s <= sel->lastStep; s++, offset++)
        {
            step = &currentPattern->track[t].step[s];

            if (isStepToFillFx(step, offset, fxIndex, fillStep, rule))
            {
                step->fx[fxIndex].value = utilMathRandom::random(fromVal, toVal + 1);
                step->fx[fxIndex].type = (fxType >= 0) ? fxType : generateRandomFx();

                step->fx[fxIndex].value = clamp<int>(step->fx[fxIndex].value,
                                                     getFxMin(step->fx[fxIndex].type),
                                                     getFxMax(step->fx[fxIndex].type));
            }
        }
    }
}

bool Sequencer::isEuclideanStep(uint16_t offset, int16_t density)
{
    int length = selection.lastStep - selection.firstStep + 1;
    float euclStepSize = (float)length / density;
    float nearestStep = round(offset / euclStepSize) * euclStepSize;
    float diff = nearestStep - (float)offset;
    return diff <= 0.5f && diff > -0.5f;
}

// Fill > Where? > Drums
// A drum pattern has up to 8 lanes. A lane is a rhythm for one track of 8, 16 or 32 steps (16th
// notes; 'x' = hit, '.' = no hit). It repeats over the selection, starting at the first selected
// step. A pattern with one lane (names start with K kick, S snare, H hat, P percussion) gives
// that rhythm to every selected track. A pattern with more lanes (a beat, names start with B)
// gives lane 0 to the first selected track, lane 1 to the next one and so on; tracks after the
// last lane are left alone. The lanes of the beats are in this order: kick, snare/clap, closed
// hat, open hat, percussion A, percussion B.
// Names have at most 9 characters (the width of the label in the Fill popup).
namespace
{
constexpr int kDrumLanes = 8;

struct strDrumPattern
{
    const char* name;
    const char* lanes[kDrumLanes]; // the unused lanes are nullptr
};

constexpr strDrumPattern drumPatterns[] = {
    // beats: kick, snare/clap, closed hat, open hat, percussion A, percussion B
    // four on the floor, clap, off-sixteenth hats, open hat on the offbeat
    {"B House",
     {"x...x...x...x...", "....x.......x...", ".x.x.x.x.x.x.x.x", "..x...x...x...x."}},
    // four on the floor, sixteenth hats, open hat on the offbeat
    {"B Techno",
     {"x...x...x...x...", "....x.......x...", "xxxxxxxxxxxxxxxx", "..x...x...x...x."}},
    // hip-hop boom bap
    {"B BoomBap",
     {"x.....x...x.....", "....x.......x...", "x.x.x.x.x.x.x.x."}},
    // lo-fi hip-hop
    {"B LoFi",
     {"x....x....x.....", "....x.......x...", "x...x...x...x..."}},
    // trap, half-time snare
    {"B Trap",
     {"x......x..x.....", "........x.......", "xxxxxxxxxxxxxxxx"}},
    // trap, double-time snare
    {"B Trap2x",
     {"x......x..x.....", "....x.......x...", "xxxxxxxxxxxxxxxx"}},
    // drill
    {"B Drill",
     {"x..x.....x......", "....x.......x...", "x.x.x.x.x.x.x.x."}},
    // UK drill, snare on 3
    {"B UKDrill",
     {"x..x.....x......", "........x.......", "x.x.x.x.x.x.x.x."}},
    // UK garage / drum and bass two-step
    {"B 2Step",
     {"x.........x.....", "....x.......x...", "x.x.x.x.x.x.x.x.", "..x...x...x...x."}},
    // reggaeton / dancehall dembow
    {"B Dembow",
     {"x...x...x...x...", "...x..x....x..x.", "x.x.x.x.x.x.x.x."}},
    // reggae one drop
    {"B OneDrop",
     {"........x.......", "........x.......", "x.x.x.x.x.x.x.x."}},
    // reggae rockers
    {"B Rockers",
     {"x.......x.......", "........x.......", "x.x.x.x.x.x.x.x."}},
    // reggae steppers
    {"B Stepper",
     {"x...x...x...x...", "........x.......", "x.x.x.x.x.x.x.x."}},
    // basic rock
    {"B Rock",
     {"x.......x.......", "....x.......x...", "x.x.x.x.x.x.x.x."}},
    // rock with a kick on the and of 3
    {"B Rock2",
     {"x.......x.x.....", "....x.......x...", "x.x.x.x.x.x.x.x."}},
    // half-time
    {"B Half",
     {"x.........x.....", "........x.......", "x.x.x.x.x.x.x.x."}},
    // synth-pop, new wave, Italo, disco, industrial and EBM style grooves
    // (first drum machines: sparse kick and snare, eighth hats, a few accents)
    // sparse synth-pop
    {"B SynPop",
     {"x.......x.....x.", "....x.......x...", "x.x.x.x.x.x.x.x."}},
    // new wave: syncopated kick, open hat on the last offbeat
    {"B NewWave",
     {"x..x....x.x.....", "....x.......x...", "x.x.x.x.x.x.x...", "..............x."}},
    // Italo: four on the floor, extra kick on the last offbeat, claps,
    // closed hats on the offbeats and the open hat after the last beat
    {"B Italo",
     {"x...x...x...x.x.", "....x.......x...", "..x...x...x.....", "..............x."}},
    // disco: sixteenth hats that leave out the snare steps, open hat on the offbeat
    {"B Disco",
     {"x...x...x...x...", "....x.......x...", "xx.x.x.xxx.x.x.x", "..x...x...x...x."}},
    // stomp, stomp, clap
    {"B Stomp",
     {"x...x...........", "........x.......", "..x...x...x...x."}},
    // industrial: heavy four on the floor, 3-3-3-3-4 metal hits, rim on the offbeats
    {"B Indust",
     {"x...x...x...x...", "....x.......x...", "x.x.x.x.x.x.x...", "..............x.",
      "x..x..x..x..x...", "......x.......x."}},
    // dark half-time
    {"B Dark",
     {"x.....x.......x.", "........x.......", "..x...x...x...x."}},
    // EBM style pulse: eighth-note kick, sixteenth hats
    {"B Pulse",
     {"x.x.x.x.x.x.x.x.", "....x.......x...", "xxxxxxxxxxxxxxxx"}},
    // single rhythms: every selected track gets the same one
    {"K 4Floor", {"x...x...x...x..."}},  // four on the floor
    {"K Rock", {"x.......x.x....."}},  // 1, 3 and the "and" of 3
    {"K Basic", {"x.......x......."}},  // 1 and 3
    {"K 2Step", {"x.........x....."}},  // 1 and the "and" of 3 (two-step)
    {"K BoomBap", {"x.....x...x....."}},  // boom bap
    {"K LoFi", {"x....x....x....."}},  // lo-fi
    {"K Trap", {"x......x..x....."}},  // trap
    {"K Drill", {"x..x.....x......"}},  // drill
    {"K Drop", {"........x......."}},  // one drop
    {"K Italo", {"x...x...x...x.x."}},  // four on the floor, extra on the last offbeat
    {"K Stomp", {"x...x..........."}},  // stomp, stomp
    {"K Pulse", {"x.x.x.x.x.x.x.x."}},  // eighth notes
    {"S Back", {"....x.......x..."}},  // backbeat: 2 and 4
    {"S Half", {"........x......."}},  // half-time: 3 only
    {"S Dembow", {"...x..x....x..x."}},  // dembow, 3-3-2
    {"S Roll", {"............xxxx"}},  // roll on the last beat
    {"S Roll8", {"........xxxxxxxx"}},  // roll on the last half bar
    {"S Turn", {"....x.......x.x."}},  // backbeat, extra hit before the end (turnaround)
    {"H 4ths", {"x...x...x...x..."}},  // quarter notes
    {"H 8ths", {"x.x.x.x.x.x.x.x."}},  // eighth notes
    {"H 16ths", {"xxxxxxxxxxxxxxxx"}},  // sixteenth notes
    {"H Offbeat", {"..x...x...x...x."}},  // offbeat (open hat in house)
    {"H 16 Off", {".x.x.x.x.x.x.x.x"}},  // off-sixteenths
    {"H Trap", {"x.x.x.x.x.x.xxxx"}},  // eighths, sixteenth burst on the last beat
    {"H Disco16", {"xxxx.xxxxxxx.xxx"}},  // sixteenths, snare steps left out
    {"H Tuck", {"..x...x...x..xx."}},  // offbeats with one extra sixteenth tucked in
    {"P Bossa", {"x..x..x...x..x.."}},  // bossa nova clave 3-2
    {"P Bossa23", {"..x..x..x..x..x."}},  // bossa nova clave 2-3
    {"P Son 3-2", {"x..x..x...x.x..."}},  // son clave 3-2
    {"P Son 2-3", {"..x.x...x..x..x."}},  // son clave 2-3
    {"P Rumba32", {"x..x...x..x.x..."}},  // rumba clave 3-2
    {"P Rumba23", {"..x.x...x..x...x"}},  // rumba clave 2-3
    {"P Shiko", {"x...x.x...x.x..."}},  // shiko
    {"P Soukous", {"x..x..x...xx...."}},  // soukous
    {"P Gahu", {"x..x..x...x...x."}},  // gahu
    {"P Tresill", {"x..x..x."}},  // tresillo (8 steps)
    {"P Cinquil", {"x.xx.xx."}},  // cinquillo (8 steps)
    {"P Habana", {"x..xx.x."}},  // habanera (8 steps)
    {"P Clank", {"x..x..x..x..x..."}},  // 3-3-3-3-4, metal hits
    {"P Rim", {"......x.......x."}},  // rim on the "and" of 2 and 4
    {"P Pickup", {"..............x."}},  // one hit on the last offbeat
};
constexpr int16_t drumPatternsCount = sizeof(drumPatterns) / sizeof(drumPatterns[0]);
static_assert(drumPatternsCount < 256, "a pattern number has to fit in 8 bits");

constexpr int drumPatternLength(const char* hits)
{
    int len = 0;
    while (hits[len] != 0)
        len++;
    return len;
}

constexpr int drumPatternLanes(const strDrumPattern& pattern)
{
    int lanes = 0;
    while (lanes < kDrumLanes && pattern.lanes[lanes] != nullptr)
        lanes++;
    return lanes;
}

constexpr bool drumPatternsAreValid()
{
    for (int i = 0; i < drumPatternsCount; i++)
    {
        int nameLen = drumPatternLength(drumPatterns[i].name);
        if (nameLen < 1 || nameLen > 9)
            return false;
        int lanes = drumPatternLanes(drumPatterns[i]);
        if (lanes < 1)
            return false;
        for (int l = lanes; l < kDrumLanes; l++) // no gap between the lanes
        {
            if (drumPatterns[i].lanes[l] != nullptr)
                return false;
        }
        for (int l = 0; l < lanes; l++)
        {
            const char* hits = drumPatterns[i].lanes[l];
            int len = drumPatternLength(hits);
            if (len != 8 && len != 16 && len != 32)
                return false;
            for (int s = 0; s < len; s++)
            {
                if (hits[s] != 'x' && hits[s] != '.')
                    return false;
            }
        }
    }
    return true;
}
static_assert(drumPatternsAreValid(), "drum pattern table is not valid");
} // namespace

int16_t Sequencer::getDrumPatternCount()
{
    return drumPatternsCount;
}

const char* Sequencer::getDrumPatternName(int16_t pattern)
{
    if (pattern < 1 || pattern > drumPatternsCount)
        return "Random";
    return drumPatterns[pattern - 1].name;
}

int16_t Sequencer::getDrumPatternLanes(int16_t pattern)
{
    if (pattern < 1 || pattern > drumPatternsCount)
        return 0;
    return static_cast<int16_t>(drumPatternLanes(drumPatterns[pattern - 1]));
}

int16_t Sequencer::resolveDrumPattern(int16_t pattern)
{
    if (pattern < 1)
        return static_cast<int16_t>(utilMathRandom::random(1, drumPatternsCount + 1));
    return pattern > drumPatternsCount ? drumPatternsCount : pattern;
}

int16_t Sequencer::makeDrumRule(int16_t pattern, uint8_t lane)
{
    if (pattern < 1)
        pattern = 0;
    return static_cast<int16_t>((pattern & 0xFF) | ((lane & 0x07) << 8));
}

bool Sequencer::isDrumPatternHit(int16_t rule, uint16_t offset)
{
    int16_t pattern = rule & 0xFF;
    int lane = (rule >> 8) & 0x07;
    if (rule < 0 || pattern < 1 || pattern > drumPatternsCount)
        return false;
    const strDrumPattern& drums = drumPatterns[pattern - 1];
    // one lane: every track gets it; more lanes: the track's lane, none after the last one
    const char* hits = drums.lanes[1] == nullptr ? drums.lanes[0] : drums.lanes[lane];
    if (hits == nullptr)
        return false;
    return hits[offset % drumPatternLength(hits)] == 'x';
}

int16_t Sequencer::drumRuleInstrumentStep(int16_t rule)
{
    int16_t pattern = rule & 0xFF;
    if (rule < 0 || pattern < 1 || pattern > drumPatternsCount)
        return 0;
    if (drumPatterns[pattern - 1].lanes[1] == nullptr)
        return 0; // a single rhythm
    return static_cast<int16_t>((rule >> 8) & 0x07);
}

// Notes of the PERC drum synth. The pad lighting of the instrument editor marks the first note
// of each drum section: kick 60, snare 62, closed hat 64, open hat 66, cymbal 68, percussion 70.
// A beat gives its lanes (kick, snare/clap, closed hat, open hat, percussion A and B) the drum of
// that kind; a single rhythm takes the drum of its role letter (K, S, H, P).
int16_t Sequencer::drumRuleSynthNote(int16_t rule)
{
    constexpr int16_t kKick = 60, kSnare = 62, kClosedHat = 64, kOpenHat = 66, kPercussion = 70;
    constexpr int16_t laneNotes[kDrumLanes] = { kKick,       kSnare,      kClosedHat,  kOpenHat,
                                                kPercussion, kPercussion, kPercussion, kPercussion };
    int16_t pattern = rule & 0xFF;
    if (rule < 0 || pattern < 1 || pattern > drumPatternsCount)
        return -1;
    const strDrumPattern& drums = drumPatterns[pattern - 1];
    if (drums.lanes[1] != nullptr)
        return laneNotes[(rule >> 8) & 0x07];
    // the offbeat hats are the open hat of the beats
    if (strcmp(drums.name, "H Offbeat") == 0)
        return kOpenHat;
    switch (drums.name[0])
    {
        case 'K': return kKick;
        case 'S': return kSnare;
        case 'H': return kClosedHat;
        case 'P': return kPercussion;
        default: return -1;
    }
}

bool Sequencer::isStepToFillFx(strPattern::strTrack::strStep* step,
                               uint16_t offset,
                               uint8_t fxIndex,
                               int16_t fillStep,
                               int16_t density)
{
    if (fillStep == fillStepByStep && offset % density == 0)
        return true;
    else if (fillStep == fillStepRandom && utilMathRandom::random(0, MAX_FILL_DENSITY) < density)
        return true;

    else if (fillStep == fillStepEuclidean)
    {
        return isEuclideanStep(offset, density);
    }

    else if (fillStep == fillStepNoFx && step->fx[fxIndex].type == 0)
        return true;

    else if (fillStep == fillStepFx && step->fx[fxIndex].type != 0)
        return true;

    else if (fillStep == fillStepNoNote && step->note < 0)
        return true;

    else if (fillStep == fillStepNote && step->note >= 0)
        return true;

    else if (fillStep == fillStepDrums)
        return isDrumPatternHit(density, offset);

    return false;
}

bool Sequencer::isStepToFillProp(strPattern::strTrack::strStep* step,
                                 uint16_t offset,
                                 bool isPropSet,
                                 int16_t fillStep,
                                 int16_t density)
{
    if (fillStep == fillStepByStep && offset % density == 0)
        return true;
    else if (fillStep == fillStepRandom && utilMathRandom::random(0, MAX_FILL_DENSITY) < density)
        return true;
    else if (fillStep == fillStepEuclidean)
        return isEuclideanStep(offset, density);
    else if (fillStep == fillStepNoFx && !isPropSet)
        return true;
    else if (fillStep == fillStepFx && isPropSet)
        return true;
    else if (fillStep == fillStepNoNote && step->note < 0)
        return true;
    else if (fillStep == fillStepNote && step->note >= 0)
        return true;
    else if (fillStep == fillStepDrums)
        return isDrumPatternHit(density, offset);

    return false;
}

void Sequencer::fillLinearMicromove(int16_t fillStep,
                                    int16_t fromVal,
                                    int16_t toVal,
                                    int16_t density)
{
    strSelection* sel = &selection;
    if (!isSelectionCorrect(sel))
        return;

    for (uint8_t t = sel->firstTrack; t <= sel->lastTrack; t++)
    {
        const int16_t rule = fillRuleForTrack(fillStep, density, t, sel->firstTrack);
        for (uint16_t s = sel->firstStep, offset = 0; s <= sel->lastStep; s++, offset++)
        {
            strPattern::strTrack::strStep* step = &currentPattern->track[t].step[s];
            if (isStepToFillProp(step, offset, step->micromove >= 0, fillStep, rule))
            {
                float tempFloat = fromVal;
                if (sel->firstStep != sel->lastStep)
                {
                    tempFloat = mapClamped<float>((float)(offset + sel->firstStep),
                                                  sel->firstStep,
                                                  sel->lastStep,
                                                  fromVal,
                                                  toVal);
                }
                step->micromove = clamp<int>((int)roundf(tempFloat), 0, 100);
            }
        }
    }
}

void Sequencer::fillRandomMicromove(int16_t fillStep,
                                    int16_t fromVal,
                                    int16_t toVal,
                                    int16_t density)
{
    fromToSwap(fromVal, toVal);

    strSelection* sel = &selection;
    if (!isSelectionCorrect(sel))
        return;

    for (uint8_t t = sel->firstTrack; t <= sel->lastTrack; t++)
    {
        const int16_t rule = fillRuleForTrack(fillStep, density, t, sel->firstTrack);
        for (uint16_t s = sel->firstStep, offset = 0; s <= sel->lastStep; s++, offset++)
        {
            strPattern::strTrack::strStep* step = &currentPattern->track[t].step[s];
            if (isStepToFillProp(step, offset, step->micromove >= 0, fillStep, rule))
            {
                step->micromove = clamp<int>(utilMathRandom::random(fromVal, toVal + 1), 0, 100);
            }
        }
    }
}

void Sequencer::fillLinearVolume(int16_t fillStep,
                                 int16_t fromVal,
                                 int16_t toVal,
                                 int16_t density)
{
    strSelection* sel = &selection;
    if (!isSelectionCorrect(sel))
        return;

    for (uint8_t t = sel->firstTrack; t <= sel->lastTrack; t++)
    {
        const int16_t rule = fillRuleForTrack(fillStep, density, t, sel->firstTrack);
        for (uint16_t s = sel->firstStep, offset = 0; s <= sel->lastStep; s++, offset++)
        {
            strPattern::strTrack::strStep* step = &currentPattern->track[t].step[s];
            if (isStepToFillProp(step, offset, step->volume >= 0, fillStep, rule))
            {
                float tempFloat = fromVal;
                if (sel->firstStep != sel->lastStep)
                {
                    tempFloat = mapClamped<float>((float)(offset + sel->firstStep),
                                                  sel->firstStep,
                                                  sel->lastStep,
                                                  fromVal,
                                                  toVal);
                }
                step->volume = clamp<int>((int)roundf(tempFloat), 0, 100);
            }
        }
    }
}

void Sequencer::fillRandomVolume(int16_t fillStep,
                                 int16_t fromVal,
                                 int16_t toVal,
                                 int16_t density)
{
    fromToSwap(fromVal, toVal);

    strSelection* sel = &selection;
    if (!isSelectionCorrect(sel))
        return;

    for (uint8_t t = sel->firstTrack; t <= sel->lastTrack; t++)
    {
        const int16_t rule = fillRuleForTrack(fillStep, density, t, sel->firstTrack);
        for (uint16_t s = sel->firstStep, offset = 0; s <= sel->lastStep; s++, offset++)
        {
            strPattern::strTrack::strStep* step = &currentPattern->track[t].step[s];
            if (isStepToFillProp(step, offset, step->volume >= 0, fillStep, rule))
            {
                step->volume = clamp<int>(utilMathRandom::random(fromVal, toVal + 1), 0, 100);
            }
        }
    }
}

bool Sequencer::isStepToFillNote(strPattern::strTrack::strStep* step,
                                 uint16_t offset,
                                 int16_t fillStep,
                                 int16_t density)
{
    if (fillStep == fillStepByStep && offset % density == 0)
        return true;
    else if (fillStep == fillStepRandom && utilMathRandom::random(0, MAX_FILL_DENSITY) < density)
        return true;

    else if (fillStep == fillStepEuclidean)
    {
        return isEuclideanStep(offset, density);
    }

    else if (fillStep == fillStepNoFx && step->fx[0].type == 0 && step->fx[1].type == 0)
        return true;

    else if (fillStep == fillStepFx && (step->fx[0].type != 0 || step->fx[1].type != 0))
        return true;

    else if (fillStep == fillStepNoNote && step->note < 0)
        return true;

    else if (fillStep == fillStepNote && step->note >= 0)
        return true;

    else if (fillStep == fillStepDrums)
        return isDrumPatternHit(density, offset);

    return false;
}

void Sequencer::fillRandomInstruments(int16_t fillStep, int16_t from, int16_t to, int16_t density)
{
    fromToSwap(from, to);

    strSelection* sel = &selection;
    if (!isSelectionCorrect(sel))
        return;
    strPattern::strTrack::strStep* step;

    for (uint8_t t = sel->firstTrack; t <= sel->lastTrack; t++)
    {
        const int16_t rule = fillRuleForTrack(fillStep, density, t, sel->firstTrack);
        for (uint16_t s = sel->firstStep, offset = 0; s <= sel->lastStep; s++, offset++)
        {
            step = &currentPattern->track[t].step[s];

            if (isStepToFillNote(step, offset, fillStep, rule))
            {
                int newInstrument = utilMathRandom::random(from, to + 1);
                step->instrument =
                  changeInstrumentInSpecificOrder(newInstrument, 0, !isSampleTrack(t));

                if (step->note == STEP_NOTE_EMPTY && isSampleInstrument(step->instrument))
                {
                    step->note = STEP_NOTE_DEFAULT;
                }
            }
        }
    }
}

void Sequencer::invertSelectedSteps(uint8_t elements)
{

    strSelection* sel = &selection;
    if (!isSelectionCorrect(sel))
        return;
    strPattern::strTrack::strStep* stepA;
    strPattern::strTrack::strStep* stepB;
    strPattern::strTrack::strStep buffStep;

    for (uint8_t t = sel->firstTrack; t <= sel->lastTrack; t++)
    {
        for (uint16_t a = sel->firstStep, b = sel->lastStep;
             a <= ((sel->lastStep - sel->firstStep) / 2) + sel->firstStep;
             a++, b--)
        {

            stepA = &currentPattern->track[t].step[a];
            stepB = &currentPattern->track[t].step[b];

            switch (elements)
            {
                case ELEMENTS_ALL_NO_PREFERENCES:
                    buffStep = *stepA;
                    *stepA = *stepB;
                    *stepB = buffStep;
                    break;
                case ELEMENTS_FX1:

                    buffStep = *stepA;
                    stepA->fx[0] = stepB->fx[0];
                    stepB->fx[0] = buffStep.fx[0];

                    break;

                case ELEMENTS_FX2:

                    buffStep = *stepA;
                    stepA->fx[1] = stepB->fx[1];
                    stepB->fx[1] = buffStep.fx[1];

                    break;

                case ELEMENTS_MICROMOVE:

                    buffStep = *stepA;
                    stepA->micromove = stepB->micromove;
                    stepB->micromove = buffStep.micromove;

                    break;

                case ELEMENTS_VOLUME:

                    buffStep = *stepA;
                    stepA->volume = stepB->volume;
                    stepB->volume = buffStep.volume;

                    break;

                case ELEMENTS_INSTRUMENTS:
                case ELEMENTS_NOTES:

                    buffStep = *stepA;
                    stepA->note = stepB->note;
                    stepB->note = buffStep.note;
                    stepA->instrument = stepB->instrument;
                    stepB->instrument = buffStep.instrument;

                    break;
                default: break;
            }
        }
    }
}

// podwajanie dlugosci patternu z zdublowaniem zawartosci
bool Sequencer::duplicatePattern()
{
    int oldPatternMaxStep = currentPattern->track[0].length;
    assert(oldPatternMaxStep <= MAXSTEP);

    if (oldPatternMaxStep > (Sequencer::MAXSTEP / 2))
        return false;

    int newPatternLength = ((oldPatternMaxStep + 1) * 2) - 1;

    for (int trackIdx = 0; trackIdx < kAllTracks; trackIdx++)
    {
        int trackLen = oldPatternMaxStep + 1;
        currentPattern->track[trackIdx].length = static_cast<PatternLength_t>(newPatternLength);

        for (int stepIdx = 0; stepIdx < trackLen; stepIdx++)
        {
            currentPattern->track[trackIdx].step[stepIdx + trackLen] =
              currentPattern->track[trackIdx].step[stepIdx];
        }
    }

    return true;
}

// podwajanie dlugosci patternu z wstawieniem pustch stepów co 1 step
bool Sequencer::expandPattern()
{
    int patternLength = currentPattern->track[0].length;
    assert(patternLength <= MAXSTEP);

    if (patternLength > (Sequencer::MAXSTEP / 2))
        return false;

    patternLength = ((patternLength + 1) * 2) - 1;

    for (int trackIdx = 0; trackIdx < kAllTracks; trackIdx++)
    {
        currentPattern->track[trackIdx].length = static_cast<PatternLength_t>(patternLength);

        for (int stepIdx = 0; stepIdx < patternLength; stepIdx += 2)
        {
            int srcStepIdx = (patternLength - stepIdx - 1) / 2;
            int destStepIdx = patternLength - stepIdx - 1;

            currentPattern->track[trackIdx].step[destStepIdx] =
              currentPattern->track[trackIdx].step[srcStepIdx];

            clearStep(&currentPattern->track[trackIdx].step[patternLength - stepIdx]);
        }
    }

    return true;
}

// dwukrotne zmniejszenie patternu poprzez skasowaniem co drugiego stepa
bool Sequencer::shrinkPattern()
{
    int patternMaxStep = currentPattern->track[0].length;
    if (patternMaxStep < 1)
        return false;

    int newPatternMaxStep = patternMaxStep / 2;

    for (int trackIdx = 0; trackIdx < kAllTracks; trackIdx++)
    {
        int trackMaxStep = (patternMaxStep / 2) + 1;

        for (int s = 1, srcStep = 2; s < trackMaxStep; s++, srcStep += 2)
        {
            currentPattern->track[trackIdx].step[s] = currentPattern->track[trackIdx].step[srcStep];
        }

        uint16_t oldTrackLen = patternMaxStep + 1;

        for (uint16_t s = trackMaxStep; s < oldTrackLen; s++)
        {
            clearStep(&currentPattern->track[trackIdx].step[s]);
        }

        currentPattern->track[trackIdx].length = static_cast<PatternLength_t>(newPatternMaxStep);
    }

    return true;
}

void Sequencer::changeSelectionNote(int16_t value)
{
    changeSelectionNote(value, false);
}
void Sequencer::changeSelectionNote(int16_t value, bool octaveShift)
{
    strSelection* sel = &selection;
    if (!isSelectionCorrect(sel))
        return;

    bool sometingWasChanged = false;
    newFileManager.storePatternUndoRevision();

    strPattern::strTrack::strStep* step;

    if (octaveShift)
        value = value * 12;

    for (uint8_t t = sel->firstTrack; t <= sel->lastTrack; t++)
    {
        for (uint16_t s = sel->firstStep, offset = 0; s <= sel->lastStep; s++, offset++)
        {
            step = &currentPattern->track[t].step[s];

            if (isSingleSelection(sel))
            {
                if (step->note == STEP_NOTE_EMPTY && value > 0)
                {
                    if (isSampleInstrument(step->instrument))
                    {
                        step->instrument = changeInstrumentInSpecificOrder(
                          mtProject.values.lastUsedInstrument, 0, !isSampleTrack(t));
                    }
                    step->note = STEP_NOTE_DEFAULT;

                    sometingWasChanged = true;
                }
                else if (step->note == STEP_NOTE_EMPTY)
                {
                    if (isSampleInstrument(step->instrument))
                    {
                        step->instrument = changeInstrumentInSpecificOrder(
                          mtProject.values.lastUsedInstrument, 0, !isSampleTrack(t));
                    }
                    step->note = clamp<int>(step->note + value, -4, STEP_NOTE_MAX);

                    sometingWasChanged = true;
                }
                else
                {
                    step->note = clamp<int>(step->note + value, -4, STEP_NOTE_MAX);

                    mtProject.values.lastUsedNote = step->note;

                    sometingWasChanged = true;
                }
            }
            else
            {
                if (step->note >= 0)
                {
                    step->note = clamp<int>(step->note + value, 0, STEP_NOTE_MAX);
                    sometingWasChanged = true;
                }
            }
        }
    }

    if (sometingWasChanged)
    {
        newFileManager.setActualPatternStructChanged();
        if (!isPlay())
        {
            playSelection();
        }
    }
}

uint8_t Sequencer::chanceCoarseStep(uint8_t value, int16_t delta)
{
    // order of the values: 0..100 %, group with last, not last, play/skip cycles (n up, m up),
    // skip/play cycles, "pass A of B" (B up, A up), first, not first
    if (value > fx.CHANCE_MAX)
        value = fx.CHANCE_MAX;

    uint8_t a = 0;
    uint8_t b = 0;

    if (delta > 1)
    {
        if (value < fx.CHANCE_PROB_MAX)
            return static_cast<uint8_t>(std::min<int>(value + delta, fx.CHANCE_PROB_MAX));
        if (value == fx.CHANCE_PROB_MAX)
            return fx.CHANCE_GROUP_WITH_LAST;
        if (value == fx.CHANCE_GROUP_WITH_LAST)
            return fx.CHANCE_NOT_LAST;
        if (value == fx.CHANCE_NOT_LAST)
            return fx.CHANCE_PLAY_SKIP_START;
        if (value == fx.CHANCE_FIRST || value == fx.CHANCE_NOT_FIRST)
            return fx.CHANCE_NOT_FIRST;
        if (fx.chanceDecodeAB(value, a, b))
        {
            // first value of the next B group
            return (b < fx.CHANCE_AB_MAX_B) ? fx.chanceEncodeAB(1, static_cast<uint8_t>(b + 1))
                                            : static_cast<uint8_t>(fx.CHANCE_FIRST);
        }

        int next = value + fx.CHANCE_CYCLE_STEPS;
        return static_cast<uint8_t>(next > fx.CHANCE_SKIP_PLAY_END ? fx.CHANCE_AB_START : next);
    }

    if (value <= fx.CHANCE_PROB_MAX)
        return static_cast<uint8_t>(std::max<int>(value + delta, 0));
    if (value == fx.CHANCE_GROUP_WITH_LAST)
        return fx.CHANCE_PROB_MAX;
    if (value == fx.CHANCE_NOT_LAST)
        return fx.CHANCE_GROUP_WITH_LAST;
    if (value == fx.CHANCE_NOT_FIRST)
        return fx.CHANCE_FIRST;
    if (value == fx.CHANCE_FIRST)
        return fx.chanceEncodeAB(1, fx.CHANCE_AB_MAX_B);
    if (fx.chanceDecodeAB(value, a, b))
    {
        if (a > 1)
            return fx.chanceEncodeAB(1, b); // start of this B group
        if (b > fx.CHANCE_AB_MIN_B)
            return fx.chanceEncodeAB(1, static_cast<uint8_t>(b - 1));
        return fx.CHANCE_SKIP_PLAY_END;
    }
    if (value < fx.CHANCE_PLAY_SKIP_START + fx.CHANCE_CYCLE_STEPS)
        return fx.CHANCE_NOT_LAST;

    return static_cast<uint8_t>(value - fx.CHANCE_CYCLE_STEPS);
}

void Sequencer::changeSelectionFxValue(uint8_t fxIndex, int16_t value)
{

    strSelection* sel = &selection;
    if (!isSelectionCorrect(sel))
        return;
    if (fxIndex > FX_SLOTS_MAX)
        return;

    bool somethingWasChanged = false;
    newFileManager.storePatternUndoRevision();

    strPattern::strTrack::strStep::strFx* _fx;

    for (uint8_t t = sel->firstTrack; t <= sel->lastTrack; t++)
    {
        for (uint16_t s = sel->firstStep, offset = 0; s <= sel->lastStep; s++, offset++)
        {

            _fx = &currentPattern->track[t].step[s].fx[fxIndex];

            if (_fx->type == fx.FX_TYPE_CHANCE && (value > 1 || value < -1))
            {
                _fx->value = chanceCoarseStep(_fx->value, value);
            }
            else
            {
                _fx->value =
                  clamp<int>(_fx->value + value, getFxMin(_fx->type), getFxMax(_fx->type));
            }

            somethingWasChanged = true;

            if (!isMultiSelection() && _fx->type == 0)
            {
                _fx->type = interfaceGlobals.fxNameToId(patternEditor.getPreviousUsedFx());
                _fx->value = patternEditor.getPreviousUsedFxValue();
            }

            patternEditor.setPreviousUsedFxValue(_fx->value);
            patternEditor.setPreviousUsedFx(interfaceGlobals.fxIdToName(_fx->type));
        }
    }

    if (somethingWasChanged)
    {
        newFileManager.setActualPatternStructChanged();
        if (!isPlay())
        {
            playSelection();
        }
    }
}
void Sequencer::setSelectionFxValueByPad(uint8_t fxIndex, int16_t pad)
{

    strSelection* sel = &selection;
    if (!isSelectionCorrect(sel))
        return;
    if (fxIndex > FX_SLOTS_MAX)
        return;

    newFileManager.storePatternUndoRevision();
    bool sometinghWasChanged = false;

    strPattern::strTrack::strStep::strFx* _fx;

    for (uint8_t t = sel->firstTrack; t <= sel->lastTrack; t++)
    {
        for (uint16_t s = sel->firstStep, offset = 0; s <= sel->lastStep; s++, offset++)
        {
            _fx = &currentPattern->track[t].step[s].fx[fxIndex];

            if (!isMultiSelection() && _fx->type == 0)
            {
                _fx->type = interfaceGlobals.fxNameToId(patternEditor.getPreviousUsedFx());
            }

            int lastHandledPad = std::min(47, getFxMax(_fx->type) - getFxMin(_fx->type));
            _fx->value = roundf(
              mapClamped<float>(pad, 0, lastHandledPad, getFxMin(_fx->type), getFxMax(_fx->type)));
            patternEditor.setPreviousUsedFxValue(_fx->value);

            sometinghWasChanged = true;
        }
    }

    if (sometinghWasChanged)
    {
        newFileManager.setActualPatternStructChanged();
        if (!isPlay())
        {
            playSelection();
        }
    }
}

void Sequencer::changeSelectionMicromove(int16_t value)
{
    strSelection* sel = &selection;
    if (!isSelectionCorrect(sel))
        return;

    bool somethingWasChanged = false;
    newFileManager.storePatternUndoRevision();

    for (uint8_t t = sel->firstTrack; t <= sel->lastTrack; t++)
    {
        for (uint16_t s = sel->firstStep; s <= sel->lastStep; s++)
        {
            strPattern::strTrack::strStep* step = &currentPattern->track[t].step[s];

            if (step->micromove >= 0)
            {
                step->micromove = clamp<int>(step->micromove + value, 0, 100);
                somethingWasChanged = true;
            }
            else if (!isMultiSelection())
            {
                step->micromove = clamp<int>(value > 0 ? value : 0, 0, 100);
                somethingWasChanged = true;
            }
        }
    }

    if (somethingWasChanged)
    {
        newFileManager.setActualPatternStructChanged();
        if (!isPlay())
        {
            playSelection();
        }
    }
}

void Sequencer::setSelectionMicromoveByPad(int16_t pad)
{
    strSelection* sel = &selection;
    if (!isSelectionCorrect(sel))
        return;

    newFileManager.storePatternUndoRevision();
    bool somethingWasChanged = false;

    for (uint8_t t = sel->firstTrack; t <= sel->lastTrack; t++)
    {
        for (uint16_t s = sel->firstStep; s <= sel->lastStep; s++)
        {
            strPattern::strTrack::strStep* step = &currentPattern->track[t].step[s];
            step->micromove = roundf(mapClamped<float>(pad, 0, 47, 0, 100));
            somethingWasChanged = true;
        }
    }

    if (somethingWasChanged)
    {
        newFileManager.setActualPatternStructChanged();
        if (!isPlay())
        {
            playSelection();
        }
    }
}

void Sequencer::changeSelectionVolume(int16_t value)
{
    strSelection* sel = &selection;
    if (!isSelectionCorrect(sel))
        return;

    bool somethingWasChanged = false;
    newFileManager.storePatternUndoRevision();

    for (uint8_t t = sel->firstTrack; t <= sel->lastTrack; t++)
    {
        for (uint16_t s = sel->firstStep; s <= sel->lastStep; s++)
        {
            strPattern::strTrack::strStep* step = &currentPattern->track[t].step[s];

            if (step->volume >= 0)
            {
                step->volume = clamp<int>(step->volume + value, 0, 100);
                somethingWasChanged = true;
            }
            else if (!isMultiSelection())
            {
                int baseVol = step->note >= 0 ? getInstrumentVolume(step->instrument) : 50;
                step->volume = clamp<int>(baseVol + value, 0, 100);
                somethingWasChanged = true;
            }
        }
    }

    if (somethingWasChanged)
    {
        newFileManager.setActualPatternStructChanged();
        if (!isPlay())
        {
            playSelection();
        }
    }
}

void Sequencer::setSelectionVolumeByPad(int16_t pad)
{
    strSelection* sel = &selection;
    if (!isSelectionCorrect(sel))
        return;

    newFileManager.storePatternUndoRevision();
    bool somethingWasChanged = false;

    for (uint8_t t = sel->firstTrack; t <= sel->lastTrack; t++)
    {
        for (uint16_t s = sel->firstStep; s <= sel->lastStep; s++)
        {
            strPattern::strTrack::strStep* step = &currentPattern->track[t].step[s];
            step->volume = roundf(mapClamped<float>(pad, 0, 47, 0, 100));
            somethingWasChanged = true;
        }
    }

    if (somethingWasChanged)
    {
        newFileManager.setActualPatternStructChanged();
        if (!isPlay())
        {
            playSelection();
        }
    }
}
void Sequencer::changeSelectionFxType(uint8_t index, int16_t value)
{

    value = clamp<int>(value, -1, 1);

    strSelection* sel = &selection;
    if (!isSelectionCorrect(sel))
        return;
    if (index > FX_SLOTS_MAX)
        return;

    bool somethingWasChanged = false;

    newFileManager.storePatternUndoRevision();

    strPattern::strTrack::strStep::strFx* _fx;

    for (uint8_t t = sel->firstTrack; t <= sel->lastTrack; t++)
    {
        for (uint16_t s = sel->firstStep, offset = 0; s <= sel->lastStep; s++, offset++)
        {
            _fx = &currentPattern->track[t].step[s].fx[index];

            if (_fx->type != fx.FX_TYPE_NONE)
            {
                uint8_t name = interfaceGlobals.fxIdToName(_fx->type);
                name += value;

                uint8_t previousType = _fx->type;

                _fx->type = clamp<int>(interfaceGlobals.fxNameToId(name), 0, 47);

                if (previousType != _fx->type && !isMultiSelection())
                {
                    _fx->value = getFxDefault(_fx->type);
                }
                else
                {
                    _fx->value = clamp<int>(_fx->value, getFxMin(_fx->type), getFxMax(_fx->type));
                }

                patternEditor.setPreviousUsedFxValue(_fx->value);
                patternEditor.setPreviousUsedFx(interfaceGlobals.fxIdToName(_fx->type));

                somethingWasChanged = true;
            }
        }
    }

    if (somethingWasChanged)
        newFileManager.setActualPatternStructChanged();
}
void Sequencer::setSelectionFxType(uint8_t fxSlot, int16_t value)
{

    strSelection* sel = &selection;
    if (!isSelectionCorrect(sel))
        return;
    if (fxSlot > FX_SLOTS_MAX)
        return;

    newFileManager.storePatternUndoRevision();

    strPattern::strTrack::strStep* step;

    for (uint8_t t = sel->firstTrack; t <= sel->lastTrack; t++)
    {
        for (uint16_t s = sel->firstStep, offset = 0; s <= sel->lastStep; s++, offset++)
        {
            step = &currentPattern->track[t].step[s];

            step->fx[fxSlot].type = value;

            if (step->fx[fxSlot].type != patternEditor.getPreviousUsedFx() && !isMultiSelection())
            {
                step->fx[fxSlot].value = getFxDefault(step->fx[fxSlot].type);
            }
            else
            {
                step->fx[fxSlot].value = clamp<int>(step->fx[fxSlot].value,
                                                    getFxMin(step->fx[fxSlot].type),
                                                    getFxMax(step->fx[fxSlot].type));
            }

            patternEditor.setPreviousUsedFxValue(step->fx[fxSlot].value);
            patternEditor.setPreviousUsedFx(interfaceGlobals.fxIdToName(step->fx[fxSlot].type));
        }
    }

    newFileManager.setActualPatternStructChanged();
}

uint8_t Sequencer::changeInstrumentInSpecificOrder(int8_t actualValue,
                                                   int16_t delta,
                                                   bool midiTypeOnly)
{
    int instrumentCount =
      midiTypeOnly ? kTotalInstrumentsCount - kSampleInstrumentsCount : kTotalInstrumentsCount;

    if (midiTypeOnly && delta == 0 && isSampleInstrument(actualValue))
    {
        return MIDI_INSTRUMENTS_START_IDX;
    }

    uint8_t uiChangeInstrumentOrder[instrumentCount];
    [[maybe_unused]] bool initArray = [&]() -> bool
    {
        int pos = 0;
        for (pos = 0; pos < kMidiInstrumentsCount; pos++)
        {
            uiChangeInstrumentOrder[pos] =
              kSampleInstrumentsCount + kMidiInstrumentsCount - pos - 1;
        }

#ifdef FEATURE_SYNTH
        int synthInstrIdx = kSampleInstrumentsCount + kMidiInstrumentsCount;
        for (; pos < kMidiInstrumentsCount + kSynthInstrumentsCount; pos++, synthInstrIdx++)
        {
            uiChangeInstrumentOrder[pos] = synthInstrIdx;
        }
#endif

        int sampleInstrIdx = 0;
        for (; pos < instrumentCount; pos++, sampleInstrIdx++)
        {
            uiChangeInstrumentOrder[pos] = sampleInstrIdx;
        }
        return true;
    }();

    int instrumentPosInOrder = 0;
    for (uint8_t a = 0; a < instrumentCount; a++)
    {
        if (uiChangeInstrumentOrder[a] == actualValue)
        {
            instrumentPosInOrder = a;
            break;
        }
    }

    instrumentPosInOrder = clamp<int>(instrumentPosInOrder + delta, 0, instrumentCount - 1);

    return uiChangeInstrumentOrder[instrumentPosInOrder];
}

void Sequencer::changeSelectionInstrument(int16_t value)
{

    strSelection* sel = &selection;
    if (!isSelectionCorrect(sel))
        return;

    newFileManager.storePatternUndoRevision();

    bool somethingWasChanged = false;

    strPattern::strTrack::strStep* step;

    for (uint8_t trackIdx = sel->firstTrack; trackIdx <= sel->lastTrack; trackIdx++)
    {
        for (uint16_t s = sel->firstStep, offset = 0; s <= sel->lastStep; s++, offset++)
        {
            step = &currentPattern->track[trackIdx].step[s];

            if (isSingleSelection(sel))
            {
                if (step->note >= 0)
                {
                    step->instrument = changeInstrumentInSpecificOrder(
                      step->instrument, value, !isSampleTrack(trackIdx));

                    mtProject.values.lastUsedInstrument = step->instrument;

                    somethingWasChanged = true;
                }
                else if (step->note == STEP_NOTE_EMPTY)
                {
                    step->note = STEP_NOTE_DEFAULT;

                    step->instrument = changeInstrumentInSpecificOrder(
                      mtProject.values.lastUsedInstrument, 0, !isSampleTrack(trackIdx));

                    mtProject.values.lastUsedInstrument = step->instrument;

                    somethingWasChanged = true;
                }
            }
            else
            {
                if (step->note >= 0)
                {
                    step->instrument = changeInstrumentInSpecificOrder(
                      step->instrument, value, !isSampleTrack(trackIdx));
                    somethingWasChanged = true;
                }
            }
        }
    }

    if (somethingWasChanged)
    {
        newFileManager.setActualPatternStructChanged();
        if (!isPlay())
        {
            playSelection();
        }
    }
}
void Sequencer::setSelectionInstrument(int16_t value)
{
    assert(value < kTotalInstrumentsCount);

    strSelection* sel = &selection;
    bool isSingleSel = isSingleSelection(sel);
    if (!isSelectionCorrect(sel))
        return;

    newFileManager.storePatternUndoRevision();

    bool somethingWasChanged = false;

    strPattern::strTrack::strStep* step;

    for (int trackIdx = sel->firstTrack; trackIdx <= sel->lastTrack; trackIdx++)
    {
        for (int stepIdx = sel->firstStep, offset = 0; stepIdx <= sel->lastStep;
             stepIdx++, offset++)
        {
            step = &currentPattern->track[trackIdx].step[stepIdx];

            if (isSingleSel)
            {
                if (isSampleInstrument(value))
                {
                    if (step->isNoteEmpty())
                    {
                        step->note = STEP_NOTE_DEFAULT;
                        somethingWasChanged = true;
                    }

                    if (step->note >= 0)
                    {
                        step->instrument =
                          changeInstrumentInSpecificOrder(value, 0, !isSampleTrack(trackIdx));
                        somethingWasChanged = true;

                        mtProject.values.lastUsedInstrument = step->instrument;
                    }
                }
                else
                {
                    if (step->note >= -1)
                    {
                        step->instrument =
                          changeInstrumentInSpecificOrder(value, 0, !isSampleTrack(trackIdx));
                        somethingWasChanged = true;

                        mtProject.values.lastUsedInstrument = step->instrument;
                    }
                }
            }
            else
            {
                if (step->note >= 0)
                {
                    step->instrument =
                      changeInstrumentInSpecificOrder(value, 0, !isSampleTrack(trackIdx));
                    somethingWasChanged = true;
                }
            }
        }
    }

    if (somethingWasChanged)
    {
        newFileManager.setActualPatternStructChanged();
        if (!isPlay())
        {
            playSelection();
        }
    }
}

void Sequencer::setSelectionNote(int16_t value)
{
    value = isOutOfRange<int>(value, (int)STEP_NOTE_MIN, (int)STEP_NOTE_MAX)
              ? (int)STEP_NOTE_DEFAULT
              : value;

    strSelection* sel = &selection;
    if (!isSelectionCorrect(sel))
        return;

    bool somethingWasChanged = false;
    newFileManager.storePatternUndoRevision();

    strPattern::strTrack::strStep* step;

    for (uint8_t t = sel->firstTrack; t <= sel->lastTrack; t++)
    {
        for (uint16_t s = sel->firstStep; s <= sel->lastStep; s++)
        {
            step = &currentPattern->track[t].step[s];

            if (isSingleSelection(sel))
            {
                if (step->isNoteEmpty())
                {
                    step->instrument = changeInstrumentInSpecificOrder(
                      mtProject.values.lastUsedInstrument, 0, !isSampleTrack(t));
                    mtProject.values.lastUsedInstrument = step->instrument;
                }
                step->note = value;

                somethingWasChanged = true;
            }
            else
            {
                if (step->note != STEP_NOTE_EMPTY)
                {
                    step->note = value;
                    somethingWasChanged = true;
                }
            }
        }
    }

    if (somethingWasChanged)
    {
        newFileManager.setActualPatternStructChanged();
        if (!isPlay())
        {
            playSelection();
        }
    }
}

void Sequencer::clearStep(strPattern::strTrack::strStep* step)
{
    clearStep(step, ELEMENTS_ALL_NO_PREFERENCES);
}

void Sequencer::clearStep(strPattern::strTrack::strStep* step, uint8_t elements)
{
    switch (elements)
    {
        case ELEMENTS_ALL_NO_PREFERENCES:
            step->note = STEP_NOTE_EMPTY;
            step->instrument = 0;
            step->fx[0].type = 0;
            step->fx[1].type = 0;
            step->volume = STEP_VELO_DEFAULT;
            step->micromove = -1;
            break;
        case ELEMENTS_FX1: step->fx[0].type = 0; break;
        case ELEMENTS_FX2: step->fx[1].type = 0; break;
        case ELEMENTS_MICROMOVE: step->micromove = -1; break;
        case ELEMENTS_VOLUME: step->volume = STEP_VELO_DEFAULT; break;
        case ELEMENTS_INSTRUMENTS:
            step->note = STEP_NOTE_EMPTY;
            step->instrument = 0;
            break;
        case ELEMENTS_NOTES:
            if (isSampleInstrument(step->instrument) || step->note < STEP_NOTE_EMPTY)
            {
                step->instrument = 0;
                step->fx[0].type = 0;
                step->fx[1].type = 0;
                step->volume = STEP_VELO_DEFAULT;
                step->micromove = -1;
            }
            step->note = STEP_NOTE_EMPTY;
            break;
        default: break;
    }
}

void Sequencer::clearSelected()
{
    clearSelected(&selection, 0);
}
void Sequencer::clearSelected(uint8_t elements)
{
    clearSelected(&selection, elements);
}
void Sequencer::clearSelected(strSelection* selection, uint8_t elements)
{
    if (!isSelectionCorrect(selection))
        return;

    for (uint8_t t = selection->firstTrack; t <= selection->lastTrack; t++)
    {
        stopOpenNoteOnTrack(t);

        for (uint16_t s = selection->firstStep; s <= selection->lastStep; s++)
        {
            clearStep(&currentPattern->track[t].step[s], elements);
        }
    }
}

void Sequencer::setPatternHeader(strPattern* patt)
{
    patt->header.id_file[0] = 'K';
    patt->header.id_file[1] = 'S';

    patt->header.fwVersion[0] = CORE_VER_1;
    patt->header.fwVersion[1] = CORE_VER_2;
    patt->header.fwVersion[2] = CORE_VER_3;
    patt->header.fwVersion[3] = CORE_VER_1;

    patt->header.type = fileTypePattern;

    patt->header.fileStructureVersion[0] = PATTERN_FILE_VERSION;
    patt->header.fileStructureVersion[1] = PATTERN_FILE_VERSION;
    patt->header.fileStructureVersion[2] = PATTERN_FILE_VERSION;
    patt->header.fileStructureVersion[3] = PATTERN_FILE_VERSION;

    patt->header.size = PatternFileValidation::headerSizeField(PATTERN_FILE_VERSION, sizeof(strPattern));
}

void Sequencer::clearPattern(strPattern* patt)
{

    patt->swingRez = 0.0;
    patt->rezerwa1 = 0;
    patt->rezerwa2 = 0;
    patt->rezerwa3 = 0;
    patt->rezerwa4 = 0;

    setPatternHeader(patt);

    for (strPattern::strTrack& track : patt->track)
    {
        track.length = DEFAULT_PATTERN_LENGTH - 1;

        for (strPattern::strTrack::strStep& step : track.step)
        {
            clearStep(&step);
        }
    }
}

void Sequencer::clearSingleTrack(strPattern::strTrack* track)
{
    for (uint16_t i = 0; i <= MAXSTEP; i++)
    {
        clearStep(&track->step[i]);
    }
}

void Sequencer::insert(strSelection* selection)
{
    if (!isSelectionCorrect(selection))
        return;

    for (uint8_t t = selection->firstTrack; t <= selection->lastTrack; t++)
    {
        for (uint16_t s = MAXSTEP; s > selection->firstStep; s--)
        {
            currentPattern->track[t].step[s] = currentPattern->track[t].step[s - 1];
        }
        clearStep(&currentPattern->track[t].step[selection->firstStep],
                  ELEMENTS_ALL_NO_PREFERENCES);
    }
}

void Sequencer::insertReversed(strSelection* selection)
{
    if (!isSelectionCorrect(selection))
        return;

    for (uint8_t t = selection->firstTrack; t <= selection->lastTrack; t++)
    {
        for (uint16_t s = MINSTEP; s < selection->firstStep; s++)
        {
            currentPattern->track[t].step[s] = currentPattern->track[t].step[s + 1];
        }
        clearStep(&currentPattern->track[t].step[selection->firstStep],
                  ELEMENTS_ALL_NO_PREFERENCES);
    }
}

// returns true if executed
bool Sequencer::backspace()
{
    if (!isSelectionCorrect(&selection))
        return false;

    if (selection.firstStep < 1)
        return false;

    strPattern::strTrack::strStep *stepFrom, *stepTo;

    for (uint8_t t = selection.firstTrack; t <= selection.lastTrack; t++)
    {
        for (uint16_t s = selection.firstStep - 1; s < currentPattern->track[0].length; s++)
        {
            stepFrom = &currentPattern->track[t].step[s + 1];
            stepTo = &currentPattern->track[t].step[s];

            *stepTo = *stepFrom;
        }

        clearStep(&currentPattern->track[t].step[currentPattern->track[0].length],
                  ELEMENTS_ALL_NO_PREFERENCES);
    }

    return true;
}
void Sequencer::copyToBuffer()
{
    copySelectionToBuffer(&sequencer.copySelection, &sequencer.pasteSelection);
}
void Sequencer::pasteFromBuffer(uint8_t elements)
{
    pasteSelectionFromBuffer(&sequencer.copySelection, &sequencer.pasteSelection, elements);
}

void Sequencer::clearCopyPasteBuffer()
{
    clearPattern(&copyPastePattern);
}

void Sequencer::copySelectionToBuffer(strSelection* from, strSelection* to)
{
    // zbedne w przypadku kopiowania calego patternu
    // ale tez nie powinno przeszkadzac
    if (!isSelectionCorrect(from))
        return;

    copyPastePattern = *currentPattern;
}
void Sequencer::copyStep(strPattern::strTrack::strStep* srcStep,
                         strPattern::strTrack::strStep* destStep,
                         bool toMidi,
                         int elements)
{

    bool pasteNote = elements == ELEMENTS_ALL_NO_PREFERENCES ||
                     elements == ELEMENTS_ALL_WITH_PREFERENCES || elements == ELEMENTS_NOTES;

    bool pasteInstruments = elements == ELEMENTS_ALL_NO_PREFERENCES ||
                            elements == ELEMENTS_ALL_WITH_PREFERENCES ||
                            elements == ELEMENTS_INSTRUMENTS;

    bool pasteFx1 = elements == ELEMENTS_ALL_NO_PREFERENCES ||
                    elements == ELEMENTS_ALL_WITH_PREFERENCES || elements == ELEMENTS_FX1;

    bool pasteFx2 = elements == ELEMENTS_ALL_NO_PREFERENCES ||
                    elements == ELEMENTS_ALL_WITH_PREFERENCES || elements == ELEMENTS_FX2;

    bool pasteMicromove = elements == ELEMENTS_ALL_NO_PREFERENCES ||
                          elements == ELEMENTS_ALL_WITH_PREFERENCES ||
                          elements == ELEMENTS_MICROMOVE;

    bool pasteVolume = elements == ELEMENTS_ALL_NO_PREFERENCES ||
                       elements == ELEMENTS_ALL_WITH_PREFERENCES || elements == ELEMENTS_VOLUME;

    clearStep(destStep);

    if (pasteNote)
    {
        destStep->note = srcStep->note;
        destStep->instrument = mtProject.values.lastUsedInstrument;
    }

    if (pasteInstruments)
    {
        destStep->instrument = srcStep->instrument;
    }

    if (pasteFx1)
    {
        destStep->fx[0] = srcStep->fx[0];
    }

    if (pasteFx2)
    {
        destStep->fx[1] = srcStep->fx[1];
    }

    if (pasteMicromove)
    {
        destStep->micromove = srcStep->micromove;
    }

    if (pasteVolume)
    {
        destStep->volume = srcStep->volume;
    }

    if (toMidi && isSampleInstrument(destStep->instrument))
    {
        if (!destStep->isNoteOff())
        {
            destStep->note = STEP_NOTE_EMPTY;
        }

        destStep->instrument = 0;
    }
}

void Sequencer::pasteSelectionFromBuffer(strSelection* from, strSelection* to, uint8_t elements)
{
    if (!isSelectionCorrect(from))
        return;
    if (!isSelectionCorrect(to))
        return;

    if (elements == ELEMENTS_ALL_WITH_PREFERENCES)
    {
        *currentPattern = copyPastePattern;
        return;
    }

    const int tracksToCopy = from->lastTrack - from->firstTrack + 1;
    const int rowsToCopy = from->lastStep - from->firstStep + 1;

    for (int destTrackIdx = to->firstTrack, trackOffset = 0;
         destTrackIdx <= to->firstTrack + tracksToCopy - 1;
         destTrackIdx++, trackOffset++)
    {
        for (int destStepIdx = to->firstStep, stepOffset = 0;
             destStepIdx <= to->firstStep + rowsToCopy - 1;
             destStepIdx++, stepOffset++)
        {
            int srcTrackIdx, srcStepIdx;
            srcTrackIdx = from->firstTrack + trackOffset;
            srcStepIdx = from->firstStep + stepOffset;

            // czy step istnieje?
            if (srcStepIdx <= MAXSTEP && srcTrackIdx < kAllTracks && destStepIdx <= MAXSTEP &&
                destTrackIdx < kAllTracks && destStepIdx <= currentPattern->track[0].length)
            {
                Sequencer::strPattern::strTrack::strStep *srcStep, *destStep;

                srcStep = &copyPastePattern.track[srcTrackIdx].step[srcStepIdx];
                destStep = &currentPattern->track[destTrackIdx].step[destStepIdx];

                bool toMidiTrack = !isSampleTrack(destTrackIdx);
                copyStep(srcStep, destStep, toMidiTrack, elements);
            }
        }
    }
}

bool Sequencer::isSelectionCorrect(strSelection* selection)
{
    if (selection->firstStep > selection->lastStep ||
        selection->firstTrack > selection->lastTrack || selection->firstStep > MAXSTEP ||
        selection->firstTrack > kAllTracks - 1 || selection->lastStep > MAXSTEP ||
        selection->lastTrack > kAllTracks - 1)
    {
        return 0;
    }
    else
    {
        return 1;
    }
}

bool Sequencer::isSingleSelection(strSelection* selection)
{
    if (selection->firstStep == selection->lastStep &&
        selection->firstTrack == selection->lastTrack && isSelectionCorrect(selection))

    {
        return 1;
    }
    else
    {
        return 0;
    }
}

void Sequencer::setCopySelection(uint16_t stepFrom,
                                 uint8_t trackFrom,
                                 uint16_t stepTo,
                                 uint8_t trackTo)
{
    if (stepTo < stepFrom)
    {
        copySelection.firstStep = stepTo;
        copySelection.lastStep = stepFrom;
    }
    else
    {
        copySelection.firstStep = stepFrom;
        copySelection.lastStep = stepTo;
    }

    if (trackTo < trackFrom)
    {
        copySelection.firstTrack = trackTo;
        copySelection.lastTrack = trackFrom;
    }
    else
    {
        copySelection.firstTrack = trackFrom;
        copySelection.lastTrack = trackTo;
    }
}
uint16_t Sequencer::getCopySelectionHeight()
{
    if (isSelectionCorrect(&copySelection))
    {
        return copySelection.lastStep - copySelection.firstStep + 1;
    }
    else
    {
        return 0;
    }
}

int8_t Sequencer::getSelectionFirstNote()
{
    int8_t retNote = getActualPattern()->track[selection.firstTrack].step[selection.firstStep].note;

    if (retNote < 0 || !isSelectionCorrect(&selection))
    {
        retNote = STEP_NOTE_DEFAULT;
    }
    return retNote;
}

void Sequencer::setSelection(uint16_t stepFrom, uint8_t trackFrom, uint16_t stepTo, uint8_t trackTo)
{
    if (stepTo < stepFrom)
    {
        selection.firstStep = stepTo;
        selection.lastStep = stepFrom;
    }
    else
    {
        selection.firstStep = stepFrom;
        selection.lastStep = stepTo;
    }

    if (trackTo < trackFrom)
    {
        selection.firstTrack = trackTo;
        selection.lastTrack = trackFrom;
    }
    else
    {
        selection.firstTrack = trackFrom;
        selection.lastTrack = trackTo;
    }
}

void Sequencer::setPasteSelection(uint16_t stepFrom,
                                  uint8_t trackFrom,
                                  uint16_t stepTo,
                                  uint8_t trackTo)
{
    if (stepTo < stepFrom)
    {
        pasteSelection.firstStep = stepTo;
        pasteSelection.lastStep = stepFrom;
    }
    else
    {
        pasteSelection.firstStep = stepFrom;
        pasteSelection.lastStep = stepTo;
    }

    if (trackTo < trackFrom)
    {
        pasteSelection.firstTrack = trackTo;
        pasteSelection.lastTrack = trackFrom;
    }
    else
    {
        pasteSelection.firstTrack = trackFrom;
        pasteSelection.lastTrack = trackTo;
    }
}
//@formatter:off
AUDIO_CORE_RAM int16_t Sequencer::getFxMax(uint8_t fxID, bool forRandom)
{
    switch (fxID)
    {
        case fx.FX_TYPE_VOLUME: return 100;
        case fx.FX_TYPE_PANNING: return 100;
        case fx.FX_TYPE_MICROTUNING: return 198;
        case fx.FX_TYPE_CHANCE: return forRandom ? fx.CHANCE_PROB_MAX : fx.CHANCE_MAX;

        case fx.FX_TYPE_BREAK_PATTERN: return 1;

        case fx.FX_TYPE_MICROMOVE: return 100;
        case fx.FX_TYPE_TEMPO: return MAX_TEMPO_FX;
        case fx.FX_TYPE_SWING: return 75;
        case fx.FX_TYPE_OFF: return 0;
        case fx.FX_TYPE_RANDOM_INSTRUMENT: return 47;
        case fx.FX_TYPE_RANDOM_VALUE: return 255;
        case fx.FX_TYPE_SAMPLE_SLICE: return 47;
        case fx.FX_TYPE_REVERSE_PLAYBACK: return 1;
        case fx.FX_TYPE_ROLL: return (fx.ROLL_PERIOD_MAX + 1) * (fx.rollNoteDir_max + 1) - 1;

        case fx.FX_TYPE_SEND_CC_A:
        case fx.FX_TYPE_SEND_CC_B:
        case fx.FX_TYPE_SEND_CC_C:
        case fx.FX_TYPE_SEND_CC_D:
        case fx.FX_TYPE_SEND_CC_E:
        case fx.FX_TYPE_SEND_CC_F: return 127;

        case fx.FX_TYPE_FILTER_LFO:
        case fx.FX_TYPE_PANNING_LFO:
        case fx.FX_TYPE_FINETUNE_LFO:
        case fx.FX_TYPE_POSITION_LFO: return CONSTRAIN_SPEED_OTHER_ENVELOPE;
        case fx.FX_TYPE_VOLUME_LFO: return CONSTRAIN_SPEED_AMP_ENVELOPE;

        case fx.FX_TYPE_ARP: return 33;
        case fx.FX_TYPE_MIDI_CHORD: return MAX_CHORD;

        case fx.FX_TYPE_BIT_DEPTH: return BIT_DEPTH_MAX;
        case fx.FX_TYPE_TUNE: return 48;
        case fx.FX_TYPE_SLIDE_UP:
        case fx.FX_TYPE_SLIDE_DOWN:
        case fx.FX_TYPE_AMP_ENV:
        case fx.FX_TYPE_FILTER_ENV: return 255;
        default: return 100;
    }
}

AUDIO_CORE_RAM int16_t Sequencer::getFxMin(uint8_t fxID, bool forRandom)
{
    switch (fxID)
    {
        case fx.FX_TYPE_MICROMOVE: return 0;
        case fx.FX_TYPE_SAMPLE_SLICE: return 0;
        case fx.FX_TYPE_TEMPO:
        {
            return forRandom ? MIN_TEMPO_FX : TEMPO_FX_STP;
        }
        case fx.FX_TYPE_SWING: return 25;
        case fx.FX_TYPE_PANNING: return 0;
        case fx.FX_TYPE_BREAK_PATTERN: return 1;
        case fx.FX_TYPE_ROLL: return fx.ROLL_PERIOD_MIN;
        case fx.FX_TYPE_MIDI_CHORD: return MIN_CHORD;

        case fx.FX_TYPE_BIT_DEPTH: return BIT_DEPTH_MIN;
        default: return 0;
    }
}
int16_t Sequencer::getFxDefault(uint8_t fxID)
{
    switch (fxID)
    {
        case fx.FX_TYPE_OFF: return 0;
        case fx.FX_TYPE_TEMPO: return 60;
        case fx.FX_TYPE_SWING: return 50;
        case fx.FX_TYPE_ROLL: return 1;
        case fx.FX_TYPE_BREAK_PATTERN: return 1;
        case fx.FX_TYPE_PANNING: return 50;
        case fx.FX_TYPE_MICROTUNING: return 99;
        case fx.FX_TYPE_MIDI_CHORD: return MIN_CHORD;

        default: return 0;
    }
}
//@formatter:on

int16_t Sequencer::rollValueToPeriod(int16_t value)
{
    switch (value)
    {
        case fx.ROLL_PERIOD_1_1: return 1;
        case fx.ROLL_PERIOD_1_2: return 2;
        case fx.ROLL_PERIOD_1_3: return 3;
        case fx.ROLL_PERIOD_1_4: return 4;
        case fx.ROLL_PERIOD_1_6: return 6;
        case fx.ROLL_PERIOD_1_8: return 8;
        case fx.ROLL_PERIOD_1_12: return 12;
        case fx.ROLL_PERIOD_1_16: return 16;
        default: return 0;
    }
}

AUDIO_CORE_RAM int16_t Sequencer::getFxValueCorrection(uint8_t type, uint8_t value)
{

    switch (type)
    {
        case fx.FX_TYPE_TEMPO: return value * 2; break;
        case fx.FX_TYPE_PANNING: return value - 50; break;
        case fx.FX_TYPE_MICROTUNING: return value - 99; break;
        case fx.FX_TYPE_TUNE: return value - 24; break;
        case fx.FX_TYPE_SAMPLE_SLICE: // jawnie od 1, programowo od 0
            return value + 1;
            break;

        default: return value;
    }
}
const uint8_t MAX_LFOSPEED_LABELS_AMP = ENVELOPE_SPEED_AMP_COUNT;
const uint8_t MAX_LFOSPEED_LABELS = ENVELOPE_SPEED_OTHER_COUNT;
const char seqlfoSpeedLabels_ampOnly[MAX_LFOSPEED_LABELS_AMP + 1][4] = {
    " 24", " 16", " 12", "  8", "  6", "  4", "  3", "  2", "3/2", "  1", "3/4", "1/2", "3/8",
    "1/3", "1/4", "316", "1/6", "1/8", "/12", "/16", "/24", "/32", "/48", "/64", "???"
};
const char seqlfoSpeedLabels[MAX_LFOSPEED_LABELS + 1][4] = {
    "128", " 96", " 64", " 48", " 32", " 24", " 16", " 12", "  8", "  6",
    "  4", "  3", "  2", "3/2", "  1", "3/4", "1/2", "3/8", "1/3", "1/4",
    "316", "1/6", "1/8", "/12", "/16", "/24", "/32", "/48", "/64", "???"
};
const uint8_t arpFxLabels_count = 34;
const char arpFxLabels[arpFxLabels_count][4] = {
    "  0",  "/ 8",  "/ 6",  "/ 4",  "/ 3",  "/ 2",  "/ 1",  "/.2",  "/.3",  "/.4",  "/.6",  "/.8",
    "\\ 8", "\\ 6", "\\ 4", "\\ 3", "\\ 2", "\\ 1", "\\.2", "\\.3", "\\.4", "\\.6", "\\.8", "R 8",
    "R 6",  "R 4",  "R 3",  "R 2",  "R 1",  "R.2",  "R.3",  "R.4",  "R.6",  "R.8",
};

void Sequencer::makeFxValLabelForSeqStep(char* ptr,
                                         int len,
                                         uint8_t fxID,
                                         uint8_t track,
                                         uint16_t step)
{
    assert(track < kAllTracks);

    if (step > MAXSTEP)
        return;

    strPattern::strTrack::strStep* actualStep = &currentPattern->track[track].step[step];

    makeFxValLabelWithAlgin(
      ptr, len, actualStep->fx[fxID].type, actualStep->fx[fxID].value, fxValLabelAlign_right);
}

const char* skipSpaceOnStart(const char* str)
{
    while (*str != 0 && isspace(*str))
    {
        str++;
    }
    return str;
}

void Sequencer::makeFxValLabelWithAlgin(char* ptr,
                                        int len,
                                        uint8_t fxType,
                                        uint8_t valueFromFx,
                                        enFxValLabelAlign align)
{

    int16_t valAfterCorrection = getFxValueCorrection(fxType, valueFromFx);

    switch (fxType)
    {
        case fx.FX_TYPE_ROLL:
        {
            char typeChar = getRollTypeChar(valueFromFx);
            if (align == fxValLabelAlign_center && typeChar == ' ')
            {
                snprintf(ptr, len, "%u", rollValueToPeriod(valueFromFx % (fx.ROLL_PERIOD_MAX + 1)));
            }
            else
            {
                snprintf(ptr,
                         len,
                         "%c%2u",
                         getRollTypeChar(valueFromFx),
                         rollValueToPeriod(valueFromFx % (fx.ROLL_PERIOD_MAX + 1)));
            }
            break;
        }
        case fx.FX_TYPE_FILTER_LFO:
        case fx.FX_TYPE_PANNING_LFO:
        case fx.FX_TYPE_POSITION_LFO:
        case fx.FX_TYPE_FINETUNE_LFO:
        {
            valAfterCorrection = clamp<int>(valAfterCorrection, 0, MAX_LFOSPEED_LABELS);
            strncpy(ptr,
                    align != fxValLabelAlign_center
                      ? seqlfoSpeedLabels[valAfterCorrection]
                      : skipSpaceOnStart(seqlfoSpeedLabels[valAfterCorrection]),
                    len);

            break;
        }
        case fx.FX_TYPE_VOLUME_LFO:
        {
            valAfterCorrection = clamp<int>(valAfterCorrection, 0, MAX_LFOSPEED_LABELS_AMP);
            strncpy(ptr,
                    align != fxValLabelAlign_center
                      ? seqlfoSpeedLabels_ampOnly[valAfterCorrection]
                      : skipSpaceOnStart(seqlfoSpeedLabels_ampOnly[valAfterCorrection]),
                    len);
            break;
        }
        case fx.FX_TYPE_ARP:
        {
            valAfterCorrection = clamp<int>(valAfterCorrection, 0, arpFxLabels_count);
            strncpy(ptr,
                    align != fxValLabelAlign_center
                      ? arpFxLabels[valAfterCorrection]
                      : skipSpaceOnStart(arpFxLabels[valAfterCorrection]),
                    len);
            break;
        }
        case fx.FX_TYPE_OFF: strncpy(ptr, "OFF", len); break;

        case fx.FX_TYPE_REVERSE_PLAYBACK:
            strncpy(ptr, valAfterCorrection > 0 ? "<<<" : ">>>", len);
            break;

        case fx.FX_TYPE_AMP_ENV:
        case fx.FX_TYPE_FILTER_ENV:
            snprintf(ptr, len, align == fxValLabelAlign_right ? " %02X" : "%02X", valueFromFx);
            break;

        case fx.FX_TYPE_BREAK_PATTERN:
            strncpy(ptr, valAfterCorrection > 0 ? "BRK" : "---", len);
            break;
        case fx.FX_TYPE_CHANCE:
        {
            if (valueFromFx <= fx.CHANCE_PROB_MAX)
            {
                if (align == fxValLabelAlign_right)
                {
                    snprintf(ptr, len, "%3i", valAfterCorrection);
                }
                else
                {
                    snprintf(ptr, len, "%i", valAfterCorrection);
                }
            }
            else if (valueFromFx == fx.CHANCE_GROUP_WITH_LAST)
            {
                strncpy(ptr, align == fxValLabelAlign_right ? " GL" : "GL", len);
            }
            else if (valueFromFx == fx.CHANCE_NOT_LAST)
            {
                strncpy(ptr, align == fxValLabelAlign_right ? " !L" : "!L", len);
            }
            else if (valueFromFx >= fx.CHANCE_PLAY_SKIP_START &&
                     valueFromFx <= fx.CHANCE_PLAY_SKIP_END)
            {
                uint8_t idx = valueFromFx - fx.CHANCE_PLAY_SKIP_START;
                uint8_t n = 1 + idx / fx.CHANCE_CYCLE_STEPS;
                uint8_t m = 1 + idx % fx.CHANCE_CYCLE_STEPS;
                snprintf(ptr, len, "%u:%u", n, m);
            }
            else if (valueFromFx >= fx.CHANCE_SKIP_PLAY_START &&
                     valueFromFx <= fx.CHANCE_SKIP_PLAY_END)
            {
                uint8_t idx = valueFromFx - fx.CHANCE_SKIP_PLAY_START;
                uint8_t n = 1 + idx / fx.CHANCE_CYCLE_STEPS;
                uint8_t m = 1 + idx % fx.CHANCE_CYCLE_STEPS;
                snprintf(ptr, len, "S%u%u", n, m);
            }
            else if (valueFromFx >= fx.CHANCE_AB_START && valueFromFx <= fx.CHANCE_AB_END)
            {
                uint8_t a = 0;
                uint8_t b = 0;
                fx.chanceDecodeAB(valueFromFx, a, b);
                snprintf(ptr, len, "%u/%u", a, b);
            }
            else if (valueFromFx == fx.CHANCE_FIRST)
            {
                strncpy(ptr, "1ST", len);
            }
            else if (valueFromFx == fx.CHANCE_NOT_FIRST)
            {
                strncpy(ptr, "!1S", len);
            }
            break;
        }
        case fx.FX_TYPE_MIDI_CHORD: strncpy(ptr, chordDefName[valAfterCorrection], len); break;
        case fx.FX_TYPE_TEMPO:
        {
            if (valueFromFx == getFxMin(fxType))
            {
                strncpy(ptr, "STP", len);
            }
            else
            {
                if (align == fxValLabelAlign_right)
                {
                    snprintf(ptr, len, "%3i", valAfterCorrection);
                }
                else
                {
                    snprintf(ptr, len, "%i", valAfterCorrection);
                }
            }
            break;
        }
        default:
        {
            if (align == fxValLabelAlign_right)
            {
                snprintf(ptr, len, "%3i", valAfterCorrection);
            }
            else
            {
                snprintf(ptr, len, "%i", valAfterCorrection);
            }
            break;
        }
    }
    ptr[len - 1] = 0;
}

char Sequencer::getRollTypeChar(uint8_t val)
{
    switch (val / (fx.ROLL_PERIOD_MAX + 1))
    {
        case fx.rollType_const: return ' ';
        case fx.rollType_noteUp: return 'N';
        case fx.rollType_noteDown: return 'n';
        case fx.rollType_noteRandom: return 'R';
        case fx.rollType_volDown: return 'v';
        case fx.rollType_volUp: return 'V';
        default: break;
    }
    return '?';
}
AUDIO_CORE_RAM uint16_t Sequencer::calcStepLength(uint8_t track, uint16_t step)
{
    uint16_t trackLen = getActualPattern()->track[0].length;

    uint16_t b = 1;
    for (uint16_t a = step + 1; a <= trackLen; a++, b++)
    {
        if (getActualPattern()->track[track].step[a].note != STEP_NOTE_EMPTY)
        {
            break;
        }
    }

    return b;
}

AUDIO_CORE_RAM uint8_t Sequencer::isTrackEngineMuted(uint8_t track)
{
    return isSampleTrack(track) ? mtProject.values.trackMute[track]
                                : mtProject.values.midiTrackMute[track % 8];
}
