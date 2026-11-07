// Picture processing unit.
//
// The PPU runs as a list of timed events per scanline (see Ppu::Ev) instead of dot by dot:
//   * every 8 dots one "group" of 8 pixels is produced from the background shift registers and the
//     sprite line buffer, and the next tile is fetched using the live loopy address (v);
//   * sprites for the next line are evaluated and fetched at dot 257;
//   * the usual vertical/horizontal address copies, the vblank flag and the mapper scanline clock have
//     their own events.
// Register accesses from the CPU first run the PPU up to that moment, so mid-frame writes (scroll
// splits, bank switches) change the picture from the right group onwards.
#include "nes.h"

namespace nes {

// A common NTSC palette (R, G, B).
static const uint8_t kPalette[64][3] = {
    { 84, 84, 84 },    { 0, 30, 116 },    { 8, 16, 144 },    { 48, 0, 136 },    { 68, 0, 100 },
    { 92, 0, 48 },     { 84, 4, 0 },      { 60, 24, 0 },     { 32, 42, 0 },     { 8, 58, 0 },
    { 0, 64, 0 },      { 0, 60, 0 },      { 0, 50, 60 },     { 0, 0, 0 },       { 0, 0, 0 },
    { 0, 0, 0 },       { 152, 150, 152 }, { 8, 76, 196 },    { 48, 50, 236 },   { 92, 30, 228 },
    { 136, 20, 176 },  { 160, 20, 100 },  { 152, 34, 32 },   { 120, 60, 0 },    { 84, 90, 0 },
    { 40, 114, 0 },    { 8, 124, 0 },     { 0, 118, 40 },    { 0, 102, 120 },   { 0, 0, 0 },
    { 0, 0, 0 },       { 0, 0, 0 },       { 236, 238, 236 }, { 76, 154, 236 },  { 120, 124, 236 },
    { 176, 98, 236 },  { 228, 84, 236 },  { 236, 88, 180 },  { 236, 106, 100 }, { 212, 136, 32 },
    { 160, 170, 0 },   { 116, 196, 0 },   { 76, 208, 32 },   { 56, 204, 108 },  { 56, 180, 204 },
    { 60, 60, 60 },    { 0, 0, 0 },       { 0, 0, 0 },       { 236, 238, 236 }, { 168, 204, 236 },
    { 188, 188, 236 }, { 212, 178, 236 }, { 236, 174, 236 }, { 236, 174, 212 }, { 236, 180, 176 },
    { 228, 196, 144 }, { 204, 210, 120 }, { 180, 222, 120 }, { 168, 226, 144 }, { 152, 226, 180 },
    { 160, 214, 228 }, { 160, 162, 160 }, { 0, 0, 0 },       { 0, 0, 0 },
};

void Ppu::init(Console* c)
{
    sys = c;
    for (int i = 0; i < 64; i++)
    {
        unsigned r = (kPalette[i][0] * 7u + 127u) / 255u;
        unsigned g = (kPalette[i][1] * 7u + 127u) / 255u;
        unsigned b = (kPalette[i][2] * 3u + 127u) / 255u;
        lut[i] = static_cast<uint8_t>((r << 5) | (g << 2) | b);
    }

    // event table for visible lines
    int n = 0;
    for (int g = 0; g < 32; g++)
        evVisible[n++] = { static_cast<uint16_t>(1 + 8 * g), kEvGroup, static_cast<uint8_t>(g) };
    evVisible[n++] = { 256, kEvIncY, 0 };
    evVisible[n++] = { 257, kEvSprites, 0 };
    evVisible[n++] = { 260, kEvA12a, 0 };
    evVisible[n++] = { 321, kEvPrefetch, 0 };
    evVisible[n++] = { 324, kEvA12b, 0 };
    evVisible[n++] = { 329, kEvPrefetch, 1 };
    evVisibleCount = n;

    // pre-render line
    n = 0;
    evPre[n++] = { 1, kEvClear, 0 };
    evPre[n++] = { 256, kEvIncY, 0 };
    evPre[n++] = { 257, kEvPreSprites, 0 };
    evPre[n++] = { 260, kEvA12a, 0 };
    evPre[n++] = { 304, kEvCopyY, 0 };
    evPre[n++] = { 321, kEvPrefetch, 0 };
    evPre[n++] = { 324, kEvA12b, 0 };
    evPre[n++] = { 329, kEvPrefetch, 1 };
    evPreCount = n;

    evVbl[0] = { 1, kEvVblank, 0 };
    reset();
}

void Ppu::reset()
{
    ctrl = mask = status = oamAddr = 0;
    vAddr = tAddr = 0;
    fineX = 0;
    wLatch = false;
    readBuf = 0;
    bus = 0;
    memset(oam, 0xFF, sizeof(oam));
    memset(palette, 0, sizeof(palette));
    memset(ciram, 0, sizeof(ciram));
    memset(spr, 0, sizeof(spr));
    q[0] = q[1] = { 0, 0, 0 };
    clock = 0;
    scanline = lines - 1;
    dot = 0;
    odd = false;
    frameDone = false;
    hitAt = -1;
    a12Mode = 0;
    suppressVbl = false;
    chrNotify = sys && sys->mapper && sys->mapper->wantsChrFetch;
    selectEvents();
}

void Ppu::selectEvents()
{
    evIdx = 0;
    if (scanline < 240)
    {
        ev = evVisible;
        evCount = evVisibleCount;
    }
    else if (scanline == 241)
    {
        ev = evVbl;
        evCount = 1;
    }
    else if (scanline == lines - 1)
    {
        ev = evPre;
        evCount = evPreCount;
    }
    else
    {
        ev = evVisible;
        evCount = 0;
    }
}

uint16_t Ppu::lineLength() const
{
    // NTSC: the pre-render line of odd frames is one dot short when rendering is on
    if (scanline == lines - 1 && odd && lines == 262 && rendering())
        return 340;
    return 341;
}

void Ppu::endLine()
{
    dot = 0;
    hitAt = -1;
    scanline++;
    if (scanline >= lines)
    {
        scanline = 0;
        odd = !odd;
    }
    selectEvents();
}

void Ppu::runTo(int64_t target)
{
    chrNotify = sys->mapper && sys->mapper->wantsChrFetch;
    while (clock < target)
    {
        int len = lineLength();
        int next = evIdx < evCount ? ev[evIdx].pos : len;
        if (hitAt > dot && hitAt < next)
            next = hitAt;
        if (next > len)
            next = len;

        int64_t room = target - clock;
        if (room < next - dot)
        {
            dot += static_cast<int>(room);
            clock = target;
            return;
        }
        clock += next - dot;
        dot = next;

        if (hitAt == dot)
        {
            status |= 0x40;
            hitAt = -1;
        }
        while (evIdx < evCount && ev[evIdx].pos == dot)
        {
            fire(ev[evIdx]);
            evIdx++;
        }
        if (dot >= len)
            endLine();
    }
}

void Ppu::fire(const Ev& e)
{
    switch (e.type)
    {
        case kEvGroup: renderGroup(e.arg); break;
        case kEvIncY:
            if (rendering())
                incY();
            break;
        case kEvSprites:
            if (rendering())
                vAddr = static_cast<uint16_t>((vAddr & ~0x041F) | (tAddr & 0x041F));
            evalSprites(scanline);
            break;
        case kEvPreSprites:
            if (rendering())
                vAddr = static_cast<uint16_t>((vAddr & ~0x041F) | (tAddr & 0x041F));
            memset(spr, 0, sizeof(spr)); // line 0 never shows sprites
            a12Mode = 0;
            if (rendering())
                chooseA12((ctrl & 0x20) != 0, 0, nullptr);
            break;
        case kEvA12a:
            if (a12Mode == 1)
                sys->mapper->a12Rise();
            break;
        case kEvA12b:
            if (a12Mode == 2)
                sys->mapper->a12Rise();
            break;
        case kEvPrefetch:
            if (rendering())
                q[e.arg] = fetchTile();
            break;
        case kEvClear:
            status &= static_cast<uint8_t>(~0xE0);
            hitAt = -1;
            break;
        case kEvCopyY:
            if (rendering())
                vAddr = static_cast<uint16_t>((vAddr & ~0x7BE0) | (tAddr & 0x7BE0));
            break;
        case kEvVblank:
            if (suppressVbl)
                suppressVbl = false;
            else
            {
                status |= 0x80;
                if (ctrl & 0x80)
                    sys->raiseNmi(0);
            }
            frameDone = true;
            break;
        default: break;
    }
}

// ---- address registers --------------------------------------------------------------------------
void Ppu::incX()
{
    if ((vAddr & 0x001F) == 31)
    {
        vAddr = static_cast<uint16_t>(vAddr & ~0x001F);
        vAddr ^= 0x0400;
    }
    else
        vAddr++;
}

void Ppu::incY()
{
    if ((vAddr & 0x7000) != 0x7000)
    {
        vAddr += 0x1000;
        return;
    }
    vAddr &= ~0x7000;
    int cy = (vAddr & 0x03E0) >> 5;
    if (cy == 29)
    {
        cy = 0;
        vAddr ^= 0x0800;
    }
    else if (cy == 31)
        cy = 0;
    else
        cy++;
    vAddr = static_cast<uint16_t>((vAddr & ~0x03E0) | (cy << 5));
}

uint8_t Ppu::chrByte(uint16_t a) const
{
    return sys->bank.chr[(a >> 10) & 7][a & 0x3FF];
}

Ppu::Tile Ppu::fetchTile()
{
    Banks& bk = sys->bank;
    Tile t;
    uint16_t a = static_cast<uint16_t>(vAddr & 0x0FFF);
    uint8_t id = bk.nt[a >> 10][a & 0x3FF];
    uint16_t aa = static_cast<uint16_t>(0x03C0 | (vAddr & 0x0C00) | ((vAddr >> 4) & 0x38) |
                                        ((vAddr >> 2) & 0x07));
    uint8_t at = bk.nt[(aa >> 10) & 3][aa & 0x3FF];
    int sh = ((vAddr >> 4) & 4) | (vAddr & 2);
    t.pal = static_cast<uint8_t>((at >> sh) & 3);
    uint16_t pa = static_cast<uint16_t>(((ctrl & 0x10) ? 0x1000 : 0) + id * 16 + ((vAddr >> 12) & 7));
    t.lo = chrByte(pa);
    t.hi = chrByte(static_cast<uint16_t>(pa + 8));
    if (chrNotify)
        sys->mapper->chrFetch(static_cast<uint16_t>(pa + 8));
    incX();
    return t;
}

// ---- sprites --------------------------------------------------------------------------------------
void Ppu::chooseA12(bool tall, int count, const uint8_t* tiles)
{
    bool bgHigh = (ctrl & 0x10) != 0;
    a12Mode = 0;
    if (!tall)
    {
        bool spHigh = (ctrl & 0x08) != 0;
        if (!bgHigh && spHigh)
            a12Mode = 1;
        else if (bgHigh && !spHigh)
            a12Mode = 2;
        return;
    }
    // 8x16: the pattern table comes from each sprite's tile number; unused slots fetch tile $FF
    bool anyHigh = count < 8;
    for (int i = 0; i < count; i++)
        if (tiles[i] & 1)
            anyHigh = true;
    if (!bgHigh && anyHigh)
        a12Mode = 1;
    else if (bgHigh && !anyHigh)
        a12Mode = 2;
    else if (bgHigh && anyHigh)
        a12Mode = 1;
}

void Ppu::evalSprites(int line)
{
    memset(spr, 0, sizeof(spr));
    a12Mode = 0;
    if (!rendering())
        return;

    bool tall = (ctrl & 0x20) != 0;
    int h = tall ? 16 : 8;
    uint8_t sel[8];
    int count = 0;
    for (int n = 0; n < 64; n++)
    {
        int row = line - oam[n * 4];
        if (row >= 0 && row < h)
        {
            if (count < 8)
                sel[count++] = static_cast<uint8_t>(n);
            else
            {
                status |= 0x20;
                break;
            }
        }
    }

    uint8_t lo[8], hi[8], tiles[8];
    for (int i = 0; i < count; i++)
    {
        const uint8_t* s = &oam[sel[i] * 4];
        int row = line - s[0];
        if (s[2] & 0x80)
            row = h - 1 - row;
        uint16_t addr;
        if (!tall)
            addr = static_cast<uint16_t>(((ctrl & 0x08) ? 0x1000 : 0) + s[1] * 16 + row);
        else
            addr = static_cast<uint16_t>(((s[1] & 1) ? 0x1000 : 0) + (s[1] & 0xFE) * 16 + (row & 7) +
                                         (row >= 8 ? 16 : 0));
        lo[i] = chrByte(addr);
        hi[i] = chrByte(static_cast<uint16_t>(addr + 8));
        tiles[i] = s[1];
        if (chrNotify)
            sys->mapper->chrFetch(static_cast<uint16_t>(addr + 8));
    }

    // highest priority (lowest OAM index) is drawn last
    for (int i = count - 1; i >= 0; i--)
    {
        const uint8_t* s = &oam[sel[i] * 4];
        uint8_t attr = s[2];
        int x0 = s[3];
        uint8_t flags = static_cast<uint8_t>(0x80 | ((attr & 0x20) ? 0x20 : 0) | (sel[i] == 0 ? 0x40 : 0));
        uint8_t pal = static_cast<uint8_t>((attr & 3) << 2);
        for (int px = 0; px < 8; px++)
        {
            int bit = (attr & 0x40) ? px : 7 - px;
            uint8_t pix = static_cast<uint8_t>(((lo[i] >> bit) & 1) | (((hi[i] >> bit) & 1) << 1));
            if (!pix)
                continue;
            int xx = x0 + px;
            if (xx > 255)
                continue;
            spr[xx] = static_cast<uint8_t>(flags | pal | pix);
        }
    }
    chooseA12(tall, count, tiles);
}

// ---- pixel output ---------------------------------------------------------------------------------
void Ppu::renderGroup(int g)
{
    uint8_t* out = fb ? fb + scanline * 256 + g * 8 : scratchRow;
    uint8_t m = mask;
    uint8_t gmask = (m & 1) ? 0x30 : 0x3F;
    if (!(m & 0x18))
    {
        memset(out, lut[palette[0] & gmask], 8);
        return;
    }

    bool bgOn = (m & 8) != 0;
    bool spOn = (m & 0x10) != 0;
    uint16_t p0 = static_cast<uint16_t>((q[0].lo << 8) | q[1].lo);
    uint16_t p1 = static_cast<uint16_t>((q[0].hi << 8) | q[1].hi);
    int x0 = g * 8;
    for (int j = 0; j < 8; j++)
    {
        int x = x0 + j;
        uint8_t bgp = 0, bgpal = 0;
        if (bgOn && (x >= 8 || (m & 2)))
        {
            int s = 15 - j - fineX;
            bgp = static_cast<uint8_t>(((p0 >> s) & 1) | (((p1 >> s) & 1) << 1));
            bgpal = (j + fineX < 8) ? q[0].pal : q[1].pal;
        }
        uint8_t sp = (spOn && (x >= 8 || (m & 4))) ? spr[x] : 0;
        uint8_t ci;
        if (sp & 0x80)
        {
            if (bgp && (sp & 0x40) && x != 255 && hitAt < 0 && !(status & 0x40))
                hitAt = x + 2;
            if (bgp == 0 || !(sp & 0x20))
                ci = palette[0x10 | (sp & 0x0F)];
            else
                ci = palette[(bgpal << 2) | bgp];
        }
        else
            ci = bgp ? palette[(bgpal << 2) | bgp] : palette[0];
        out[j] = lut[ci & gmask];
    }

    // fetch the tile that will be shown two groups from now
    q[0] = q[1];
    q[1] = fetchTile();
}

// ---- register interface -----------------------------------------------------------------------------
static inline int palIndex(uint16_t a)
{
    int i = a & 0x1F;
    if ((i & 0x13) == 0x10)
        i &= ~0x10;
    return i;
}

uint8_t Ppu::vramRead(uint16_t a)
{
    a &= 0x3FFF;
    if (a < 0x2000)
        return sys->bank.chr[a >> 10][a & 0x3FF];
    if (a < 0x3F00)
        return sys->bank.nt[(a >> 10) & 3][a & 0x3FF];
    return palette[palIndex(a)];
}

void Ppu::vramWrite(uint16_t a, uint8_t v)
{
    a &= 0x3FFF;
    if (a < 0x2000)
    {
        if (sys->bank.chrWrite[a >> 10])
            sys->bank.chr[a >> 10][a & 0x3FF] = v;
    }
    else if (a < 0x3F00)
        sys->bank.nt[(a >> 10) & 3][a & 0x3FF] = v;
    else
        palette[palIndex(a)] = static_cast<uint8_t>(v & 0x3F);
}

uint8_t Ppu::readReg(int reg, int64_t time)
{
    runTo(time);
    switch (reg & 7)
    {
        case 2:
        {
            uint8_t s = status;
            if (scanline == 241 && dot == 0)
            {
                // read one dot before vblank is set: sees it clear and stops it from being set
                s &= static_cast<uint8_t>(~0x80);
                suppressVbl = true;
            }
            status &= static_cast<uint8_t>(~0x80);
            wLatch = false;
            bus = static_cast<uint8_t>((bus & 0x1F) | (s & 0xE0));
            return bus;
        }
        case 4:
        {
            uint8_t v = oam[oamAddr];
            if ((oamAddr & 3) == 2)
                v &= 0xE3;
            bus = v;
            return v;
        }
        case 7:
        {
            uint16_t a = static_cast<uint16_t>(vAddr & 0x3FFF);
            uint8_t ret;
            if (a >= 0x3F00)
            {
                ret = static_cast<uint8_t>((palette[palIndex(a)] & 0x3F) | (bus & 0xC0));
                readBuf = vramRead(static_cast<uint16_t>(a - 0x1000));
            }
            else
            {
                ret = readBuf;
                readBuf = vramRead(a);
            }
            vAddr = static_cast<uint16_t>(vAddr + ((ctrl & 4) ? 32 : 1));
            bus = ret;
            return ret;
        }
        default: return bus;
    }
}

void Ppu::writeReg(int reg, uint8_t v, int64_t time)
{
    runTo(time);
    bus = v;
    switch (reg & 7)
    {
        case 0:
        {
            uint8_t old = ctrl;
            ctrl = v;
            tAddr = static_cast<uint16_t>((tAddr & ~0x0C00) | ((v & 3) << 10));
            if (!(old & 0x80) && (v & 0x80) && (status & 0x80))
                sys->raiseNmi(1);
            break;
        }
        case 1: mask = v; break;
        case 3: oamAddr = v; break;
        case 4:
            oam[oamAddr] = v;
            oamAddr++;
            break;
        case 5:
            if (!wLatch)
            {
                tAddr = static_cast<uint16_t>((tAddr & ~0x001F) | (v >> 3));
                fineX = v & 7;
            }
            else
            {
                tAddr = static_cast<uint16_t>((tAddr & ~0x73E0) | ((v & 7) << 12) | ((v & 0xF8) << 2));
            }
            wLatch = !wLatch;
            break;
        case 6:
            if (!wLatch)
                tAddr = static_cast<uint16_t>((tAddr & 0x00FF) | ((v & 0x3F) << 8));
            else
            {
                tAddr = static_cast<uint16_t>((tAddr & 0xFF00) | v);
                vAddr = tAddr;
            }
            wLatch = !wLatch;
            break;
        case 7:
            vramWrite(vAddr, v);
            vAddr = static_cast<uint16_t>(vAddr + ((ctrl & 4) ? 32 : 1));
            break;
        default: break;
    }
}

} // namespace nes
