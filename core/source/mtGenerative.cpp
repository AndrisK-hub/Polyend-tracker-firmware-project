#ifdef TRACKER_MINI

#include "mtGenerative.h"

#include "scales.h"

namespace gen
{
namespace
{
// ---------------------------------------------------------------------------------------------
// Pseudo-random numbers without state: a hash of the seed and where we are in the piece.
uint32_t hash32(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    uint32_t h = a * 0x9E3779B1u;
    h ^= (b + 0x7F4A7C15u) * 0x85EBCA77u;
    h = (h << 13) | (h >> 19);
    h ^= (c + 0x165667B1u) * 0xC2B2AE3Du;
    h = (h << 17) | (h >> 15);
    h ^= (d + 0x27D4EB2Fu) * 0x9E3779B1u;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return h;
}

// 0..255
int rnd(const Setup& s, uint32_t salt, uint32_t a, uint32_t b)
{
    return static_cast<int>(hash32(static_cast<uint32_t>(s.seed) * 2654435761u + 1u, salt, a, b) >> 24);
}

int clampInt(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

int floorDiv(int a, int b)
{
    int q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0)))
        q--;
    return q;
}

// ---------------------------------------------------------------------------------------------
// Styles. Every lane of a style is one bar of 16 steps; a digit is how sure the hit is:
// 9 = part of the groove, 1..8 = comes and goes (more often when the energy is high), '.' = never.
// Drum lanes: kick, snare/clap, closed hat, open hat, percussion, cymbal (the cymbal is not
// written here: it is placed where the energy rises).
struct Style
{
    const char* name;
    const char* drums[kDrumLanes];
    const char* bass;       // when the bass plays (same notation)
    uint8_t prog[4];        // root of the chord as a scale degree (0 = the key note)
    uint8_t progBars;       // bars per chord
    uint8_t octave;         // chance (%) that a bass note jumps an octave
    uint8_t fifth;          // chance (%) of the fifth
    uint8_t passing;        // chance (%) of a passing note (third, seventh, step below)
    bool bounce;            // the offbeat eighths jump an octave
    uint8_t ownScale;       // scale used when the chosen one is Chromatic
    const char* lead;       // when the lead plays (same notation)
    uint8_t bassGate;       // how much of the time to the next note a bass note sounds (%)
    uint8_t leadGate;       // the same for the lead
    uint8_t leap;           // chance (%) that the lead jumps instead of walking
    uint8_t still;          // chance (%) that the lead repeats its note
};

constexpr Style styles[kStyleCount] = {
    // the kick has its places at 1 and 3, a snare on 2 and 4, steady eighth hats
    { "SynthPop",
      { "9.....2.9.3...5.", "....9.......9..3", "9292929292929292", "......3.......5.",
        "......4.....3.4.", nullptr },
      "9.3.9.3.9.3.9.3.", { 0, 5, 2, 6 }, 2, 12, 14, 8, false, 1, "9.3.9.5.9.3.9.5.", 70, 75, 12, 10 },
    // syncopated kick, open hat on the last offbeat
    { "NewWave",
      { "9..9....9.7...3.", "....9.......9..2", "9292929292929230", "..............7.",
        "..3...5...3...5.", nullptr },
      "9.3.9.3.9.5.9.3.", { 0, 2, 5, 4 }, 2, 14, 12, 10, false, 1, "9..5..9...5..9..", 65, 60, 15, 10 },
    // four on the floor, claps, offbeat hats, rolling octave bass
    { "Italo",
      { "9...9...9...9.5.", "....9.......9..3", "..9.3.9...9.3.9.", "..............7.",
        "..3..4..3..4..3.", nullptr },
      "9.9.9.9.9.9.9.9.", { 0, 5, 3, 4 }, 2, 0, 6, 6, true, 1, "9.5.9.5.9.5.9.5.", 55, 55, 20, 5 },
    // sixteenth hats that leave out the snare steps, octave bounce
    { "Disco",
      { "9...9...9...9...", "....9.......9...", "99.9.9.999.9.9.9", "..9...9...9...9.",
        "...3..3....3..3.", nullptr },
      "9.9.5.9.9.9.5.9.", { 0, 3, 6, 2 }, 2, 0, 10, 8, true, 3, "9.9.5.9.9.5.9.9.", 60, 50, 18, 8 },
    // four on the floor, offbeat hats and open hats, deep stabs
    { "House",
      { "9...9...9...9...", "....9.......9...", ".9.9.9.9.9.9.9.9", "..9...9...9...9.",
        "..3...4...3...5.", nullptr },
      "..9...9...9...9.", { 0, 0, 5, 6 }, 4, 12, 12, 10, false, 3, "..9...5...9..5..", 55, 40, 10, 20 },
    // four on the floor, sixteenth hats, rolling offbeat bass
    { "Techno",
      { "9...9...9...9...", "....7.......7...", "9595959595959595", "..9...9...9...9.",
        "..3..5..3..5..3.", nullptr },
      ".9.9.9.9.9.9.9.9", { 0, 0, 1, 0 }, 4, 8, 10, 14, false, 7, "9.5.9.9.5.9.9.5.", 45, 35, 8, 40 },
    // eighth note kick, sixteenth hats, sequenced bass, metal hits
    { "EBM",
      { "9.9.9.9.9.9.9.9.", "....9.......9...", "9999999999999999", "..............5.",
        "5..5..5..5..5...", nullptr },
      "9.9.9.9.9.9.9.9.", { 0, 0, 3, 4 }, 4, 18, 14, 10, false, 7, "9.9.9.9.9.9.9.9.", 50, 50, 5, 45 },
    // syncopated kick and claps
    { "Electro",
      { "9......5..9...3.", "....9.......9...", "9.9.9.9.9.9.9.9.", "......3.......5.",
        "..3.5.3.5.3.....", nullptr },
      "9.....5.9..9.3..", { 0, 0, 5, 3 }, 2, 10, 14, 10, false, 1, "9..9..9.9..9..9.", 60, 55, 15, 15 },
    // broken beats: ghost snares between the backbeats
    { "Breaks",
      { "9.....3...9...2.", "....9..2.2..9.2.", "9.9.9.9.9.9.9.9.", "..9...5...9...5.",
        ".3..3...3.3.3...", nullptr },
      "9.....5..9....3.", { 0, 0, 3, 5 }, 2, 12, 12, 10, false, 1, "9.5..9..5..9..5.", 70, 60, 15, 10 },
    // boom bap
    { "HipHop",
      { "9.....5...9...2.", "....9.......9...", "9.9.9.9.9.9.9.9.", "..............3.",
        "......3.....3...", nullptr },
      "9.....5...9...3.", { 0, 5, 3, 4 }, 2, 6, 10, 10, false, 1, "9.....5.9.....3.", 90, 90, 10, 15 },
    // half-time snare, hat bursts, long sub bass
    { "Trap",
      { "9......5..9....3", "........9.......", "9.9.9.9.9.9.9.9.", "..............4.",
        "..3.....3.....3.", nullptr },
      "9......5..9....3", { 0, 0, 5, 4 }, 4, 6, 8, 8, false, 1, "9.......5...9...", 100, 100, 12, 20 },
    // two-step: kick, snare on 2 and 4, fast hats, reese bass
    { "DnB",
      { "9.........7....2", "....9.......9...", "9.9.9.9.9.9.9.9.", "......3.......5.",
        "..3.....3.3...3.", nullptr },
      "9.........7....3", { 0, 5, 3, 6 }, 2, 10, 10, 12, false, 1, "9.5.9.5.9.9.5.9.", 85, 60, 22, 5 },
    // very sparse: a kick now and then, soft ticks, one long bass note per bar
    { "Ambient",
      { "9.......2.......", "........3.......", "..3.....2...3...", "................",
        "...2......2.....", nullptr },
      "9...............", { 0, 3, 5, 4 }, 4, 0, 20, 10, false, 1, "9.......3.......", 100, 100, 8, 25 },
};

int weightOf(const char* pattern, int pos)
{
    if (pattern == nullptr)
        return 0;
    const char c = pattern[pos];
    return (c >= '1' && c <= '9') ? (c - '0') : 0;
}

// ---------------------------------------------------------------------------------------------
// The arrangement
constexpr int kBarSteps = 16;
constexpr int kPhraseBars = 4;

int barCount(const Setup& s)
{
    int bars = (s.length + kBarSteps - 1) / kBarSteps;
    return bars < 1 ? 1 : bars;
}

int phraseCount(const Setup& s)
{
    return (barCount(s) + kPhraseBars - 1) / kPhraseBars;
}

int evolveIndex(const Setup& s)
{
    return s.evolve < kEvolveCount ? static_cast<int>(s.evolve) : static_cast<int>(kNormal);
}

// The break of a long arrangement (6 phrases or more) sits at 55%. It is a breakdown, not a gap:
// the music thins out to the energy of the intro (kick, hats, the first pad voice, the bass and
// the main notes of the lead) so that the whole selection is always filled.
bool isBreakPhrase(int phrase, int phrases)
{
    if (phrases < 6)
        return false;
    const int brk = (phrases * 11 + 10) / 20;
    const int breakLen = phrases >= 12 ? 2 : 1;
    return phrase >= brk && phrase < brk + breakLen;
}

// the energy a phrase would have without any chance
int phraseBase(int phrase, int phrases)
{
    if (phrases == 1)
        return 3; // a single phrase is a steady groove (barEnergy eases the first bar in)
    if (phrases == 2)
        return phrase == 0 ? 2 : 3;
    if (phrases < 6)
    {
        const int pct = ((phrase * 2 + 1) * 100) / (phrases * 2);
        return pct < 15 ? 1 : (pct < 45 ? 2 : (pct < 75 ? 3 : 4));
    }
    // 6 phrases or more: intro, build, a breakdown at 55%, the peak, an outro
    const int brk = (phrases * 11 + 10) / 20;
    if (isBreakPhrase(phrase, phrases))
        return 1;
    if (phrase == phrases - 1)
        return 3;
    if (phrase > brk)
        return 4;
    const int pct = ((phrase * 2 + 1) * 100) / (phrases * 2);
    return pct < 12 ? 1 : (pct < 35 ? 2 : 3);
}

// the phrase where the music comes back after the break; it is never changed by chance, so a long
// arrangement always reaches the top energy at least once
int firstPeakPhrase(int phrases)
{
    if (phrases < 6)
        return -1;
    return (phrases * 11 + 10) / 20 + (phrases >= 12 ? 2 : 1);
}

int phraseEnergy(const Setup& s, int phrase)
{
    const int phrases = phraseCount(s);
    int e = phraseBase(phrase, phrases);
    if (phrases >= 3 && phrase != 0 && phrase != phrases - 1 && phrase != firstPeakPhrase(phrases) &&
        !isBreakPhrase(phrase, phrases))
    {
        static const uint8_t chance[kEvolveCount] = { 26, 64, 115 }; // of 255
        if (rnd(s, 101, static_cast<uint32_t>(phrase), 0) < chance[evolveIndex(s)])
            e += (rnd(s, 102, static_cast<uint32_t>(phrase), 0) & 1) ? 1 : -1;
        e = clampInt(e, 1, 4);
    }
    return e;
}

// which variation of the groove a bar plays: Gentle changes every 8 bars, Normal every phrase,
// Wild every bar
int variationKey(const Setup& s, int bar)
{
    switch (evolveIndex(s))
    {
        case kGentle: return bar / 8;
        case kNormal: return bar / 4;
        default: return bar;
    }
}

// how much the extra hits and the fills are worth: Gentle less, Wild more (in tenths)
int mutationTenths(const Setup& s)
{
    static const uint8_t tenths[kEvolveCount] = { 7, 10, 14 };
    return tenths[evolveIndex(s)];
}

// strength of the fill that ends a bar: 1 = closing, 2 = leads into the same energy, 3 = leads
// into a rise
int fillStrength(const Setup& s, int bar)
{
    const int bars = barCount(s);
    if (bar >= bars - 1)
        return 1;
    const int now = barEnergy(s, bar);
    const int next = barEnergy(s, bar + 1);
    return next > now ? 3 : (next == now ? 2 : 1);
}

// ---------------------------------------------------------------------------------------------
// Scales and chords
struct Scale
{
    int count;
    uint8_t semis[12];
};

Scale makeScale(uint8_t scaleIndex)
{
    Scale sc;
    sc.count = 0;
    const uint16_t mask = scaleDef[scaleIndex > MAX_SCALE ? MAX_SCALE : scaleIndex];
    for (int i = 0; i < 12; i++)
    {
        if (i == 0 || (mask & (1u << i)))
            sc.semis[sc.count++] = static_cast<uint8_t>(i);
    }
    return sc;
}

// a scale with more than 8 notes (Chromatic, the bebop scales) has no useful chords
Scale effectiveScale(const Setup& s, const Style& st)
{
    Scale sc = makeScale(s.scale);
    if (s.scale == 0 || sc.count > 8)
        sc = makeScale(st.ownScale);
    return sc;
}

// the note that sits 'degree' scale notes above the key note nearest to the anchor
int noteOfDegree(const Setup& s, const Scale& sc, int degree)
{
    int root = static_cast<int>(s.anchor) - (((static_cast<int>(s.anchor) - s.root) % 12 + 12) % 12);
    if (s.anchor - root > 6)
        root += 12;
    const int octave = floorDiv(degree, sc.count);
    const int index = degree - octave * sc.count;
    int note = root + octave * 12 + sc.semis[index];
    while (note > 127)
        note -= 12;
    while (note < 0)
        note += 12;
    return note;
}

// The chord roots of an algo song: four chords made up from the seed out of a palette that fits
// the style. Gentle stays with the chords of the style, Normal adds the fourth and the fifth,
// Wild may use every degree of the scale. The first chord is mostly the key note, and a chord is
// never repeated right after itself.
int algoProgDegree(const Setup& s, const Style& st, int slot)
{
    unsigned palette = 1u;
    for (int i = 0; i < 4; i++)
        palette |= 1u << (st.prog[i] % 7);
    const int ev = evolveIndex(s);
    if (ev == kNormal)
        palette |= (1u << 3) | (1u << 4);
    else if (ev == kWild)
        palette = 0x7Fu;

    int previous = -1;
    int degree = 0;
    for (int i = 0; i <= slot && i < 4; i++)
    {
        int candidates[7];
        int count = 0;
        for (int d = 0; d < 7; d++)
        {
            if ((palette & (1u << d)) != 0 && d != previous)
                candidates[count++] = d;
        }
        if (count == 0)
            candidates[count++] = previous < 0 ? 0 : previous;
        degree = candidates[rnd(s, 240, static_cast<uint32_t>(i), 0) % count];
        if (i == 0 && rnd(s, 241, 0, 0) * 100 / 256 < 75)
            degree = 0;
        previous = degree;
    }
    return degree;
}

int chordDegree(const Setup& s, const Style& st, int bar, int barsPerChord)
{
    const int index = bar / barsPerChord;
    int slot = index % 4;
    if (index >= 4)
    {
        static const uint8_t chance[kEvolveCount] = { 0, 26, 64 }; // a different chord of the progression
        if (rnd(s, 60, static_cast<uint32_t>(index), 0) < chance[evolveIndex(s)])
            slot = (slot + 1 + rnd(s, 61, static_cast<uint32_t>(index), 0) % 3) % 4;
    }
    return s.algo ? algoProgDegree(s, st, slot) : st.prog[slot];
}

// Every melodic part reads its chord from here, so a bassline, pads and a lead of one style and
// one song always change chord in the same bar and agree on which chord it is.
int chordBarsOf(const Setup& s, const Style& st)
{
    int bars = st.progBars;
    if (s.algo)
    {
        // an algo song also picks how fast its chords change: as the style says, twice as long
        // or half as long
        const int pick = rnd(s, 242, 0, 0) % 4;
        if (pick == 2)
            bars = bars * 2 > 4 ? 4 : bars * 2;
        else if (pick == 3 && bars >= 2)
            bars = bars / 2;
    }
    return bars * (evolveIndex(s) == kGentle ? 2 : 1);
}

int chordAt(const Setup& s, const Style& st, int bar)
{
    return chordDegree(s, st, bar, chordBarsOf(s, st));
}

// ---------------------------------------------------------------------------------------------
// Drums
// the energy from which a lane plays
constexpr int8_t kLaneMinEnergy[kDrumLanes] = { 1, 2, 1, 3, 2, 0 };
constexpr uint8_t kLaneVelocity[kDrumLanes] = { 118, 112, 90, 100, 84, 110 };
// how much a not-quite-sure hit (digit 1..8) counts per energy level (of 255 per digit)
constexpr uint8_t kGain[5] = { 0, 10, 20, 30, 40 };

// ---------------------------------------------------------------------------------------------
// The skeleton of a part: which steps of a bar it plays and how sure it is about them. A classic
// song takes it from the style as it is written above. An algo song makes it up from the seed: the
// core of the style stays (the kick on the beat, the backbeat, the first main note of the bass, the
// first two of the lead, so the style is still recognisable), and every other step is thrown away or
// re-rolled with a chance, the more so the wilder the piece. The chance of a new hit follows the
// kind of step (on the beat, an eighth off the beat, a sixteenth) and how dense the style's own
// pattern is, so a sparse style stays sparse. The skeleton changes now and then during the piece
// (every 16, 8 or 4 bars for Gentle, Normal and Wild), so an algo piece does not loop one bar.
constexpr int kPartBass = 6;
constexpr int kPartLead = 7;

const char* partPattern(const Style& st, int part)
{
    return part < kDrumLanes ? st.drums[part] : (part == kPartBass ? st.bass : st.lead);
}

int percentOf(const Setup& s, uint32_t salt, uint32_t a, uint32_t b)
{
    return rnd(s, salt, a, b) * 100 / 256;
}

int firstPillar(const char* pattern)
{
    for (int i = 0; i < kBarSteps; i++)
    {
        if (weightOf(pattern, i) >= 9)
            return i;
    }
    return -1;
}

bool isCoreStep(const Style& st, int part, int pos)
{
    const char* pattern = partPattern(st, part);
    if (weightOf(pattern, pos) < (part == 1 ? 7 : 9)) // (a snare of 7 is a backbeat too)
        return false;
    switch (part)
    {
        case 0: return (pos % 4) == 0; // the kick on the beat
        case 1: return true;           // the backbeat
        case kPartBass: return pos == firstPillar(pattern);
        case kPartLead: // the first two main notes
        {
            const int first = firstPillar(pattern);
            if (pos == first)
                return true;
            for (int i = first + 1; i < kBarSteps; i++)
            {
                if (weightOf(pattern, i) >= 9)
                    return i == pos;
            }
            return false;
        }
        default: return false;
    }
}

int skeletonWeight(const Setup& s, const Style& st, int part, int pos, int bar)
{
    const char* pattern = partPattern(st, part);
    const int original = weightOf(pattern, pos);
    if (!s.algo || part < 0 || part > kPartLead || part == 5)
        return original;
    if (isCoreStep(st, part, pos))
        return 9;

    const int ev = evolveIndex(s);
    static const uint8_t epochBars[kEvolveCount] = { 16, 8, 4 };
    static const uint8_t changePercent[kEvolveCount] = { 50, 70, 85 };
    static const uint8_t busyPercent[kEvolveCount] = { 70, 100, 140 };
    const uint32_t epoch = static_cast<uint32_t>(bar / epochBars[ev]);
    const uint32_t step = static_cast<uint32_t>(pos);
    const uint32_t partKey = static_cast<uint32_t>(part);

    if (percentOf(s, 200 + partKey, step, epoch) >= changePercent[ev])
        return original; // this step keeps what the style has

    // a new decision: chance (%) of a hit on the beat, off the beat (eighth) and a sixteenth
    static const uint8_t chance[8][3] = { { 20, 25, 12 },  // kick
                                          { 6, 10, 12 },   // snare, clap
                                          { 70, 60, 45 },  // closed hat
                                          { 10, 30, 10 },  // open hat
                                          { 15, 22, 20 },  // percussion
                                          { 0, 0, 0 },     // (the cymbal is placed by the energy)
                                          { 35, 35, 18 },  // bass
                                          { 40, 30, 14 } }; // lead
    static const uint8_t pillar[8] = { 20, 10, 25, 15, 10, 0, 35, 40 }; // chance (%) of a sure hit
    const int cls = (pos % 4) == 0 ? 0 : ((pos % 2) == 0 ? 1 : 2);
    int count = 0;
    for (int i = 0; i < kBarSteps; i++)
    {
        if (weightOf(pattern, i) > 0)
            count++;
    }
    const int density = 30 + count * 140 / kBarSteps; // percent of the table: 30 (empty) .. 170 (full)
    const int p = chance[part][cls] * density / 100 * busyPercent[ev] / 100;
    if (percentOf(s, 210 + partKey, step, epoch) >= p)
        return 0;
    if (percentOf(s, 220 + partKey, step, epoch) < pillar[part])
        return 9;
    return 2 + rnd(s, 230 + partKey, step, epoch) % 6; // 2..7
}

Event drumEvent(const Setup& s, uint8_t lane, uint16_t offset)
{
    Event ev;
    if (lane >= kDrumLanes)
        return ev;
    const Style& st = styles[s.style % kStyleCount];
    const int bar = offset / kBarSteps;
    const int pos = offset % kBarSteps;
    const int e = barEnergy(s, bar);
    const int mut = mutationTenths(s);
    const bool fill = isFillBar(s, bar);
    bool on = false;
    int weight = skeletonWeight(s, st, lane, pos, bar);

    if (lane == 5)
    {
        // the cymbal marks the start of a phrase where the energy rises
        if (pos == 0 && (bar % kPhraseBars) == 0 && e >= 2)
        {
            const int before = bar == 0 ? 0 : barEnergy(s, bar - 1);
            on = e > before;
        }
    }
    else
    {
        if (e >= kLaneMinEnergy[lane])
        {
            if (weight >= 9)
                on = true;
            else if (weight > 0)
            {
                const int chance = weight * kGain[e] * mut / 10;
                on = rnd(s, 10 + lane, static_cast<uint32_t>(variationKey(s, bar)), static_cast<uint32_t>(pos)) <
                     chance;
            }
        }
        if (fill && pos >= 8)
        {
            const int strength = fillStrength(s, bar);
            const int roll = rnd(s, 40 + lane, static_cast<uint32_t>(bar), static_cast<uint32_t>(pos));
            const int scaled = strength * mut; // 1..3 times 7..14
            switch (lane)
            {
                case 0: // the kick drops out just before the next downbeat
                    if (pos >= 13 && strength >= 2 && roll < 150)
                        on = false;
                    break;
                case 1: // a snare roll
                {
                    static const uint8_t base[8] = { 0, 0, 0, 0, 40, 64, 110, 200 };
                    if (base[pos - 8] != 0 && roll < base[pos - 8] * scaled / 20)
                        on = true;
                    break;
                }
                case 2: // a burst of sixteenths
                    if (pos >= 12 && roll < 140 * scaled / 20)
                        on = true;
                    break;
                case 3: // an open hat on the last sixteenth
                    if (pos == 15 && barEnergy(s, bar) >= 2 && roll < 120 * scaled / 20)
                        on = true;
                    break;
                case 4: // a few percussion hits
                    if (pos >= 12 && roll < 70 * scaled / 20)
                        on = true;
                    break;
                default: break;
            }
        }
    }

    if (!on)
        return ev;

    int v = kLaneVelocity[lane];
    if (pos % 4 == 0)
        v += 8;
    else if (pos % 2 == 1)
        v -= 14;
    if (weight > 0 && weight < 5 && lane != 5)
        v -= 24; // a ghost note
    v -= (4 - e) * 3;
    v -= rnd(s, 50 + lane, static_cast<uint32_t>(offset), 0) % 14;
    ev.on = true;
    ev.velocity = static_cast<uint8_t>(clampInt(v, 20, 127));
    return ev;
}

// ---------------------------------------------------------------------------------------------
// When the melodic parts start and stop. This is separate from what they play, so that the length
// of a note can look ahead to the next one.
struct Hit
{
    bool on = false;
    bool off = false;
};

// Bassline. 'pickup' is set when the note is the pickup before a chord change.
Hit bassHit(const Setup& s, const Style& st, uint16_t offset, bool* pickup)
{
    Hit h;
    if (pickup != nullptr)
        *pickup = false;
    const int bar = offset / kBarSteps;
    const int pos = offset % kBarSteps;
    const int bars = barCount(s);
    const int e = barEnergy(s, bar);
    const int mut = mutationTenths(s);

    if (e == 0)
    {
        // the break: the bass is cut at its start
        if (pos == 0 && (bar == 0 || barEnergy(s, bar - 1) > 0))
            h.off = true;
        return h;
    }

    const int weight = skeletonWeight(s, st, kPartBass, pos, bar);
    if (weight >= 9)
        h.on = true;
    else if (weight > 0)
    {
        const int chance = weight * kGain[e] * mut / 10 + 40; // a bassline is busier than the hats
        h.on = rnd(s, 20, static_cast<uint32_t>(variationKey(s, bar)), static_cast<uint32_t>(pos)) < chance;
    }

    // a pickup on the last sixteenth of a fill bar leads to the next chord
    if (!h.on && isFillBar(s, bar) && pos == 15 && bar + 1 < bars)
    {
        const int strength = fillStrength(s, bar);
        if (rnd(s, 41, static_cast<uint32_t>(bar), 0) < 90 * strength * mut / 20)
        {
            h.on = true;
            if (pickup != nullptr)
                *pickup = true;
        }
    }
    return h;
}

// Pad voices: they enter as the energy rises, roll in one after another on a chord change, and
// fade out in the breakdown. 'ornament' is set for the single high notes between the chords.
constexpr int8_t kVoiceMinEnergy[kAmbientVoices] = { 1, 2, 2, 3 };

Hit padHit(const Setup& s, const Style& st, uint8_t voice, uint16_t offset, bool* ornament)
{
    Hit h;
    if (ornament != nullptr)
        *ornament = false;
    if (voice >= kAmbientVoices)
        return h;
    const int bar = offset / kBarSteps;
    const int pos = offset % kBarSteps;
    const int e = barEnergy(s, bar);
    const int before = bar == 0 ? 0 : barEnergy(s, bar - 1);
    const int barsPerChord = chordBarsOf(s, st);

    if (e == 0)
    {
        if (pos == voice * 2 && before > 0)
            h.off = true; // fade out, one voice after the other
        return h;
    }
    if (e < kVoiceMinEnergy[voice])
    {
        // a voice that drops out (into the breakdown, or when the energy falls) fades out, one
        // voice after the other, so that it does not ring through the thinner part
        if (pos == voice * 2 && before >= kVoiceMinEnergy[voice])
            h.off = true;
        return h;
    }

    if (pos == voice * 4) // the voices roll in one beat apart
    {
        const bool chordChange = (bar % barsPerChord) == 0;
        const bool entering = (bar % kPhraseBars) == 0 && before < kVoiceMinEnergy[voice];
        h.on = chordChange || entering;
        // a pad is played again in the middle of a long chord from time to time
        if (!h.on && (bar % barsPerChord) == barsPerChord / 2 && evolveIndex(s) != kGentle)
            h.on = rnd(s, 80, static_cast<uint32_t>(bar), voice) < (evolveIndex(s) == kWild ? 140 : 70);
    }
    // ornaments: single notes of the scale high up, more of them as the energy rises
    if (!h.on && voice >= 2 && (pos % 4) == 0)
    {
        const int chance = (e * 4 + evolveIndex(s) * 6) * (voice == 3 ? 3 : 2) / 2;
        if (rnd(s, 81, static_cast<uint32_t>(offset), voice) < chance)
        {
            h.on = true;
            if (ornament != nullptr)
                *ornament = true;
        }
    }
    return h;
}

// Lead. The rhythm of a phrase: the first two bars play the same rhythm (on their own chord), the
// third answers and the fourth closes. Gentle repeats the motif for eight bars, Wild plays
// something new every bar. The phrase breathes: bar 2 and a closing bar end on a held note and a
// rest, and now and then a main note comes a sixteenth early (a syncopation).
uint32_t motifKey(const Setup& s, int bar)
{
    if (evolveIndex(s) == kWild)
        return static_cast<uint32_t>(bar);
    const int role = bar % kPhraseBars;
    return static_cast<uint32_t>(variationKey(s, bar) * 4 + (role < 2 ? 0 : role));
}

// the lowest weight the lead plays at each energy: the lead enters with the build
constexpr int8_t kLeadMinWeight[5] = { 10, 10, 5, 3, 1 };

// The last sixteenth of a bar where the lead may start a note. Bar 2 of a phrase and a closing bar
// (one that does not lead into the next phrase with a run) end on a held note and a rest.
int lastLeadPos(const Setup& s, int bar)
{
    const int role = bar % kPhraseBars;
    const bool runs = isFillBar(s, bar) && fillStrength(s, bar) >= 2;
    if (role == 1 || (role == 3 && !runs))
        return 8 + 4 * (rnd(s, 320, motifKey(s, bar), 0) & 1); // 8 or 12
    return kBarSteps - 1;
}

// How the bar ends: 1 = closes the phrase (on the root), 2 = a half cadence (on the third or the
// fifth), 0 = it goes on. The last bar of a selection closes too.
int cadenceKind(const Setup& s, int bar)
{
    const int role = bar % kPhraseBars;
    if (role == 3 || (role == 1 && bar + 1 >= barCount(s)))
        return 1;
    return role == 1 ? 2 : 0;
}

// the notes the skeleton and the energy ask for (before the phrase breathes)
bool leadBaseOn(const Setup& s, const Style& st, int bar, int pos)
{
    const int e = barEnergy(s, bar);
    // (the lead is hushed in the intro and the breakdown: only the main notes of the style, which
    // kLeadMinWeight lets through at energy 1; the busier notes come with the build)
    const int mut = mutationTenths(s);
    const int weight = skeletonWeight(s, st, kPartLead, pos, bar);
    const uint32_t key = motifKey(s, bar);
    bool on = false;
    if (weight >= 9)
    {
        on = true;
        // notes between the beats drop out now and then (Normal a little, Wild a lot)
        static const uint8_t drop[kEvolveCount] = { 0, 15, 40 }; // of 255
        if ((pos % 4) != 0 && rnd(s, 122, key, static_cast<uint32_t>(pos)) < drop[evolveIndex(s)])
            on = false;
    }
    else if (weight > 0 && weight >= kLeadMinWeight[e])
    {
        const int chance = weight * kGain[e] * mut / 20 + 20;
        on = rnd(s, 120, key, static_cast<uint32_t>(pos)) < chance;
    }
    else if (weight == 0 && evolveIndex(s) == kWild && (pos % 2) == 0 && e >= 3)
    {
        on = rnd(s, 123, key, static_cast<uint32_t>(pos)) < 20; // Wild adds notes of its own
    }
    // the intro and the breakdown play the plain line: half of the notes between the beats rest (the
    // first main note of the bar stays, the lead never stops)
    if (on && e < 2 && (pos % 4) != 0 && rnd(s, 124, key, static_cast<uint32_t>(pos)) < 128)
    {
        int first = 0;
        while (first < pos && skeletonWeight(s, st, kPartLead, first, bar) < 9)
            first++;
        if (first != pos)
            on = false;
    }
    // a run of notes at the end of a fill bar
    if (!on && isFillBar(s, bar) && pos >= 12)
    {
        const int scaled = fillStrength(s, bar) * mut;
        on = rnd(s, 121, static_cast<uint32_t>(bar), static_cast<uint32_t>(pos)) < 110 * scaled / 20;
    }
    return on;
}

// A main note on a beat is played a sixteenth early now and then. The first note of the hook (the
// first two bars of a phrase) stays on the beat. The same for every bar that repeats the rhythm.
bool isAnticipated(const Setup& s, const Style& st, int bar, int beat)
{
    const int pos = beat * 4;
    if (bar <= 0 && pos == 0)
        return false;
    if (pos == 0 && (bar % kPhraseBars) < 2)
        return false; // the hook starts on the beat, and so does its repeat
    if (skeletonWeight(s, st, kPartLead, pos, bar) < 9)
        return false;
    // the early note and the note it replaces both have to be allowed
    if (pos > lastLeadPos(s, bar))
        return false;
    if (pos == 0 && lastLeadPos(s, bar - 1) < kBarSteps - 1)
        return false;
    static const uint8_t chance[kEvolveCount] = { 8, 26, 56 }; // of 255
    const int c = chance[evolveIndex(s)] + (s.algo ? 20 : 0);
    return rnd(s, 310, motifKey(s, bar), static_cast<uint32_t>(pos)) < c;
}

Hit leadHit(const Setup& s, const Style& st, uint16_t offset)
{
    Hit h;
    const int bar = offset / kBarSteps;
    const int pos = offset % kBarSteps;
    const int last = lastLeadPos(s, bar);
    h.on = pos <= last && leadBaseOn(s, st, bar, pos);
    // a bar that ends a phrase (or answers it) always has its held cadence note
    if (pos == last && last < kBarSteps - 1 && cadenceKind(s, bar) != 0)
        h.on = true;
    if ((pos % 4) == 0 && h.on && isAnticipated(s, st, bar, pos / 4))
        h.on = false; // it is played a sixteenth earlier
    if ((pos % 4) == 3 && !h.on && static_cast<unsigned>(offset) + 1 < s.length)
    {
        const bool nextBar = pos == kBarSteps - 1;
        if (isAnticipated(s, st, nextBar ? bar + 1 : bar, nextBar ? 0 : (pos + 1) / 4))
            h.on = true; // the note of the next beat arrives early
    }
    return h;
}

Hit hitAt(const Setup& s, uint8_t lane, uint16_t offset)
{
    const Style& st = styles[s.style % kStyleCount];
    switch (s.mode)
    {
        case kBass: return bassHit(s, st, offset, nullptr);
        case kAmbient: return padHit(s, st, lane, offset, nullptr);
        case kLead: return leadHit(s, st, offset);
        default: return Hit();
    }
}

// Steps from 'offset' to the next start or stop of the same voice (at most maxSteps). A voice
// with nothing after it in the selection has the rest of the selection.
int stepsToNext(const Setup& s, uint8_t lane, uint16_t offset, int maxSteps)
{
    for (int j = 1; j <= maxSteps; j++)
    {
        const unsigned o = static_cast<unsigned>(offset) + static_cast<unsigned>(j);
        if (o >= s.length)
            return static_cast<int>(s.length) - offset;
        const Hit h = hitAt(s, lane, static_cast<uint16_t>(o));
        if (h.on || h.off)
            return j;
    }
    return maxSteps;
}

// How long a note sounds when it gets 'gate' percent of the free time 'span' (in steps). 0 means
// until the next note.
uint16_t gatedLength(int span, int gate)
{
    if (gate >= 96 || span < 1)
        return 0;
    int ticks = span * kMicroStepsPerStep * gate / 100;
    if (ticks < 6)
        ticks = 6; // not shorter than an eighth of a step
    if (ticks >= span * kMicroStepsPerStep)
        return 0;
    return static_cast<uint16_t>(ticks);
}

// ---------------------------------------------------------------------------------------------
// Bassline
Event bassEvent(const Setup& s, uint16_t offset)
{
    Event ev;
    const Style& st = styles[s.style % kStyleCount];
    const int bar = offset / kBarSteps;
    const int pos = offset % kBarSteps;
    const int e = barEnergy(s, bar);
    const Scale sc = effectiveScale(s, st);

    bool pickup = false;
    const Hit hit = bassHit(s, st, offset, &pickup);
    if (hit.off)
    {
        ev.off = true;
        return ev;
    }
    if (!hit.on)
        return ev;

    const int chord = chordAt(s, st, bar);
    int degree = chord;
    if (pickup)
    {
        degree = chordAt(s, st, bar + 1) - 1; // a step below the next root
    }
    else
    {
        const bool chordStart = (bar % chordBarsOf(s, st)) == 0 && pos < 4;
        const int scale100 = evolveIndex(s) == kGentle ? 50 : (evolveIndex(s) == kNormal ? 100 : 170);
        const int pOct = st.octave * scale100 / 100;
        const int pFifth = st.fifth * scale100 / 100;
        const int pPass = st.passing * scale100 / 100;
        const int r = rnd(s, 70, static_cast<uint32_t>(bar), static_cast<uint32_t>(pos)) % 100;
        if (st.bounce && (pos % 4) == 2)
            degree = chord + sc.count; // the octave bounce
        else if (chordStart && evolveIndex(s) != kWild)
            degree = chord; // the root on the chord change
        else if (r < pOct)
            degree = chord + sc.count;
        else if (r < pOct + pFifth)
            degree = chord + 4;
        else if (r < pOct + pFifth + pPass)
        {
            static const int8_t passing[3] = { 2, 6, -1 };
            degree = chord + passing[rnd(s, 71, static_cast<uint32_t>(bar), static_cast<uint32_t>(pos)) % 3];
        }
    }

    int v = pos % 4 == 0 ? 112 : (pos % 2 == 0 ? 100 : 88);
    v -= (4 - e) * 3;
    v -= rnd(s, 72, static_cast<uint32_t>(offset), 0) % 12;
    ev.on = true;
    ev.velocity = static_cast<uint8_t>(clampInt(v, 30, 127));
    ev.note = static_cast<int16_t>(noteOfDegree(s, sc, degree));

    // The length: the style says how much of the time to the next note a note sounds, and every
    // bar the gate moves a little so the line breathes. The notes before a chord change ring on.
    if (!pickup)
    {
        const int span = stepsToNext(s, 0, offset, kBarSteps);
        int gate = st.bassGate;
        const int r = rnd(s, 90, static_cast<uint32_t>(variationKey(s, bar)), static_cast<uint32_t>(pos)) % 100;
        if (r < 22)
            gate -= 25;
        else if (r < 36)
            gate += 30;
        if (isFillBar(s, bar) && pos >= 8)
            gate += 25;
        gate -= (e - 2) * 4; // tighter when the music is busier
        ev.length = gatedLength(span, clampInt(gate, 15, 100));
    }
    return ev;
}

// ---------------------------------------------------------------------------------------------
// Ambient pads
Event ambientEvent(const Setup& s, uint8_t voice, uint16_t offset)
{
    Event ev;
    if (voice >= kAmbientVoices)
        return ev;
    const Style& st = styles[s.style % kStyleCount];
    const int bar = offset / kBarSteps;
    const int e = barEnergy(s, bar);
    const Scale sc = effectiveScale(s, st);
    const int barsPerChord = chordBarsOf(s, st);

    bool ornament = false;
    const Hit hit = padHit(s, st, voice, offset, &ornament);
    if (hit.off)
    {
        ev.off = true;
        return ev;
    }
    if (!hit.on)
        return ev;

    const int chord = chordAt(s, st, bar);
    static const int8_t tone[kAmbientVoices] = { 0, 4, 2, 4 };
    int degree = chord + tone[voice] + (voice >= 2 ? sc.count : 0);
    if (ornament)
    {
        static const int8_t ornaments[5] = { 1, 2, 4, 6, 8 };
        degree = chord + ornaments[rnd(s, 82, static_cast<uint32_t>(offset), voice) % 5] + sc.count;
    }
    if (voice == 3 && e >= 4 && evolveIndex(s) == kWild)
        degree += 2; // the seventh colours the top voice at the peak

    int v = 78 - voice * 4 + rnd(s, 83, static_cast<uint32_t>(offset), voice) % 14;
    v -= (4 - e) * 2;
    ev.on = true;
    ev.velocity = static_cast<uint8_t>(clampInt(v, 30, 120));
    ev.note = static_cast<int16_t>(noteOfDegree(s, sc, degree));

    // The length: ornaments are short, the chord notes ring for most of the chord. The lowest
    // voice holds the whole chord; the others let go a little earlier, a different amount for
    // every chord, so the pad breathes.
    const int span = stepsToNext(s, voice, offset, barsPerChord * kBarSteps);
    if (ornament)
    {
        const int steps = 2 + rnd(s, 84, static_cast<uint32_t>(offset), voice) % 5;
        ev.length = steps >= span ? 0 : static_cast<uint16_t>(steps * kMicroStepsPerStep);
    }
    else if (voice > 0)
    {
        static const uint8_t gates[3][5] = { { 100, 100, 100, 92, 100 }, //
                                             { 100, 92, 85, 75, 100 },   //
                                             { 100, 85, 70, 60, 45 } };
        const int chordIndex = bar / barsPerChord;
        const int gate = gates[evolveIndex(s)][rnd(s, 85, static_cast<uint32_t>(chordIndex), voice) % 5];
        ev.length = gatedLength(span, gate);
    }
    return ev;
}

// ---------------------------------------------------------------------------------------------
// Lead: the melody. It is a hook that comes back, not a walk of random steps.
//  * Every phrase (4 bars) has a contour (an arch, a climb, a fall or a wave) that says how high the
//    melody is on each beat. The hook is the contour of the first bar: bar 2 plays it again (and
//    ends away from home), bar 3 goes on, bar 4 closes on the root of the chord.
//  * The beats rest on chord tones: the chord tone nearest to the contour. Between two beats the
//    melody walks by step to the next one: a passing note halfway, a neighbour note when the two
//    beats are close, an approach note right before the beat.
//  * The hook comes back: Normal plays hook A, A, B, A; Gentle A, A, B, B; Wild a new hook every
//    other phrase. The seed of the song decides the hooks, so the parts of one song agree.
// Heights are scale degrees counted from the key note (the lead sits around the 'anchor').

// How high the melody is on the 16 beats of a phrase (0 = low .. 5 = high)
constexpr int8_t kContour[4][16] = {
    { 1, 2, 2, 3, 3, 4, 4, 5, 5, 4, 4, 3, 3, 2, 1, 0 }, // an arch
    { 0, 1, 1, 2, 2, 3, 3, 3, 4, 4, 5, 5, 5, 4, 3, 2 }, // a climb
    { 5, 5, 4, 4, 4, 3, 3, 3, 3, 2, 2, 2, 2, 1, 1, 0 }, // a fall
    { 2, 3, 4, 3, 2, 1, 2, 3, 4, 5, 4, 3, 2, 1, 1, 0 }, // a wave
};

// which hook a phrase plays: 0 is the main hook that keeps coming back
int hookOfPhrase(const Setup& s, int phrase)
{
    const int flip = s.algo ? rnd(s, 300, 0, 0) % 4 : 2; // an algo song puts its second hook elsewhere
    switch (evolveIndex(s))
    {
        case kGentle: return ((phrase + (s.algo ? flip : 0)) / 2) % 2;
        case kNormal: return (phrase % 4) == flip ? 1 : 0;
        default: return (phrase % 2) == 0 ? 0 : 1 + phrase / 2;
    }
}

// which of the 16 beats of the contour a beat of a bar plays: the second bar repeats the hook but
// ends elsewhere, Wild goes on instead
int contourIndex(const Setup& s, int bar, int beat)
{
    switch (bar % kPhraseBars)
    {
        case 0: return beat;
        case 1: return evolveIndex(s) == kWild ? 4 + beat : (beat == 3 ? 7 : beat);
        case 2: return 8 + beat;
        default: return 12 + beat;
    }
}

// the key of a thing the hook decides (the same in every bar that repeats the hook)
uint32_t hookKey(const Setup& s, int bar, int beat)
{
    const uint32_t hook = static_cast<uint32_t>(hookOfPhrase(s, bar / kPhraseBars));
    const uint32_t wild = evolveIndex(s) == kWild ? static_cast<uint32_t>(bar) : 0u;
    return hook * 1024u + wild * 16u + static_cast<uint32_t>(contourIndex(s, bar, beat));
}

// the chord tones of the scale (the root, the third and the fifth) as steps of the scale
struct ChordTones
{
    int n;
    int count;
    int res[3];
};

ChordTones chordTonesOf(const Scale& sc)
{
    ChordTones ct;
    ct.n = 0;
    ct.count = sc.count;
    const int cand[3] = { 0, 2 % sc.count, 4 % sc.count };
    for (int i = 0; i < 3; i++)
    {
        bool dup = false;
        for (int j = 0; j < ct.n; j++)
            dup = dup || ct.res[j] == cand[i];
        if (!dup)
            ct.res[ct.n++] = cand[i];
    }
    for (int i = 0; i < ct.n; i++) // ascending
        for (int j = i + 1; j < ct.n; j++)
            if (ct.res[j] < ct.res[i])
            {
                const int t = ct.res[i];
                ct.res[i] = ct.res[j];
                ct.res[j] = t;
            }
    return ct;
}

// the chord tone of 'chord' (a scale degree) nearest to 'target'. 'only' restricts it to the
// root (1), to the third and the fifth (2), or lets every chord tone in (0).
int nearestTone(const ChordTones& ct, int chord, int target, bool upperOnTie, int only)
{
    int best = chord;
    int bestDist = 999;
    const int baseOct = floorDiv(target - chord, ct.count);
    for (int o = baseOct - 1; o <= baseOct + 1; o++)
    {
        for (int i = 0; i < ct.n; i++)
        {
            if ((only == 1 && ct.res[i] != 0) || (only == 2 && ct.res[i] == 0 && ct.n > 1))
                continue;
            const int h = chord + o * ct.count + ct.res[i];
            const int dist = h > target ? h - target : target - h;
            if (dist < bestDist || (dist == bestDist && upperOnTie && h > best))
            {
                best = h;
                bestDist = dist;
            }
        }
    }
    return best;
}

// the next chord tone above (dir > 0) or below 'from'
int stepTone(const ChordTones& ct, int chord, int from, int dir)
{
    int best = from + dir * ct.count;
    const int baseOct = floorDiv(from - chord, ct.count);
    for (int o = baseOct - 1; o <= baseOct + 1; o++)
    {
        for (int i = 0; i < ct.n; i++)
        {
            const int h = chord + o * ct.count + ct.res[i];
            if (dir > 0 && h > from && h < best)
                best = h;
            else if (dir < 0 && h < from && h > best)
                best = h;
        }
    }
    return best;
}

// how high the melody wants to be on a beat
int contourHeight(const Setup& s, const Style& st, const Scale& sc, int bar, int beat)
{
    const uint32_t hk = hookKey(s, bar, beat);
    const uint32_t hook = static_cast<uint32_t>(hookOfPhrase(s, bar / kPhraseBars));
    int h = kContour[rnd(s, 301, hook, 0) % 4][contourIndex(s, bar, beat)];
    if (s.algo && (rnd(s, 304, hook, 0) & 1))
        h = 5 - h; // a valley instead of a hill
    // a personal touch of the hook: a beat a step higher or lower; an algo song takes more liberties
    const int j = rnd(s, 302, hk, 0) % 100;
    const int touch = s.algo ? 28 : 16;
    if (j < touch)
        h += 1;
    else if (j < touch * 2)
        h -= 1;
    // now and then a leap that the next beat takes back
    if (beat > 0 && rnd(s, 303, hk, 1) % 100 < (st.leap + (s.algo ? 8 : 0)) / 2)
        h += (rnd(s, 305, hk, 2) & 1) ? 3 : -3;
    int base = sc.count / 2 - 1;
    if (base < 1)
        base = 1;
    if (hook != 0)
        base += 1; // the second hook sits a step higher
    return base + h;
}

// The chord tone the melody rests on at a strong beat (0 or 2) of a bar. The bar that ends a
// phrase closes on the root, bar 2 ends away from home (on the third or the fifth).
int strongTone(const Setup& s, const Style& st, const Scale& sc, int bar, int beat, bool cadence = true)
{
    const int chord = chordAt(s, st, bar);
    const ChordTones ct = chordTonesOf(sc);
    const int kind = cadenceKind(s, bar);
    const int last = lastLeadPos(s, bar);
    const int endBeat = last < kBarSteps - 1 ? last / 4 : -1; // the beat of the held note
    const int target = contourHeight(s, st, sc, bar, beat);
    const bool upper = (rnd(s, 306, hookKey(s, bar, beat), 3) & 1) != 0;
    const bool closes = cadence && beat == endBeat && kind == 1;
    const bool halfCloses = cadence && beat == endBeat && kind == 2;
    return nearestTone(ct, chord, target, upper, closes ? 1 : (halfCloses ? 2 : 0));
}

// the chord tone next to 'from' in the direction 'dir'; with 'keepOffRoot' the root is no answer (a
// half cadence rests on the third or the fifth), the other direction is taken then
int stepToneFrom(const ChordTones& ct, int chord, int from, int dir, bool keepOffRoot)
{
    int to = stepTone(ct, chord, from, dir);
    if (keepOffRoot && ct.n > 1 && ((to - chord) % ct.count + ct.count) % ct.count == 0)
        to = stepTone(ct, chord, from, -dir);
    return to;
}

// The notes the melody rests on at the four beats of a bar. The strong beats (0 and 2) are chord
// tones; a weak beat (1 and 3) is often a passing or a neighbour note between the two chord tones
// around it, so that the melody moves by step and not only by the jumps of the chord.
// out[4] is the note a plain bar would rest on at beat 2: the first half of a bar that ends on a
// cadence note at beat 2 still walks like the hook, and only the end of it turns to the cadence.
void leadBeats(const Setup& s, const Style& st, const Scale& sc, int bar, int out[5])
{
    const int chord = chordAt(s, st, bar);
    const ChordTones ct = chordTonesOf(sc);
    const int last = lastLeadPos(s, bar);
    const int endBeat = last < kBarSteps - 1 ? last / 4 : -1;
    out[0] = strongTone(s, st, sc, bar, 0);
    out[2] = strongTone(s, st, sc, bar, 2);
    const int target0 = contourHeight(s, st, sc, bar, 0);
    const int target2 = contourHeight(s, st, sc, bar, 2);
    if (out[2] == out[0])
    {
        // not the same note twice: the closing note stays, the note that is not part of the hook moves
        if (endBeat == 2 && cadenceKind(s, bar) == 1)
            out[0] = stepTone(ct, chord, out[0], target2 >= target0 ? -1 : 1);
        else
            out[2] = stepToneFrom(ct, chord, out[2], endBeat == 2 || target2 >= target0 ? 1 : -1, endBeat == 2 && cadenceKind(s, bar) == 2);
    }
    out[4] = out[2];
    if (endBeat == 2 && cadenceKind(s, bar) != 0)
    {
        out[4] = strongTone(s, st, sc, bar, 2, false);
        if (out[4] == out[0])
            out[4] = stepTone(ct, chord, out[4], target2 >= target0 ? 1 : -1);
    }
    const int next = strongTone(s, st, sc, bar + 1, 0);
    static const uint8_t passing[kEvolveCount] = { 35, 55, 70 }; // % of the weak beats
    for (int b = 1; b < 4; b += 2)
    {
        const int left = out[b - 1];
        const int right = b == 1 ? out[4] : next;
        const uint32_t key = hookKey(s, bar, b);
        const int lo = left < right ? left : right;
        const int hi = left < right ? right : left;
        if (b != endBeat && static_cast<int>(rnd(s, 307, key, 4) % 100) < passing[evolveIndex(s)] + (s.algo ? 10 : 0))
        {
            if (hi - lo >= 2)
                out[b] = lo + (hi - lo) / 2; // a passing note
            else
                out[b] = (rnd(s, 308, key, 5) & 1) ? hi + 1 : lo - 1; // a neighbour note
        }
        else
        {
            const int target = contourHeight(s, st, sc, bar, b);
            const bool upper = (rnd(s, 306, key, 3) & 1) != 0;
            int tone = nearestTone(ct, chord, target, upper, b == endBeat ? cadenceKind(s, bar) : 0);
            if (tone == left)
            {
                const int dir = target >= contourHeight(s, st, sc, bar, b - 1) ? 1 : -1;
                if (b == endBeat && cadenceKind(s, bar) == 1)
                    out[b - 1] = stepTone(ct, chord, left, -dir); // the closing root stays
                else
                    tone = stepToneFrom(ct, chord, tone, b == endBeat ? 1 : dir, b == endBeat && cadenceKind(s, bar) == 2);
            }
            out[b] = tone;
        }
    }
}

// The scale degree the lead plays at 'pos' of a bar
int leadDegree(const Setup& s, const Style& st, const Scale& sc, int bar, int pos)
{
    int cur[5];
    leadBeats(s, st, sc, bar, cur);
    const int beat = pos / 4;
    const int k = pos % 4;
    const int next = beat < 3 ? cur[beat + 1] : strongTone(s, st, sc, bar + 1, 0);
    const int from = cur[beat];
    const int lean = (beat == 1 && k < 3) ? cur[4] : next; // (the walk towards a cadence note starts like the hook)
    const int d = lean - from;
    const int ad = d > 0 ? d : -d;
    const uint32_t key = hookKey(s, bar, beat);
    const int od = (rnd(s, 340, key, static_cast<uint32_t>(k)) & 1) ? 1 : -1; // neighbour: above or below
    const int dir = d > 0 ? 1 : (d < 0 ? -1 : od);

    // the end of a fill bar: a run towards the next bar
    if (isFillBar(s, bar) && pos >= 12 && bar + 1 < barCount(s))
        return cur[3] + dir * (pos - 12);

    // a note arriving a sixteenth early plays the note of the beat it anticipates
    if (k == 3 && static_cast<unsigned>(bar) * kBarSteps + static_cast<unsigned>(pos) + 1 < s.length)
    {
        const bool nextIsBar = pos == kBarSteps - 1;
        if (isAnticipated(s, st, nextIsBar ? bar + 1 : bar, nextIsBar ? 0 : beat + 1))
            return next;
    }
    if (k == 0)
        return from;
    // a riff style repeats its note between the beats now and then
    if (rnd(s, 341, key, static_cast<uint32_t>(k)) % 100 < st.still / 2)
        return from;
    switch (k)
    {
        case 1: return from + dir; // a step towards the next beat
        case 2: // a neighbour note when the beats are close, a passing note halfway when they are not
            return ad <= 1 ? from + od : from + dir * (ad / 2);
        default: // the approach: a step before the next beat, from the far side when the beats are close
            return ad <= 1 ? next + dir : next - dir;
    }
}

Event leadEvent(const Setup& s, uint16_t offset)
{
    Event ev;
    const Style& st = styles[s.style % kStyleCount];
    const int bar = offset / kBarSteps;
    const int pos = offset % kBarSteps;
    const int e = barEnergy(s, bar);
    const Scale sc = effectiveScale(s, st);

    const Hit hit = leadHit(s, st, offset);
    if (hit.off)
    {
        ev.off = true;
        return ev;
    }
    if (!hit.on)
        return ev;

    int v = 98 + (pos % 4 == 0 ? 10 : (pos % 2 == 1 ? -10 : 0));
    v -= (4 - e) * 3;
    if (e < 2)
        v -= 14; // hushed in the intro and the breakdown
    v -= rnd(s, 134, static_cast<uint32_t>(offset), 0) % 12;
    ev.on = true;
    ev.velocity = static_cast<uint8_t>(clampInt(v, 30, 127));
    ev.note = static_cast<int16_t>(noteOfDegree(s, sc, leadDegree(s, st, sc, bar, pos)));

    // The length: mostly legato (the style says how much), some notes short, some held; the last
    // note of a bar that ends on a held note sounds until the next one.
    const int span = stepsToNext(s, 0, offset, kBarSteps);
    int gate = 62 + st.leadGate * 38 / 100;
    const int r = rnd(s, 140, motifKey(s, bar), static_cast<uint32_t>(pos)) % 100;
    if (r < 18)
        gate -= 28;
    else if (r < 40)
        gate = 100;
    const bool cadence = lastLeadPos(s, bar) < kBarSteps - 1 && pos + span >= kBarSteps;
    if (cadence || ((bar % kPhraseBars) == kPhraseBars - 1 && span >= 3 && pos >= 8))
        gate = 100;
    ev.length = gatedLength(span, clampInt(gate, 15, 100));
    return ev;
}

// ---------------------------------------------------------------------------------------------
// Drone: a few notes that hold for bars and slowly change.
//  * Four voices: the ground (the key note), the fifth, a colour tone (a second, a third, a seventh
//    ... as the style likes it) and a quiet high shimmer. They enter one after the other.
//  * Every voice plays its own schedule of notes of 1 to 16 bars (mostly 2 and more). A schedule is made of spans
//    (4, 8 or 16 bars) that are cut into notes by a table of partitions, so the notes of the voices
//    start at different bars and the drone never changes all at once. The style sets the pace of the
//    changes (Ambient the slowest, Techno and DnB the quickest), Evolve makes it slower or faster,
//    the ground is the slowest voice and the shimmer the quickest.
//  * The ground note follows a slow root line: Gentle stays on the key note, Normal and Wild move
//    to other degrees of the style's progression now and then and come back. The other voices pick
//    their note when they start, so the harmony shifts gradually.
//  * Every note comes in at its own intensity: a slow wave (a different period for every voice)
//    sets how loud the voice is, a little chance shakes it; a note ends a little early now and
//    then, and the shimmer and the colour tone rest for a note now and then.
//  * Every note is snapped to the scale, so the Scale setting is what the drone is made of.
struct DroneStyle
{
    uint8_t pace;    // 0 = the slowest table .. 4 = the quickest
    uint8_t colours; // which set of colour tones
    int8_t loud;     // added to the velocity
};

constexpr DroneStyle droneStyles[kStyleCount] = {
    { 2, 5, 0 },  // SynthPop: bright
    { 2, 1, -2 }, // NewWave: thirds
    { 2, 5, 2 },  // Italo
    { 2, 3, 2 },  // Disco: sevenths
    { 2, 3, 2 },  // House
    { 3, 2, 6 },  // Techno: dark
    { 3, 2, 8 },  // EBM
    { 2, 0, 4 },  // Electro: open
    { 3, 0, 0 },  // Breaks
    { 2, 3, 2 },  // HipHop
    { 1, 2, 6 },  // Trap: dark and slow
    { 3, 4, 0 },  // DnB: airy and quick
    { 0, 4, -6 }, // Ambient: airy, the slowest and softest
};

// colour tones: semitones above the octave of the ground (snapped to the scale)
constexpr int8_t kDroneColours[6][4] = {
    { 2, 5, 2, 9 },   // open: second, fourth, sixth
    { 3, 4, 3, 10 },  // thirds
    { 1, 3, 6, 10 },  // dark: flat second, minor third, tritone, flat seventh
    { 10, 11, 3, 4 }, // sevenths
    { 2, 4, 9, 2 },   // airy: second, third, sixth
    { 4, 9, 11, 2 },  // bright: third, sixth, major seventh, ninth
};

// How a span is cut into notes (in bars). The table is the pace of the voice, a span is 16, 8 or
// 4 bars long. A zero ends the list.
constexpr uint8_t kDroneSpanBars[5] = { 16, 16, 8, 8, 4 };
constexpr uint8_t kDronePartitions[5][8][5] = {
    { { 16 }, { 16 }, { 16 }, { 10, 6 }, { 12, 4 }, { 8, 8 }, { 6, 10 }, { 16 } },
    { { 16 }, { 10, 6 }, { 8, 8 }, { 12, 4 }, { 6, 10 }, { 8, 4, 4 }, { 4, 4, 8 }, { 5, 5, 6 } },
    { { 8 }, { 4, 4 }, { 5, 3 }, { 3, 5 }, { 6, 2 }, { 8 }, { 2, 3, 3 }, { 4, 2, 2 } },
    { { 4, 4 }, { 3, 5 }, { 2, 2, 4 }, { 3, 2, 3 }, { 4, 2, 2 }, { 2, 4, 2 }, { 3, 3, 2 }, { 2, 2, 2, 2 } },
    { { 2, 2 }, { 4 }, { 1, 3 }, { 3, 1 }, { 2, 2 }, { 4 }, { 2, 2 }, { 1, 1, 2 } },
};

// the bar where a voice comes in: the ground at once, the others one after the other
int droneEnterBar(const Setup& s, int voice)
{
    if (voice <= 0)
        return 0;
    const int bars = barCount(s);
    int gap = bars >= 8 ? (evolveIndex(s) == kGentle ? 3 : (evolveIndex(s) == kNormal ? 2 : 1)) :
                          (bars >= 4 ? 1 : 0);
    if (s.algo && gap > 0)
        gap += rnd(s, 410, static_cast<uint32_t>(voice), 0) & 1;
    const int enter = voice * gap;
    return enter > (bars - 1) / 2 ? (bars - 1) / 2 : enter;
}

// which table of partitions a voice uses in a block of 16 bars
int droneTable(const Setup& s, int voice, int block)
{
    static const int8_t evolveStep[kEvolveCount] = { -1, 0, 1 };
    static const int8_t voiceStep[kDroneVoices] = { -1, 0, 0, 1 };
    int table = droneStyles[s.style % kStyleCount].pace + evolveStep[evolveIndex(s)] + voiceStep[voice];
    if (s.algo)
        table += static_cast<int>(rnd(s, 411, static_cast<uint32_t>(block), static_cast<uint32_t>(voice)) % 3) - 1;
    return clampInt(table, 0, 4);
}

struct DroneSegment
{
    int start; // the bar where the note starts
    int bars;  // bars until the next note of the voice
};

DroneSegment droneSegment(const Setup& s, int voice, int bar)
{
    const int enter = droneEnterBar(s, voice);
    const int t = bar - enter;
    const int block = t / 16;
    const int within = t % 16;
    const int table = droneTable(s, voice, block);
    const int span = kDroneSpanBars[table];
    const int sub = within / span;
    const int j = within % span;
    const uint8_t* part =
      kDronePartitions[table][rnd(s, 412, static_cast<uint32_t>(block * 4 + sub), static_cast<uint32_t>(voice)) % 8];
    int acc = 0;
    for (int i = 0; i < 5 && part[i] != 0; i++)
    {
        if (j < acc + part[i])
            return { enter + block * 16 + sub * span + acc, part[i] };
        acc += part[i];
    }
    return { bar, 1 };
}

// a voice other than the ground rests for a note now and then (never the one it comes in with)
bool droneRests(const Setup& s, int voice, int startBar)
{
    static const uint8_t chance[kDroneVoices] = { 0, 4, 12, 28 };
    static const uint8_t scale[kEvolveCount] = { 70, 100, 150 };
    if (voice == 0 || startBar == droneEnterBar(s, voice))
        return false;
    return percentOf(s, 413, static_cast<uint32_t>(startBar), static_cast<uint32_t>(voice)) <
           chance[voice] * scale[evolveIndex(s)] / 100;
}

// the degree the ground note sits on in this bar
int droneRootDegree(const Setup& s, const Style& st, int bar)
{
    const int ev = evolveIndex(s);
    if (ev == kGentle && !s.algo)
        return 0; // a pedal on the key note
    const int span = ev == kWild ? 4 : (ev == kNormal ? 8 : 16);
    const int k = bar / span;
    if (k == 0)
        return 0;
    static const uint8_t tonic[kEvolveCount] = { 70, 55, 30 }; // % of the spans on the key note
    if (percentOf(s, 414, static_cast<uint32_t>(k), 0) < tonic[ev])
        return 0;
    unsigned palette = 0;
    for (int i = 0; i < 4; i++)
        palette |= 1u << (st.prog[i] % 7);
    if (ev == kNormal)
        palette |= (1u << 3) | (1u << 4);
    else if (ev == kWild)
        palette = 0x7Fu;
    palette &= ~1u;
    int count = 0;
    for (int d = 1; d < 7; d++)
        count += (palette >> d) & 1u;
    if (count == 0)
        return 0;
    int pick = static_cast<int>(rnd(s, 415, static_cast<uint32_t>(k), 0) % count);
    for (int d = 1; d < 7; d++)
    {
        if (((palette >> d) & 1u) != 0 && pick-- == 0)
            return d;
    }
    return 0;
}

bool inScale(const Setup& s, const Scale& sc, int note)
{
    const int pc = ((note - s.root) % 12 + 12) % 12;
    for (int i = 0; i < sc.count; i++)
    {
        if (sc.semis[i] == pc)
            return true;
    }
    return false;
}

// the note of the scale nearest to a pitch (the lower one when two are equally near)
int droneSnap(const Setup& s, const Scale& sc, int target)
{
    for (int k = 0; k <= 6; k++)
    {
        if (inScale(s, sc, target - k))
            return target - k;
        if (inScale(s, sc, target + k))
            return target + k;
    }
    return target;
}

// the ground note of a degree, kept within a fourth below and a tritone above the key note
int droneGround(const Setup& s, const Scale& sc, int degree)
{
    const int base = noteOfDegree(s, sc, 0);
    int ground = noteOfDegree(s, sc, degree);
    while (ground - base > 6)
        ground -= 12;
    while (ground - base < -5)
        ground += 12;
    return ground;
}

int dronePitch(const Setup& s, const Style& st, const Scale& sc, int voice, int startBar)
{
    const int ground = droneGround(s, sc, droneRootDegree(s, st, startBar));
    if (voice == 0)
        return ground;
    int target = ground + 7;
    if (voice == 2)
    {
        const int pick = static_cast<int>(rnd(s, 416, static_cast<uint32_t>(startBar), 2) % 4);
        target = ground + 12 + kDroneColours[droneStyles[s.style % kStyleCount].colours][pick];
    }
    else if (voice == 3)
    {
        static const int8_t high[4] = { 24, 19, 24, 26 };
        const int choices = evolveIndex(s) == kGentle ? 2 : 4;
        target = ground + high[rnd(s, 417, static_cast<uint32_t>(startBar), 3) % choices];
    }
    while (target > 120)
        target -= 12;
    return droneSnap(s, sc, target);
}

// how loud a note comes in: a slow wave per voice, a little chance on top
int droneVelocity(const Setup& s, int voice, int startBar)
{
    static const int8_t centre[kDroneVoices] = { 86, 76, 68, 58 };
    static const uint8_t halfRange[kEvolveCount] = { 9, 16, 26 };
    static const uint8_t jitter[kEvolveCount] = { 3, 5, 8 };
    static const uint8_t period[kEvolveCount][kDroneVoices] = {
        { 40, 32, 28, 24 }, { 24, 20, 16, 12 }, { 12, 10, 8, 6 }
    };
    const int ev = evolveIndex(s);
    const int p = period[ev][voice];
    const int phase = static_cast<int>(rnd(s, 418, static_cast<uint32_t>(voice), 0)) % (2 * p);
    int x = (startBar + phase) % (2 * p);
    if (x > p)
        x = 2 * p - x;
    const int wave = x * 256 / p; // 0 .. 256
    const int chance =
      static_cast<int>(rnd(s, 419, static_cast<uint32_t>(startBar), static_cast<uint32_t>(voice))) %
        (2 * jitter[ev] + 1) -
      jitter[ev];
    const int v = centre[voice] + droneStyles[s.style % kStyleCount].loud + (wave - 128) * halfRange[ev] / 128 + chance;
    return clampInt(v, 20, 120);
}

Event droneEvent(const Setup& s, uint8_t voice, uint16_t offset)
{
    Event ev;
    if (voice >= kDroneVoices || (offset % kBarSteps) != 0)
        return ev;
    const int bar = offset / kBarSteps;
    if (bar < droneEnterBar(s, voice))
        return ev;
    const DroneSegment seg = droneSegment(s, voice, bar);
    if (seg.start != bar || droneRests(s, voice, bar))
        return ev;

    const Style& st = styles[s.style % kStyleCount];
    const Scale sc = effectiveScale(s, st);
    ev.on = true;
    ev.velocity = static_cast<uint8_t>(droneVelocity(s, voice, bar));
    ev.note = static_cast<int16_t>(dronePitch(s, st, sc, voice, bar));

    // a note rings for its bars, or lets go a little early (a breath before the next one)
    static const uint8_t gates[kEvolveCount][5] = { { 100, 100, 100, 96, 100 },
                                                    { 100, 100, 94, 82, 100 },
                                                    { 100, 88, 72, 58, 100 } };
    const int gate = gates[evolveIndex(s)][rnd(s, 420, static_cast<uint32_t>(bar), voice) % 5];
    int steps = seg.bars * kBarSteps * gate / 100;
    if (steps < 12)
        steps = 12;
    if (offset + steps > s.length)
        steps = s.length - offset; // a note never rings past the selection
    ev.length = static_cast<uint16_t>(steps * kMicroStepsPerStep);
    return ev;
}

} // namespace

// ---------------------------------------------------------------------------------------------
const char* styleName(int style)
{
    if (style < 0 || style >= kStyleCount)
        return "";
    return styles[style].name;
}

int laneCount(uint8_t mode)
{
    switch (mode)
    {
        case kDrums: return kDrumLanes;
        case kAmbient: return kAmbientVoices;
        case kBass: return kBassVoices;
        case kLead: return kLeadVoices;
        case kDrone: return kDroneVoices;
        default: return 0;
    }
}

int16_t drumNote(uint8_t lane)
{
    // kick 60, snare 62, closed hat 64, open hat 66, cymbal 68, percussion 70
    static const int16_t notes[kDrumLanes] = { 60, 62, 64, 66, 70, 68 };
    return lane < kDrumLanes ? notes[lane] : -1;
}

uint16_t songSeed(int style, int song)
{
    const uint32_t h =
      hash32(static_cast<uint32_t>(style) + 1u, static_cast<uint32_t>(song) + 17u, 0xA5E3u, 77u);
    const uint16_t v = static_cast<uint16_t>(h >> 16);
    return v == 0 ? 1 : v;
}

uint16_t algoSeed(int style, int song)
{
    const uint32_t h =
      hash32(static_cast<uint32_t>(style) + 3u, static_cast<uint32_t>(song) + 29u, 0x5A17u, 191u);
    const uint16_t v = static_cast<uint16_t>(h >> 16);
    return v == 0 ? 1 : v;
}

int songStyle(int song)
{
    return static_cast<int>(hash32(static_cast<uint32_t>(song) + 5u, 0x51u, 0x7Au, 3u) >> 8) % kStyleCount;
}

int chordRoot(const Setup& s, int bar)
{
    return chordAt(s, styles[s.style % kStyleCount], bar < 0 ? 0 : bar);
}

int chordLength(const Setup& s)
{
    return chordBarsOf(s, styles[s.style % kStyleCount]);
}

int skeletonWeightOf(const Setup& s, int part, int pos, int bar)
{
    return skeletonWeight(s, styles[s.style % kStyleCount], part, pos, bar);
}

bool isBreakBar(const Setup& s, int bar)
{
    if (bar < 0 || bar >= barCount(s))
        return false;
    return isBreakPhrase(bar / kPhraseBars, phraseCount(s));
}

int barEnergy(const Setup& s, int bar)
{
    const int bars = barCount(s);
    if (bar < 0)
        bar = 0;
    if (bar >= bars)
        bar = bars - 1;
    const int phrases = phraseCount(s);
    if (phrases == 1)
        return (bars >= 3 && bar == 0) ? 2 : 3; // ease in
    return phraseEnergy(s, bar / kPhraseBars);
}

bool isFillBar(const Setup& s, int bar)
{
    const int bars = barCount(s);
    if (bars < 3)
        return false;
    return bar == bars - 1 || (bar % kPhraseBars) == kPhraseBars - 1;
}

Event stepEvent(const Setup& s, uint8_t lane, uint16_t offset)
{
    if (offset >= s.length)
        return Event();
    switch (s.mode)
    {
        case kDrums: return drumEvent(s, lane, offset);
        case kAmbient: return ambientEvent(s, lane, offset);
        case kBass: return lane == 0 ? bassEvent(s, offset) : Event();
        case kLead: return lane == 0 ? leadEvent(s, offset) : Event();
        case kDrone: return droneEvent(s, lane, offset);
        default: return Event();
    }
}

} // namespace gen

#endif // TRACKER_MINI
