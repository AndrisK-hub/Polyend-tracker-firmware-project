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

    State state = kStateIdle;
    nes::Console* console = nullptr;
    uint8_t* backBuffer = nullptr;
    bool frameReady = false;
    uint32_t nextFrameUs = 0;
    uint32_t frameMicros = 0;
    uint32_t lastAudioBlocks = 0;
    uint32_t lastAudioProgressUs = 0;

    hControl titleText = -1;
    hControl detailText = -1;
    hControl hintText = -1;
};

extern cGameModule gameModule;

// Chooses the game (the name as listed in the Games folder, without ".nes") that start() loads.
void gameModuleSetFile(const char* name);

#endif // TRACKER_MINI

#endif /* GAMES_GAMEMODULE_H_ */
