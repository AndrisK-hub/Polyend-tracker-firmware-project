// 6502 core (the NES's 2A03 has no decimal mode) including the stable "unofficial" opcodes.
//
// Every bus access goes through Console::read/write, which count one CPU cycle each, so the number
// of cycles an instruction takes comes out of the access pattern (page-cross penalties, dummy
// accesses of read-modify-write instructions, ...) and does not need a separate table.
#include "nes.h"

namespace nes {

void Console::adc(uint8_t m)
{
    unsigned sum = static_cast<unsigned>(a) + m + (fC ? 1u : 0u);
    fV = ((~(a ^ m) & (a ^ sum)) & 0x80) != 0;
    fC = sum > 0xFF;
    a = static_cast<uint8_t>(sum);
    nz(a);
}

// addressing for indexed modes. mode 5: zero page indexed, anything else: absolute indexed.
uint16_t Console::eaIdx(int mode, bool useY, bool store)
{
    uint8_t idx = useY ? y : x;
    if (mode == 5)
    {
        uint8_t zp = fetch();
        cycles++; // dummy read of the unindexed address
        return static_cast<uint8_t>(zp + idx);
    }
    uint8_t lo = fetch();
    uint8_t hi = fetch();
    uint16_t base = static_cast<uint16_t>(lo | (hi << 8));
    uint16_t addr = static_cast<uint16_t>(base + idx);
    if (store || ((base ^ addr) & 0xFF00))
        cycles++; // dummy read (always for stores, only on a page cross for reads)
    return addr;
}

// "cc=01" group addressing: ORA AND EOR ADC STA LDA CMP SBC. Mode 2 (immediate) is handled by callers.
uint16_t Console::ea(int mode, bool store)
{
    switch (mode)
    {
        case 0: // (zp,X)
        {
            uint8_t zp = fetch();
            cycles++;
            zp = static_cast<uint8_t>(zp + x);
            uint8_t lo = ram[zp];
            uint8_t hi = ram[static_cast<uint8_t>(zp + 1)];
            cycles += 2;
            return static_cast<uint16_t>(lo | (hi << 8));
        }
        case 1: return fetch();
        case 3:
        {
            uint8_t lo = fetch();
            uint8_t hi = fetch();
            return static_cast<uint16_t>(lo | (hi << 8));
        }
        case 4: // (zp),Y
        {
            uint8_t zp = fetch();
            uint8_t lo = ram[zp];
            uint8_t hi = ram[static_cast<uint8_t>(zp + 1)];
            cycles += 2;
            uint16_t base = static_cast<uint16_t>(lo | (hi << 8));
            uint16_t addr = static_cast<uint16_t>(base + y);
            if (store || ((base ^ addr) & 0xFF00))
                cycles++;
            return addr;
        }
        case 5: return eaIdx(5, false, store);
        case 6: return eaIdx(7, true, store);
        default: return eaIdx(7, false, store);
    }
}

// Store of "register AND (high byte + 1)" used by SHA / SHX / SHY / TAS.
static inline uint16_t shAddress(uint16_t base, uint8_t idx, uint8_t val)
{
    uint16_t addr = static_cast<uint16_t>(base + idx);
    if ((base ^ addr) & 0xFF00)
        addr = static_cast<uint16_t>((addr & 0x00FF) | (val << 8));
    return addr;
}

void Console::serviceInterrupt(uint16_t vector, bool brk)
{
    if (!brk)
        cycles += 2;
    push(static_cast<uint8_t>(pc >> 8));
    push(static_cast<uint8_t>(pc & 0xFF));
    push(packP(brk));
    fI = true;
    uint8_t lo = read(vector);
    uint8_t hi = read(static_cast<uint16_t>(vector + 1));
    pc = static_cast<uint16_t>(lo | (hi << 8));
}

void Console::execGroup3(uint8_t op)
{
    int aaa = op >> 5;
    int bbb = (op >> 2) & 7;

    if (bbb == 2) // immediate forms
    {
        uint8_t m = fetch();
        switch (aaa)
        {
            case 0:
            case 1: // ANC
                a &= m;
                nz(a);
                fC = fN;
                break;
            case 2: // ALR
                a &= m;
                a = lsr(a);
                break;
            case 3: // ARR
            {
                a &= m;
                a = static_cast<uint8_t>((a >> 1) | (fC ? 0x80 : 0));
                nz(a);
                fC = (a & 0x40) != 0;
                fV = (((a >> 6) ^ (a >> 5)) & 1) != 0;
                break;
            }
            case 4: // XAA (unstable on hardware, the usual approximation)
                a = static_cast<uint8_t>((a | 0xEE) & x & m);
                nz(a);
                break;
            case 5: // LXA
                a = x = static_cast<uint8_t>((a | 0xEE) & m);
                nz(a);
                break;
            case 6: // AXS
            {
                uint8_t t = a & x;
                fC = t >= m;
                x = static_cast<uint8_t>(t - m);
                nz(x);
                break;
            }
            default: // 0xEB: SBC
                adc(static_cast<uint8_t>(~m));
                break;
        }
        return;
    }

    // the SH* family and LAS have their own address handling
    if (aaa == 4 && (bbb == 4 || bbb == 6 || bbb == 7))
    {
        uint16_t base;
        if (bbb == 4) // 0x93 SHA (zp),Y
        {
            uint8_t zp = fetch();
            uint8_t lo = ram[zp];
            uint8_t hi = ram[static_cast<uint8_t>(zp + 1)];
            cycles += 2;
            base = static_cast<uint16_t>(lo | (hi << 8));
        }
        else
        {
            uint8_t lo = fetch();
            uint8_t hi = fetch();
            base = static_cast<uint16_t>(lo | (hi << 8));
        }
        cycles++;
        uint8_t val;
        if (bbb == 6) // 0x9B TAS
        {
            sp = a & x;
            val = sp;
        }
        else
            val = a & x;
        val &= static_cast<uint8_t>((base >> 8) + 1);
        write(shAddress(base, y, val), val);
        return;
    }

    uint16_t addr = 0;
    bool store = aaa != 5;
    switch (bbb)
    {
        case 0: addr = ea(0, store); break;
        case 1: addr = fetch(); break;
        case 3: addr = ea(3, store); break;
        case 4: addr = ea(4, store); break;
        case 5: addr = eaIdx(5, aaa == 4 || aaa == 5, store); break;
        case 6: addr = eaIdx(7, true, store); break;
        default: addr = eaIdx(7, aaa == 5, store); break; // bbb 7: abs,Y for LAX, abs,X otherwise
    }

    if (aaa == 5 && bbb == 6) // 0xBB LAS
    {
        uint8_t m = static_cast<uint8_t>(read(addr) & sp);
        a = x = sp = m;
        nz(m);
        return;
    }

    switch (aaa)
    {
        case 4: write(addr, a & x); break; // SAX
        case 5:                            // LAX
        {
            uint8_t m = read(addr);
            a = x = m;
            nz(m);
            break;
        }
        default: // read-modify-write combos
        {
            uint8_t m = read(addr);
            write(addr, m); // the dummy write real hardware does
            switch (aaa)
            {
                case 0: // SLO
                    m = asl(m);
                    a |= m;
                    nz(a);
                    break;
                case 1: // RLA
                    m = rol(m);
                    a &= m;
                    nz(a);
                    break;
                case 2: // SRE
                    m = lsr(m);
                    a ^= m;
                    nz(a);
                    break;
                case 3: // RRA
                    m = ror(m);
                    adc(m);
                    break;
                case 6: // DCP
                    m = static_cast<uint8_t>(m - 1);
                    cmp(a, m);
                    break;
                default: // ISC
                    m = static_cast<uint8_t>(m + 1);
                    adc(static_cast<uint8_t>(~m));
                    break;
            }
            write(addr, m);
            break;
        }
    }
}

uint8_t Console::step()
{
    if (jammed)
    {
        cycles += 2;
        return 0x02;
    }

    uint8_t op = fetch();
    switch (op)
    {
        // ---- control flow and stack ----
        case 0x00: // BRK
            fetch();
            serviceInterrupt(0xFFFE, true);
            break;
        case 0x20: // JSR
        {
            uint8_t lo = fetch();
            cycles++;
            push(static_cast<uint8_t>(pc >> 8));
            push(static_cast<uint8_t>(pc & 0xFF));
            uint8_t hi = read(pc);
            pc = static_cast<uint16_t>(lo | (hi << 8));
            break;
        }
        case 0x40: // RTI
        {
            cycles += 2;
            unpackP(pull());
            uint8_t lo = pull();
            uint8_t hi = pull();
            pc = static_cast<uint16_t>(lo | (hi << 8));
            break;
        }
        case 0x60: // RTS
        {
            cycles += 3;
            uint8_t lo = pull();
            uint8_t hi = pull();
            pc = static_cast<uint16_t>((lo | (hi << 8)) + 1);
            break;
        }
        case 0x4C: // JMP abs
        {
            uint8_t lo = fetch();
            uint8_t hi = read(pc);
            pc = static_cast<uint16_t>(lo | (hi << 8));
            break;
        }
        case 0x6C: // JMP (ind) with the page wrap bug
        {
            uint8_t lo = fetch();
            uint8_t hi = fetch();
            uint16_t ptr = static_cast<uint16_t>(lo | (hi << 8));
            uint8_t tl = read(ptr);
            uint8_t th = read(static_cast<uint16_t>((ptr & 0xFF00) | ((ptr + 1) & 0x00FF)));
            pc = static_cast<uint16_t>(tl | (th << 8));
            break;
        }
        case 0x48: // PHA
            cycles++;
            push(a);
            break;
        case 0x08: // PHP
            cycles++;
            push(packP(true));
            break;
        case 0x68: // PLA
            cycles += 2;
            a = pull();
            nz(a);
            break;
        case 0x28: // PLP
            cycles += 2;
            unpackP(pull());
            break;

        // ---- implied ----
        case 0xAA: cycles++; x = a; nz(x); break;
        case 0x8A: cycles++; a = x; nz(a); break;
        case 0xA8: cycles++; y = a; nz(y); break;
        case 0x98: cycles++; a = y; nz(a); break;
        case 0xBA: cycles++; x = sp; nz(x); break;
        case 0x9A: cycles++; sp = x; break;
        case 0xE8: cycles++; x = static_cast<uint8_t>(x + 1); nz(x); break;
        case 0xCA: cycles++; x = static_cast<uint8_t>(x - 1); nz(x); break;
        case 0xC8: cycles++; y = static_cast<uint8_t>(y + 1); nz(y); break;
        case 0x88: cycles++; y = static_cast<uint8_t>(y - 1); nz(y); break;
        case 0x18: cycles++; fC = false; break;
        case 0x38: cycles++; fC = true; break;
        case 0x58: cycles++; fI = false; break;
        case 0x78: cycles++; fI = true; break;
        case 0xB8: cycles++; fV = false; break;
        case 0xD8: cycles++; fD = false; break;
        case 0xF8: cycles++; fD = true; break;
        case 0xEA: cycles++; break;

        default:
        {
            if ((op & 0x1F) == 0x10) // branches
            {
                bool flag;
                switch (op >> 6)
                {
                    case 0: flag = fN; break;
                    case 1: flag = fV; break;
                    case 2: flag = fC; break;
                    default: flag = fZ; break;
                }
                branch(flag == (((op >> 5) & 1) != 0));
                break;
            }

            int cc = op & 3;
            int bbb = (op >> 2) & 7;
            int aaa = op >> 5;
            switch (cc)
            {
                case 1:
                {
                    uint8_t m;
                    if (bbb == 2)
                    {
                        m = fetch();
                        if (aaa == 4) // 0x89: NOP #imm
                            break;
                    }
                    else
                    {
                        uint16_t addr = ea(bbb, aaa == 4);
                        if (aaa == 4)
                        {
                            write(addr, a);
                            break;
                        }
                        m = read(addr);
                    }
                    switch (aaa)
                    {
                        case 0: a |= m; nz(a); break;
                        case 1: a &= m; nz(a); break;
                        case 2: a ^= m; nz(a); break;
                        case 3: adc(m); break;
                        case 5: a = m; nz(a); break;
                        case 6: cmp(a, m); break;
                        default: adc(static_cast<uint8_t>(~m)); break;
                    }
                    break;
                }

                case 2:
                {
                    switch (bbb)
                    {
                        case 0:
                            if (aaa == 5)
                            {
                                x = fetch();
                                nz(x);
                            }
                            else if (aaa >= 4)
                                fetch(); // NOP #imm
                            else
                            {
                                jammed = true;
                                pc--;
                            }
                            break;
                        case 2: // accumulator shifts (aaa < 4; the others are handled above)
                            cycles++;
                            switch (aaa)
                            {
                                case 0: a = asl(a); break;
                                case 1: a = rol(a); break;
                                case 2: a = lsr(a); break;
                                default: a = ror(a); break;
                            }
                            break;
                        case 4:
                            jammed = true; // KIL opcodes
                            pc--;
                            break;
                        case 6: cycles++; break; // unofficial implied NOPs
                        default:                 // 1, 3, 5, 7: memory forms
                        {
                            if (op == 0x9E) // SHX abs,Y
                            {
                                uint8_t lo = fetch();
                                uint8_t hi = fetch();
                                uint16_t base = static_cast<uint16_t>(lo | (hi << 8));
                                cycles++;
                                uint8_t val = x & static_cast<uint8_t>(hi + 1);
                                write(shAddress(base, y, val), val);
                                break;
                            }
                            uint16_t addr;
                            if (bbb == 1)
                                addr = fetch();
                            else if (bbb == 3)
                                addr = ea(3, true);
                            else
                                addr = eaIdx(bbb, aaa == 4 || aaa == 5, aaa != 5);
                            if (aaa == 4)
                                write(addr, x);
                            else if (aaa == 5)
                            {
                                x = read(addr);
                                nz(x);
                            }
                            else
                            {
                                uint8_t m = read(addr);
                                write(addr, m);
                                switch (aaa)
                                {
                                    case 0: m = asl(m); break;
                                    case 1: m = rol(m); break;
                                    case 2: m = lsr(m); break;
                                    case 3: m = ror(m); break;
                                    case 6: m = static_cast<uint8_t>(m - 1); nz(m); break;
                                    default: m = static_cast<uint8_t>(m + 1); nz(m); break;
                                }
                                write(addr, m);
                            }
                            break;
                        }
                    }
                    break;
                }

                case 0:
                {
                    switch (bbb)
                    {
                        case 0: // immediate
                            if (aaa == 5)
                            {
                                y = fetch();
                                nz(y);
                            }
                            else if (aaa == 6)
                                cmp(y, fetch());
                            else if (aaa == 7)
                                cmp(x, fetch());
                            else
                                fetch(); // 0x80 NOP #imm
                            break;
                        case 7:
                        {
                            if (op == 0x9C) // SHY abs,X
                            {
                                uint8_t lo = fetch();
                                uint8_t hi = fetch();
                                uint16_t base = static_cast<uint16_t>(lo | (hi << 8));
                                cycles++;
                                uint8_t val = y & static_cast<uint8_t>(hi + 1);
                                write(shAddress(base, x, val), val);
                                break;
                            }
                            uint16_t addr = eaIdx(7, false, false);
                            if (aaa == 5)
                            {
                                y = read(addr);
                                nz(y);
                            }
                            else
                                read(addr); // NOP abs,X
                            break;
                        }
                        default: // 1 (zp), 3 (abs), 5 (zp,X)
                        {
                            uint16_t addr;
                            if (bbb == 1)
                                addr = fetch();
                            else if (bbb == 3)
                                addr = ea(3, false);
                            else
                                addr = eaIdx(5, false, aaa == 4);
                            switch (aaa)
                            {
                                case 1: // BIT (only the zp / abs forms exist; 0x34 is a NOP)
                                {
                                    uint8_t m = read(addr);
                                    if (bbb != 5)
                                    {
                                        fZ = (a & m) == 0;
                                        fN = (m & 0x80) != 0;
                                        fV = (m & 0x40) != 0;
                                    }
                                    break;
                                }
                                case 4: write(addr, y); break;
                                case 5: y = read(addr); nz(y); break;
                                case 6: if (bbb == 5) read(addr); else cmp(y, read(addr)); break;
                                case 7: if (bbb == 5) read(addr); else cmp(x, read(addr)); break;
                                default: read(addr); break; // NOP zp / abs / zp,X
                            }
                            break;
                        }
                    }
                    break;
                }

                default: execGroup3(op); break;
            }
            break;
        }
    }
    return op;
}

} // namespace nes
