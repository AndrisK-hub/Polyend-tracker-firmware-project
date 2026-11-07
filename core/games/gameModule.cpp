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

extern uint8_t externalRamScratchBuff[];
extern void setMiniNavButtonLock(bool enabled);

cGameModule gameModule;

namespace {

// ---------------------------------------------------------------------------------------------
// Button layout. The Mini has a D-pad, a middle key and four function keys:
//   arrows = D-pad, Fn1 = Select, Fn2 = Start, Fn3 = B, Fn4 = A, Middle = Start.
// Hold Menu to leave the game.
constexpr uint8_t kMapFn1 = nes::kBtnSelect;
constexpr uint8_t kMapFn2 = nes::kBtnStart;
constexpr uint8_t kMapFn3 = nes::kBtnB;
constexpr uint8_t kMapFn4 = nes::kBtnA;
constexpr uint8_t kMapMiddle = nes::kBtnStart;

// ---------------------------------------------------------------------------------------------
// Memory. The 768 KB scratch area in external RAM is free while a game runs:
//   [ Console | ROM image | cartridge RAM and mapper state (arena) ... | back buffer ]
constexpr uint32_t kScratchSize = SHARED_SCRATCH_MEMORY_SIZE;
constexpr uint32_t kFrameBytes = 256 * 240;
constexpr uint32_t kBackBufferOffset = kScratchSize - kFrameBytes;
constexpr uint32_t kMinArena = 16 * 1024; // the cartridge may still need much more; load() tells
constexpr uint32_t kReadChunk = 16 * 1024; // a multiple of 512, as the SD driver wants

constexpr uint32_t kFrameMicros = 16639; // NTSC: 60.0988 frames per second

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
    if (state == buttonHold)
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
    const uint32_t romMax = kBackBufferOffset - consoleBytes - kMinArena;
    if (fileSize > romMax)
    {
        showError("The game is too big for the memory (limit about 700 KB)");
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
    const uint32_t arenaBytes = static_cast<uint32_t>(backBuffer - arena);

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

    setMiniNavButtonLock(false);

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

void cGameModule::update()
{
    if (state != kStateRunning)
    {
        return;
    }

    presentFrame();

    const uint32_t now = platform->getTimer()->micros();
    const int32_t late = static_cast<int32_t>(now - nextFrameUs);
    if (late < 0)
    {
        return; // not time for the next frame yet
    }

    // If the console cannot keep up, skip ahead instead of racing to catch up.
    if (late > static_cast<int32_t>(3 * kFrameMicros))
    {
        nextFrameUs = now;
    }
    nextFrameUs += kFrameMicros;

    console->setPad(0, readPad());
    console->runFrame();
    frameReady = true;

    presentFrame();
}

#endif // TRACKER_MINI
