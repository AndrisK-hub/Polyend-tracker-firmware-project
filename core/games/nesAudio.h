// The sound of a running NES game, as an input of the audio engine (Tracker Mini only).
//
// The game loop (main thread) writes the samples the emulated sound chip makes into a ring buffer;
// the audio engine takes them out again, one block at a time, from its interrupt. The buffer is
// plain memory the caller provides, so nothing is reserved while no game runs.
//
// The one instance is made by the audio engine (audioEngine::getNesStream()), not as a global here:
// the engine connects it to the mixer while it is being constructed, and the constructor of an audio
// stream clears its connections, so it must exist before that.
#ifndef GAMES_NESAUDIO_H_
#define GAMES_NESAUDIO_H_

#ifdef TRACKER_MINI

#include <stdint.h>

#include "AudioStream.h"

class AudioNesStream : public AudioStream
{
  public:
    AudioNesStream()
      : AudioStream(0, NULL)
    {
    }

    // Starts feeding the audio engine from the given memory. The capacity (in samples) must be a
    // power of two.
    void begin(int16_t* memory, uint32_t capacity);
    // Stops. The memory may be used for something else as soon as this returns.
    void end();

    bool isRunning() const { return playing; }
    // samples waiting to be played
    uint32_t fill() const { return head - tail; }
    // Adds samples; returns how many fitted (the rest are dropped).
    uint32_t push(const int16_t* samples, uint32_t count);
    // counts up with every block the audio engine asks for, so the game loop can tell that the
    // audio clock is running
    uint32_t blocksRequested() const { return blocks; }
    // The loudest sample sent on towards the mixer since the last call (then starts again from 0).
    // Only for the diagnostics on the game screen.
    uint32_t takeSentPeak()
    {
        const uint32_t peak = sentPeak;
        sentPeak = 0;
        return peak;
    }

    virtual void update();

    // Sound is held back until this many samples are in the buffer, and again after running dry.
    static constexpr uint32_t kPrimeSamples = 512;

  private:
    int16_t* volatile buf = nullptr;
    volatile uint32_t mask = 0;
    volatile uint32_t head = 0; // written by the game loop only
    volatile uint32_t tail = 0; // written by the audio interrupt only
    volatile bool playing = false;
    volatile bool primed = false;
    volatile uint32_t blocks = 0;
    volatile uint32_t sentPeak = 0;
    int16_t last = 0;
};

#endif // TRACKER_MINI

#endif /* GAMES_NESAUDIO_H_ */
