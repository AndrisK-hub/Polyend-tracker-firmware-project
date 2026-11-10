#include "tradeMonitorData.h"

#include <string.h>

namespace trademon {

namespace {

constexpr uint8_t kManufacturer = 0x7D;
constexpr uint8_t kMagic1 = 0x54; // 'T'
constexpr uint8_t kMagic2 = 0x4D; // 'M'

void copyText(char* dst, int capacity, const uint8_t* src, int count)
{
    int n = count < capacity ? count : capacity;
    for (int i = 0; i < n; i++)
    {
        const uint8_t c = src[i];
        dst[i] = (c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : '?';
    }
    dst[n] = 0;
}

} // namespace

void Page::clear()
{
    memset(rows, 0, sizeof(rows));
    memset(rowStyle, 0, sizeof(rowStyle));
    memset(log, 0, sizeof(log));
    logNext = 0;
    logCount = 0;
    changes++;
}

const char* Page::logLine(int age) const
{
    if (age < 0 || age >= logCount)
    {
        return nullptr;
    }
    int index = logNext - 1 - age;
    if (index < 0)
    {
        index += kLogLines;
    }
    return log[index];
}

void Receiver::reset()
{
    length = 0;
    inMessage = false;
}

void Receiver::feed(const uint8_t* bytes, uint32_t count, uint32_t nowMs)
{
    for (uint32_t i = 0; i < count; i++)
    {
        const uint8_t b = bytes[i];
        if (b == 0xF0)
        {
            // a new message; anything unfinished before it is lost
            inMessage = true;
            length = 0;
        }
        else if (b == 0xF7)
        {
            if (inMessage)
            {
                handle(nowMs);
            }
            inMessage = false;
            length = 0;
        }
        else if (b >= 0x80)
        {
            // no other status byte belongs inside a SysEx message: drop what we have
            if (inMessage)
            {
                rejected++;
            }
            inMessage = false;
            length = 0;
        }
        else if (inMessage && length < kMaxMessage)
        {
            buf[length++] = b; // the rest of a too long message is cut off
        }
    }
}

void Receiver::handle(uint32_t nowMs)
{
    if (length < 4 || buf[0] != kManufacturer || buf[1] != kMagic1 || buf[2] != kMagic2)
    {
        rejected++;
        return;
    }
    const uint8_t* data = buf + 4;
    const int dataLength = length - 4;

    switch (buf[3])
    {
        case kCmdSetRow:
        {
            if (dataLength < 2 || data[0] >= kRows)
            {
                rejected++;
                return;
            }
            const uint8_t row = data[0];
            copyText(page->rows[row], kRowChars, data + 2, dataLength - 2);
            page->rowStyle[row] = data[1];
            break;
        }
        case kCmdLog:
        {
            copyText(page->log[page->logNext], kLogChars, data, dataLength);
            page->logNext = static_cast<uint8_t>((page->logNext + 1) % kLogLines);
            if (page->logCount < kLogLines)
            {
                page->logCount++;
            }
            break;
        }
        case kCmdClear: page->clear(); break;
        case kCmdHeartbeat: break;
        case kCmdClearLog:
        {
            memset(page->log, 0, sizeof(page->log));
            page->logNext = 0;
            page->logCount = 0;
            break;
        }
        default: rejected++; return;
    }

    messages++;
    page->changes++;
    page->lastMessageMs = nowMs;
    page->received = true;
}

} // namespace trademon
