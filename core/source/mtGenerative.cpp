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
};

constexpr Style styles[kStyleCount] = {
    // the kick has its places at 1 and 3, a snare on 2 and 4, steady eighth hats
    { "SynthPop",
      { "9.....2.9.3...5.", "....9.......9..3", "9292929292929292", "......3.......5.",
        "......4.....3.4.", nullptr },
      "9.3.9.3.9.3.9.3.", { 0, 5, 2, 6 }, 2, 12, 14, 8, false, 1 },
    // syncopated kick, open hat on the last offbeat
    { "NewWave",
      { "9..9....9.7...3.", "....9.......9..2", "9292929292929230", "..............7.",
        "..3...5...3...5.", nullptr },
      "9.3.9.3.9.5.9.3.", { 0, 2, 5, 4 }, 2, 14, 12, 10, false, 1 },
    // four on the floor, claps, offbeat hats, rolling octave bass
    { "Italo",
      { "9...9...9...9.5.", "....9.......9..3", "..9.3.9...9.3.9.", "..............7.",
        "..3..4..3..4..3.", nullptr },
      "9.9.9.9.9.9.9.9.", { 0, 5, 3, 4 }, 2, 0, 6, 6, true, 1 },
    // sixteenth hats that leave out the snare steps, octave bounce
    { "Disco",
      { "9...9...9...9...", "....9.......9...", "99.9.9.999.9.9.9", "..9...9...9...9.",
        "...3..3....3..3.", nullptr },
      "9.9.5.9.9.9.5.9.", { 0, 3, 6, 2 }, 2, 0, 10, 8, true, 3 },
    // four on the floor, offbeat hats and open hats, deep stabs
    { "House",
      { "9...9...9...9...", "....9.......9...", ".9.9.9.9.9.9.9.9", "..9...9...9...9.",
        "..3...4...3...5.", nullptr },
      "..9...9...9...9.", { 0, 0, 5, 6 }, 4, 12, 12, 10, false, 3 },
    // four on the floor, sixteenth hats, rolling offbeat bass
    { "Techno",
      { "9...9...9...9...", "....7.......7...", "9595959595959595", "..9...9...9...9.",
        "..3..5..3..5..3.", nullptr },
      ".9.9.9.9.9.9.9.9", { 0, 0, 1, 0 }, 4, 8, 10, 14, false, 7 },
    // eighth note kick, sixteenth hats, sequenced bass, metal hits
    { "EBM",
      { "9.9.9.9.9.9.9.9.", "....9.......9...", "9999999999999999", "..............5.",
        "5..5..5..5..5...", nullptr },
      "9.9.9.9.9.9.9.9.", { 0, 0, 3, 4 }, 4, 18, 14, 10, false, 7 },
    // syncopated kick and claps
    { "Electro",
      { "9......5..9...3.", "....9.......9...", "9.9.9.9.9.9.9.9.", "......3.......5.",
        "..3.5.3.5.3.....", nullptr },
      "9.....5.9..9.3..", { 0, 0, 5, 3 }, 2, 10, 14, 10, false, 1 },
    // broken beats: ghost snares between the backbeats
    { "Breaks",
      { "9.....3...9...2.", "....9..2.2..9.2.", "9.9.9.9.9.9.9.9.", "..9...5...9...5.",
        ".3..3...3.3.3...", nullptr },
      "9.....5..9....3.", { 0, 0, 3, 5 }, 2, 12, 12, 10, false, 1 },
    // boom bap
    { "HipHop",
      { "9.....5...9...2.", "....9.......9...", "9.9.9.9.9.9.9.9.", "..............3.",
        "......3.....3...", nullptr },
      "9.....5...9...3.", { 0, 5, 3, 4 }, 2, 6, 10, 10, false, 1 },
    // half-time snare, hat bursts, long sub bass
    { "Trap",
      { "9......5..9....3", "........9.......", "9.9.9.9.9.9.9.9.", "..............4.",
        "..3.....3.....3.", nullptr },
      "9......5..9....3", { 0, 0, 5, 4 }, 4, 6, 8, 8, false, 1 },
    // two-step: kick, snare on 2 and 4, fast hats, reese bass
    { "DnB",
      { "9.........7....2", "....9.......9...", "9.9.9.9.9.9.9.9.", "......3.......5.",
        "..3.....3.3...3.", nullptr },
      "9.........7....3", { 0, 5, 3, 6 }, 2, 10, 10, 12, false, 1 },
    // very sparse: a kick now and then, soft ticks, one long bass note per bar
    { "Ambient",
      { "9.......2.......", "........3.......", "..3.....2...3...", "................",
        "...2......2.....", nullptr },
      "9...............", { 0, 3, 5, 4 }, 4, 0, 20, 10, false, 1 },
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
// Bassline
Event bassEvent(const Setup& s, uint16_t offset)
{
    Event ev;
    const Style& st = styles[s.style % kStyleCount];
    const int bar = offset / kBarSteps;
    const int pos = offset % kBarSteps;
    const int bars = barCount(s);
    const int e = barEnergy(s, bar);
    const int mut = mutationTenths(s);
    const Scale sc = effectiveScale(s, st);

    if (e == 0)
    {
        // the break: the bass is cut at its start
        if (pos == 0 && (bar == 0 || barEnergy(s, bar - 1) > 0))
            ev.off = true;
        return ev;
    }

    bool on = false;
    const int weight = weightOf(st.bass, pos);
    if (weight >= 9)
        on = true;
    else if (weight > 0)
    {
        const int chance = weight * kGain[e] * mut / 10 + 40; // a bassline is busier than the hats
        on = rnd(s, 20, static_cast<uint32_t>(variationKey(s, bar)), static_cast<uint32_t>(pos)) < chance;
    }

    const int chord = chordDegree(s, st, bar, st.progBars);
    int degree = chord;
    bool pickup = false;

    // a pickup on the last sixteenth of a fill bar leads to the next chord
    if (!on && isFillBar(s, bar) && pos == 15 && bar + 1 < bars)
    {
        const int strength = fillStrength(s, bar);
        if (rnd(s, 41, static_cast<uint32_t>(bar), 0) < 90 * strength * mut / 20)
        {
            on = true;
            pickup = true;
            degree = chordDegree(s, st, bar + 1, st.progBars) - 1; // a step below the next root
        }
    }
    if (!on)
        return ev;

    if (!pickup)
    {
        const bool chordStart = (bar % st.progBars) == 0 && pos < 4;
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
    return ev;
}

// ---------------------------------------------------------------------------------------------
// Ambient pads: voices enter as the energy rises, roll in one after another on a chord change,
// and are cut in the break.
constexpr int8_t kVoiceMinEnergy[kAmbientVoices] = { 1, 2, 2, 3 };

Event ambientEvent(const Setup& s, uint8_t voice, uint16_t offset)
{
    Event ev;
    if (voice >= kAmbientVoices)
        return ev;
    const Style& st = styles[s.style % kStyleCount];
    const int bar = offset / kBarSteps;
    const int pos = offset % kBarSteps;
    const int e = barEnergy(s, bar);
    const int before = bar == 0 ? 0 : barEnergy(s, bar - 1);
    const Scale sc = effectiveScale(s, st);
    const int barsPerChord = evolveIndex(s) == kGentle ? 8 : 4;

    if (e == 0)
    {
        if (pos == voice * 2 && before > 0)
            ev.off = true; // fade out, one voice after the other
        return ev;
    }
    if (e < kVoiceMinEnergy[voice])
        return ev;

    bool on = false;
    if (pos == voice * 4) // the voices roll in one beat apart
    {
        const bool chordChange = (bar % barsPerChord) == 0;
        const bool entering = (bar % kPhraseBars) == 0 && before < kVoiceMinEnergy[voice];
        on = chordChange || entering;
        // a pad is played again in the middle of a long chord from time to time
        if (!on && (bar % barsPerChord) == barsPerChord / 2 && evolveIndex(s) != kGentle)
            on = rnd(s, 80, static_cast<uint32_t>(bar), voice) < (evolveIndex(s) == kWild ? 140 : 70);
    }
    // ornaments: single notes of the scale high up, more of them as the energy rises
    int degree;
    const int chord = chordDegree(s, st, bar, barsPerChord);
    static const int8_t tone[kAmbientVoices] = { 0, 4, 2, 4 };
    degree = chord + tone[voice] + (voice >= 2 ? sc.count : 0);
    if (!on && voice >= 2 && (pos % 4) == 0)
    {
        const int chance = (e * 4 + evolveIndex(s) * 6) * (voice == 3 ? 3 : 2) / 2;
        if (rnd(s, 81, static_cast<uint32_t>(offset), voice) < chance)
        {
            on = true;
            static const int8_t ornament[5] = { 1, 2, 4, 6, 8 };
            degree = chord + ornament[rnd(s, 82, static_cast<uint32_t>(offset), voice) % 5] + sc.count;
        }
    }
    if (!on)
        return ev;
    if (voice == 3 && e >= 4 && evolveIndex(s) == kWild)
        degree += 2; // the seventh colours the top voice at the peak

    int v = 78 - voice * 4 + rnd(s, 83, static_cast<uint32_t>(offset), voice) % 14;
    v -= (4 - e) * 2;
    ev.on = true;
    ev.velocity = static_cast<uint8_t>(clampInt(v, 30, 120));
    ev.note = static_cast<int16_t>(noteOfDegree(s, sc, degree));
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
        default: return 0;
    }
}

int16_t drumNote(uint8_t lane)
{
    // kick 60, snare 62, closed hat 64, open hat 66, cymbal 68, percussion 70
    static const int16_t notes[kDrumLanes] = { 60, 62, 64, 66, 70, 68 };
    return lane < kDrumLanes ? notes[lane] : -1;
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
        default: return Event();
    }
}

} // namespace gen

#endif // TRACKER_MINI
