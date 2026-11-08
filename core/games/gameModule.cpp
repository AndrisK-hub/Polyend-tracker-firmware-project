#ifdef TRACKER_MINI

#include "gameModule.h"

#include <new>
#include <stdio.h>
#include <string.h>

#include "Platform.h"
#include "core/interface.h"
#include "core/songTimer.h"
#include "mtAudioEngine.h"
#include "mtPadBoard.h"
#include "mtPadsBacklight.h"
#include "mtSequencer.h"
#include "nes/nes.h"
#include "nesAudio.h"

extern uint8_t externalRamScratchBuff[];
extern void setMiniNavButtonLock(bool enabled);

// The sound stream is owned by the audio engine (see nesAudio.h).
static inline AudioNesStream& nesAudio()
{
    return *engine.getNesStream();
}

cGameModule gameModule;

namespace {

// ---------------------------------------------------------------------------------------------
// Button layout. The Mini has a D-pad, a middle key and four function keys:
//   arrows = D-pad, Fn1 = Select, Fn2 = Start, Fn3 = B, Fn4 = A, Middle = Start.
// Tap Menu for a sound test (a beep and some numbers), hold Menu to leave the game.
constexpr uint8_t kMapFn1 = nes::kBtnSelect;
constexpr uint8_t kMapFn2 = nes::kBtnStart;
constexpr uint8_t kMapFn3 = nes::kBtnB;
constexpr uint8_t kMapFn4 = nes::kBtnA;
constexpr uint8_t kMapMiddle = nes::kBtnStart;

// ---------------------------------------------------------------------------------------------
// Memory. The 768 KB scratch area in external RAM is free while a game runs:
//   [ Console | ROM image | cartridge RAM and mapper state (arena) ... | sound buffer | back buffer ]
constexpr uint32_t kScratchSize = SHARED_SCRATCH_MEMORY_SIZE;
constexpr uint32_t kFrameBytes = 256 * 240;
constexpr uint32_t kBackBufferOffset = kScratchSize - kFrameBytes;
constexpr uint32_t kAudioRingSamples = 4096; // a power of two; about 93 ms of sound
constexpr uint32_t kAudioRingBytes = kAudioRingSamples * sizeof(int16_t);
constexpr uint32_t kAudioRingOffset = kBackBufferOffset - kAudioRingBytes;
constexpr uint32_t kMinArena = 16 * 1024; // the cartridge may still need much more; load() tells
constexpr uint32_t kReadChunk = 16 * 1024; // a multiple of 512, as the SD driver wants

constexpr uint32_t kAudioSampleRate = 44118; // the audio engine's rate (44117.6 Hz)
constexpr uint32_t kFrameMicros = 16639;    // NTSC: 60.0988 frames per second
constexpr uint32_t kFrameMicrosPal = 19997; // PAL: 50.007 frames per second

// While sound is playing, the audio clock sets the pace: the next frame is made when the sound
// buffer has run down to this many samples (about 46 ms). That leaves room for a frame of emulation
// plus the rest of the main loop without the sound running dry.
constexpr uint32_t kAudioTargetFill = 2048;
// Below this (about 23 ms of sound) the emulation is falling behind: the next frame is run without
// drawing its picture, which takes about 60 % of the time, and if the buffer is still that low
// afterwards a second frame follows straight away.
constexpr uint32_t kAudioLowFill = 1024;
constexpr uint8_t kMaxSkippedPictures = 3; // in a row, so the picture still moves
// Without a request for sound for this long (microseconds) the timer takes over again.
constexpr int32_t kAudioStallUs = 100000;

constexpr uint32_t alignUp32(uint32_t v)
{
    return (v + 31u) & ~31u;
}

char s_gameName[256] = "";
volatile uint8_t s_pad = 0;

// Pushes freshly written bytes out of the data cache so that the DMA reads the new picture.
void cleanDCache(const void* addr, uint32_t size)
{
#ifdef PLATFORM_IMX
    // SCB_BASE (0xE000ED00) + 0x268 = 0xE000EF68 (SCB->DCCMVAC): clean by address to PoC
    volatile uint32_t* const dccmvac = reinterpret_cast<volatile uint32_t*>(0xE000EF68);
    uintptr_t p = reinterpret_cast<uintptr_t>(addr) & ~static_cast<uintptr_t>(0x1F);
    const uintptr_t end = reinterpret_cast<uintptr_t>(addr) + size;

    asm volatile("dsb 0xf" ::: "memory");
    while (p < end)
    {
        *dccmvac = static_cast<uint32_t>(p);
        p += 32;
    }
    asm volatile("dsb 0xf" ::: "memory");
    asm volatile("isb 0xf" ::: "memory");
#else
    (void)addr;
    (void)size;
#endif
}

void setPadBit(uint8_t mask, uint8_t state)
{
    if (state == buttonPress)
    {
        s_pad = s_pad | mask;
    }
    else if (state == buttonRelease)
    {
        s_pad = s_pad & ~mask;
    }
    // hold and double-click events carry no new information for a game
}

// What the console should see. Left+Right or Up+Down at once cannot happen on a real pad and
// confuses many games, so such pairs cancel out.
uint8_t readPad()
{
    uint8_t p = s_pad;
    if ((p & (nes::kBtnLeft | nes::kBtnRight)) == (nes::kBtnLeft | nes::kBtnRight))
    {
        p &= ~(nes::kBtnLeft | nes::kBtnRight);
    }
    if ((p & (nes::kBtnUp | nes::kBtnDown)) == (nes::kBtnUp | nes::kBtnDown))
    {
        p &= ~(nes::kBtnUp | nes::kBtnDown);
    }
    return p;
}

// A tiny 3x5 font for the numbers of the sound test: 0-9 and A-F, then the letters M W T P J S that
// label the numbers (and L K); five rows of three bits each.
constexpr uint16_t kGlyphs[24] = { 0x7B6F, 0x2C97, 0x73E7, 0x73CF, 0x5BC9, 0x79CF, 0x79EF, 0x7252,
                                   0x7BEF, 0x7BCF, 0x7BED, 0x6BAE, 0x7927, 0x6B6E, 0x79E7, 0x79E4,
                                   0x5FED, 0x5BFD, 0x7492, 0x7BE4, 0x126A, 0x388E, 0x4927, 0x5BAD };
constexpr uint8_t kInfoBackground = 0x00; // black and white in the picture's colour format
constexpr uint8_t kInfoForeground = 0xFF;
constexpr int kInfoScale = 2;
constexpr int kInfoAdvance = 4 * kInfoScale;
constexpr int kInfoRowHeight = 6 * kInfoScale;
constexpr int kInfoRows = 5;
constexpr uint16_t kInfoFrames = 900; // about fifteen seconds
constexpr uint32_t kBeepSamples = 11025; // a quarter of a second
constexpr int16_t kBeepLevel = 8000;
constexpr uint32_t kBeepHalfPeriod = 25; // about 880 Hz

void drawGlyph(uint8_t* frame, int x, int y, uint8_t index)
{
    const uint16_t bits = kGlyphs[index < 24 ? index : 0];
    for (int row = 0; row < 5; row++)
    {
        for (int col = 0; col < 3; col++)
        {
            if (bits & (1u << (14 - (row * 3 + col))))
            {
                for (int dy = 0; dy < kInfoScale; dy++)
                {
                    memset(frame + (y + row * kInfoScale + dy) * 256 + x + col * kInfoScale,
                           kInfoForeground,
                           kInfoScale);
                }
            }
        }
    }
}

// Draws capital hex digits (0-9, A-F) and the labels M W T P J S L K; every other character is a gap.
void drawInfoText(uint8_t* frame, int x, int y, const char* text)
{
    static const char kLabels[] = "MWTPJSLK"; // glyphs 16..23
    for (; *text != 0; text++, x += kInfoAdvance)
    {
        const char c = *text;
        if (c >= '0' && c <= '9')
        {
            drawGlyph(frame, x, y, static_cast<uint8_t>(c - '0'));
        }
        else if (c >= 'A' && c <= 'F')
        {
            drawGlyph(frame, x, y, static_cast<uint8_t>(c - 'A' + 10));
        }
        else
        {
            for (uint8_t i = 0; kLabels[i] != 0; i++)
            {
                if (kLabels[i] == c)
                {
                    drawGlyph(frame, x, y, static_cast<uint8_t>(16 + i));
                }
            }
        }
    }
}

void functUp(uint8_t state)
{
    setPadBit(nes::kBtnUp, state);
}
void functDown(uint8_t state)
{
    setPadBit(nes::kBtnDown, state);
}
void functLeft(uint8_t state)
{
    setPadBit(nes::kBtnLeft, state);
}
void functRight(uint8_t state)
{
    setPadBit(nes::kBtnRight, state);
}
void functMiddle(uint8_t state)
{
    setPadBit(kMapMiddle, state);
}
void functFn1(uint8_t state)
{
    setPadBit(kMapFn1, state);
}
void functFn2(uint8_t state)
{
    setPadBit(kMapFn2, state);
}
void functFn3(uint8_t state)
{
    setPadBit(kMapFn3, state);
}
void functFn4(uint8_t state)
{
    setPadBit(kMapFn4, state);
}

// A game in progress is only left on purpose (a long press), so a slip of the thumb doesn't lose
// it. The error screen has nothing to lose, so a short press leaves it.
void functMenuRunning(uint8_t state)
{
    if (state == buttonPress)
    {
        gameModule.soundTest();
    }
    else if (state == buttonHold)
    {
        gameModule.quitRequested();
    }
}

void functMenuError(uint8_t state)
{
    if (state == buttonPress)
    {
        gameModule.quitRequested();
    }
}

} // namespace

// ---------------------------------------------------------------------------------------------
void gameModuleSetFile(const char* name)
{
    strncpy(s_gameName, name, sizeof(s_gameName) - 1);
    s_gameName[sizeof(s_gameName) - 1] = 0;
}

void cGameModule::quitRequested()
{
    uint8_t button = interfaceButtonConfig;
    eventFunct(eventSwitchModule, this, &button, nullptr);
}

void cGameModule::initDisplayControls()
{
    strControlProperties prop;
    prop.x = 400;
    prop.w = 760;
    prop.h = 30;
    prop.colors = display.getTheme().noSdInfoTextColors;
    prop.style = (controlStyleCenterY | controlStyleCenterX); // hidden until there is an error

    prop.y = 190;
    titleText = display.createControl<cSimpleText>(&prop);
    prop.y = 235;
    detailText = display.createControl<cSimpleText>(&prop);
    prop.y = 300;
    hintText = display.createControl<cSimpleText>(&prop);
}

void cGameModule::showError(const char* reason)
{
    state = kStateError;

    ip->setButtonObj(interfaceButtonMiniMenu, functMenuError);

    display.setControlText(titleText, "This game cannot be started");
    display.setControlText(detailText, reason);
    display.setControlText(hintText, "Press Menu to go back");
    display.setControlShow(titleText);
    display.setControlShow(detailText);
    display.setControlShow(hintText);
    display.refreshControl(titleText);
    display.refreshControl(detailText);
    display.refreshControl(hintText);
}

// Reads the ROM into the scratch memory and hands it to the emulator. On failure the reason is
// shown on the screen.
bool cGameModule::loadRom(const char* name)
{
    FileSystem* fs = platform->getFileSystem();

    // The list shows the names without ".nes", but a file may also lack that ending.
    char path[300];
    snprintf(path, sizeof(path), "Games/%s.nes", name);
    size_t fileSize = fs->getFileSize(path);
    if (fileSize == 0)
    {
        snprintf(path, sizeof(path), "Games/%s", name);
        fileSize = fs->getFileSize(path);
    }
    if (fileSize == 0)
    {
        showError("The file is empty or cannot be read");
        return false;
    }

    const uint32_t consoleBytes = alignUp32(sizeof(nes::Console));
    const uint32_t romMax = kAudioRingOffset - consoleBytes - kMinArena;
    if (fileSize > romMax)
    {
        showError("The game is too big for the memory (limit about 690 KB)");
        return false;
    }

    // nothing else may use the scratch memory now
    engine.getSdPlayer()->stop();

    uint8_t* base = externalRamScratchBuff;
    uint8_t* rom = base + consoleBytes;
    const uint32_t romBytes = static_cast<uint32_t>(fileSize);

    auto [opened, file] = fs->openFile(path, FileSystem::kRead);
    if (!opened)
    {
        showError("The file cannot be opened");
        return false;
    }

    uint32_t done = 0;
    while (done < romBytes)
    {
        uint32_t chunk = romBytes - done;
        if (chunk > kReadChunk)
        {
            chunk = kReadChunk;
        }
        int32_t got = fs->readFile(rom + done, chunk, file);
        if (got != static_cast<int32_t>(chunk))
        {
            break;
        }
        done += chunk;
    }
    fs->closeFile(file, false);

    if (done != romBytes)
    {
        showError("The file could not be read completely");
        return false;
    }

    console = new (base) nes::Console();

    uint8_t* arena = rom + alignUp32(romBytes);
    backBuffer = base + kBackBufferOffset;
    const uint32_t arenaBytes = static_cast<uint32_t>((base + kAudioRingOffset) - arena);

    switch (console->load(rom, romBytes, arena, arenaBytes))
    {
        case nes::kLoadOk: break;
        case nes::kLoadBadHeader:
            showError("This is not a valid NES file");
            return false;
        case nes::kLoadTruncated:
            showError("The file is incomplete (shorter than its header says)");
            return false;
        case nes::kLoadUnsupportedMapper:
            showError("This game uses a cartridge type that is not supported yet");
            return false;
        case nes::kLoadNoMemory:
        default: showError("Not enough memory for this game"); return false;
    }

    return true;
}

void cGameModule::start(uint32_t options)
{
    (void)options;

    moduleRefresh = 1;
    state = kStateIdle;
    frameReady = false;
    s_pad = 0;
    console = nullptr;
    beepLeft = 0;
    beepPhase = 0;
    soundInfoFrames = 0;
    lastFrameSamples = 0;
    lastPeak = 0;
    lastFrameUs = 0;
    maxPeak = 0;
    sentPeakHold = 0;
    selfTestPeak = 0;
    skippedInRow = 0;
    skipHistory = 0;
    lastUpdateUs = 0;
    maxGapUs = 0;

    songTimer.hide();
    mtPadBoard.releaseAllInstruments();
    padsBacklight.clearGlobalPads(1, 1, 1);
    sequencer.stop();

    if (!loadRom(s_gameName))
    {
        return; // loadRom() has put the reason on the screen
    }

    memset(backBuffer, 0, kFrameBytes);
    console->setFrameBuffer(backBuffer);

    // A fixed note through the real sound chip code on this device, before the game starts; the
    // result (S on the sound test screen) tells "the chip is broken here" from "the game is quiet".
    console->apu.setSampleRate(kAudioSampleRate); // kept by the reset at the end of the test
    selfTestPeak = console->apuSelfTest();

    setMiniNavButtonLock(false);

    // the sound goes to the audio engine through a ring buffer at the end of the scratch memory
    frameMicros = console->palTiming ? kFrameMicrosPal : kFrameMicros;
    nesAudio().begin(reinterpret_cast<int16_t*>(externalRamScratchBuff + kAudioRingOffset), kAudioRingSamples);
    engine.setNesSoundEnabled(true);
    lastAudioBlocks = nesAudio().blocksRequested();
    lastAudioProgressUs = platform->getTimer()->micros();

    ip->setButtonObj(interfaceButtonUp, functUp);
    ip->setButtonObj(interfaceButtonDown, functDown);
    ip->setButtonObj(interfaceButtonLeft, functLeft);
    ip->setButtonObj(interfaceButtonRight, functRight);
    ip->setButtonObj(interfaceButtonMiniMiddle, functMiddle);
    ip->setButtonObj(interfaceButtonMiniFn1, functFn1);
    ip->setButtonObj(interfaceButtonMiniFn2, functFn2);
    ip->setButtonObj(interfaceButtonMiniFn3, functFn3);
    ip->setButtonObj(interfaceButtonMiniFn4, functFn4);
    ip->setButtonObj(interfaceButtonMiniMenu, functMenuRunning);

    // The normal screen stops drawing; from now on the display only shows the game's picture.
    display.disable();
    display.startVideoStream();

    nextFrameUs = platform->getTimer()->micros();
    state = kStateRunning;
}

void cGameModule::stop()
{
    moduleRefresh = 0;

    // the sound buffer lives in the memory the game used; shut it out before anything else does
    engine.setNesSoundEnabled(false);
    nesAudio().end();

    if (state == kStateRunning)
    {
        display.stopVideoStream();
        display.enable();
    }

    state = kStateIdle;
    console = nullptr;
    backBuffer = nullptr;
    s_pad = 0;

    // give the Fn keys and the D-pad back to the rest of the firmware
    setMiniNavButtonLock(true);
    mtInterface.initSpecialMiniTrackerButtons();

    mtPadBoard.releaseAllInstruments();
    padsBacklight.clearGlobalPads(1, 1, 1);
}

// Sends the newest finished picture to the display, if the display is ready for one.
void cGameModule::presentFrame()
{
    if (frameReady && display.isVideoStreamIdle())
    {
        uint8_t* shown = reinterpret_cast<uint8_t*>(display.getFrameBuffer());
        memcpy(shown, backBuffer, kFrameBytes);
        cleanDCache(shown, kFrameBytes);
        frameReady = false;
        display.updateVideoStream(true); // starts the transfer
    }
    else
    {
        display.updateVideoStream(false); // keeps a transfer in progress moving
    }
}

// Hands the sound the emulated chip has made since the last call to the audio engine.
void cGameModule::collectSound()
{
    int16_t samples[256];
    uint32_t total = 0;
    uint32_t peak = 0;
    for (;;)
    {
        const int count = console->apu.readSamples(samples, 256);
        if (count <= 0)
        {
            break;
        }
        total += static_cast<uint32_t>(count);
        for (int i = 0; i < count; i++)
        {
            const uint32_t level = static_cast<uint32_t>(samples[i] < 0 ? -samples[i] : samples[i]);
            if (level > peak)
            {
                peak = level;
            }
        }

        // the test beep goes through the same buffer, mixer and output as the game's sound
        for (int i = 0; i < count && beepLeft > 0; i++, beepLeft--, beepPhase++)
        {
            const int32_t beep = ((beepPhase / kBeepHalfPeriod) & 1) ? kBeepLevel : -kBeepLevel;
            int32_t mixed = samples[i] + beep;
            if (mixed > 32767)
            {
                mixed = 32767;
            }
            else if (mixed < -32768)
            {
                mixed = -32768;
            }
            samples[i] = static_cast<int16_t>(mixed);
        }

        nesAudio().push(samples, static_cast<uint32_t>(count));
    }
    lastFrameSamples = total;
    lastPeak = peak;
    if (peak > maxPeak)
    {
        maxPeak = peak;
    }
}

void cGameModule::soundTest()
{
    if (state != kStateRunning)
    {
        return;
    }
    beepLeft = kBeepSamples;
    beepPhase = 0;
    soundInfoFrames = kInfoFrames;
    nesAudio().takeSentPeak(); // T starts from this tap
    sentPeakHold = 0;
    maxGapUs = 0;
}

// Numbers about the sound in the top left corner of the picture, all in hexadecimal:
//   B  audio blocks the audio engine has asked for (counts up while the audio engine runs)
//   F  samples waiting in the sound buffer
//   A  1 = the sound stream is connected to the mixer, 2 = it is switched on (3 = both)
//   C  samples the emulated sound chip made for the last picture (about 2DE)
//   E  loudest of them (0 = the game is silent right now)
//   D  milliseconds the emulator needed for the last picture (16 or less keeps up)
//   M  loudest sound the chip has made since the game started (0 = nothing yet)
//   W  writes to the sound chip's registers since the game started (0 = the game never asks for sound)
//   T  loudest sample sent to the mixer since the Menu tap (about 1F40 or more once the beep is there)
//   P  where the game's processor is now
//   J  1 = the processor has stopped (the game crashed)
//   S  loudest sample of the built-in test note (about 14CB when the sound chip works on this device)
//   L  longest time between two turns of the main loop since the Menu tap, in ms (the game can only
//      keep up while this stays below about 40)
//   K  pictures left out of the last 32 frames to keep the sound going (0 = the device keeps up)
void cGameModule::drawSoundInfo()
{
    char line[40];
    uint8_t* frame = backBuffer;

    memset(frame, kInfoBackground, 256 * (kInfoRows * kInfoRowHeight + 2));

    const unsigned flags = (nesAudio().isActive() ? 1u : 0u) | (nesAudio().isRunning() ? 2u : 0u);
    snprintf(line, sizeof(line), "B%04X F%04X A%X", static_cast<unsigned>(nesAudio().blocksRequested() & 0xFFFF),
             static_cast<unsigned>(nesAudio().fill() & 0xFFFF), flags);
    drawInfoText(frame, 2, 1, line);

    snprintf(line, sizeof(line), "C%03X E%04X D%02X", static_cast<unsigned>(lastFrameSamples & 0xFFF),
             static_cast<unsigned>(lastPeak & 0xFFFF), static_cast<unsigned>((lastFrameUs / 1000) & 0xFF));
    drawInfoText(frame, 2, 1 + kInfoRowHeight, line);

    const uint32_t sent = nesAudio().takeSentPeak();
    if (sent > sentPeakHold)
    {
        sentPeakHold = sent;
    }
    snprintf(line, sizeof(line), "M%04X W%04X T%04X", static_cast<unsigned>(maxPeak & 0xFFFF),
             static_cast<unsigned>(console->apu.writeCount & 0xFFFF), static_cast<unsigned>(sentPeakHold & 0xFFFF));
    drawInfoText(frame, 2, 1 + 2 * kInfoRowHeight, line);

    snprintf(line, sizeof(line), "P%04X J%X S%04X", static_cast<unsigned>(console->pc),
             console->jammed ? 1u : 0u, static_cast<unsigned>(selfTestPeak & 0xFFFF));
    drawInfoText(frame, 2, 1 + 3 * kInfoRowHeight, line);

    snprintf(line, sizeof(line), "L%02X K%02X", static_cast<unsigned>((maxGapUs / 1000) & 0xFF),
             static_cast<unsigned>(__builtin_popcount(skipHistory)));
    drawInfoText(frame, 2, 1 + 4 * kInfoRowHeight, line);
}

void cGameModule::update()
{
    if (state != kStateRunning)
    {
        return;
    }

    presentFrame();

    const uint32_t now = platform->getTimer()->micros();
    if (lastUpdateUs != 0 && now - lastUpdateUs > maxGapUs)
    {
        maxGapUs = now - lastUpdateUs;
    }
    lastUpdateUs = now;

    // The sound card takes samples at a fixed rate, so keeping a steady amount of sound waiting
    // paces the game exactly and keeps picture and sound together. If the audio engine has not asked
    // for sound lately, the timer sets the pace instead.
    const uint32_t blocks = nesAudio().blocksRequested();
    if (blocks != lastAudioBlocks)
    {
        lastAudioBlocks = blocks;
        lastAudioProgressUs = now;
    }
    const bool audioClock =
      nesAudio().isRunning() && static_cast<int32_t>(now - lastAudioProgressUs) < kAudioStallUs;

    if (audioClock)
    {
        if (nesAudio().fill() > kAudioTargetFill)
        {
            return; // enough sound waiting; not time for the next frame yet
        }
        nextFrameUs = now + frameMicros;
    }
    else
    {
        const int32_t late = static_cast<int32_t>(now - nextFrameUs);
        if (late < 0)
        {
            return; // not time for the next frame yet
        }

        // If the console cannot keep up, skip ahead instead of racing to catch up.
        if (late > static_cast<int32_t>(3 * frameMicros))
        {
            nextFrameUs = now;
        }
        nextFrameUs += frameMicros;
    }

    // at most two frames per turn, so the buttons and the screen are still looked after
    for (int run = 0; run < 2; run++)
    {
        const uint32_t fill = nesAudio().fill();
        const bool behind = audioClock && fill < kAudioLowFill;
        runOneFrame(behind && skippedInRow < kMaxSkippedPictures);
        if (!(audioClock && nesAudio().fill() < kAudioLowFill))
        {
            break; // caught up (or paced by the timer)
        }
    }

    presentFrame();
}

void cGameModule::runOneFrame(bool skipPicture)
{
    console->setPad(0, readPad());
    console->ppu.skipPixels = skipPicture;
    const uint32_t runStartUs = platform->getTimer()->micros();
    console->runFrame();
    lastFrameUs = platform->getTimer()->micros() - runStartUs;
    skipHistory = (skipHistory << 1) | (skipPicture ? 1u : 0u);
    if (skipPicture)
    {
        skippedInRow++;
    }
    else
    {
        skippedInRow = 0;
        frameReady = true;
    }
    collectSound();
    if (soundInfoFrames > 0)
    {
        soundInfoFrames--;
        if (!skipPicture)
        {
            drawSoundInfo();
        }
    }
}

#endif // TRACKER_MINI
