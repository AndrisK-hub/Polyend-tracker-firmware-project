#include "CrcCalculatorImpl.h"

#include "FastCRC.h"

uint32_t CrcCalculatorImpl::calculate32bitCrc(const uint8_t* data, const uint32_t datalen)
{
    FastCRC32 crcCalc;

    // FastCRC takes a 16 bit length; data on this device is never longer
    return crcCalc.crc32(data, static_cast<uint16_t>(datalen));
}
