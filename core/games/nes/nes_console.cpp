// Console glue: cartridge loading, the CPU bus, controllers, OAM DMA and the frame loop.
#include "nes.h"

namespace nes {

Console::Console()
{
    memset(&bank, 0, sizeof(bank));
    memset(ram, 0, sizeof(ram));
    ppu.init(this);
    apu.init(this);
}

void* Console::alloc(uint32_t size)
{
    size = (size + 15u) & ~15u;
    if (!arena || size > arenaLeft)
        return nullptr;
    void* p = arena;
    arena += size;
    arenaLeft -= size;
    memset(p, 0, size);
    return p;
}

void Console::applyMirroring(Mirroring m)
{
    uint8_t* c0 = ppu.ciram;
    uint8_t* c1 = ppu.ciram + 1024;
    switch (m)
    {
        case kMirrorHoriz:
            bank.nt[0] = bank.nt[1] = c0;
            bank.nt[2] = bank.nt[3] = c1;
            break;
        case kMirrorVert:
            bank.nt[0] = bank.nt[2] = c0;
            bank.nt[1] = bank.nt[3] = c1;
            break;
        case kMirrorSingleLo: bank.nt[0] = bank.nt[1] = bank.nt[2] = bank.nt[3] = c0; break;
        case kMirrorSingleHi: bank.nt[0] = bank.nt[1] = bank.nt[2] = bank.nt[3] = c1; break;
        default:
            if (ntExtra)
            {
                bank.nt[0] = c0;
                bank.nt[1] = c1;
                bank.nt[2] = ntExtra;
                bank.nt[3] = ntExtra + 1024;
            }
            else
                applyMirroring(kMirrorVert);
            break;
    }
}

LoadResult Console::load(uint8_t* image, uint32_t imageSize, uint8_t* extra, uint32_t extraSize)
{
    mapper = nullptr;
    arena = extra;
    arenaLeft = extraSize;
    ntExtra = nullptr;
    cart = Cart();

    if (!image || imageSize < 16 || memcmp(image, "NES\x1A", 4) != 0)
        return kLoadBadHeader;
    const uint8_t* h = image;

    bool nes2 = (h[7] & 0x0C) == 0x08;
    uint32_t prgUnits = h[4];
    uint32_t chrUnits = h[5];
    uint16_t mapperId = static_cast<uint16_t>((h[6] >> 4) | (h[7] & 0xF0));
    uint8_t sub = 0;
    bool junk = false;
    if (nes2)
    {
        mapperId = static_cast<uint16_t>(mapperId | ((h[8] & 0x0F) << 8));
        sub = h[8] >> 4;
        if ((h[9] & 0x0F) == 0x0F || (h[9] >> 4) == 0x0F)
            return kLoadBadHeader; // exponent sizes are not used by any real cartridge
        prgUnits |= static_cast<uint32_t>(h[9] & 0x0F) << 8;
        chrUnits |= static_cast<uint32_t>(h[9] >> 4) << 8;
    }
    else if (h[12] | h[13] | h[14] | h[15])
    {
        // old dumping tools wrote text (like "DiskDude!") into bytes 7-15: none of those bytes can be trusted
        mapperId &= 0x0F;
        junk = true;
    }

    cart.nes2 = nes2;
    cart.mapper = mapperId;
    cart.submapper = sub;
    cart.prgSize = prgUnits * 16384u;
    cart.chrSize = chrUnits * 8192u;
    cart.battery = (h[6] & 2) != 0;
    cart.pal = nes2 ? ((h[12] & 3) == 1) : (!junk && (h[9] & 1) != 0);
    cart.mirroring = (h[6] & 8) ? kMirrorFour : ((h[6] & 1) ? kMirrorVert : kMirrorHoriz);

    if (cart.prgSize == 0)
        return kLoadBadHeader;
    if (!mapperSupported(mapperId))
        return kLoadUnsupportedMapper;

    bool trainer = (h[6] & 4) != 0;
    uint32_t off = 16u + (trainer ? 512u : 0u);
    if (off + cart.prgSize + cart.chrSize > imageSize)
        return kLoadTruncated;
    cart.prg = image + off;

    // cartridge RAM
    uint32_t wram = 8192;
    if (nes2)
    {
        uint32_t vol = (h[10] & 0x0F) ? (64u << (h[10] & 0x0F)) : 0u;
        uint32_t nv = (h[10] >> 4) ? (64u << (h[10] >> 4)) : 0u;
        wram = vol + nv;
    }
    else if (h[8] && !junk)
        wram = h[8] * 8192u;
    if (wram < 8192)
        wram = 8192;
    if (wram > 32768)
        wram = 32768;
    cart.wramSize = wram;
    cart.wram = static_cast<uint8_t*>(alloc(wram));
    if (!cart.wram)
        return kLoadNoMemory;
    if (trainer)
        memcpy(cart.wram + 0x1000, image + 16, 512);

    if (chrUnits == 0)
    {
        uint32_t cr = 8192;
        if (nes2 && (h[11] & 0x0F))
            cr = 64u << (h[11] & 0x0F);
        if (cr < 8192)
            cr = 8192;
        if (cr > 32768)
            cr = 32768;
        cart.chrSize = cr;
        cart.chrRam = true;
        cart.chr = static_cast<uint8_t*>(alloc(cr));
        if (!cart.chr)
            return kLoadNoMemory;
    }
    else
        cart.chr = image + off + cart.prgSize;

    if (cart.mirroring == kMirrorFour)
    {
        ntExtra = static_cast<uint8_t*>(alloc(2048));
        if (!ntExtra)
            return kLoadNoMemory;
    }

    bank.wram = cart.wram;
    bank.wramRead = bank.wramWrite = true;
    palTiming = cart.pal;
    ppu.lines = cart.pal ? 312 : 262;

    mapper = createMapper(*this, mapperId, sub);
    if (!mapper)
    {
        return kLoadNoMemory;
    }
    reset();
    return kLoadOk;
}

void Console::reset()
{
    memset(ram, 0, sizeof(ram));
    a = x = y = 0;
    sp = 0xFD;
    fC = fZ = fD = fV = fN = false;
    fI = true;
    irqMasked = true;
    cycles = 0;
    nmiPending = false;
    nmiDelay = 0;
    jammed = false;
    padShift[0] = padShift[1] = 0;
    padStrobe = false;
    lastMapperClock = 0;
    openBus = 0;
    bank.wramRead = bank.wramWrite = true;
    bank.wram = cart.wram;
    applyMirroring(cart.mirroring);
    for (int i = 0; i < 8; i++)
        bank.chrWrite[i] = cart.chrRam;
    if (mapper)
    {
        mapper->irq = false;
        mapper->reset();
    }
    ppu.reset();
    apu.reset();
    uint8_t lo = read(0xFFFC);
    uint8_t hi = read(0xFFFD);
    pc = static_cast<uint16_t>(lo | (hi << 8));
    cycles = 7;
}

uint8_t Console::padRead(int n)
{
    if (padStrobe)
        padShift[n] = padState[n];
    uint8_t bit = padShift[n] & 1;
    padShift[n] = static_cast<uint8_t>((padShift[n] >> 1) | 0x80);
    return bit;
}

uint8_t Console::readSlow(uint16_t addr)
{
    uint8_t v;
    if (addr < 0x4000)
        v = ppu.readReg(addr & 7, dotsAt(cycles) + 1);
    else if (addr < 0x4020)
    {
        switch (addr)
        {
            case 0x4015: v = apu.readStatus(cycles); break;
            case 0x4016: v = static_cast<uint8_t>(0x40 | padRead(0)); break;
            case 0x4017: v = static_cast<uint8_t>(0x40 | padRead(1)); break;
            default: return openBus;
        }
    }
    else if (addr < 0x6000)
        v = mapper->read(addr, openBus);
    else if (bank.wramRead && bank.wram)
        v = bank.wram[addr & 0x1FFF];
    else
        v = mapper->read(addr, openBus);
    openBus = v;
    return v;
}

void Console::oamDma(uint8_t page)
{
    ppu.runTo(dotsAt(cycles) + 1);
    uint16_t base = static_cast<uint16_t>(page << 8);
    for (int i = 0; i < 256; i++)
    {
        uint16_t src = static_cast<uint16_t>(base + i);
        uint8_t v;
        if (src < 0x2000)
            v = ram[src & 0x7FF];
        else if (src >= 0x8000)
            v = bank.prg[(src >> 13) & 3][src & 0x1FFF];
        else if (src >= 0x6000 && bank.wram && bank.wramRead)
            v = bank.wram[src & 0x1FFF];
        else
            v = openBus;
        ppu.oam[static_cast<uint8_t>(ppu.oamAddr + i)] = v;
    }
    // 513 cycles, one more when the transfer starts on an odd cycle (this write's own cycle is added later)
    cycles += 512 + 1 + ((cycles + 1) & 1);
}

void Console::writeSlow(uint16_t addr, uint8_t v)
{
    openBus = v;
    if (addr < 0x4000)
        ppu.writeReg(addr & 7, v, dotsAt(cycles) + 1);
    else if (addr < 0x4020)
    {
        if (addr == 0x4014)
            oamDma(v);
        else if (addr == 0x4016)
        {
            padStrobe = (v & 1) != 0;
            if (padStrobe)
            {
                padShift[0] = padState[0];
                padShift[1] = padState[1];
            }
        }
        else
            apu.write(addr, v, cycles);
    }
    else if (addr < 0x6000)
        mapper->write(addr, v);
    else if (addr < 0x8000)
    {
        if (bank.wramWrite && bank.wram)
            bank.wram[addr & 0x1FFF] = v;
        mapper->write(addr, v);
    }
    else
        mapper->write(addr, v);
}

void Console::pollInterrupts(bool iFlag)
{
    if (nmiPending)
    {
        if (nmiDelay)
            nmiDelay--;
        else
        {
            nmiPending = false;
            serviceInterrupt(0xFFFA, false);
            return;
        }
    }
    if (!iFlag && (mapper->irq || apu.irq()))
        serviceInterrupt(0xFFFE, false);
}

void Console::runFrame()
{
    ppu.frameDone = false;
    // guard against a runaway loop if the picture chip were ever stuck
    int64_t limit = cycles + 200000;
    while (!ppu.frameDone && cycles < limit)
    {
        bool prevI = fI;
        uint8_t op = step();

        int64_t t = dotsAt(cycles);
        if (t > ppu.clock)
            ppu.runTo(t);
        apu.runTo(cycles);
        if (mapper->wantsCpuClock)
        {
            mapper->cpuClock(static_cast<int>(cycles - lastMapperClock));
            lastMapperClock = cycles;
        }
        // CLI, SEI and PLP only take effect for interrupts after the next instruction
        bool iFlag = (op == 0x58 || op == 0x78 || op == 0x28) ? prevI : fI;
        pollInterrupts(iFlag);
    }
}

} // namespace nes
