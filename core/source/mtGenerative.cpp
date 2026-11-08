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
    // 6 phrases or more: intro, build, a break at 55%, the peak, an outro
    const int brk = (phrases * 11 + 10) / 20;
    const int breakLen = phrases >= 12 ? 2 : 1;
    if (phrase >= brk && phrase < brk + breakLen)
        return 0;
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
    if (phrases >= 3 && phrase != 0 && phrase != phrases - 1 && phrase != firstPeakPhrase(phrases) && e != 0)
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
    return st.prog[slot];
}

// Every melodic part reads its chord from here, so a bassline, pads and a lead of one style and
// one song always change chord in the same bar and agree on which chord it is.
int chordBarsOf(const Setup& s, const Style& st)
{
    return st.progBars * (evolveIndex(s) == kGentle ? 2 : 1);
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
    int weight = weightOf(st.drums[lane], pos);

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

    const int weight = weightOf(st.bass, pos);
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
// are cut in the break. 'ornament' is set for the single high notes between the chords.
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
        return h;

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

// Lead. It has a motif per phrase: the first two bars play the same rhythm and contour (on
// their own chord), the third answers and the fourth closes. Gentle repeats the motif for eight
// bars, Wild plays something new every bar.
uint32_t motifKey(const Setup& s, int bar)
{
    if (evolveIndex(s) == kWild)
        return static_cast<uint32_t>(bar);
    const int role = bar % kPhraseBars;
    return static_cast<uint32_t>(variationKey(s, bar) * 4 + (role < 2 ? 0 : role));
}

// the lowest weight the lead plays at each energy: the lead enters with the build
constexpr int8_t kLeadMinWeight[5] = { 10, 10, 5, 3, 1 };

Hit leadHit(const Setup& s, const Style& st, uint16_t offset)
{
    Hit h;
    const int bar = offset / kBarSteps;
    const int pos = offset % kBarSteps;
    const int e = barEnergy(s, bar);
    const int before = bar == 0 ? 0 : barEnergy(s, bar - 1);

    if (e < 2)
    {
        if (pos == 0 && before >= 2)
            h.off = true; // the lead stops where the music drops
        return h;
    }

    const int mut = mutationTenths(s);
    const int weight = weightOf(st.lead, pos);
    const uint32_t key = motifKey(s, bar);
    if (weight >= 9)
    {
        h.on = true;
        // notes between the beats drop out now and then (Normal a little, Wild a lot)
        static const uint8_t drop[kEvolveCount] = { 0, 15, 40 }; // of 255
        if ((pos % 4) != 0 && rnd(s, 122, key, static_cast<uint32_t>(pos)) < drop[evolveIndex(s)])
            h.on = false;
    }
    else if (weight > 0 && weight >= kLeadMinWeight[e])
    {
        const int chance = weight * kGain[e] * mut / 20 + 20;
        h.on = rnd(s, 120, key, static_cast<uint32_t>(pos)) < chance;
    }
    else if (weight == 0 && evolveIndex(s) == kWild && (pos % 2) == 0 && e >= 3)
    {
        h.on = rnd(s, 123, key, static_cast<uint32_t>(pos)) < 20; // Wild adds notes of its own
    }
    // a run of notes at the end of a fill bar
    if (!h.on && isFillBar(s, bar) && pos >= 12)
    {
        const int scaled = fillStrength(s, bar) * mut;
        h.on = rnd(s, 121, static_cast<uint32_t>(bar), static_cast<uint32_t>(pos)) < 110 * scaled / 20;
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
// Lead
// the chord tones (the root, the third and the fifth) are the places where the melody rests
int snapToChord(int degree, int chord, int count)
{
    const int rel = degree - chord;
    const int octave = floorDiv(rel, count);
    const int d = rel - octave * count;
    const int tones[4] = { 0, 2 % count, 4 % count, count };
    int best = 0;
    int bestDist = 99;
    for (int i = 0; i < 4; i++)
    {
        const int dist = d > tones[i] ? d - tones[i] : tones[i] - d;
        if (dist < bestDist)
        {
            bestDist = dist;
            best = tones[i];
        }
    }
    return chord + octave * count + best;
}

// The scale degree the lead plays at 'pos' of a bar: a walk that starts on a chord tone, moves
// mostly by step and lands on chord tones on the beats. The walk is the same for the bars of a
// motif, only the chord under it changes.
int leadDegree(const Setup& s, const Style& st, const Scale& sc, int bar, int pos)
{
    const int chord = chordAt(s, st, bar);
    const uint32_t key = motifKey(s, bar);
    const int lo = -2;
    const int hi = sc.count + 4;
    const bool fill = isFillBar(s, bar);
    int degree = chord;
    bool started = false;

    for (int p = 0; p <= pos; p++)
    {
        if (!leadHit(s, st, static_cast<uint16_t>(bar * kBarSteps + p)).on)
            continue;
        if (!started)
        {
            static const int8_t tones[3] = { 0, 2, 4 };
            degree = chord + tones[rnd(s, 130, key, 0) % 3];
            if (rnd(s, 131, key, 0) % 100 < 35)
                degree += sc.count;
            started = true;
        }
        else
        {
            const int r = rnd(s, 132, key, static_cast<uint32_t>(p)) % 100;
            const int roll = rnd(s, 133, key, static_cast<uint32_t>(p));
            int dir = (roll & 1) ? 1 : -1;
            int step;
            if (fill && p >= 12)
            {
                dir = 1; // the run at the end of a fill bar climbs
                step = 1 + (roll >> 1) % 2;
            }
            else if (r < st.still)
                step = 0;
            else if (r < st.still + st.leap)
                step = dir * (3 + (roll >> 1) % 3);
            else
                step = dir * (((roll >> 1) % 10) < 7 ? 1 : 2);
            degree += step;
        }
        while (degree > hi)
            degree -= sc.count;
        while (degree < lo)
            degree += sc.count;
        if (p % 4 == 0)
            degree = snapToChord(degree, chord, sc.count);
    }
    return degree;
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
    v -= rnd(s, 134, static_cast<uint32_t>(offset), 0) % 12;
    ev.on = true;
    ev.velocity = static_cast<uint8_t>(clampInt(v, 30, 127));
    ev.note = static_cast<int16_t>(noteOfDegree(s, sc, leadDegree(s, st, sc, bar, pos)));

    // The length: the style says how much of the time to the next note a note sounds; some notes
    // are clipped, some held, and the last note of a phrase is held.
    const int span = stepsToNext(s, 0, offset, kBarSteps);
    int gate = st.leadGate;
    const int r = rnd(s, 140, motifKey(s, bar), static_cast<uint32_t>(pos)) % 100;
    if (r < 25)
        gate -= 25;
    else if (r < 45)
        gate += 35;
    if ((bar % kPhraseBars) == kPhraseBars - 1 && span >= 3 && pos >= 8)
        gate = 100;
    ev.length = gatedLength(span, clampInt(gate, 15, 100));
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
        default: return Event();
    }
}

} // namespace gen

#endif // TRACKER_MINI
