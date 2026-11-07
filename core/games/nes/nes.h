// A small NES (Famicom) emulator for the Polyend Tracker Mini.
//
// Written from scratch for this firmware; it contains no code from any other emulator.
// It is plain C++ with no heap use and no dependencies beyond <stdint.h> and <string.h>, so the
// same sources build on the device and on a PC for testing.
//
// Overview
//   Console   owns everything: 6502 CPU, the cartridge, the controllers and the glue.
//   Ppu       the picture chip. It is stepped in small slices (8 dots) and is cycle-timed against
//             the CPU, so mid-frame effects (sprite-0 splits, MMC3 IRQ) land on the right line.
//   Mapper    one subclass per cartridge board (bank switching, IRQ counters). Mappers only change
//             pointer tables in Banks, so the CPU and PPU hot paths never call into them for reads.
//   Apu       sound (see nes_apu.cpp).
#ifndef GAMES_NES_NES_H_
#define GAMES_NES_NES_H_

#include <stdint.h>
#include <string.h>

namespace nes {

class Console;

enum Mirroring : uint8_t
{
    kMirrorHoriz = 0,  // $2000=$2400, $2800=$2C00 (vertical scrolling games)
    kMirrorVert = 1,   // $2000=$2800, $2400=$2C00 (horizontal scrolling games)
    kMirrorSingleLo = 2,
    kMirrorSingleHi = 3,
    kMirrorFour = 4,
};

enum Button : uint8_t
{
    kBtnA = 1 << 0,
    kBtnB = 1 << 1,
    kBtnSelect = 1 << 2,
    kBtnStart = 1 << 3,
    kBtnUp = 1 << 4,
    kBtnDown = 1 << 5,
    kBtnLeft = 1 << 6,
    kBtnRight = 1 << 7,
};

enum LoadResult : uint8_t
{
    kLoadOk = 0,
    kLoadBadHeader,
    kLoadTruncated,
    kLoadUnsupportedMapper,
    kLoadNoMemory,
};

// Where the CPU and PPU look for their data. Mappers rewrite these tables.
struct Banks
{
    uint8_t* prg[4];  // 8 KB windows at $8000, $A000, $C000, $E000
    uint8_t* wram;    // 8 KB window at $6000 (may be null)
    bool wramRead;
    bool wramWrite;
    uint8_t* chr[8];  // 1 KB windows at $0000..$1FFF of the PPU address space
    bool chrWrite[8]; // true where the window is RAM
    uint8_t* nt[4];   // 1 KB windows at $2000, $2400, $2800, $2C00
};

struct Cart
{
    uint8_t* prg = nullptr;
    uint32_t prgSize = 0;
    uint8_t* chr = nullptr; // ROM or RAM
    uint32_t chrSize = 0;
    bool chrRam = false;
    uint8_t* wram = nullptr;
    uint32_t wramSize = 0;
    bool battery = false;
    uint16_t mapper = 0;
    uint8_t submapper = 0;
    Mirroring mirroring = kMirrorHoriz;
    bool pal = false;
    bool nes2 = false;
};

// ---------------------------------------------------------------------------------------------
class Mapper
{
  public:
    explicit Mapper(Console& c)
      : sys(c)
    {
    }
    virtual void reset() {}
    // CPU writes to $4020..$FFFF.
    virtual void write(uint16_t addr, uint8_t v) = 0;
    // CPU reads of $4020..$5FFF (and $6000..$7FFF when there is no PRG-RAM).
    virtual uint8_t read(uint16_t addr, uint8_t openBus)
    {
        (void)addr;
        return openBus;
    }
    // Called when the PPU address line A12 rises (MMC3 style scanline counter).
    virtual void a12Rise() {}
    // Called with the number of CPU cycles that passed (only if wantsCpuClock).
    virtual void cpuClock(int n) { (void)n; }
    // Called with the address of the high pattern byte of each fetched tile (only if wantsChrFetch).
    virtual void chrFetch(uint16_t addr) { (void)addr; }
    virtual const char* name() const = 0;

    bool irq = false;
    bool wantsChrFetch = false;
    bool wantsCpuClock = false;

  protected:
    Console& sys;

    // helpers to rewrite the bank tables
    void setPrg8(int slot, uint32_t bank);
    void setPrg16(int slot16, uint32_t bank); // slot16 0 -> $8000, 1 -> $C000
    void setPrg32(uint32_t bank);
    void setChr1(int slot, uint32_t bank);
    void setChr2(int slot2, uint32_t bank); // slot2 0..3, bank in 2 KB units
    void setChr4(int slot4, uint32_t bank); // slot4 0..1, bank in 4 KB units
    void setChr8(uint32_t bank);
    void setMirroring(Mirroring m);
    uint32_t prg8Count() const;
    uint32_t chr1Count() const;
};

// Creates the mapper for a cartridge (placement-new into memory taken from the console's arena).
Mapper* createMapper(Console& c, uint16_t id, uint8_t sub);
// True when the mapper number is implemented.
bool mapperSupported(uint16_t id);

// ---------------------------------------------------------------------------------------------
class Apu
{
  public:
    void init(Console* c) { sys = c; }
    void reset();
    void write(uint16_t addr, uint8_t v, int64_t cpuCycle);
    uint8_t readStatus(int64_t cpuCycle);
    void runTo(int64_t cpuCycle); // advance the sound generation to this CPU cycle
    bool irq() const { return frameIrq || dmcIrq; }
    // Pulls up to 'count' mono samples (signed 16 bit, 44.1 kHz class rate) generated so far.
    int readSamples(int16_t* out, int count);
    int samplesAvailable() const;
    void setSampleRate(uint32_t hz) { sampleRate = hz; }
    void clearBuffer();

    bool frameIrq = false;
    bool dmcIrq = false;

  private:
    Console* sys = nullptr;
    uint32_t sampleRate = 44100;
    // implementation state lives in nes_apu.cpp's private struct
    struct Impl;
    alignas(8) uint8_t implStorage[1024];
    Impl* impl();
};

// ---------------------------------------------------------------------------------------------
class Ppu
{
  public:
    void init(Console* c);
    void reset();

    // CPU side register access; 'time' is the PPU dot at which the access happens.
    uint8_t readReg(int reg, int64_t time);
    void writeReg(int reg, uint8_t v, int64_t time);
    void runTo(int64_t time); // execute PPU dots until 'time' (in dots since power-on)

    uint8_t vramRead(uint16_t a);
    void vramWrite(uint16_t a, uint8_t v);

    // output
    uint8_t* fb = nullptr; // 256x240, one byte per pixel (RGB332)
    uint8_t lut[64];       // NES colour index -> RGB332
    bool frameDone = false;

    // registers and memory
    uint8_t ctrl = 0, mask = 0, status = 0, oamAddr = 0;
    uint16_t vAddr = 0, tAddr = 0;
    uint8_t fineX = 0;
    bool wLatch = false;
    uint8_t readBuf = 0, bus = 0;
    uint8_t oam[256];
    uint8_t palette[32];
    uint8_t ciram[2048];

    int64_t clock = 0; // dots executed since power-on
    int scanline = 261;
    int dot = 0; // dots completed in this line
    bool odd = false;
    int lines = 262; // 262 NTSC, 312 PAL

  private:
    struct Tile
    {
        uint8_t lo, hi, pal;
    };
    struct Ev
    {
        uint16_t pos;
        uint8_t type;
        uint8_t arg;
    };
    enum EvType : uint8_t
    {
        kEvGroup,
        kEvIncY,
        kEvSprites,
        kEvA12a,
        kEvA12b,
        kEvPrefetch,
        kEvVblank,
        kEvClear,
        kEvCopyX,
        kEvCopyY,
        kEvPreSprites,
    };

    Console* sys = nullptr;
    Tile q[2];
    uint8_t spr[264];
    int hitAt = -1;
    int evIdx = 0;
    int evCount = 0;
    const Ev* ev = nullptr;
    int a12Mode = 0;
    bool suppressVbl = false;
    bool chrNotify = false;
    Ev evVisible[40];
    int evVisibleCount = 0;
    Ev evPre[12];
    int evPreCount = 0;
    Ev evVbl[1];
    uint8_t scratchRow[8];

    bool rendering() const { return (mask & 0x18) != 0; }
    void endLine();
    void selectEvents();
    void fire(const Ev& e);
    void renderGroup(int g);
    Tile fetchTile();
    void incX();
    void incY();
    void evalSprites(int line);
    uint8_t chrByte(uint16_t a) const;
    uint16_t lineLength() const;
    void chooseA12(bool eightBySixteen, int count, const uint8_t* tiles);
};

// ---------------------------------------------------------------------------------------------
class Console
{
  public:
    Console();

    // Parses an iNES / NES 2.0 image that is already in memory and sets up the cartridge.
    // 'image' must stay valid while the console runs; 'extra' is scratch memory for RAM the cartridge needs.
    LoadResult load(uint8_t* image, uint32_t imageSize, uint8_t* extra, uint32_t extraSize);
    void reset();

    // Runs the emulation until the picture chip has finished a frame.
    void runFrame();

    void setPad(int pad, uint8_t buttons) { padState[pad & 1] = buttons; }
    void setFrameBuffer(uint8_t* p) { ppu.fb = p; }

    // Cartridge RAM that should be saved on the SD card (null if the cartridge has no battery).
    uint8_t* saveRam() { return cart.battery ? cart.wram : nullptr; }
    uint32_t saveRamSize() const { return cart.battery ? cart.wramSize : 0; }
    const char* mapperName() const { return mapper ? mapper->name() : "none"; }
    bool loaded() const { return mapper != nullptr; }

    // arena allocator for mappers and cartridge RAM
    void* alloc(uint32_t size);

    // --- CPU state ---
    uint8_t a = 0, x = 0, y = 0, sp = 0xFD;
    uint16_t pc = 0;
    bool fC = false, fZ = false, fI = true, fD = false, fV = false, fN = false;
    int64_t cycles = 0; // CPU cycles since power-on
    bool nmiPending = false;
    uint8_t nmiDelay = 0;
    bool jammed = false;
    bool irqMasked = true; // I flag as seen by the interrupt poll

    // --- memory ---
    uint8_t ram[2048];
    Banks bank;
    Cart cart;
    Mapper* mapper = nullptr;
    Ppu ppu;
    Apu apu;
    uint8_t openBus = 0;
    uint8_t padState[2] = { 0, 0 };
    uint8_t padShift[2] = { 0, 0 };
    bool padStrobe = false;
    int64_t lastMapperClock = 0;
    bool palTiming = false;
    uint8_t* ntExtra = nullptr; // 2 KB of extra nametable RAM for four-screen cartridges

    // PPU dot that corresponds to the start of CPU cycle 'c'
    int64_t dotsAt(int64_t c) const { return palTiming ? (c * 16) / 5 : c * 3; }
    void applyMirroring(Mirroring m);

    // bus access: each call takes one CPU cycle
    inline uint8_t read(uint16_t addr)
    {
        uint8_t v;
        if (addr < 0x2000)
            v = ram[addr & 0x7FF];
        else if (addr >= 0x8000)
            v = bank.prg[(addr >> 13) & 3][addr & 0x1FFF];
        else
            v = readSlow(addr);
        cycles++;
        return v;
    }
    inline void write(uint16_t addr, uint8_t v)
    {
        if (addr < 0x2000)
            ram[addr & 0x7FF] = v;
        else
            writeSlow(addr, v);
        cycles++;
    }
    uint8_t readSlow(uint16_t addr);
    void writeSlow(uint16_t addr, uint8_t v);

    // used by the PPU
    void raiseNmi(uint8_t delay)
    {
        nmiPending = true;
        nmiDelay = delay;
    }

    uint8_t step(); // execute one instruction, returns its opcode
    void serviceInterrupt(uint16_t vector, bool brk);

  private:
    uint8_t* arena = nullptr;
    uint32_t arenaLeft = 0;

    uint8_t fetch()
    {
        uint8_t v = read(pc);
        pc++;
        return v;
    }
    void push(uint8_t v)
    {
        write(static_cast<uint16_t>(0x100 | sp), v);
        sp--;
    }
    uint8_t pull()
    {
        sp++;
        return read(static_cast<uint16_t>(0x100 | sp));
    }
    uint8_t packP(bool brk) const
    {
        return static_cast<uint8_t>((fN << 7) | (fV << 6) | 0x20 | (brk ? 0x10 : 0) | (fD << 3) |
                                    (fI << 2) | (fZ << 1) | fC);
    }
    void unpackP(uint8_t p)
    {
        fN = (p & 0x80) != 0;
        fV = (p & 0x40) != 0;
        fD = (p & 0x08) != 0;
        fI = (p & 0x04) != 0;
        fZ = (p & 0x02) != 0;
        fC = (p & 0x01) != 0;
    }
    void nz(uint8_t v)
    {
        fZ = v == 0;
        fN = (v & 0x80) != 0;
    }
    void adc(uint8_t m);
    void cmp(uint8_t r, uint8_t m)
    {
        fC = r >= m;
        nz(static_cast<uint8_t>(r - m));
    }
    uint8_t asl(uint8_t v)
    {
        fC = (v & 0x80) != 0;
        v = static_cast<uint8_t>(v << 1);
        nz(v);
        return v;
    }
    uint8_t lsr(uint8_t v)
    {
        fC = (v & 1) != 0;
        v >>= 1;
        nz(v);
        return v;
    }
    uint8_t rol(uint8_t v)
    {
        uint8_t c = fC;
        fC = (v & 0x80) != 0;
        v = static_cast<uint8_t>((v << 1) | c);
        nz(v);
        return v;
    }
    uint8_t ror(uint8_t v)
    {
        uint8_t c = fC;
        fC = (v & 1) != 0;
        v = static_cast<uint8_t>((v >> 1) | (c << 7));
        nz(v);
        return v;
    }
    void branch(bool cond)
    {
        int8_t off = static_cast<int8_t>(fetch());
        if (cond)
        {
            cycles++;
            uint16_t np = static_cast<uint16_t>(pc + off);
            if ((np ^ pc) & 0xFF00)
                cycles++;
            pc = np;
        }
    }
    uint16_t ea(int mode, bool store);        // addressing for the "cc=01" group
    uint16_t ea2(int mode, int aaa);          // addressing for the "cc=10" group
    uint16_t eaIdx(int mode, bool useY, bool store); // zp,x / zp,y / abs,x / abs,y helpers
    void oamDma(uint8_t page);
    uint8_t padRead(int n);
    void pollInterrupts(bool iFlagForPoll);
    void execGroup3(uint8_t op);
    friend class Ppu;
};

} // namespace nes

#endif // GAMES_NES_NES_H_
