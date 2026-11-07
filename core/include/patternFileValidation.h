#ifndef PATTERN_FILE_VALIDATION_H
#define PATTERN_FILE_VALIDATION_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Pattern files are fixed-size images of the in-memory pattern structure:
//
//   [ header 28 bytes ][ track 0 ] ... [ track N-1 ][ CRC32 ]
//   track = [ length (1 or 2 bytes) ][ steps ]
//
// Versions:
//   <= 5  128 steps, 6 byte steps (no volume / micromove), 8, 12 or 16 tracks
//   6     128 steps, 8 byte steps, 8, 12 or 16 tracks
//   7     512 steps, 8 byte steps, 16 bit track length, 16 tracks
//   8     1024 steps, same as 7
//
// The header size field is 16 bits wide, so from version 7 it holds the size in 4 byte units.
namespace PatternFileValidation
{
constexpr uint8_t kLastLegacyPatternVersion = 5;
constexpr uint8_t kFirstScaledSizeVersion = 7;

constexpr size_t kHeaderBytes = 28;
constexpr size_t kCrcSize = sizeof(uint32_t);
constexpr uint8_t kLegacyStepBytes = 6;
constexpr uint8_t kStepBytes = 8;
constexpr uint16_t kLegacySteps = 128;

constexpr uint16_t k8TracksPatternSizeV5 = 6184;
constexpr uint16_t k12TracksPatternSizeV5 = 9260;
constexpr uint16_t k16TracksPatternSizeV5 = 12336;
constexpr uint16_t k8TracksPatternSize = 8232;
constexpr uint16_t k12TracksPatternSize = 12332;
constexpr uint16_t k16TracksPatternSize128Steps = 16432;

struct Layout
{
    size_t crcDataSize;
    bool hasCrc;
    uint8_t tracks;      // tracks stored in the file
    uint16_t steps;      // steps stored per track
    uint8_t stepBytes;   // bytes of one step in the file
    uint8_t lengthBytes; // bytes of the per track length field
};

constexpr size_t layoutSize(uint8_t tracks, uint8_t lengthBytes, uint8_t stepBytes, uint16_t steps)
{
    return kHeaderBytes + static_cast<size_t>(tracks) * (lengthBytes + stepBytes * steps) +
           kCrcSize;
}

constexpr uint16_t headerSizeField(uint8_t version, size_t bytes)
{
    return static_cast<uint16_t>(version >= kFirstScaledSizeVersion ? bytes / 4 : bytes);
}

constexpr size_t headerSizeBytes(uint8_t version, uint16_t field)
{
    return version >= kFirstScaledSizeVersion ? static_cast<size_t>(field) * 4 : field;
}

// Works out how a file with the given version and header size field is laid out.
// Fails for unknown versions / sizes and for files with more steps than maxSteps.
inline bool describeLayout(uint8_t version, uint16_t sizeField, uint16_t maxSteps, Layout& layout)
{
    uint16_t steps;
    uint8_t stepBytes;
    uint8_t lengthBytes;
    bool onlyFullTracks = false;

    if (version <= kLastLegacyPatternVersion)
    {
        steps = kLegacySteps;
        stepBytes = kLegacyStepBytes;
        lengthBytes = 1;
    }
    else if (version == 6)
    {
        steps = kLegacySteps;
        stepBytes = kStepBytes;
        lengthBytes = 1;
    }
    else if (version == 7 || version == 8)
    {
        steps = version == 7 ? 512 : 1024;
        stepBytes = kStepBytes;
        lengthBytes = 2;
        onlyFullTracks = true;
    }
    else
    {
        return false;
    }

    if (steps > maxSteps)
    {
        return false;
    }

    const size_t declared = headerSizeBytes(version, sizeField);
    static constexpr uint8_t kTrackCounts[] = { 8, 12, 16 };

    for (uint8_t tracks : kTrackCounts)
    {
        if (onlyFullTracks && tracks != 16)
        {
            continue;
        }

        if (declared == layoutSize(tracks, lengthBytes, stepBytes, steps))
        {
            layout = { 0, true, tracks, steps, stepBytes, lengthBytes };
            return true;
        }
    }

    return false;
}

inline bool resolveLayout(uint8_t version,
                          uint16_t declaredSize,
                          size_t fileSize,
                          uint16_t maxSteps,
                          Layout& layout)
{
    if (!describeLayout(version, declaredSize, maxSteps, layout))
    {
        return false;
    }

    const size_t declared = headerSizeBytes(version, declaredSize);

    if (fileSize == declared)
    {
        layout.crcDataSize = fileSize - kCrcSize;
        layout.hasCrc = true;
        return true;
    }

    if (version <= kLastLegacyPatternVersion && fileSize + kCrcSize == declared)
    {
        layout.crcDataSize = fileSize;
        layout.hasCrc = false;
        return true;
    }

    return false;
}

inline uint16_t readTrackLength(const uint8_t* p, uint8_t lengthBytes)
{
    if (lengthBytes == 1)
    {
        return *p;
    }

    uint16_t value;
    memcpy(&value, p, sizeof(value));
    return value;
}

inline void writeTrackLength(uint8_t* p, uint8_t lengthBytes, uint16_t value)
{
    if (lengthBytes == 1)
    {
        *p = static_cast<uint8_t>(value);
        return;
    }

    memcpy(p, &value, sizeof(value));
}

// True when a file with layout `src` already has the in-memory layout.
inline bool isCurrentLayout(const Layout& src,
                            uint16_t dstSteps,
                            uint8_t dstLengthBytes,
                            uint8_t dstTracks)
{
    return src.steps == dstSteps && src.stepBytes == kStepBytes &&
           src.lengthBytes == dstLengthBytes && src.tracks == dstTracks;
}

// Converts a pattern file image that sits at the start of `image` into the in-memory layout
// (dstSteps steps per track, dstLengthBytes track length, dstTracks tracks) in place.
// `image` must have room for the whole destination structure.
//  - 6 byte steps get the default volume / micromove (taken from emptyStep)
//  - steps and tracks missing in the file are filled with emptyStep; missing tracks get the
//    length of track 0
//  - track lengths that point outside the stored steps are clamped
//  - the CRC is cleared (it is calculated again when the pattern is saved)
// Tracks are converted from the last to the first, steps likewise, so nothing is overwritten
// before it was read: every destination offset is >= its source offset.
inline void expandPatternImage(uint8_t* image,
                               const Layout& src,
                               uint16_t dstSteps,
                               uint8_t dstLengthBytes,
                               uint8_t dstTracks,
                               const uint8_t (&emptyStep)[kStepBytes])
{
    const size_t srcStride = src.lengthBytes + static_cast<size_t>(src.stepBytes) * src.steps;
    const size_t dstStride = dstLengthBytes + static_cast<size_t>(kStepBytes) * dstSteps;
    const uint16_t maxStoredLength = static_cast<uint16_t>(src.steps - 1);

    uint16_t track0Length = readTrackLength(image + kHeaderBytes, src.lengthBytes);
    if (track0Length > maxStoredLength)
    {
        track0Length = maxStoredLength;
    }

    for (int track = dstTracks - 1; track >= 0; --track)
    {
        uint8_t* dst = image + kHeaderBytes + static_cast<size_t>(track) * dstStride;

        if (track >= src.tracks)
        {
            for (int step = dstSteps - 1; step >= 0; --step)
            {
                memcpy(dst + dstLengthBytes + static_cast<size_t>(step) * kStepBytes,
                       emptyStep,
                       kStepBytes);
            }
            writeTrackLength(dst, dstLengthBytes, track0Length);
            continue;
        }

        const uint8_t* srcTrack = image + kHeaderBytes + static_cast<size_t>(track) * srcStride;

        uint16_t length = readTrackLength(srcTrack, src.lengthBytes);
        if (length > maxStoredLength)
        {
            length = maxStoredLength;
        }

        for (int step = dstSteps - 1; step >= src.steps; --step)
        {
            memcpy(dst + dstLengthBytes + static_cast<size_t>(step) * kStepBytes,
                   emptyStep,
                   kStepBytes);
        }

        for (int step = src.steps - 1; step >= 0; --step)
        {
            uint8_t converted[kStepBytes];
            const uint8_t* srcStep =
              srcTrack + src.lengthBytes + static_cast<size_t>(step) * src.stepBytes;

            memcpy(converted, srcStep, src.stepBytes);
            for (size_t i = src.stepBytes; i < kStepBytes; ++i)
            {
                converted[i] = emptyStep[i];
            }

            memcpy(dst + dstLengthBytes + static_cast<size_t>(step) * kStepBytes,
                   converted,
                   kStepBytes);
        }

        writeTrackLength(dst, dstLengthBytes, length);
    }

    const size_t totalSize = layoutSize(dstTracks, dstLengthBytes, kStepBytes, dstSteps);
    memset(image + totalSize - kCrcSize, 0, kCrcSize);
}
}

#endif
