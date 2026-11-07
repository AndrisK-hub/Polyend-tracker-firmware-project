// Sound chip (APU): two pulse channels, triangle, noise and the delta-modulation channel.
//
// The channels run on the CPU clock and only do work when something changes (a timer runs out, a
// frame-counter step, a register write). Every change of the mixed output becomes a band-limited
// step that is added to a small buffer at the right sub-sample position, so the result at 44.1 kHz
// has no harsh aliasing without ever simulating 1.79 million samples per second.
#include "nes.h"

#include <math.h>

namespace nes {

namespace {

constexpr uint8_t kLengthTable[32] = { 10, 254, 20, 2,  40, 4,  80, 6,   160, 8,   60,
                                       10, 14,  12, 26, 14, 12, 16, 24,  18,  48,  20,
                                       96, 22,  192, 24, 72, 26, 16, 28, 32,  30 };

constexpr uint8_t kDuty[4][8] = { { 0, 1, 0, 0, 0, 0, 0, 0 },
                                  { 0, 1, 1, 0, 0, 0, 0, 0 },
                                  { 0, 1, 1, 1, 1, 0, 0, 0 },
                                  { 1, 0, 0, 1, 1, 1, 1, 1 } };

constexpr uint16_t kNoiseNtsc[16] = { 4,  8,  16,  32,  64,  96,   128,  160,
                                      202, 254, 380, 508, 762, 1016, 2034, 4068 };
constexpr uint16_t kNoisePal[16] = { 4,  8,  14,  30,  60,  88,  118,  148,
                                     188, 236, 354, 472, 708, 944, 1890, 3778 };

constexpr uint16_t kDmcNtsc[16] = { 428, 380, 340, 320, 286, 254, 226, 214,
                                    190, 160, 142, 128, 106, 84,  72,  54 };
constexpr uint16_t kDmcPal[16] = { 398, 354, 316, 298, 276, 236, 210, 198,
                                   176, 148, 132, 118, 98,  78,  66,  50 };

// Frame counter: CPU cycles after the counter was reset at which each step happens, and the length
// of the whole sequence.
constexpr int32_t kFrameNtsc4[4] = { 7457, 14913, 22371, 29829 };
constexpr int32_t kFrameNtsc5[5] = { 7457, 14913, 22371, 29829, 37281 };
constexpr int32_t kFramePal4[4] = { 8313, 16627, 24939, 33253 };
constexpr int32_t kFramePal5[5] = { 8313, 16627, 24939, 33253, 41565 };
constexpr int32_t kFrameLenNtsc4 = 29830, kFrameLenNtsc5 = 37282;
constexpr int32_t kFrameLenPal4 = 33254, kFrameLenPal5 = 41566;

int roundi(double x)
{
    return static_cast<int>(x >= 0 ? x + 0.5 : x - 0.5);
}

constexpr int kKernelTaps = 16;
constexpr int kKernelPhases = 32;
constexpr int32_t kKernelSum = 16384;

int16_t g_pulseMix[31];
int16_t g_tndMix[203];
int32_t g_kernel[kKernelPhases][kKernelTaps];
bool g_tablesReady = false;

// The two non-linear mixer curves of the real chip and the band-limited step kernel.
void buildTables()
{
    if (g_tablesReady)
        return;

    g_pulseMix[0] = 0;
    for (int n = 1; n < 31; n++)
        g_pulseMix[n] = static_cast<int16_t>(roundi(16384.0 * 95.52 / (8128.0 / n + 100.0)));
    g_tndMix[0] = 0;
    for (int n = 1; n < 203; n++)
        g_tndMix[n] = static_cast<int16_t>(roundi(16384.0 * 163.67 / (24329.0 / n + 100.0)));

    const double pi = 3.14159265358979323846;
    const double cutoff = 0.92; // of the Nyquist frequency
    const double half = 8.0;
    for (int ph = 0; ph < kKernelPhases; ph++)
    {
        const double d = ph / static_cast<double>(kKernelPhases);
        double h[kKernelTaps];
        double sum = 0;
        for (int k = 0; k < kKernelTaps; k++)
        {
            const double x = (k - 8) - d; // distance of this output sample from the step
            const double a = pi * cutoff * x;
            const double s = fabs(a) < 1e-9 ? 1.0 : sin(a) / a;
            const double w =
              fabs(x) >= half ? 0.0 : 0.42 + 0.5 * cos(pi * x / half) + 0.08 * cos(2 * pi * x / half);
            h[k] = s * w;
            sum += h[k];
        }
        int32_t total = 0;
        int biggest = 0;
        for (int k = 0; k < kKernelTaps; k++)
        {
            g_kernel[ph][k] = static_cast<int32_t>(roundi(h[k] / sum * kKernelSum));
            total += g_kernel[ph][k];
            if (fabs(h[k]) > fabs(h[biggest]))
                biggest = k;
        }
        g_kernel[ph][biggest] += kKernelSum - total; // every phase adds up to exactly 1.0
    }
    g_tablesReady = true;
}

} // namespace

// ---------------------------------------------------------------------------------------------
void Apu::Envelope::clock()
{
    if (start)
    {
        start = false;
        decay = 15;
        divider = volume;
    }
    else if (divider == 0)
    {
        divider = volume;
        if (decay > 0)
            decay--;
        else if (loop)
            decay = 15;
    }
    else
        divider--;
}

bool Apu::Pulse::muted() const
{
    if (period < 8)
        return true;
    // The sweep unit always works out the period it would move to, and a period that would not
    // fit silences the channel even when the sweep is off.
    if (!sweepNegate)
        return (period + (period >> sweepShift)) > 0x7FF;
    return false;
}

void Apu::Pulse::sweepClock()
{
    const int change = period >> sweepShift;
    const int target = sweepNegate ? period - change - (onesComplement ? 1 : 0) : period + change;
    if (sweepDivider == 0 && sweepEnabled && sweepShift > 0 && !muted())
        period = static_cast<uint16_t>(target & 0x7FF);
    if (sweepDivider == 0 || sweepReload)
    {
        sweepDivider = sweepPeriod;
        sweepReload = false;
    }
    else
        sweepDivider--;
}

// ---------------------------------------------------------------------------------------------
void Apu::setSampleRate(uint32_t hz)
{
    if (hz == 0)
        hz = 44100;
    sampleRate = hz;
    factor = (static_cast<uint64_t>(hz) << 32) / static_cast<uint64_t>(clockHz);
    hpA1 = expf(-6.2831853f * 37.0f / static_cast<float>(hz));
    hpA2 = expf(-6.2831853f * 14.0f / static_cast<float>(hz));
}

void Apu::reset()
{
    buildTables();

    pal = sys && sys->palTiming;
    clockHz = pal ? 1662607 : 1789773;
    now = sys ? sys->cycles : 0;

    pulse1 = Pulse();
    pulse2 = Pulse();
    pulse1.onesComplement = true;
    pulse1.next = pulse2.next = kNever;
    tri = Triangle();
    tri.next = kNever;
    tri.out = 15; // the sequencer starts at the top of the wave
    noise = Noise();
    noise.period = pal ? kNoisePal[0] : kNoiseNtsc[0];
    noise.next = kNever;
    dmc = Dmc();
    dmc.rate = pal ? kDmcPal[0] : kDmcNtsc[0];
    dmc.next = now + dmc.rate;

    frameIrq = false;
    dmcIrq = false;
    mode5 = false;
    irqInhibit = false;
    frameIdx = 0;
    frameStart = now;
    scheduleFrame();

    refreshOutputs();
    lastLevel = g_pulseMix[pulse1.out + pulse2.out] +
                g_tndMix[3 * tri.out + 2 * noise.out + dmc.level];
    setSampleRate(sampleRate);
    clearBuffer();
    recomputeNext();
}

void Apu::clearBuffer()
{
    memset(acc, 0, sizeof(acc));
    readPos = 0;
    baseCycle = now;
    baseFrac = 0;
    // The output filters start settled at the present level, so there is no click.
    integ = lastLevel * kKernelSum;
    const float real = static_cast<float>(lastLevel) * (1.0f / 16384.0f);
    hpX1 = real;
    hpY1 = 0;
    hpX2 = 0;
    hpY2 = 0;
}

// ---------------------------------------------------------------------------------------------
void Apu::scheduleFrame()
{
    const int32_t* steps;
    if (pal)
        steps = mode5 ? kFramePal5 : kFramePal4;
    else
        steps = mode5 ? kFrameNtsc5 : kFrameNtsc4;
    frameNext = frameStart + steps[frameIdx];
}

void Apu::clockQuarter()
{
    pulse1.env.clock();
    pulse2.env.clock();
    noise.env.clock();

    if (tri.linearReload)
        tri.linear = tri.linearValue;
    else if (tri.linear > 0)
        tri.linear--;
    if (!tri.control)
        tri.linearReload = false;
}

void Apu::clockHalf()
{
    if (!pulse1.env.loop && pulse1.length > 0)
        pulse1.length--;
    if (!pulse2.env.loop && pulse2.length > 0)
        pulse2.length--;
    if (!tri.control && tri.length > 0)
        tri.length--;
    if (!noise.env.loop && noise.length > 0)
        noise.length--;
    pulse1.sweepClock();
    pulse2.sweepClock();
}

void Apu::clockFrameStep()
{
    const int64_t t = frameNext;
    const int idx = frameIdx;
    if (!mode5)
    {
        clockQuarter();
        if (idx == 1 || idx == 3)
            clockHalf();
        if (idx == 3 && !irqInhibit)
            frameIrq = true;
    }
    else if (idx != 3)
    {
        clockQuarter();
        if (idx == 1 || idx == 4)
            clockHalf();
    }

    const int count = mode5 ? 5 : 4;
    if (++frameIdx == count)
    {
        frameIdx = 0;
        int32_t len;
        if (pal)
            len = mode5 ? kFrameLenPal5 : kFrameLenPal4;
        else
            len = mode5 ? kFrameLenNtsc5 : kFrameLenNtsc4;
        frameStart += len;
    }
    scheduleFrame();

    pulseSchedule(pulse1, t);
    pulseSchedule(pulse2, t);
    triangleSchedule(t);
    noiseSchedule(t);
}

// ---------------------------------------------------------------------------------------------
// A silent channel needs no timer events at all; these start one when a channel wakes up.
void Apu::pulseSchedule(Pulse& p, int64_t at)
{
    if (!p.active())
        p.next = kNever;
    else if (p.next == kNever)
        p.next = at + 2 * (static_cast<int64_t>(p.period) + 1);
}

void Apu::triangleSchedule(int64_t at)
{
    if (!tri.active())
        tri.next = kNever;
    else if (tri.next == kNever)
        tri.next = at + tri.period + 1;
}

void Apu::noiseSchedule(int64_t at)
{
    if (noise.length == 0)
        noise.next = kNever;
    else if (noise.next == kNever)
        noise.next = at + noise.period;
}

void Apu::refreshOutputs()
{
    pulse1.out = (pulse1.active() && kDuty[pulse1.duty][pulse1.dutyPos]) ? pulse1.env.level() : 0;
    pulse2.out = (pulse2.active() && kDuty[pulse2.duty][pulse2.dutyPos]) ? pulse2.env.level() : 0;
    noise.out = (noise.length > 0 && !(noise.lfsr & 1)) ? noise.env.level() : 0;
}

void Apu::recomputeNext()
{
    int64_t n = frameNext;
    if (pulse1.next < n)
        n = pulse1.next;
    if (pulse2.next < n)
        n = pulse2.next;
    if (tri.next < n)
        n = tri.next;
    if (noise.next < n)
        n = noise.next;
    if (dmc.next < n)
        n = dmc.next;
    nextEvent = n;
}

// ---------------------------------------------------------------------------------------------
void Apu::dmcFetch()
{
    if (dmc.bytesLeft == 0 || dmc.bufferFull)
        return;
    uint8_t b = 0;
    if (sys)
    {
        b = sys->bank.prg[(dmc.addr >> 13) & 3][dmc.addr & 0x1FFF];
        sys->cycles += 4; // the fetch takes the bus from the CPU for a few cycles
    }
    dmc.buffer = b;
    dmc.bufferFull = true;
    dmc.addr = static_cast<uint16_t>(dmc.addr + 1);
    if (dmc.addr == 0)
        dmc.addr = 0x8000;
    if (--dmc.bytesLeft == 0)
    {
        if (dmc.loop)
        {
            dmc.addr = dmc.startAddr;
            dmc.bytesLeft = dmc.sampleLength;
        }
        else if (dmc.irqEnable)
            dmcIrq = true;
    }
}

void Apu::dmcClock()
{
    if (!dmc.silence)
    {
        if (dmc.shift & 1)
        {
            if (dmc.level <= 125)
                dmc.level += 2;
        }
        else if (dmc.level >= 2)
            dmc.level -= 2;
        dmc.shift >>= 1;
    }
    if (--dmc.bits == 0)
    {
        dmc.bits = 8;
        if (dmc.bufferFull)
        {
            dmc.silence = false;
            dmc.shift = dmc.buffer;
            dmc.bufferFull = false;
            dmcFetch();
        }
        else
            dmc.silence = true;
    }
}

// ---------------------------------------------------------------------------------------------
void Apu::addStep(int64_t cycle, int delta)
{
    const uint64_t pos = baseFrac + static_cast<uint64_t>(cycle - baseCycle) * factor;
    const uint32_t idx = static_cast<uint32_t>(pos >> 32);
    if (idx > static_cast<uint32_t>(kAccSize - kKernelTaps - 1))
        return; // nobody has taken the samples for a long time
    const int32_t* kern = g_kernel[(pos >> 27) & (kKernelPhases - 1)];
    for (int i = 0; i < kKernelTaps; i++)
        acc[(readPos + idx + i) & (kAccSize - 1)] += delta * kern[i];
}

void Apu::setMix(int64_t cycle)
{
    const int level =
      g_pulseMix[pulse1.out + pulse2.out] + g_tndMix[3 * tri.out + 2 * noise.out + dmc.level];
    if (level != lastLevel)
    {
        addStep(cycle, level - lastLevel);
        lastLevel = level;
    }
}

void Apu::advance(int64_t target)
{
    while (nextEvent <= target)
    {
        const int64_t t = nextEvent;

        if (frameNext == t)
            clockFrameStep();

        if (pulse1.next == t)
        {
            pulse1.dutyPos = (pulse1.dutyPos + 1) & 7;
            pulse1.next = t + 2 * (static_cast<int64_t>(pulse1.period) + 1);
            pulseSchedule(pulse1, t);
        }
        if (pulse2.next == t)
        {
            pulse2.dutyPos = (pulse2.dutyPos + 1) & 7;
            pulse2.next = t + 2 * (static_cast<int64_t>(pulse2.period) + 1);
            pulseSchedule(pulse2, t);
        }
        if (tri.next == t)
        {
            if (tri.active())
            {
                tri.pos = (tri.pos + 1) & 31;
                tri.out = tri.pos < 16 ? 15 - tri.pos : tri.pos - 16;
            }
            tri.next = t + tri.period + 1;
            triangleSchedule(t);
        }
        if (noise.next == t)
        {
            const uint16_t bit = (noise.lfsr ^ (noise.lfsr >> (noise.shortMode ? 6 : 1))) & 1;
            noise.lfsr = static_cast<uint16_t>((noise.lfsr >> 1) | (bit << 14));
            noise.next = t + noise.period;
            noiseSchedule(t);
        }
        if (dmc.next == t)
        {
            dmcClock();
            dmc.next = t + dmc.rate;
        }

        refreshOutputs();
        recomputeNext();
        setMix(t);
    }
    if (target > now)
        now = target;
}

void Apu::runTo(int64_t cpuCycle)
{
    if (cpuCycle < nextEvent)
    {
        if (cpuCycle > now)
            now = cpuCycle;
        return;
    }
    advance(cpuCycle);
}

// ---------------------------------------------------------------------------------------------
void Apu::afterWrite(int64_t cycle)
{
    pulseSchedule(pulse1, cycle);
    pulseSchedule(pulse2, cycle);
    triangleSchedule(cycle);
    noiseSchedule(cycle);
    refreshOutputs();
    recomputeNext();
    setMix(cycle);
}

void Apu::write(uint16_t addr, uint8_t v, int64_t cpuCycle)
{
    advance(cpuCycle);

    switch (addr)
    {
        case 0x4000:
        case 0x4004:
        {
            Pulse& p = addr == 0x4000 ? pulse1 : pulse2;
            p.duty = v >> 6;
            p.env.loop = (v & 0x20) != 0;
            p.env.constant = (v & 0x10) != 0;
            p.env.volume = v & 15;
            break;
        }
        case 0x4001:
        case 0x4005:
        {
            Pulse& p = addr == 0x4001 ? pulse1 : pulse2;
            p.sweepEnabled = (v & 0x80) != 0;
            p.sweepPeriod = (v >> 4) & 7;
            p.sweepNegate = (v & 8) != 0;
            p.sweepShift = v & 7;
            p.sweepReload = true;
            break;
        }
        case 0x4002:
        case 0x4006:
        {
            Pulse& p = addr == 0x4002 ? pulse1 : pulse2;
            p.period = static_cast<uint16_t>((p.period & 0x700) | v);
            break;
        }
        case 0x4003:
        case 0x4007:
        {
            Pulse& p = addr == 0x4003 ? pulse1 : pulse2;
            p.period = static_cast<uint16_t>((p.period & 0xFF) | ((v & 7) << 8));
            if (p.enabled)
                p.length = kLengthTable[v >> 3];
            p.dutyPos = 0;
            p.env.start = true;
            break;
        }
        case 0x4008:
            tri.control = (v & 0x80) != 0;
            tri.linearValue = v & 0x7F;
            break;
        case 0x400A: tri.period = static_cast<uint16_t>((tri.period & 0x700) | v); break;
        case 0x400B:
            tri.period = static_cast<uint16_t>((tri.period & 0xFF) | ((v & 7) << 8));
            if (tri.enabled)
                tri.length = kLengthTable[v >> 3];
            tri.linearReload = true;
            break;
        case 0x400C:
            noise.env.loop = (v & 0x20) != 0;
            noise.env.constant = (v & 0x10) != 0;
            noise.env.volume = v & 15;
            break;
        case 0x400E:
            noise.shortMode = (v & 0x80) != 0;
            noise.period = pal ? kNoisePal[v & 15] : kNoiseNtsc[v & 15];
            break;
        case 0x400F:
            if (noise.enabled)
                noise.length = kLengthTable[v >> 3];
            noise.env.start = true;
            break;
        case 0x4010:
            dmc.irqEnable = (v & 0x80) != 0;
            dmc.loop = (v & 0x40) != 0;
            dmc.rate = pal ? kDmcPal[v & 15] : kDmcNtsc[v & 15];
            if (!dmc.irqEnable)
                dmcIrq = false;
            break;
        case 0x4011: dmc.level = v & 0x7F; break;
        case 0x4012: dmc.startAddr = static_cast<uint16_t>(0xC000 + (v << 6)); break;
        case 0x4013: dmc.sampleLength = static_cast<uint16_t>((v << 4) + 1); break;
        case 0x4015:
            pulse1.enabled = (v & 1) != 0;
            pulse2.enabled = (v & 2) != 0;
            tri.enabled = (v & 4) != 0;
            noise.enabled = (v & 8) != 0;
            if (!pulse1.enabled)
                pulse1.length = 0;
            if (!pulse2.enabled)
                pulse2.length = 0;
            if (!tri.enabled)
                tri.length = 0;
            if (!noise.enabled)
                noise.length = 0;
            dmcIrq = false;
            if (v & 0x10)
            {
                if (dmc.bytesLeft == 0)
                {
                    dmc.addr = dmc.startAddr;
                    dmc.bytesLeft = dmc.sampleLength;
                }
                dmcFetch();
            }
            else
                dmc.bytesLeft = 0;
            break;
        case 0x4017:
            mode5 = (v & 0x80) != 0;
            irqInhibit = (v & 0x40) != 0;
            if (irqInhibit)
                frameIrq = false;
            frameIdx = 0;
            frameStart = cpuCycle + 3; // the counter restarts a few cycles after the write
            scheduleFrame();
            if (mode5)
            {
                clockQuarter();
                clockHalf();
            }
            break;
        default: break;
    }

    afterWrite(cpuCycle);
}

uint8_t Apu::readStatus(int64_t cpuCycle)
{
    advance(cpuCycle);
    uint8_t v = 0;
    if (pulse1.length > 0)
        v |= 0x01;
    if (pulse2.length > 0)
        v |= 0x02;
    if (tri.length > 0)
        v |= 0x04;
    if (noise.length > 0)
        v |= 0x08;
    if (dmc.bytesLeft > 0)
        v |= 0x10;
    if (frameIrq)
        v |= 0x40;
    if (dmcIrq)
        v |= 0x80;
    frameIrq = false;
    return v;
}

// ---------------------------------------------------------------------------------------------
int Apu::samplesAvailable() const
{
    const uint64_t pos = baseFrac + static_cast<uint64_t>(now - baseCycle) * factor;
    const uint64_t n = pos >> 32;
    return n > static_cast<uint64_t>(kAccSize - kKernelTaps - 1)
             ? kAccSize - kKernelTaps - 1
             : static_cast<int>(n);
}

int Apu::takeSample()
{
    const uint32_t slot = readPos & (kAccSize - 1);
    integ += acc[slot];
    acc[slot] = 0;
    readPos++;

    // 1.0 of mixer level is kKernelSum * 16384 in the integrator
    const float x = static_cast<float>(integ) * (1.0f / (16384.0f * static_cast<float>(kKernelSum)));
    const float y1 = hpA1 * (hpY1 + x - hpX1);
    hpX1 = x;
    hpY1 = y1;
    const float y2 = hpA2 * (hpY2 + y1 - hpX2);
    hpX2 = y1;
    hpY2 = y2;

    float v = y2 * 32768.0f;
    if (v > 32767.0f)
        v = 32767.0f;
    else if (v < -32768.0f)
        v = -32768.0f;
    return static_cast<int>(v);
}

int Apu::readSamples(int16_t* out, int count)
{
    // Nobody took the samples for longer than the buffer holds: what was lost cannot be put together
    // again, so start afresh at the present level instead of lagging behind for ever.
    const uint64_t raw = (baseFrac + static_cast<uint64_t>(now - baseCycle) * factor) >> 32;
    if (raw > static_cast<uint64_t>(kAccSize - kKernelTaps - 1))
    {
        clearBuffer();
        return 0;
    }
    int n = samplesAvailable();
    if (count < n)
        n = count;
    for (int i = 0; i < n; i++)
        out[i] = static_cast<int16_t>(takeSample());

    // Count positions from the first sample that has not been taken.
    const uint64_t pos = baseFrac + static_cast<uint64_t>(now - baseCycle) * factor;
    baseFrac = pos - (static_cast<uint64_t>(n) << 32);
    baseCycle = now;
    return n;
}

} // namespace nes
