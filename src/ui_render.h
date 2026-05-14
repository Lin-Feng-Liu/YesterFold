#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <conio.h>

// ─── 全局控制台句柄 ───
extern HANDLE g_hOut;
extern HANDLE g_hIn;
extern ULONGLONG g_startTick;

// ─── 黑白终端配色方案 ───
constexpr WORD AMBER      = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY; // 0x0F 亮白
constexpr WORD AMBER_DIM  = FOREGROUND_INTENSITY;                                                     // 0x08 暗灰
constexpr WORD AMBER_BG   = BACKGROUND_RED | BACKGROUND_GREEN;                                       // 保留(未使用)
constexpr WORD ATTR_NORMAL = 0x07;   // 默认灰白

// ─── 菜单返回码 ───
constexpr int MENU_ESC = -2;
constexpr int MENU_RESIZE = -3;

constexpr int FIXED_SHELL_W = 118;
constexpr int FIXED_SHELL_H = 31;

struct CenteredRect {
    int x;
    int y;
    int w;
    int h;
};

struct ConsoleViewport {
    int x;
    int y;
    int w;
    int h;
};

struct MenuItem {
    std::wstring text;
    bool selectable;
};

struct FooterHintRegion {
    int x;
    int y;
    int w;
};

struct InputResult {
    bool cancelled;
    std::string value;
    bool resized;
};

struct RenderedLine {
    std::wstring text;
    WORD attr = ATTR_NORMAL;
};

// ─── 基础工具 ───
std::wstring utf8_to_wstring(const std::string& utf8);
std::string wstring_to_utf8(const std::wstring& wstr);
int wcharWidth(wchar_t ch);
void wprint(const std::wstring& s);
void wprintln(const std::wstring& s);
void wprintln();
void clearScreen();

// ─── 颜色控制 ───
void setAmber();
void setAmberDim();
void resetAttr();

// ─── 坐标输出 ───
void writeAt(int x, int y, const std::wstring& text);
void writeAtColor(int x, int y, const std::wstring& text, WORD attr);
void fillLine(int x, int y, int len, wchar_t ch, WORD attr);
void fillRegion(int x, int y, int w, int h, wchar_t ch, WORD attr);
std::wstring fitTextToWidth(const std::wstring& text, int maxWidth);
std::vector<std::wstring> wrapDisplayText(const std::wstring& text, int maxWidth);
std::vector<std::wstring> splitDisplayLines(const std::wstring& text, const std::wstring& prefix = L"");
std::wstring padOrTrimText(const std::wstring& text, int width);
int getAdaptiveWrapWidth(int availableWidth);

// ─── 渲染组件 ───
void drawDoubleBox(int x, int y, int w, int h);
void drawSingleBox(int x, int y, int w, int h);
void drawProgressBar(int x, int y, int barWidth, double percent);
void drawDiaryTitle(int x, int y);
void drawLargeDigits(int x, int y, const std::wstring& text,
                     WORD attr = AMBER, WORD shadowAttr = AMBER_DIM);
int measureLargeDigits(const std::wstring& text);
int largeDigitsHeight();
void drawHeatmapCell(int x, int y, int level);
ConsoleViewport getConsoleViewport();
CenteredRect getCenteredRect(int screenW, int screenH, int desiredW, int desiredH, int minW = 88, int minH = 12);
CenteredRect drawTerminalShell(const std::wstring& envLabel, bool fixedCentered = false);
void writeWrappedPanelLines(int x, int y, int w, int h,
                            const std::vector<std::wstring>& logicalLines,
                            WORD attr = AMBER);
bool waitForConsoleInputOrResize(HANDLE hIn, int& outScreenW, int& outScreenH, DWORD timeoutMs = 80, DWORD settleMs = 160);

// ─── 固定壳子页底栏提示 ───
void setFooterHintRegion(int x, int y, int w);
void resetFooterHintRegion();
void clearFooterHintLine();
void writeFooterHint(const std::wstring& text, WORD attr = AMBER);
void pauseScreen();

// ─── 区域方向键菜单 ───
int menuSelect(const std::vector<MenuItem>& items, int startIdx = 0);
int menuSelectInRegion(int x, int y, int w, int h,
                       const std::vector<MenuItem>& items, int startIdx = 0);
int menuSelectScrollableInRegion(int x, int y, int w, int h,
                                 const std::vector<MenuItem>& items, int startIdx = 0);
