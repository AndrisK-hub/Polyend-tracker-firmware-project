#ifdef TRACKER_MINI

#include "nesAudio.h"

#include "config.h"

namespace {

inline void compilerBarrier()
{
    asm volatile("" ::: "memory");
}

} // namespace

void AudioNesStream::begin(int16_t* memory, uint32_t capacity)
{
    end();
    buf = memory;
    mask = capacity - 1;
    head = 0;
    tail = 0;
    last = 0;
    primed = false;
    compilerBarrier();
    playing = true;
}

void AudioNesStream::end()
{
    playing = false;
    compilerBarrier();
    buf = nullptr;
}

uint32_t AudioNesStream::push(const int16_t* samples, uint32_t count)
{
    if (!playing)
    {
        return 0;
    }
    const uint32_t capacity = mask + 1;
    const uint32_t used = head - tail;
    const uint32_t room = capacity - used;
    if (count > room)
    {
        count = room;
    }
    uint32_t h = head;
    for (uint32_t i = 0; i < count; i++)
    {
        buf[(h + i) & mask] = samples[i];
    }
    compilerBarrier(); // the samples are in place before the audio interrupt can see them
    head = h + count;
    return count;
}

AUDIO_CORE_FLASH void AudioNesStream::update()
{
    if (!playing)
    {
        return;
    }
    blocks = blocks + 1;

    const uint32_t avail = head - tail;
    if (!primed)
    {
        if (avail < kPrimeSamples)
        {
            return; // silence until there is enough for a steady stream
        }
        primed = true;
    }

    audio_block_t* block = allocate();
    if (block == nullptr)
    {
        return;
    }

    const uint32_t n = avail < AUDIO_BLOCK_SAMPLES ? avail : AUDIO_BLOCK_SAMPLES;
    const uint32_t t = tail;
    int16_t v = last;
    uint32_t i = 0;
    uint32_t peak = sentPeak;
    for (; i < n; i++)
    {
        v = buf[(t + i) & mask];
        block->data[i] = v;
        const uint32_t level = static_cast<uint32_t>(v < 0 ? -v : v);
        if (level > peak)
        {
            peak = level;
        }
    }
    sentPeak = peak;
    // ran dry: let the last value fade away instead of cutting it off
    for (; i < AUDIO_BLOCK_SAMPLES; i++)
    {
        v = static_cast<int16_t>((v * 63) / 64);
        block->data[i] = v;
    }
    last = v;
    compilerBarrier();
    tail = t + n;

    if (n < AUDIO_BLOCK_SAMPLES)
    {
        primed = false; // wait for the buffer to fill up again
    }

    transmit(block, 0);
    release(block);
}

#endif // TRACKER_MINI
