// The "Games" screen of the Tracker Mini: loads a .nes file from the SD card, runs the emulator
// and shows its picture through the display's video stream.
#ifndef GAMES_GAMEMODULE_H_
#define GAMES_GAMEMODULE_H_

#ifdef TRACKER_MINI

#include "core/modulesBase.h"

namespace nes {
class Console;
}

class cGameModule : public ModuleBase
{
  public:
    virtual void update();
    virtual void start(uint32_t options);
    virtual void stop();

    virtual void initDisplayControls();
    virtual void destroyDisplayControls() {}

    cGameModule() {}
    ~cGameModule() {}

    // called by the buttons while a game is on screen
    void quitRequested();
    // Menu tap while a game runs: plays a short test beep through the game's sound path and shows
    // a few numbers about the sound on the picture for a few seconds
    void soundTest();

  private:
    enum State : uint8_t
    {
        kStateIdle = 0,
        kStateRunning,
        kStateError,
    };

    void showError(const char* reason);
    bool loadRom(const char* name);
    void presentFrame();
    void collectSound();
    void drawSoundInfo();

    State state = kStateIdle;
    nes::Console* console = nullptr;
    uint8_t* backBuffer = nullptr;
    bool frameReady = false;
    uint32_t nextFrameUs = 0;
    uint32_t frameMicros = 0;
    uint32_t lastAudioBlocks = 0;
    uint32_t lastAudioProgressUs = 0;

    // sound test and the numbers it shows
    uint32_t beepLeft = 0;          // samples of test beep still to add to the game's sound
    uint32_t beepPhase = 0;
    uint16_t soundInfoFrames = 0;   // frames the numbers stay on the picture
    uint32_t lastFrameSamples = 0;  // sound samples the last frame made
    uint32_t lastPeak = 0;          // loudest of them (before the beep)
    uint32_t lastFrameUs = 0;       // time the emulator needed for the last frame
    uint32_t maxPeak = 0;           // loudest sound the chip has made since the start
    uint32_t sentPeakHold = 0;      // loudest sample sent to the mixer since the last Menu tap
    uint32_t selfTestPeak = 0;      // result of the built-in test note

    hControl titleText = -1;
    hControl detailText = -1;
    hControl hintText = -1;
};

extern cGameModule gameModule;

// Chooses the game (the name as listed in the Games folder, without ".nes") that start() loads.
void gameModuleSetFile(const char* name);

#endif // TRACKER_MINI

#endif /* GAMES_GAMEMODULE_H_ */
