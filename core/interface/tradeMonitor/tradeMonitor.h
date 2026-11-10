// The "Trade monitor" screen of the Tracker Mini (Config > Trade monitor): shows a page of text
// that a program on the computer sends over USB MIDI (see tradeMonitorData.h for the format and
// tools/tracker_monitor.py for the sending side). The page is received all the time, so it is
// up to date as soon as the screen is opened.
#ifndef INTERFACE_TRADEMONITOR_TRADEMONITOR_H_
#define INTERFACE_TRADEMONITOR_TRADEMONITOR_H_

#ifdef TRACKER_MINI

#include "core/modulesBase.h"

class UsbMidi;

class cTradeMonitor : public ModuleBase
{
  public:
    virtual void update();
    virtual void start(uint32_t options);
    virtual void stop();

    virtual void initDisplayControls();
    virtual void destroyDisplayControls();

    cTradeMonitor() {}
    ~cTradeMonitor() {}

    void quitRequested();

  private:
    hControl view = -1;
    uint32_t shownChanges = 0;
    uint32_t shownSecond = 0;
    uint32_t lastRefreshMs = 0;
};

extern cTradeMonitor tradeMonitor;

// Reads the SysEx bytes the USB MIDI port has collected into the page; called from the MIDI loop.
void tradeMonitorReceive(UsbMidi* usbMidi);

// Config > Trade monitor > Open: asks the Config screen to switch over on its next update.
void tradeMonitorOpenRequest();
bool tradeMonitorTakeOpenRequest();

#endif // TRACKER_MINI

#endif /* INTERFACE_TRADEMONITOR_TRADEMONITOR_H_ */
