#ifdef TRACKER_MINI

#include "tradeMonitor.h"

#include "FT812.h"
#include "Platform.h"
#include "core/interface.h"
#include "core/songTimer.h"
#include "cr_section_macros.h"
#include "displayControls.h"
#include "tradeMonitorData.h"

#include <stdio.h>
#include <string.h>

cTradeMonitor tradeMonitor;

namespace {

// the page is kept in external RAM: the fast internal RAM is nearly full
__NOINIT(EXTERNAL_RAM) trademon::Page s_page;
trademon::Receiver s_receiver(&s_page);
bool s_pageReady = false;
bool s_openRequested = false;

void ensurePage()
{
    if (!s_pageReady)
    {
        memset(&s_page, 0, sizeof(s_page));
        s_receiver.reset();
        s_pageReady = true;
    }
}

uint32_t nowMs()
{
    return mtInterface.getPlatform()->getTimer()->millis();
}

// ---------------------------------------------------------------------------------------------
// Drawing. The display chip's built-in fonts 26-31 are used; each character costs about 4 bytes
// of the 8 KB the whole screen may use, so the text drawn is capped (the oldest console lines go
// first).
constexpr int kScreenW = 800;
constexpr int kScreenH = 480;
constexpr int kMargin = 10;
constexpr int kGlyphBudget = 1500;

constexpr uint8_t kSizeFont[trademon::kStyleSizes] = { 26, 27, 28, 29, 30, 31 };
constexpr uint8_t kSizeLine[trademon::kStyleSizes] = { 18, 22, 27, 31, 39, 52 };
// characters that fit across the screen at each size (rough averages of the fonts)
constexpr uint8_t kSizeMaxChars[trademon::kStyleSizes] = { 110, 86, 70, 60, 45, 32 };
constexpr uint32_t kColours[trademon::kStyleColours] = { 0xF2F4F6, 0x9AA4AE, 0x4CD964, 0xFF5A5A,
                                                          0xF5C542, 0x5AC8FA, 0xFF9F43 };
constexpr uint32_t kBackground = 0x15181C;
constexpr uint32_t kConsoleBackground = 0x0B0D10;
constexpr uint32_t kConsoleText = 0xD0D4D8;
constexpr uint8_t kConsoleFont = 26;
constexpr int kConsoleLine = 17;
constexpr int kStatusHeight = 20;
constexpr int kMinConsoleLines = 6;

uint8_t shrunk(uint8_t style, int steps)
{
    const int size = trademon::styleSize(style) - steps;
    return static_cast<uint8_t>(size > 0 ? size : 0);
}

class cTradeMonitorView : public cDisplayControl
{
  public:
    cTradeMonitorView(strControlProperties* properties = nullptr)
      : cDisplayControl(controlIdSimpleText, 1)
    {
        if (properties != nullptr)
        {
            style = properties->style;
        }
    }
    virtual ~cTradeMonitorView() {}

    virtual void update();
    virtual void setStyle(uint32_t style) { this->style = style; }
    virtual void setText(const char* text) { (void)text; }
    virtual void setValue(int value) { (void)value; }
    virtual void setColors(const uint32_t* colors) { (void)colors; }
    virtual void setData(void* data) { (void)data; }

  private:
    int glyphs = 0;
    void text(int x, int y, uint8_t font, uint16_t options, uint32_t colour, const char* s, int maxChars);
    void rect(int x0, int y0, int x1, int y1, uint32_t colour);
};

void cTradeMonitorView::rect(int x0, int y0, int x1, int y1, uint32_t colour)
{
    API_COLOR(colour);
    API_BEGIN(RECTS);
    API_VERTEX2F(x0, y0);
    API_VERTEX2F(x1, y1);
    API_END();
}

void cTradeMonitorView::text(int x, int y, uint8_t font, uint16_t options, uint32_t colour,
                             const char* s, int maxChars)
{
    char line[trademon::kLogChars + 1];
    int n = static_cast<int>(strlen(s));
    if (n > maxChars)
    {
        n = maxChars;
    }
    if (n <= 0 || glyphs + n > kGlyphBudget)
    {
        return;
    }
    memcpy(line, s, n);
    line[n] = 0;
    glyphs += n;
    API_COLOR(colour);
    API_CMD_TEXT(x, y, font, options, line);
}

void cTradeMonitorView::update()
{
    using namespace trademon;
    glyphs = 0;
    const uint32_t now = nowMs();

    rect(0, 0, kScreenW, kScreenH, kBackground);

    // status line at the bottom: what this is, how to leave, how fresh the data is
    const int statusY = kScreenH - kStatusHeight + 1;
    text(kMargin, statusY, 26, 0, kColours[1], "TRADE MONITOR  -  Menu: back", 40);
    char age[48];
    uint32_t ageColour = kColours[1];
    if (!s_page.received)
    {
        snprintf(age, sizeof(age), "waiting for data over USB");
    }
    else
    {
        const uint32_t seconds = (now - s_page.lastMessageMs) / 1000;
        if (seconds < 2)
        {
            snprintf(age, sizeof(age), "live");
            ageColour = kColours[2];
        }
        else if (seconds < 30)
        {
            snprintf(age, sizeof(age), "updated %u s ago", static_cast<unsigned>(seconds));
        }
        else
        {
            if (seconds < 3600)
                snprintf(age, sizeof(age), "NO DATA FOR %u s", static_cast<unsigned>(seconds));
            else
                snprintf(age, sizeof(age), "NO DATA FOR %u min", static_cast<unsigned>(seconds / 60));
            ageColour = kColours[3];
        }
    }
    text(kScreenW - kMargin, statusY, 26, OPT_RIGHTX, ageColour, age, 40);

    if (!s_page.received)
    {
        text(kScreenW / 2, 170, 30, OPT_CENTERX, kColours[0], "Waiting for data", 40);
        text(kScreenW / 2, 225, 27, OPT_CENTERX, kColours[1],
             "Connect the Tracker to the computer by USB and start", 70);
        text(kScreenW / 2, 250, 27, OPT_CENTERX, kColours[1],
             "the program that sends to it (tracker_monitor.py).", 70);
        return;
    }

    // Rows above the console from the top, rows below it stacked up from the status line, the
    // console in between. If that leaves the console fewer than kMinConsoleLines lines, all rows are
    // drawn one size smaller (twice at most).
    int shrink = 0;
    int topEnd = 0, bottomTop = 0, topShown = 0;
    for (; shrink <= 2; shrink++)
    {
        int bottomHeight = 0;
        for (int r = kTopRows; r < kRows; r++)
        {
            if (s_page.rows[r][0] != 0)
                bottomHeight += kSizeLine[shrunk(s_page.rowStyle[r], shrink)];
        }
        bottomTop = kScreenH - kStatusHeight - 4 - bottomHeight;
        topEnd = 6;
        topShown = 0;
        for (int r = 0; r < kTopRows; r++)
        {
            if (s_page.rows[r][0] == 0)
                continue;
            const int h = kSizeLine[shrunk(s_page.rowStyle[r], shrink)];
            if (topEnd + h > bottomTop)
                break;
            topEnd += h;
            topShown = r + 1;
        }
        const int consoleLines = (bottomTop - topEnd - 14) / kConsoleLine;
        if (s_page.logCount == 0 || consoleLines >= kMinConsoleLines || shrink == 2)
            break;
    }

    int y = 6;
    for (int r = 0; r < topShown; r++)
    {
        if (s_page.rows[r][0] == 0)
            continue;
        const uint8_t size = shrunk(s_page.rowStyle[r], shrink);
        text(kScreenW / 2, y, kSizeFont[size], OPT_CENTERX, kColours[styleColour(s_page.rowStyle[r])],
             s_page.rows[r], kSizeMaxChars[size]);
        y += kSizeLine[size];
    }

    int by = bottomTop;
    for (int r = kTopRows; r < kRows; r++)
    {
        if (s_page.rows[r][0] == 0)
            continue;
        const uint8_t size = shrunk(s_page.rowStyle[r], shrink);
        text(kScreenW / 2, by, kSizeFont[size], OPT_CENTERX, kColours[styleColour(s_page.rowStyle[r])],
             s_page.rows[r], kSizeMaxChars[size]);
        by += kSizeLine[size];
    }

    // the console fills the space between, newest line at the bottom
    const int consoleTop = y + 4;
    const int consoleBottom = bottomTop - 4;
    const int lines = (consoleBottom - consoleTop - 6) / kConsoleLine;
    if (s_page.logCount > 0 && lines > 0)
    {
        rect(kMargin, consoleTop, kScreenW - kMargin, consoleBottom, kConsoleBackground);
        const int shown = lines < s_page.logCount ? lines : s_page.logCount;
        for (int age = 0; age < shown; age++)
        {
            const char* line = s_page.logLine(age);
            const int ly = consoleBottom - 3 - (age + 1) * kConsoleLine;
            text(kMargin + 6, ly, kConsoleFont, 0, kConsoleText, line, kSizeMaxChars[0]);
        }
    }
}

void functMenu(uint8_t state)
{
    if (state == buttonPress)
    {
        tradeMonitor.quitRequested();
    }
}

} // namespace

// ---------------------------------------------------------------------------------------------
void tradeMonitorReceive(UsbMidi* usbMidi)
{
    ensurePage();
    uint8_t bytes[128];
    for (int guard = 0; guard < 64; guard++) // at most 8 KB per turn of the main loop
    {
        const uint32_t n = usbMidi->readSysexBytes(bytes, sizeof(bytes));
        if (n == 0)
        {
            break;
        }
        s_receiver.feed(bytes, n, nowMs());
    }
}

void tradeMonitorOpenRequest()
{
    s_openRequested = true;
}

bool tradeMonitorTakeOpenRequest()
{
    const bool requested = s_openRequested;
    s_openRequested = false;
    return requested;
}

void cTradeMonitor::initDisplayControls()
{
    ensurePage();
    strControlProperties prop;
    prop.x = 0;
    prop.y = 0;
    prop.w = kScreenW;
    prop.h = kScreenH;
    prop.style = controlStyleShow;
    view = display.createControl<cTradeMonitorView>(&prop);
}

void cTradeMonitor::destroyDisplayControls()
{
    // the interface destroys the screen's controls when it is left
}

void cTradeMonitor::start(uint32_t options)
{
    (void)options;
    moduleRefresh = 1;
    songTimer.hide();
    ip->setButtonObj(interfaceButtonMiniMenu, functMenu);
    shownChanges = s_page.changes - 1; // draw at once
    lastRefreshMs = 0;
}

void cTradeMonitor::stop()
{
    moduleRefresh = 0;
    view = -1; // destroyed with the other module controls
}

void cTradeMonitor::update()
{
    if (view < 0)
    {
        return;
    }
    const uint32_t now = nowMs();
    const uint32_t second = now / 1000; // the "updated ... ago" text changes every second
    if ((s_page.changes != shownChanges || second != shownSecond) && now - lastRefreshMs >= 100)
    {
        shownChanges = s_page.changes;
        shownSecond = second;
        lastRefreshMs = now;
        display.setControlShow(view);
        display.refreshControl(view);
    }
}

void cTradeMonitor::quitRequested()
{
    uint8_t button = interfaceButtonConfig;
    eventFunct(eventSwitchModule, this, &button, nullptr);
}

#endif // TRACKER_MINI
