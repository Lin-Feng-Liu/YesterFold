#include "ui_render.h"
#include <cstdlib>
#include <algorithm>

// ─── 全局句柄定义 ───
HANDLE g_hOut = nullptr;
HANDLE g_hIn  = nullptr;
ULONGLONG g_startTick = 0;

static FooterHintRegion g_footerHintRegion{1, 0, 0};

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
    if (ch == 0x2018 || ch == 0x2019 || ch == 0x201C || ch == 0x201D) return 1;
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

std::wstring fitTextToWidth(const std::wstring& text, int maxWidth) {
    if (maxWidth <= 0) return L"";
    int width = 0;
    std::wstring out;
    for (wchar_t ch : text) {
        int cw = wcharWidth(ch);
        if (cw <= 0) continue;
        if (width + cw > maxWidth) break;
        out.push_back(ch);
        width += cw;
    }
    return out;
}

std::vector<std::wstring> wrapDisplayText(const std::wstring& text, int maxWidth) {
    std::vector<std::wstring> lines;
    if (maxWidth <= 0) {
        lines.push_back(L"");
        return lines;
    }

    std::wstring current;
    int width = 0;
    for (wchar_t ch : text) {
        if (ch == L'\r') continue;
        if (ch == L'\n') {
            lines.push_back(current);
            current.clear();
            width = 0;
            continue;
        }

        int cw = wcharWidth(ch);
        if (cw <= 0) continue;
        if (width + cw > maxWidth && !current.empty()) {
            lines.push_back(current);
            current.clear();
            width = 0;
        }
        current.push_back(ch);
        width += cw;
    }

    if (!current.empty() || lines.empty()) lines.push_back(current);
    return lines;
}

std::vector<std::wstring> splitDisplayLines(const std::wstring& text, const std::wstring& prefix) {
    std::vector<std::wstring> lines;
    std::wstring current = prefix;
    bool touched = false;

    for (wchar_t ch : text) {
        if (ch == L'\r') continue;
        if (ch == L'\n') {
            lines.push_back(current);
            current = prefix;
            touched = true;
            continue;
        }
        current.push_back(ch);
        touched = true;
    }

    if (touched || !prefix.empty()) lines.push_back(current);
    return lines;
}

std::wstring padOrTrimText(const std::wstring& text, int width) {
    if (width <= 0) return L"";
    std::wstring fitted = fitTextToWidth(text, width);
    int used = 0;
    for (wchar_t ch : fitted) used += wcharWidth(ch);
    if (used < width) fitted.append(width - used, L' ');
    return fitted;
}

int getAdaptiveWrapWidth(int availableWidth) {
    if (availableWidth <= 0) return 0;
    if (availableWidth <= 12) return availableWidth;
    return availableWidth - ((availableWidth >= 36) ? 2 : 1);
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

ConsoleViewport getConsoleViewport() {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(g_hOut, &csbi);

    ConsoleViewport view{};
    view.x = csbi.srWindow.Left;
    view.y = csbi.srWindow.Top;
    view.w = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    view.h = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    if (view.w <= 0) view.w = csbi.dwSize.X;
    if (view.h <= 0) view.h = csbi.dwSize.Y;
    return view;
}

CenteredRect getCenteredRect(int screenW, int screenH, int desiredW, int desiredH, int minW, int minH) {
    CenteredRect rect{};
    rect.w = desiredW;
    rect.h = desiredH;

    if (rect.w > screenW) rect.w = screenW;
    if (rect.h > screenH) rect.h = screenH;
    if (rect.w < minW) rect.w = (screenW < minW) ? screenW : minW;
    if (rect.h < minH) rect.h = (screenH < minH) ? screenH : minH;

    rect.x = (screenW - rect.w) / 2;
    rect.y = (screenH - rect.h) / 2;
    if (rect.x < 0) rect.x = 0;
    if (rect.y < 0) rect.y = 0;
    return rect;
}

CenteredRect drawTerminalShell(const std::wstring& envLabel, bool fixedCentered) {
    ConsoleViewport view = getConsoleViewport();
    int screenW = view.w;
    int screenH = view.h;
    int boxW = screenW;
    int boxH = screenH;
    int boxX = view.x;
    int boxY = view.y;

    if (fixedCentered) {
        CenteredRect rect = getCenteredRect(screenW, screenH, FIXED_SHELL_W, FIXED_SHELL_H, 88, 24);
        boxX = view.x + rect.x;
        boxY = view.y + rect.y;
        boxW = rect.w;
        boxH = rect.h;
    } else if (boxW < 88) {
        boxW = 88;
    }

    clearScreen();
    fillRegion(view.x, view.y, screenW, screenH, L' ', ATTR_NORMAL);
    drawDoubleBox(boxX, boxY, boxW, boxH);
    writeAtColor(boxX + 2, boxY + 1, L"[>_ SYS.TERMINAL // " + envLabel, AMBER);
    writeAtColor(boxX + boxW - 15, boxY + 1, L"[■][O][X] ", AMBER_DIM);
    writeAtColor(boxX, boxY + 2, L"╠", AMBER);
    fillLine(boxX + 1, boxY + 2, boxW - 2, L'═', AMBER);
    writeAtColor(boxX + boxW - 1, boxY + 2, L"╣", AMBER);

    return {boxX, boxY, boxW, boxH};
}

void writeWrappedPanelLines(int x, int y, int w, int h,
                            const std::vector<std::wstring>& logicalLines,
                            WORD attr) {
    if (w <= 0 || h <= 0) return;

    std::vector<std::wstring> wrappedLines;
    int wrapW = getAdaptiveWrapWidth(w);
    if (wrapW <= 0) wrapW = w;

    for (const auto& raw : logicalLines) {
        if (raw.empty()) {
            wrappedLines.push_back(L"");
            continue;
        }
        std::vector<std::wstring> wrapped = wrapDisplayText(raw, wrapW);
        if (wrapped.empty()) wrapped.push_back(L"");
        wrappedLines.insert(wrappedLines.end(), wrapped.begin(), wrapped.end());
    }

    for (int row = 0; row < h; ++row) {
        fillLine(x, y + row, w, L' ', ATTR_NORMAL);
        if (row < static_cast<int>(wrappedLines.size())) {
            writeAtColor(x, y + row, padOrTrimText(wrappedLines[row], w), attr);
        }
    }
}

static FooterHintRegion getDefaultFooterHintRegion() {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(g_hOut, &csbi);
    return {1, std::max(1, static_cast<int>(csbi.dwSize.Y) - 2), csbi.dwSize.X - 2};
}

void setFooterHintRegion(int x, int y, int w) {
    g_footerHintRegion = {x, y, w};
}

void resetFooterHintRegion() {
    g_footerHintRegion = getDefaultFooterHintRegion();
}

void clearFooterHintLine() {
    FooterHintRegion region = g_footerHintRegion;
    if (region.w <= 0) region = getDefaultFooterHintRegion();
    fillLine(region.x, region.y, region.w, L' ', ATTR_NORMAL);
}

void writeFooterHint(const std::wstring& text, WORD attr) {
    FooterHintRegion region = g_footerHintRegion;
    if (region.w <= 0) region = getDefaultFooterHintRegion();
    fillLine(region.x, region.y, region.w, L' ', ATTR_NORMAL);
    writeAtColor(region.x + 2, region.y, fitTextToWidth(text, std::max(1, region.w - 4)), attr);
}

void pauseScreen() {
    FlushConsoleInputBuffer(g_hIn);
    writeFooterHint(L"按任意键继续...", AMBER);
    DWORD oldMode;
    GetConsoleMode(g_hIn, &oldMode);
    SetConsoleMode(g_hIn, ENABLE_EXTENDED_FLAGS | ENABLE_WINDOW_INPUT);
    while (true) {
        INPUT_RECORD ir;
        DWORD read;
        if (!ReadConsoleInputW(g_hIn, &ir, 1, &read) || read == 0) continue;
        if (ir.EventType == KEY_EVENT && ir.Event.KeyEvent.bKeyDown) break;
    }
    SetConsoleMode(g_hIn, oldMode);
    clearFooterHintLine();
}

bool waitForConsoleInputOrResize(HANDLE hIn, int& outScreenW, int& outScreenH, DWORD timeoutMs, DWORD settleMs) {
    DWORD waitResult = WaitForSingleObject(hIn, timeoutMs);
    if (waitResult == WAIT_OBJECT_0) {
        INPUT_RECORD peek{};
        DWORD peeked = 0;
        if (PeekConsoleInputW(hIn, &peek, 1, &peeked) && peeked > 0 && peek.EventType == WINDOW_BUFFER_SIZE_EVENT) {
            INPUT_RECORD consumed{};
            DWORD read = 0;
            do {
                if (!ReadConsoleInputW(hIn, &consumed, 1, &read) || read == 0) break;
                peeked = 0;
            } while (PeekConsoleInputW(hIn, &peek, 1, &peeked) && peeked > 0 && peek.EventType == WINDOW_BUFFER_SIZE_EVENT);

            ConsoleViewport current = getConsoleViewport();
            outScreenW = current.w;
            outScreenH = current.h;
            return false;
        }
        return true;
    }

    ConsoleViewport current = getConsoleViewport();
    int currentW = current.w;
    int currentH = current.h;
    if (currentW != outScreenW || currentH != outScreenH) {
        outScreenW = currentW;
        outScreenH = currentH;
        return false;
    }

    return true;
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
    int watchW = csbi.dwSize.X;
    int watchH = csbi.dwSize.Y;

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
        if (!waitForConsoleInputOrResize(g_hIn, watchW, watchH)) {
            return MENU_RESIZE;
        }
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
    ConsoleViewport view = getConsoleViewport();
    int watchW = view.w;
    int watchH = view.h;

    CONSOLE_CURSOR_INFO oldCursorInfo{};
    GetConsoleCursorInfo(g_hOut, &oldCursorInfo);
    CONSOLE_CURSOR_INFO hiddenCursor = oldCursorInfo;
    hiddenCursor.bVisible = FALSE;
    SetConsoleCursorInfo(g_hOut, &hiddenCursor);

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
        if (!waitForConsoleInputOrResize(g_hIn, watchW, watchH)) {
            SetConsoleTextAttribute(g_hOut, ATTR_NORMAL);
            SetConsoleCursorInfo(g_hOut, &oldCursorInfo);
            return MENU_RESIZE;
        }
        INPUT_RECORD ir{};
        DWORD read = 0;
        if (!ReadConsoleInputW(g_hIn, &ir, 1, &read) || read == 0) continue;
        if (ir.EventType != KEY_EVENT || !ir.Event.KeyEvent.bKeyDown) continue;

        WORD vk = ir.Event.KeyEvent.wVirtualKeyCode;
        WCHAR ch = ir.Event.KeyEvent.uChar.UnicodeChar;

        if (vk == VK_UP || vk == VK_DOWN) {
            int oldIdx = (prevSelIdx >= 0) ? prevSelIdx : -1;
            int oldPos = selPos;
            if (vk == VK_UP) {
                selPos = (selPos - 1 + static_cast<int>(si.size())) % static_cast<int>(si.size());
            } else {
                selPos = (selPos + 1) % static_cast<int>(si.size());
            }
            if (selPos != oldPos) {
                if (oldIdx >= 0) drawItemLine(oldIdx, false);
                drawItemLine(si[selPos], true);
                prevSelIdx = si[selPos];
            }
        } else if (vk == VK_RETURN) {
            SetConsoleTextAttribute(g_hOut, ATTR_NORMAL);
            SetConsoleCursorInfo(g_hOut, &oldCursorInfo);
            return si[selPos];
        } else if (vk == VK_ESCAPE) {
            SetConsoleTextAttribute(g_hOut, ATTR_NORMAL);
            SetConsoleCursorInfo(g_hOut, &oldCursorInfo);
            return MENU_ESC;
        } else if (ch >= L'0' && ch <= L'9') {
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
                    SetConsoleCursorInfo(g_hOut, &oldCursorInfo);
                    return si[selPos];
                }
            }
        }
    }
}

int menuSelectScrollableInRegion(int x, int y, int w, int h,
                                 const std::vector<MenuItem>& items,
                                 int startIdx) {
    std::vector<int> selectableIndices;
    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
        if (items[i].selectable) selectableIndices.push_back(i);
    }
    if (selectableIndices.empty() || h <= 0 || w <= 0) return -1;

    int selPos = 0;
    for (int i = 0; i < static_cast<int>(selectableIndices.size()); ++i) {
        if (selectableIndices[i] == startIdx) {
            selPos = i;
            break;
        }
    }

    ConsoleViewport view = getConsoleViewport();
    int watchW = view.w;
    int watchH = view.h;
    int visibleCount = std::max(1, h);
    int topItemIdx = std::max(0, selectableIndices[selPos] - visibleCount + 1);
    int maxTop = std::max(0, static_cast<int>(items.size()) - visibleCount);
    if (topItemIdx > maxTop) topItemIdx = maxTop;

    CONSOLE_CURSOR_INFO oldCursorInfo{};
    GetConsoleCursorInfo(g_hOut, &oldCursorInfo);
    CONSOLE_CURSOR_INFO hiddenCursor = oldCursorInfo;
    hiddenCursor.bVisible = FALSE;
    SetConsoleCursorInfo(g_hOut, &hiddenCursor);

    auto drawLine = [&](int row, int itemIdx, bool highlight) {
        fillLine(x, y + row, w, L' ', ATTR_NORMAL);
        std::wstring text = (highlight ? L"> " : L"  ") + items[itemIdx].text;
        writeAtColor(x, y + row, padOrTrimText(text, w), highlight ? 0x70 : AMBER);
    };

    auto fullRender = [&]() {
        for (int row = 0; row < visibleCount; ++row) {
            int itemIdx = topItemIdx + row;
            fillLine(x, y + row, w, L' ', ATTR_NORMAL);
            if (itemIdx >= static_cast<int>(items.size())) continue;
            drawLine(row, itemIdx, itemIdx == selectableIndices[selPos]);
        }
    };

    auto ensureVisible = [&]() {
        int selectedItemIdx = selectableIndices[selPos];
        if (selectedItemIdx < topItemIdx) {
            topItemIdx = selectedItemIdx;
        } else if (selectedItemIdx >= topItemIdx + visibleCount) {
            topItemIdx = selectedItemIdx - visibleCount + 1;
        }
        int localMaxTop = std::max(0, static_cast<int>(items.size()) - visibleCount);
        if (topItemIdx < 0) topItemIdx = 0;
        if (topItemIdx > localMaxTop) topItemIdx = localMaxTop;
    };

    fullRender();
    while (true) {
        if (!waitForConsoleInputOrResize(g_hIn, watchW, watchH)) {
            SetConsoleCursorInfo(g_hOut, &oldCursorInfo);
            return MENU_RESIZE;
        }

        INPUT_RECORD ir{};
        DWORD read = 0;
        if (!ReadConsoleInputW(g_hIn, &ir, 1, &read) || read == 0) continue;
        if (ir.EventType != KEY_EVENT || !ir.Event.KeyEvent.bKeyDown) continue;

        WORD vk = ir.Event.KeyEvent.wVirtualKeyCode;
        WCHAR ch = ir.Event.KeyEvent.uChar.UnicodeChar;
        int oldSelPos = selPos;
        int oldTop = topItemIdx;

        if (vk == VK_UP) {
            selPos = (selPos - 1 + static_cast<int>(selectableIndices.size())) % static_cast<int>(selectableIndices.size());
            ensureVisible();
        } else if (vk == VK_DOWN) {
            selPos = (selPos + 1) % static_cast<int>(selectableIndices.size());
            ensureVisible();
        } else if (vk == VK_PRIOR) {
            selPos -= std::max(1, visibleCount - 1);
            if (selPos < 0) selPos = 0;
            ensureVisible();
        } else if (vk == VK_NEXT) {
            selPos += std::max(1, visibleCount - 1);
            if (selPos >= static_cast<int>(selectableIndices.size())) selPos = static_cast<int>(selectableIndices.size()) - 1;
            ensureVisible();
        } else if (vk == VK_HOME) {
            selPos = 0;
            ensureVisible();
        } else if (vk == VK_END) {
            selPos = static_cast<int>(selectableIndices.size()) - 1;
            ensureVisible();
        } else if (vk == VK_RETURN) {
            SetConsoleCursorInfo(g_hOut, &oldCursorInfo);
            return selectableIndices[selPos];
        } else if (vk == VK_ESCAPE) {
            SetConsoleCursorInfo(g_hOut, &oldCursorInfo);
            return MENU_ESC;
        } else if (ch >= L'0' && ch <= L'9') {
            for (int i = 0; i < static_cast<int>(items.size()); ++i) {
                if (!items[i].selectable) continue;
                std::wstring t = items[i].text;
                size_t pos = t.find_first_not_of(L" ");
                if (pos != std::wstring::npos && t[pos] == static_cast<wchar_t>(ch) &&
                    pos + 1 < t.size() && (t[pos + 1] == L'.' || t[pos + 1] == L' ')) {
                    for (int j = 0; j < static_cast<int>(selectableIndices.size()); ++j) {
                        if (selectableIndices[j] == i) {
                            selPos = j;
                            ensureVisible();
                            SetConsoleCursorInfo(g_hOut, &oldCursorInfo);
                            return selectableIndices[selPos];
                        }
                    }
                }
            }
            continue;
        } else {
            continue;
        }

        if (oldTop != topItemIdx) {
            fullRender();
        } else if (oldSelPos != selPos) {
            int oldRow = selectableIndices[oldSelPos] - topItemIdx;
            int newRow = selectableIndices[selPos] - topItemIdx;
            if (oldRow >= 0 && oldRow < visibleCount) drawLine(oldRow, selectableIndices[oldSelPos], false);
            if (newRow >= 0 && newRow < visibleCount) drawLine(newRow, selectableIndices[selPos], true);
        }
    }
}
