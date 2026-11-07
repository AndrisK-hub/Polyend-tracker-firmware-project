// Sound chip. This first version only keeps the registers quiet; the real generators come next.
#include "nes.h"

namespace nes {

struct Apu::Impl
{
    int unused;
};

Apu::Impl* Apu::impl()
{
    return reinterpret_cast<Impl*>(implStorage);
}

void Apu::reset()
{
    frameIrq = false;
    dmcIrq = false;
}

void Apu::write(uint16_t addr, uint8_t v, int64_t cpuCycle)
{
    (void)addr;
    (void)v;
    (void)cpuCycle;
}

uint8_t Apu::readStatus(int64_t cpuCycle)
{
    (void)cpuCycle;
    return 0;
}

void Apu::runTo(int64_t cpuCycle)
{
    (void)cpuCycle;
}

int Apu::readSamples(int16_t* out, int count)
{
    (void)out;
    (void)count;
    return 0;
}

int Apu::samplesAvailable() const
{
    return 0;
}

void Apu::clearBuffer() {}

} // namespace nes
