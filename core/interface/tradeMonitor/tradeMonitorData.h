// Trade monitor: the page of text a program on the computer sends to the Tracker over USB MIDI,
// and the receiver that builds it from SysEx messages. No display or platform code in here, so it
// can be tested on its own.
//
// Message format (all bytes after F0 are 7-bit):
//   F0 7D 54 4D <command> <data...> F7          7D = non-commercial ID, 54 4D = "TM"
//   01 <row> <style> <text...>   set row 0-23 (0-15 above the console, 16-23 below it); empty text
//                                hides the row
//   02 <text...>                 add a line to the console
//   03                           clear everything
//   04                           heartbeat (only says "the program is still running")
//   05                           clear the console
// style = size (bits 0-2: 0 small, 1 normal, 2 bold, 3 large, 4 title, 5 big)
//       + colour << 3 (0 white, 1 grey, 2 green, 3 red, 4 yellow, 5 cyan, 6 orange)
// Text is printable ASCII; anything else is shown as '?'. Longer text than fits is cut off.
#ifndef INTERFACE_TRADEMONITOR_TRADEMONITORDATA_H_
#define INTERFACE_TRADEMONITOR_TRADEMONITORDATA_H_

#include <stdint.h>

namespace trademon {

constexpr int kTopRows = 16;
constexpr int kBottomRows = 8;
constexpr int kRows = kTopRows + kBottomRows;
constexpr int kRowChars = 80;   // characters kept per row
constexpr int kLogLines = 16;   // console lines kept
constexpr int kLogChars = 120;  // characters kept per console line
constexpr int kStyleSizes = 6;
constexpr int kStyleColours = 7;

enum Command : uint8_t
{
    kCmdSetRow = 0x01,
    kCmdLog = 0x02,
    kCmdClear = 0x03,
    kCmdHeartbeat = 0x04,
    kCmdClearLog = 0x05,
};

struct Page
{
    char rows[kRows][kRowChars + 1];
    uint8_t rowStyle[kRows];
    char log[kLogLines][kLogChars + 1];
    uint8_t logNext;     // where the next console line goes
    uint8_t logCount;    // console lines kept (up to kLogLines)
    uint32_t changes;    // counts up whenever something shown changes
    uint32_t lastMessageMs;
    bool received;       // a valid message has arrived since power-on

    void clear();
    // console line 'age' lines back from the newest (0 = newest), or nullptr
    const char* logLine(int age) const;
};

class Receiver
{
  public:
    explicit Receiver(Page* page) : page(page) {}
    void reset();
    // bytes as they come from the USB SysEx queue (F0 ... F7, possibly split anywhere)
    void feed(const uint8_t* bytes, uint32_t count, uint32_t nowMs);

    uint32_t messages = 0;  // valid messages handled
    uint32_t rejected = 0;  // complete messages that were not for the monitor or were broken

  private:
    static constexpr int kMaxMessage = 4 + 2 + kLogChars + 8; // header, row and style, text
    void handle(uint32_t nowMs);

    Page* page;
    uint8_t buf[kMaxMessage];
    uint16_t length = 0;
    bool inMessage = false;
};

// helpers shared with the screen
inline uint8_t styleSize(uint8_t style)
{
    const uint8_t s = style & 7;
    return s < kStyleSizes ? s : 1;
}
inline uint8_t styleColour(uint8_t style)
{
    const uint8_t c = (style >> 3) & 7;
    return c < kStyleColours ? c : 0;
}

} // namespace trademon

#endif /* INTERFACE_TRADEMONITOR_TRADEMONITORDATA_H_ */
