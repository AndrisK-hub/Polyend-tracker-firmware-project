// Cartridge boards ("mappers"). Each one only rewrites the pointer tables in Console::bank.
#include "nes.h"
#include <new>

namespace nes {

// ---- helpers ----------------------------------------------------------------------------------------
uint32_t Mapper::prg8Count() const
{
    uint32_t n = sys.cart.prgSize / 8192u;
    return n ? n : 1;
}

uint32_t Mapper::chr1Count() const
{
    uint32_t n = sys.cart.chrSize / 1024u;
    return n ? n : 1;
}

void Mapper::setPrg8(int slot, uint32_t bank)
{
    sys.bank.prg[slot & 3] = sys.cart.prg + (bank % prg8Count()) * 8192u;
}

void Mapper::setPrg16(int slot16, uint32_t bank)
{
    uint32_t b = (bank * 2u) % prg8Count();
    setPrg8(slot16 * 2, b);
    setPrg8(slot16 * 2 + 1, b + 1);
}

void Mapper::setPrg32(uint32_t bank)
{
    uint32_t b = (bank * 4u) % prg8Count();
    for (int i = 0; i < 4; i++)
        setPrg8(i, b + i);
}

void Mapper::setChr1(int slot, uint32_t bank)
{
    sys.bank.chr[slot & 7] = sys.cart.chr + (bank % chr1Count()) * 1024u;
    sys.bank.chrWrite[slot & 7] = sys.cart.chrRam;
}

void Mapper::setChr2(int slot2, uint32_t bank)
{
    setChr1(slot2 * 2, bank * 2u);
    setChr1(slot2 * 2 + 1, bank * 2u + 1);
}

void Mapper::setChr4(int slot4, uint32_t bank)
{
    for (int i = 0; i < 4; i++)
        setChr1(slot4 * 4 + i, bank * 4u + i);
}

void Mapper::setChr8(uint32_t bank)
{
    for (int i = 0; i < 8; i++)
        setChr1(i, bank * 8u + i);
}

void Mapper::setMirroring(Mirroring m)
{
    sys.applyMirroring(m);
}

namespace {

// ---- 0: NROM --------------------------------------------------------------------------------------
class Nrom final : public Mapper
{
  public:
    using Mapper::Mapper;
    void reset() override
    {
        setPrg32(0);
        setChr8(0);
    }
    void write(uint16_t, uint8_t) override {}
    const char* name() const override { return "NROM"; }
};

// ---- 1: MMC1 --------------------------------------------------------------------------------------
class Mmc1 final : public Mapper
{
  public:
    using Mapper::Mapper;
    void reset() override
    {
        shift = 0;
        count = 0;
        ctrl = 0x0C;
        chr0 = chr1 = prgReg = 0;
        lastWrite = -10;
        sync();
    }
    void write(uint16_t addr, uint8_t v) override
    {
        if (addr < 0x8000)
            return;
        // read-modify-write instructions write twice in a row; only the first write counts
        bool consecutive = sys.cycles == lastWrite + 1;
        lastWrite = sys.cycles;
        if (consecutive)
            return;
        if (v & 0x80)
        {
            shift = 0;
            count = 0;
            ctrl |= 0x0C;
            sync();
            return;
        }
        shift = static_cast<uint8_t>(shift | ((v & 1) << count));
        count++;
        if (count == 5)
        {
            switch ((addr >> 13) & 3)
            {
                case 0: ctrl = shift; break;
                case 1: chr0 = shift; break;
                case 2: chr1 = shift; break;
                default: prgReg = shift; break;
            }
            shift = 0;
            count = 0;
            sync();
        }
    }
    const char* name() const override { return "MMC1"; }

  private:
    uint8_t shift = 0, count = 0, ctrl = 0x0C, chr0 = 0, chr1 = 0, prgReg = 0;
    int64_t lastWrite = -10;

    void sync()
    {
        switch (ctrl & 3)
        {
            case 0: setMirroring(kMirrorSingleLo); break;
            case 1: setMirroring(kMirrorSingleHi); break;
            case 2: setMirroring(kMirrorVert); break;
            default: setMirroring(kMirrorHoriz); break;
        }
        // 512 KB boards (SUROM) use CHR bit 4 as the 256 KB half select
        uint32_t outer = 0;
        if (sys.cart.prgSize > 262144u)
            outer = (chr0 & 0x10) ? 16u : 0u;
        uint32_t b = prgReg & 0x0F;
        switch ((ctrl >> 2) & 3)
        {
            case 0:
            case 1:
                setPrg16(0, outer + (b & ~1u));
                setPrg16(1, outer + (b | 1u));
                break;
            case 2:
                setPrg16(0, outer);
                setPrg16(1, outer + b);
                break;
            default:
                setPrg16(0, outer + b);
                setPrg16(1, outer + 15u);
                break;
        }
        if (ctrl & 0x10)
        {
            setChr4(0, chr0);
            setChr4(1, chr1);
        }
        else
            setChr8(chr0 >> 1);
        // 32 KB of PRG-RAM (SXROM) is banked by CHR bits 2-3
        uint32_t ramBanks = sys.cart.wramSize / 8192u;
        if (ramBanks > 1)
            sys.bank.wram = sys.cart.wram + ((chr0 >> 2) & 3) % ramBanks * 8192u;
    }
};

// ---- 2: UxROM, 180 ----------------------------------------------------------------------------------
class Uxrom final : public Mapper
{
  public:
    Uxrom(Console& c, bool reversed)
      : Mapper(c)
      , rev(reversed)
    {
    }
    void reset() override
    {
        sel = 0;
        sync();
        setChr8(0);
    }
    void write(uint16_t addr, uint8_t v) override
    {
        if (addr < 0x8000)
            return;
        sel = v;
        sync();
    }
    const char* name() const override { return rev ? "UNROM (180)" : "UxROM"; }

  private:
    bool rev;
    uint8_t sel = 0;
    void sync()
    {
        uint32_t last = prg8Count() / 2 - 1;
        if (!rev)
        {
            setPrg16(0, sel);
            setPrg16(1, last);
        }
        else
        {
            setPrg16(0, 0);
            setPrg16(1, sel);
        }
    }
};

// ---- 3: CNROM ---------------------------------------------------------------------------------------
class Cnrom final : public Mapper
{
  public:
    using Mapper::Mapper;
    void reset() override
    {
        setPrg32(0);
        setChr8(0);
    }
    void write(uint16_t addr, uint8_t v) override
    {
        if (addr >= 0x8000)
            setChr8(v);
    }
    const char* name() const override { return "CNROM"; }
};

// ---- 4: MMC3 (and 118 TxSROM, 119 TQROM) ----------------------------------------------------------------
class Mmc3 : public Mapper
{
  public:
    enum Variant : uint8_t
    {
        kPlain,
        kTxsrom,
        kTqrom
    };
    Mmc3(Console& c, Variant v)
      : Mapper(c)
      , variant(v)
    {
        tqRam = variant == kTqrom ? static_cast<uint8_t*>(c.alloc(8192)) : nullptr;
    }
    void reset() override
    {
        sel = 0;
        static const uint8_t init[8] = { 0, 2, 4, 5, 6, 7, 0, 1 };
        for (int i = 0; i < 8; i++)
            regs[i] = init[i];
        latch = 0;
        counter = 0;
        reload = false;
        irqEnabled = false;
        irq = false;
        if (sys.cart.mirroring != kMirrorFour && variant != kTxsrom)
            setMirroring(kMirrorVert);
        sync();
    }
    void write(uint16_t addr, uint8_t v) override
    {
        if (addr < 0x8000)
            return;
        switch (((addr >> 13) & 3) * 2 + (addr & 1))
        {
            case 0:
                sel = v;
                sync();
                break;
            case 1:
                regs[sel & 7] = v;
                sync();
                break;
            case 2:
                if (variant != kTxsrom && sys.cart.mirroring != kMirrorFour)
                    setMirroring((v & 1) ? kMirrorHoriz : kMirrorVert);
                break;
            case 3: break; // PRG-RAM protect: left enabled
            case 4: latch = v; break;
            case 5:
                counter = 0;
                reload = true;
                break;
            case 6:
                irqEnabled = false;
                irq = false;
                break;
            default: irqEnabled = true; break;
        }
    }
    void a12Rise() override
    {
        if (counter == 0 || reload)
        {
            counter = latch;
            reload = false;
        }
        else
            counter--;
        if (counter == 0 && irqEnabled)
            irq = true;
    }
    const char* name() const override
    {
        return variant == kTxsrom ? "TxSROM" : (variant == kTqrom ? "TQROM" : "MMC3");
    }

  private:
    Variant variant;
    uint8_t sel = 0, regs[8] = {}, latch = 0, counter = 0;
    bool reload = false, irqEnabled = false;
    uint8_t* tqRam = nullptr;

    void chr1(int slot, uint32_t b)
    {
        if (variant == kTqrom && tqRam)
        {
            if (b & 0x40)
            {
                sys.bank.chr[slot] = tqRam + (b & 7u) * 1024u;
                sys.bank.chrWrite[slot] = true;
            }
            else
            {
                sys.bank.chr[slot] = sys.cart.chr + ((b & 0x3Fu) % chr1Count()) * 1024u;
                sys.bank.chrWrite[slot] = false;
            }
        }
        else
            setChr1(slot, b);
    }
    void sync()
    {
        uint32_t last = prg8Count() - 1;
        if (!(sel & 0x40))
        {
            setPrg8(0, regs[6] & 0x3F);
            setPrg8(1, regs[7] & 0x3F);
            setPrg8(2, last - 1);
            setPrg8(3, last);
        }
        else
        {
            setPrg8(0, last - 1);
            setPrg8(1, regs[7] & 0x3F);
            setPrg8(2, regs[6] & 0x3F);
            setPrg8(3, last);
        }
        int lo = (sel & 0x80) ? 4 : 0; // where the two 2 KB banks go
        int hi = (sel & 0x80) ? 0 : 4; // where the four 1 KB banks go
        chr1(lo + 0, regs[0] & ~1u);
        chr1(lo + 1, regs[0] | 1u);
        chr1(lo + 2, regs[1] & ~1u);
        chr1(lo + 3, regs[1] | 1u);
        for (int i = 0; i < 4; i++)
            chr1(hi + i, regs[2 + i]);
        if (variant == kTxsrom)
        {
            // each of the four lower 1 KB pages picks the nametable with bit 7 of its bank value
            uint8_t* c0 = sys.ppu.ciram;
            uint8_t* c1 = sys.ppu.ciram + 1024;
            uint8_t b7[4];
            if (!(sel & 0x80))
            {
                b7[0] = b7[1] = regs[0] >> 7;
                b7[2] = b7[3] = regs[1] >> 7;
            }
            else
                for (int i = 0; i < 4; i++)
                    b7[i] = regs[2 + i] >> 7;
            for (int i = 0; i < 4; i++)
                sys.bank.nt[i] = b7[i] ? c1 : c0;
        }
    }
};

// ---- 206: Namco 108 / DxROM -------------------------------------------------------------------------
class Namco108 final : public Mapper
{
  public:
    using Mapper::Mapper;
    void reset() override
    {
        sel = 0;
        static const uint8_t init[8] = { 0, 2, 4, 5, 6, 7, 0, 1 };
        for (int i = 0; i < 8; i++)
            regs[i] = init[i];
        sync();
    }
    void write(uint16_t addr, uint8_t v) override
    {
        if (addr < 0x8000 || (addr & 0x6000) != 0)
            return; // the registers only exist in $8000-$9FFF
        if (addr & 1)
            regs[sel & 7] = v;
        else
            sel = v;
        sync();
    }
    const char* name() const override { return "DxROM"; }

  private:
    uint8_t sel = 0, regs[8] = {};
    void sync()
    {
        uint32_t last = prg8Count() - 1;
        setPrg8(0, regs[6] & 0x0F);
        setPrg8(1, regs[7] & 0x0F);
        setPrg8(2, last - 1);
        setPrg8(3, last);
        setChr1(0, regs[0] & 0x3Eu);
        setChr1(1, (regs[0] & 0x3Eu) | 1u);
        setChr1(2, regs[1] & 0x3Eu);
        setChr1(3, (regs[1] & 0x3Eu) | 1u);
        for (int i = 0; i < 4; i++)
            setChr1(4 + i, regs[2 + i] & 0x3Fu);
    }
};

// ---- 7: AxROM ----------------------------------------------------------------------------------------
class Axrom final : public Mapper
{
  public:
    using Mapper::Mapper;
    void reset() override
    {
        setPrg32(0);
        setChr8(0);
        setMirroring(kMirrorSingleLo);
    }
    void write(uint16_t addr, uint8_t v) override
    {
        if (addr < 0x8000)
            return;
        setPrg32(v & 7);
        setMirroring((v & 0x10) ? kMirrorSingleHi : kMirrorSingleLo);
    }
    const char* name() const override { return "AxROM"; }
};

// ---- 9 / 10: MMC2 and MMC4 ----------------------------------------------------------------------------
class Mmc2 final : public Mapper
{
  public:
    Mmc2(Console& c, bool mmc4)
      : Mapper(c)
      , is4(mmc4)
    {
        wantsChrFetch = true;
    }
    void reset() override
    {
        for (int i = 0; i < 4; i++)
            chrReg[i] = 0;
        latch[0] = latch[1] = 1; // $FE after power on
        prgBank = 0;
        syncPrg();
        syncChr();
    }
    void write(uint16_t addr, uint8_t v) override
    {
        if (addr < 0xA000)
            return;
        switch ((addr >> 12) & 7)
        {
            case 2:
                prgBank = v & 0x0F;
                syncPrg();
                break;
            case 3:
                chrReg[0] = v & 0x1F;
                syncChr();
                break;
            case 4:
                chrReg[1] = v & 0x1F;
                syncChr();
                break;
            case 5:
                chrReg[2] = v & 0x1F;
                syncChr();
                break;
            case 6:
                chrReg[3] = v & 0x1F;
                syncChr();
                break;
            case 7: setMirroring((v & 1) ? kMirrorHoriz : kMirrorVert); break;
            default: break;
        }
    }
    void chrFetch(uint16_t addr) override
    {
        // 'addr' is the high pattern byte of a tile
        uint16_t a = addr & 0x1FFF;
        int side = (a >> 12) & 1;
        uint16_t lowBits = a & 0x0FF8;
        int newLatch = -1;
        if (is4 || side == 1)
        {
            if (lowBits == 0x0FD8)
                newLatch = 0;
            else if (lowBits == 0x0FE8)
                newLatch = 1;
        }
        else
        {
            // MMC2's left pattern table only reacts to the exact addresses
            if (a == 0x0FD8)
                newLatch = 0;
            else if (a == 0x0FE8)
                newLatch = 1;
        }
        if (newLatch >= 0 && latch[side] != newLatch)
        {
            latch[side] = static_cast<uint8_t>(newLatch);
            syncChr();
        }
    }
    const char* name() const override { return is4 ? "MMC4" : "MMC2"; }

  private:
    bool is4;
    uint8_t chrReg[4] = {}, latch[2] = { 1, 1 }, prgBank = 0;
    void syncPrg()
    {
        if (is4)
        {
            setPrg16(0, prgBank);
            setPrg16(1, prg8Count() / 2 - 1);
        }
        else
        {
            uint32_t n = prg8Count();
            setPrg8(0, prgBank);
            setPrg8(1, n - 3);
            setPrg8(2, n - 2);
            setPrg8(3, n - 1);
        }
    }
    void syncChr()
    {
        setChr4(0, chrReg[latch[0]]);
        setChr4(1, chrReg[2 + latch[1]]);
    }
};

// ---- 11: Color Dreams ----------------------------------------------------------------------------------
class ColorDreams final : public Mapper
{
  public:
    using Mapper::Mapper;
    void reset() override
    {
        setPrg32(0);
        setChr8(0);
    }
    void write(uint16_t addr, uint8_t v) override
    {
        if (addr < 0x8000)
            return;
        setPrg32(v & 3);
        setChr8(v >> 4);
    }
    const char* name() const override { return "Color Dreams"; }
};

// ---- 66: GxROM -------------------------------------------------------------------------------------------
class Gxrom final : public Mapper
{
  public:
    using Mapper::Mapper;
    void reset() override
    {
        setPrg32(0);
        setChr8(0);
    }
    void write(uint16_t addr, uint8_t v) override
    {
        if (addr < 0x8000)
            return;
        setPrg32((v >> 4) & 3);
        setChr8(v & 3);
    }
    const char* name() const override { return "GxROM"; }
};

// ---- 34: BNROM / NINA-001 -----------------------------------------------------------------------------------
class Bnrom final : public Mapper
{
  public:
    using Mapper::Mapper;
    void reset() override
    {
        setPrg32(0);
        chrLo = 0;
        chrHi = 1;
        syncChr();
    }
    void write(uint16_t addr, uint8_t v) override
    {
        if (addr >= 0x8000)
            setPrg32(v);
        else if (addr == 0x7FFD)
            setPrg32(v);
        else if (addr == 0x7FFE)
        {
            chrLo = v;
            syncChr();
        }
        else if (addr == 0x7FFF)
        {
            chrHi = v;
            syncChr();
        }
    }
    const char* name() const override { return sys.cart.chrRam ? "BNROM" : "NINA-001"; }

  private:
    uint8_t chrLo = 0, chrHi = 1;
    void syncChr()
    {
        if (sys.cart.chrRam)
            setChr8(0);
        else
        {
            setChr4(0, chrLo);
            setChr4(1, chrHi);
        }
    }
};

// ---- 71: Camerica ------------------------------------------------------------------------------------------------
class Camerica final : public Mapper
{
  public:
    using Mapper::Mapper;
    void reset() override
    {
        setPrg16(0, 0);
        setPrg16(1, prg8Count() / 2 - 1);
        setChr8(0);
    }
    void write(uint16_t addr, uint8_t v) override
    {
        if (addr >= 0xC000)
            setPrg16(0, v);
        else if (addr >= 0x9000 && addr < 0xA000)
            setMirroring((v & 0x10) ? kMirrorSingleHi : kMirrorSingleLo);
    }
    const char* name() const override { return "Camerica"; }
};

// ---- 79 / 113-ish NINA-03/06 --------------------------------------------------------------------------------------
class Nina06 final : public Mapper
{
  public:
    using Mapper::Mapper;
    void reset() override
    {
        setPrg32(0);
        setChr8(0);
    }
    void write(uint16_t addr, uint8_t v) override
    {
        if (addr >= 0x4020 && addr < 0x6000 && (addr & 0x0100))
        {
            setPrg32((v >> 3) & 1);
            setChr8(v & 7);
        }
    }
    const char* name() const override { return "NINA-03/06"; }
};

// ---- 87 / 140: simple latches in $6000-$7FFF --------------------------------------------------------------------------
class Jaleco87 final : public Mapper
{
  public:
    Jaleco87(Console& c, bool is140)
      : Mapper(c)
      , m140(is140)
    {
    }
    void reset() override
    {
        setPrg32(0);
        setChr8(0);
    }
    void write(uint16_t addr, uint8_t v) override
    {
        if (addr < 0x6000 || addr >= 0x8000)
            return;
        if (m140)
        {
            setPrg32((v >> 4) & 3);
            setChr8(v & 0x0F);
        }
        else
            setChr8(static_cast<uint8_t>(((v & 1) << 1) | ((v >> 1) & 1)));
    }
    const char* name() const override { return m140 ? "Jaleco JF-11" : "Konami 87"; }

  private:
    bool m140;
};

// ---- 33 / 48: Taito TC0190 --------------------------------------------------------------------------------------------
class Taito final : public Mapper
{
  public:
    Taito(Console& c, bool is48)
      : Mapper(c)
      , m48(is48)
    {
    }
    void reset() override
    {
        for (int i = 0; i < 8; i++)
            regs[i] = 0;
        irqLatch = 0;
        irqCounter = 0;
        irqReload = false;
        irqEnabled = false;
        irq = false;
        sync();
    }
    void write(uint16_t addr, uint8_t v) override
    {
        if (addr < 0x8000)
            return;
        switch ((addr & 0x6003))
        {
            case 0x0000:
                regs[0] = v & 0x3F;
                if (!m48)
                    setMirroring((v & 0x40) ? kMirrorHoriz : kMirrorVert);
                break;
            case 0x0001: regs[1] = v & 0x3F; break;
            case 0x0002: regs[2] = v; break;
            case 0x0003: regs[3] = v; break;
            case 0x2000: regs[4] = v; break;
            case 0x2001: regs[5] = v; break;
            case 0x2002: regs[6] = v; break;
            case 0x2003: regs[7] = v; break;
            case 0x4000:
                if (m48)
                    irqLatch = static_cast<uint8_t>(v ^ 0xFF);
                break;
            case 0x4001:
                if (m48)
                {
                    irqCounter = 0;
                    irqReload = true;
                }
                break;
            case 0x4002:
                if (m48)
                    irqEnabled = true;
                break;
            case 0x4003:
                if (m48)
                {
                    irqEnabled = false;
                    irq = false;
                }
                break;
            case 0x6000:
                if (m48)
                    setMirroring((v & 0x40) ? kMirrorHoriz : kMirrorVert);
                break;
            default: break;
        }
        sync();
    }
    void a12Rise() override
    {
        if (!m48)
            return;
        if (irqCounter == 0 || irqReload)
        {
            irqCounter = irqLatch;
            irqReload = false;
        }
        else
            irqCounter--;
        if (irqCounter == 0 && irqEnabled)
            irq = true;
    }
    const char* name() const override { return m48 ? "Taito TC0690" : "Taito TC0190"; }

  private:
    bool m48;
    uint8_t regs[8] = {}, irqLatch = 0, irqCounter = 0;
    bool irqReload = false, irqEnabled = false;
    void sync()
    {
        uint32_t last = prg8Count() - 1;
        setPrg8(0, regs[0]);
        setPrg8(1, regs[1]);
        setPrg8(2, last - 1);
        setPrg8(3, last);
        setChr2(0, regs[2]);
        setChr2(1, regs[3]);
        for (int i = 0; i < 4; i++)
            setChr1(4 + i, regs[4 + i]);
    }
};

template <class T, class... Args>
Mapper* make(Console& c, Args... args)
{
    void* p = c.alloc(sizeof(T));
    return p ? new (p) T(c, args...) : nullptr;
}

} // namespace

bool mapperSupported(uint16_t id)
{
    switch (id)
    {
        case 0:
        case 1:
        case 2:
        case 3:
        case 4:
        case 7:
        case 9:
        case 10:
        case 11:
        case 33:
        case 34:
        case 48:
        case 66:
        case 71:
        case 79:
        case 87:
        case 118:
        case 119:
        case 140:
        case 180:
        case 206: return true;
        default: return false;
    }
}

Mapper* createMapper(Console& c, uint16_t id, uint8_t sub)
{
    (void)sub;
    switch (id)
    {
        case 0: return make<Nrom>(c);
        case 1: return make<Mmc1>(c);
        case 2: return make<Uxrom>(c, false);
        case 180: return make<Uxrom>(c, true);
        case 3: return make<Cnrom>(c);
        case 4: return make<Mmc3>(c, Mmc3::kPlain);
        case 118: return make<Mmc3>(c, Mmc3::kTxsrom);
        case 119: return make<Mmc3>(c, Mmc3::kTqrom);
        case 206: return make<Namco108>(c);
        case 7: return make<Axrom>(c);
        case 9: return make<Mmc2>(c, false);
        case 10: return make<Mmc2>(c, true);
        case 11: return make<ColorDreams>(c);
        case 66: return make<Gxrom>(c);
        case 34: return make<Bnrom>(c);
        case 71: return make<Camerica>(c);
        case 79: return make<Nina06>(c);
        case 87: return make<Jaleco87>(c, false);
        case 140: return make<Jaleco87>(c, true);
        case 33: return make<Taito>(c, false);
        case 48: return make<Taito>(c, true);
        default: return nullptr;
    }
}

} // namespace nes
