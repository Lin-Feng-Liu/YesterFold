#include "ui_render.h"
#include <cstdlib>

// ─── 全局句柄定义 ───
HANDLE g_hOut = nullptr;
HANDLE g_hIn  = nullptr;
ULONGLONG g_startTick = 0;

// ─── 基础工具实现 ───

std::wstring utf8_to_wstring(const std::string& utf8) {
    if (utf8.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
    if (len <= 0) return L"";
    std::wstring result(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), &result[0], len);
    return result;
}

std::string wstring_to_utf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return "";
    std::string result(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.size()), &result[0], len, nullptr, nullptr);
    return result;
}

int wcharWidth(wchar_t ch) {
    if (ch == L'\n' || ch == L'\r') return 0;
    if (ch < 0x20) return 0;
    if (ch >= 0x4E00 && ch <= 0x9FFF) return 2;
    if (ch >= 0x3000 && ch <= 0x303F) return 2;
    if (ch >= 0xFF00 && ch <= 0xFFEF) return 2;
    if (ch >= 0x2000 && ch <= 0x206F) return 2;
    if (ch == 0x2014) return 2;
    if (ch < 0x80) return 1;
    return 2;
}

void wprint(const std::wstring& s) {
    DWORD written;
    WriteConsoleW(g_hOut, s.c_str(), static_cast<DWORD>(s.size()), &written, nullptr);
}

void wprintln(const std::wstring& s) {
    wprint(s);
    wprint(L"\r\n");
}

void wprintln() {
    wprint(L"\r\n");
}

void clearScreen() { system("cls"); }

// ─── 颜色控制 ───

void setAmber()   { SetConsoleTextAttribute(g_hOut, AMBER); }
void setAmberDim() { SetConsoleTextAttribute(g_hOut, AMBER_DIM); }
void resetAttr()  { SetConsoleTextAttribute(g_hOut, ATTR_NORMAL); }

// ─── 坐标输出 ───

void writeAt(int x, int y, const std::wstring& text) {
    COORD pos = {static_cast<SHORT>(x), static_cast<SHORT>(y)};
    SetConsoleCursorPosition(g_hOut, pos);
    wprint(text);
}

void writeAtColor(int x, int y, const std::wstring& text, WORD attr) {
    SetConsoleTextAttribute(g_hOut, attr);
    writeAt(x, y, text);
}

void fillLine(int x, int y, int len, wchar_t ch, WORD attr) {
    COORD pos = {static_cast<SHORT>(x), static_cast<SHORT>(y)};
    DWORD written;
    FillConsoleOutputAttribute(g_hOut, attr, len, pos, &written);
    FillConsoleOutputCharacterW(g_hOut, ch, len, pos, &written);
}

void fillRegion(int x, int y, int w, int h, wchar_t ch, WORD attr) {
    for (int row = 0; row < h; ++row) {
        fillLine(x, y + row, w, ch, attr);
    }
}

// ─── 双线边框 ───

void drawDoubleBox(int x, int y, int w, int h) {
    if (w < 3 || h < 3) return;

    // 四角
    writeAtColor(x, y, L"\u2554", AMBER);           // ╔
    writeAtColor(x + w - 1, y, L"\u2557", AMBER);   // ╗

    // 顶边
    fillLine(x + 1, y, w - 2, L'\u2550', AMBER);    // ═

    // 左右边
    for (int row = 1; row < h - 1; ++row) {
        writeAtColor(x, y + row, L"\u2551", AMBER);       // ║
        writeAtColor(x + w - 1, y + row, L"\u2551", AMBER);
    }

    // 底边
    writeAtColor(x, y + h - 1, L"\u255A", AMBER);         // ╚
    writeAtColor(x + w - 1, y + h - 1, L"\u255D", AMBER); // ╝
    fillLine(x + 1, y + h - 1, w - 2, L'\u2550', AMBER);
}

// ─── 单线边框 ───

void drawSingleBox(int x, int y, int w, int h) {
    if (w < 3 || h < 3) return;

    writeAtColor(x, y, L"\u250C", AMBER);             // ┌
    writeAtColor(x + w - 1, y, L"\u2510", AMBER);     // ┐
    fillLine(x + 1, y, w - 2, L'\u2500', AMBER);      // ─

    for (int row = 1; row < h - 1; ++row) {
        writeAtColor(x, y + row, L"\u2502", AMBER);         // │
        writeAtColor(x + w - 1, y + row, L"\u2502", AMBER);
    }

    writeAtColor(x, y + h - 1, L"\u2514", AMBER);           // └
    writeAtColor(x + w - 1, y + h - 1, L"\u2518", AMBER);   // ┘
    fillLine(x + 1, y + h - 1, w - 2, L'\u2500', AMBER);
}

// ─── 进度条 ───

void drawProgressBar(int x, int y, int barWidth, double percent) {
    int filled = static_cast<int>(barWidth * percent / 100.0);
    if (filled < 0) filled = 0;
    if (filled > barWidth) filled = barWidth;

    if (filled > 0) {
        COORD pos = {static_cast<SHORT>(x), static_cast<SHORT>(y)};
        DWORD written;
        std::wstring fill(filled, L'\u2593'); // ▓
        SetConsoleTextAttribute(g_hOut, AMBER);
        WriteConsoleOutputCharacterW(g_hOut, fill.c_str(), filled, pos, &written);
        FillConsoleOutputAttribute(g_hOut, AMBER, filled, pos, &written);
    }
    if (barWidth - filled > 0) {
        COORD pos = {static_cast<SHORT>(x + filled), static_cast<SHORT>(y)};
        DWORD written;
        int unfilled = barWidth - filled;
        std::wstring ufill(unfilled, L'\u2591'); // ░
        SetConsoleTextAttribute(g_hOut, AMBER_DIM);
        WriteConsoleOutputCharacterW(g_hOut, ufill.c_str(), unfilled, pos, &written);
        FillConsoleOutputAttribute(g_hOut, AMBER_DIM, unfilled, pos, &written);
    }
}

// ─── DIARY ASCII Art 大字 ───

void drawDiaryTitle(int x, int y) {
    const wchar_t* lines[] = {
        L"  \u2588\u2588\u2588\u2588\u2588\u2588\u2557  \u2588\u2588\u2557  \u2588\u2588\u2588\u2588\u2588\u2557  \u2588\u2588\u2588\u2588\u2588\u2588\u2557  \u2588\u2588\u2557   \u2588\u2588\u2557",
        L"  \u2588\u2588\u2554\u2550\u2550\u2588\u2588\u2557 \u2588\u2588\u2551 \u2588\u2588\u2554\u2550\u2550\u2588\u2588\u2557 \u2588\u2588\u2554\u2550\u2550\u2588\u2588\u2557 \u255A\u2588\u2588\u2557 \u2588\u2588\u2554\u255D",
        L"  \u2588\u2588\u2551  \u2588\u2588\u2551 \u2588\u2588\u2551 \u2588\u2588\u2588\u2588\u2588\u2588\u2588\u2551 \u2588\u2588\u2588\u2588\u2588\u2588\u2554\u255D  \u255A\u2588\u2588\u2588\u2588\u2554\u255D",
        L"  \u2588\u2588\u2551  \u2588\u2588\u2551 \u2588\u2588\u2551 \u2588\u2588\u2554\u2550\u2550\u2588\u2588\u2551 \u2588\u2588\u2554\u2550\u2550\u2588\u2588\u2557   \u255A\u2588\u2588\u2554\u255D",
        L"  \u2588\u2588\u2588\u2588\u2588\u2588\u2554\u255D \u2588\u2588\u2551 \u2588\u2588\u2551  \u2588\u2588\u2551 \u2588\u2588\u2551  \u2588\u2588\u2551    \u2588\u2588\u2551",
        L"  \u255A\u2550\u2550\u2550\u2550\u2550\u255D  \u255A\u2550\u255D \u255A\u2550\u255D  \u255A\u2550\u255D \u255A\u2550\u255D  \u255A\u2550\u255D    \u255A\u2550\u255D",
    };
    setAmber();
    for (int i = 0; i < 6; ++i) {
        writeAt(x, y + i, lines[i]);
    }
    resetAttr();
}

// ─── 热力图 cell ───

void drawHeatmapCell(int x, int y, int level) {
    const wchar_t chars[] = {L'\u00B7', L'\u2591', L'\u2592', L'\u2593', L'\u2588'};
    // · ░ ▒ ▓ █
    if (level < 0) level = 0;
    if (level > 4) level = 4;

    if (level == 0) {
        writeAtColor(x, y, std::wstring(1, chars[0]), AMBER_DIM);
    } else {
        writeAtColor(x, y, std::wstring(1, chars[level]), AMBER);
    }
}

// ─── 全屏方向键菜单 ───

int menuSelect(const std::vector<MenuItem>& items, int startIdx) {
    std::vector<int> si;
    for (int i = 0; i < (int)items.size(); i++)
        if (items[i].selectable) si.push_back(i);
    if (si.empty()) return -1;

    int selPos = 0;
    for (int i = 0; i < (int)si.size(); i++) {
        if (si[i] == startIdx) { selPos = i; break; }
    }

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(g_hOut, &csbi);
    DWORD bufSize = csbi.dwSize.X * csbi.dwSize.Y;
    SHORT screenW = csbi.dwSize.X;

    int prevSelIdx = -1;

    auto clearLineW = [&](int lineIdx) {
        COORD pos = {0, (SHORT)lineIdx}; DWORD written;
        FillConsoleOutputCharacterW(g_hOut, L' ', screenW, pos, &written);
        FillConsoleOutputAttribute(g_hOut, csbi.wAttributes, screenW, pos, &written);
        SetConsoleCursorPosition(g_hOut, pos);
    };

    auto drawLineW = [&](int itemIdx, bool highlight) {
        clearLineW(itemIdx);
        if (highlight) {
            SetConsoleTextAttribute(g_hOut, 0x70);
            wprint(L"> "); wprint(items[itemIdx].text);
            SetConsoleTextAttribute(g_hOut, 0x07);
        } else {
            wprint(L"  "); wprint(items[itemIdx].text);
        }
    };

    auto fullRenderW = [&]() {
        COORD pos = {0, 0}; DWORD written;
        FillConsoleOutputCharacterW(g_hOut, L' ', bufSize, pos, &written);
        FillConsoleOutputAttribute(g_hOut, csbi.wAttributes, bufSize, pos, &written);
        SetConsoleCursorPosition(g_hOut, pos);
        for (int i = 0; i < (int)items.size(); i++) {
            drawLineW(i, i == si[selPos]);
            wprintln();
        }
        prevSelIdx = si[selPos];
    };

    fullRenderW();
    while (true) {
        int ch = _getch();
        if (ch == 0xE0 || ch == 0) {
            int key = _getch();
            int oldIdx = (prevSelIdx >= 0) ? prevSelIdx : -1;
            if (key == 0x48) {
                selPos = (selPos - 1 + (int)si.size()) % (int)si.size();
                if (oldIdx >= 0) drawLineW(oldIdx, false);
                drawLineW(si[selPos], true);
                prevSelIdx = si[selPos];
            } else if (key == 0x50) {
                selPos = (selPos + 1) % (int)si.size();
                if (oldIdx >= 0) drawLineW(oldIdx, false);
                drawLineW(si[selPos], true);
                prevSelIdx = si[selPos];
            }
        } else if (ch == '\r') {
            return si[selPos];
        } else if (ch == 27) {
            return MENU_ESC;
        } else if (ch >= '0' && ch <= '9') {
            for (int i = 0; i < (int)items.size(); i++) {
                if (!items[i].selectable) continue;
                std::wstring t = items[i].text;
                size_t pos = t.find_first_not_of(L" ");
                if (pos != std::wstring::npos && t[pos] == (wchar_t)ch &&
                    pos + 1 < t.size() && (t[pos+1] == L'.' || t[pos+1] == L' ')) {
                    for (int j = 0; j < (int)si.size(); j++) {
                        if (si[j] == i) { selPos = j; break; }
                    }
                    return si[selPos];
                }
            }
        }
    }
}

// ─── 区域方向键菜单 ───

int menuSelectInRegion(int x, int y, int w, int h,
                       const std::vector<MenuItem>& items, int startIdx) {
    std::vector<int> si;
    for (int i = 0; i < static_cast<int>(items.size()); ++i)
        if (items[i].selectable) si.push_back(i);
    if (si.empty()) return -1;

    int selPos = 0;
    for (int i = 0; i < static_cast<int>(si.size()); ++i) {
        if (si[i] == startIdx) { selPos = i; break; }
    }

    int prevSelIdx = -1;
    int visibleCount = (int)items.size();
    if (visibleCount > h) visibleCount = h;

    auto drawItemLine = [&](int itemIdx, bool highlight) {
        int screenY = y + itemIdx;
        if (screenY >= y + h) return;
        COORD pos = {static_cast<SHORT>(x), static_cast<SHORT>(screenY)};
        SetConsoleCursorPosition(g_hOut, pos);
        if (highlight) {
            SetConsoleTextAttribute(g_hOut, 0x70); // 白底黑字
            wprint(L"> " + items[itemIdx].text);
            SetConsoleTextAttribute(g_hOut, AMBER);
        } else {
            SetConsoleTextAttribute(g_hOut, AMBER);
            wprint(L"  " + items[itemIdx].text);
        }
    };

    // 初始全渲染
    for (int i = 0; i < visibleCount; ++i)
        drawItemLine(i, i == si[selPos]);
    prevSelIdx = si[selPos];

    // 输入循环
    while (true) {
        int ch = _getch();
        if (ch == 0xE0 || ch == 0) {
            int key = _getch();
            int oldIdx = (prevSelIdx >= 0) ? prevSelIdx : -1;
            int oldPos = selPos;
            if (key == 0x48) {       // Up
                selPos = (selPos - 1 + static_cast<int>(si.size())) % static_cast<int>(si.size());
            } else if (key == 0x50) { // Down
                selPos = (selPos + 1) % static_cast<int>(si.size());
            }
            if (selPos != oldPos) {
                if (oldIdx >= 0) drawItemLine(oldIdx, false);
                drawItemLine(si[selPos], true);
                prevSelIdx = si[selPos];
            }
        } else if (ch == '\r') {
            SetConsoleTextAttribute(g_hOut, ATTR_NORMAL);
            return si[selPos];
        } else if (ch == 27) {
            SetConsoleTextAttribute(g_hOut, ATTR_NORMAL);
            return MENU_ESC;
        } else if (ch >= '0' && ch <= '9') {
            for (int i = 0; i < static_cast<int>(items.size()); ++i) {
                if (!items[i].selectable) continue;
                std::wstring t = items[i].text;
                size_t pos = t.find_first_not_of(L" ");
                if (pos != std::wstring::npos && t[pos] == static_cast<wchar_t>(ch) &&
                    pos + 1 < t.size() && (t[pos + 1] == L'.' || t[pos + 1] == L' ')) {
                    for (int j = 0; j < static_cast<int>(si.size()); ++j) {
                        if (si[j] == i) { selPos = j; break; }
                    }
                    SetConsoleTextAttribute(g_hOut, ATTR_NORMAL);
                    return si[selPos];
                }
            }
        }
    }
}
