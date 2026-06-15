#include "page_main.h"

#include "diary_store.h"
#include "metrics.h"
#include "ui_render.h"

#include <windows.h>

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct MainPageLayout {
    int boxX;
    int boxY;
    int boxW;
    int boxH;
    int innerX;

    int menuX;
    int menuY;
    int menuW;
    int menuH;

    int syncX;
    int syncY;
    int syncW;

    int dayProgX;
    int dayProgY;
    int dayProgW;
    int dayProgBarX;
    int dayProgBarW;

    int uptimeX;
    int uptimeY;
    int uptimeW;
};

struct LiveMainSnapshot {
    int currentYear;
    int currentMonth;
    int currentDay;
    double dayProgress;
    int dayProgressPercent;
    std::wstring syncLine;
    std::wstring uptimeLine;
};

enum class WaitOutcome {
    Input,
    Timeout,
    Resize,
};

static const wchar_t* monthNames[] = {
    L"", L"JANUARY", L"FEBRUARY", L"MARCH", L"APRIL",
    L"MAY", L"JUNE", L"JULY", L"AUGUST",
    L"SEPTEMBER", L"OCTOBER", L"NOVEMBER", L"DECEMBER"
};

const std::vector<MenuItem>& mainMenuItems() {
    static const std::vector<MenuItem> items = {
        {L"1. 写入 / 编辑今日", true},
        {L"2. 查看全部", true},
        {L"3. 按日期编辑", true},
        {L"4. 导出 / 导入", true},
        {L"5. 修改密码", true},
        {L"6. 神秘计数器", true},
        {L"7. 保存并退出", true},
    };
    return items;
}

int daysInMonth(int year, int month) {
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) return 30;
    bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    if (month == 2 && leap) return 29;
    return days[month - 1];
}

int dayOfYear(int year, int month, int day) {
    int doy = day;
    for (int m = 1; m < month; ++m) doy += daysInMonth(year, m);
    return doy;
}

double computeDayProgress(const SYSTEMTIME& st) {
    int secondsToday = st.wHour * 3600 + st.wMinute * 60 + st.wSecond;
    return static_cast<double>(secondsToday) / 86400.0 * 100.0;
}

std::wstring buildSyncLine(const char* dataPath) {
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExA(dataPath, GetFileExInfoStandard, &fad)) {
        return L"SYNC  : File unavailable";
    }

    FILETIME ftNow;
    GetSystemTimeAsFileTime(&ftNow);
    ULARGE_INTEGER unow, ufile;
    unow.LowPart = ftNow.dwLowDateTime;
    unow.HighPart = ftNow.dwHighDateTime;
    ufile.LowPart = fad.ftLastWriteTime.dwLowDateTime;
    ufile.HighPart = fad.ftLastWriteTime.dwHighDateTime;

    LONGLONG diff100ns = static_cast<LONGLONG>(unow.QuadPart - ufile.QuadPart);
    if (diff100ns < 0) diff100ns = 0;

    int minAgo = static_cast<int>(diff100ns / 600000000LL);
    std::wstringstream sync;
    sync << L"SYNC  : Last save ";
    if (minAgo < 1) {
        sync << L"just now";
    } else if (minAgo < 60) {
        sync << minAgo << L" min ago";
    } else {
        sync << (minAgo / 60) << L" h " << (minAgo % 60) << L" m ago";
    }
    return sync.str();
}

std::wstring buildUptimeLine() {
    double secs = static_cast<double>(static_cast<ULONGLONG>(GetTickCount()) - g_startTick) / 1000.0;
    wchar_t line[80];
    swprintf(line, 80, L"  UPTIME %.1fs", secs);
    return line;
}

LiveMainSnapshot captureLiveSnapshot(const DiaryMetrics& metrics, const char* dataPath) {
    SYSTEMTIME st;
    GetLocalTime(&st);

    LiveMainSnapshot snap{};
    snap.currentYear = st.wYear;
    snap.currentMonth = st.wMonth;
    snap.currentDay = st.wDay;
    snap.dayProgress = computeDayProgress(st);
    snap.dayProgressPercent = static_cast<int>(snap.dayProgress + 0.5);
    snap.syncLine = buildSyncLine(dataPath);
    snap.uptimeLine = buildUptimeLine();
    return snap;
}

void drawMainFrame(int x, int y, int w, int h) {
    writeAtColor(x, y, L"\u2554", AMBER);
    fillLine(x + 1, y, w - 2, L'\u2550', AMBER);
    writeAtColor(x + w - 1, y, L"\u2557", AMBER);

    writeAtColor(x, y + 1, L"\u2551", AMBER);
    writeAtColor(x + w - 1, y + 1, L"\u2551", AMBER);

    writeAtColor(x, y + 2, L"\u2560", AMBER);
    fillLine(x + 1, y + 2, w - 2, L'\u2550', AMBER);
    writeAtColor(x + w - 1, y + 2, L"\u2563", AMBER);

    for (int row = 3; row < h - 1; ++row) {
        writeAtColor(x, y + row, L"\u2551", AMBER);
        writeAtColor(x + w - 1, y + row, L"\u2551", AMBER);
    }

    writeAtColor(x, y + h - 1, L"\u255A", AMBER);
    fillLine(x + 1, y + h - 1, w - 2, L'\u2550', AMBER);
    writeAtColor(x + w - 1, y + h - 1, L"\u255D", AMBER);
}

void renderMenu(const MainPageLayout& layout, const std::vector<MenuItem>& items, int selected) {
    for (int i = 0; i < layout.menuH; ++i) {
        fillLine(layout.menuX, layout.menuY + i, layout.menuW, L' ', ATTR_NORMAL);
        if (i >= static_cast<int>(items.size())) continue;
        std::wstring line = (i == selected ? L"> " : L"  ") + items[i].text;
        writeAtColor(layout.menuX, layout.menuY + i,
                     padOrTrimText(line, layout.menuW), i == selected ? 0x70 : AMBER);
    }
}

void renderSyncLine(const MainPageLayout& layout, const std::wstring& syncLine) {
    fillLine(layout.syncX, layout.syncY, layout.syncW, L' ', ATTR_NORMAL);
    writeAtColor(layout.syncX, layout.syncY, padOrTrimText(syncLine, layout.syncW), AMBER);
}

void patchTextDiff(int x, int y,
                   const std::wstring& prev,
                   const std::wstring& next,
                   WORD attr) {
    size_t maxLen = std::max(prev.size(), next.size());
    size_t idx = 0;
    while (idx < maxLen) {
        wchar_t prevCh = idx < prev.size() ? prev[idx] : L' ';
        wchar_t nextCh = idx < next.size() ? next[idx] : L' ';
        if (prevCh == nextCh) {
            ++idx;
            continue;
        }

        size_t start = idx;
        std::wstring chunk;
        while (idx < maxLen) {
            prevCh = idx < prev.size() ? prev[idx] : L' ';
            nextCh = idx < next.size() ? next[idx] : L' ';
            if (prevCh == nextCh) break;
            chunk.push_back(nextCh);
            ++idx;
        }
        writeAtColor(x + static_cast<int>(start), y, chunk, attr);
    }
}

void renderDayProgressLine(const MainPageLayout& layout, double dayProgress) {
    fillLine(layout.dayProgX, layout.dayProgY, layout.dayProgW, L' ', ATTR_NORMAL);

    std::wstringstream prefix;
    prefix << L"> DAY PROG [";
    writeAtColor(layout.dayProgX, layout.dayProgY, prefix.str(), AMBER);
    drawProgressBar(layout.dayProgBarX, layout.dayProgY, layout.dayProgBarW, dayProgress);

    std::wstringstream suffix;
    suffix << L"] " << std::fixed << std::setprecision(0) << dayProgress << L"%";
    writeAtColor(layout.dayProgBarX + layout.dayProgBarW + 1, layout.dayProgY, suffix.str(), AMBER);
}

void renderUptimeLine(const MainPageLayout& layout, const std::wstring& uptimeLine) {
    fillLine(layout.uptimeX, layout.uptimeY, layout.uptimeW, L' ', ATTR_NORMAL);
    writeAtColor(layout.uptimeX, layout.uptimeY, padOrTrimText(uptimeLine, layout.uptimeW), AMBER_DIM);
}

void patchUptimeLine(const MainPageLayout& layout,
                     const std::wstring& prevLine,
                     const std::wstring& nextLine) {
    patchTextDiff(layout.uptimeX, layout.uptimeY,
                  padOrTrimText(prevLine, layout.uptimeW),
                  padOrTrimText(nextLine, layout.uptimeW),
                  AMBER_DIM);
}

MainPageLayout renderMainPage(const DiaryMetrics& metrics, const char* dataPath, int selectedIndex) {
    ConsoleViewport view = getConsoleViewport();
    CenteredRect shell = getCenteredRect(view.w, view.h, FIXED_SHELL_W, FIXED_SHELL_H, 88, 24);
    shell.x += view.x;
    shell.y += view.y;

    const LiveMainSnapshot live = captureLiveSnapshot(metrics, dataPath);

    int boxX = shell.x;
    int boxY = shell.y;
    int boxW = shell.w;
    int boxH = shell.h;
    int innerX = boxX + 2;
    int leftAreaX = innerX + 1;
    int rightAreaX = innerX + 48;
    int rightAreaW = boxW - (rightAreaX - boxX) - 4;

    clearScreen();
    drawMainFrame(boxX, boxY, boxW, boxH);

    {
        std::wstring left = L" [>_ SYS.TERMINAL // LOCAL_DIARY_ENV";
        std::wstring btns = L"[\u25A0][O][X] ";
        writeAtColor(innerX, boxY + 1, left, AMBER);
        int btnX = boxX + boxW - 2 - static_cast<int>(btns.size());
        writeAtColor(btnX, boxY + 1, btns, AMBER_DIM);
    }

    drawDiaryTitle(leftAreaX, boxY + 4);

    {
        int vaultY = boxY + 4;
        writeAtColor(rightAreaX, vaultY, L"[ VAULT_INTEGRITY ]", AMBER_DIM);
        if (rightAreaW > 0) fillLine(rightAreaX, vaultY + 1, rightAreaW, L'\u2500', AMBER_DIM);

        WIN32_FILE_ATTRIBUTE_DATA fad;
        bool fileOk = GetFileAttributesExA(dataPath, GetFileExInfoStandard, &fad);
        if (fileOk) {
            ULONGLONG fsize = (static_cast<ULONGLONG>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
            std::wstringstream fss;
            if (fsize < 1024) fss << fsize << L" B";
            else if (fsize < 1048576) fss << std::fixed << std::setprecision(1) << (fsize / 1024.0) << L" KB";
            else fss << std::fixed << std::setprecision(1) << (fsize / 1048576.0) << L" MB";

            writeAtColor(rightAreaX, vaultY + 2, L"PATH  : " + utf8_to_wstring(std::string(dataPath)), AMBER);
            writeAtColor(rightAreaX, vaultY + 3, L"SIZE  : " + fss.str() + L" (Encrypted)", AMBER);
            writeAtColor(rightAreaX, vaultY + 4, L"STATE : XChaCha20-Poly1305 [LOCKED]", AMBER);
            writeAtColor(rightAreaX, vaultY + 5, padOrTrimText(live.syncLine, rightAreaW), AMBER);
        }
    }

    int menuBoxX = leftAreaX + 5;
    int menuBoxY = boxY + 11;
    int menuBoxW = 28;
    int menuBoxH = 12;
    drawSingleBox(menuBoxX, menuBoxY, menuBoxW, menuBoxH);

    {
        int actX = rightAreaX;
        std::wstring label = L"[ " + std::wstring(monthNames[metrics.currentMonth]) + L" ACTIVITY LOG ]";
        writeAtColor(actX, menuBoxY, label, AMBER_DIM);
        if (rightAreaW > 0) fillLine(actX, menuBoxY + 1, rightAreaW, L'\u2500', AMBER_DIM);

        int dim = metrics.daysInCurrentMonth;
        int todayWeek = (metrics.currentDay - 1) / 7 + 1;
        for (int wk = 0; wk < metrics.monthWeeks; ++wk) {
            int weekY = menuBoxY + 2 + wk;
            int rowX = actX;

            std::wstringstream ss;
            ss << L"WEEK " << (wk + 1) << L": ";
            writeAtColor(rowX, weekY, ss.str(), AMBER);
            rowX += static_cast<int>(ss.str().size());
            writeAtColor(rowX, weekY, L"[", AMBER);
            rowX++;

            for (int d = 0; d < 7; ++d) {
                int dayIdx = wk * 7 + d;
                if (dayIdx >= dim) break;
                drawHeatmapCell(rowX, weekY, metrics.monthHeatmap[dayIdx]);
                rowX += 2;
            }

            writeAtColor(rowX, weekY, L"]", AMBER);
            if (wk + 1 == todayWeek) {
                writeAtColor(rowX + 2, weekY, L"<-- TODAY", AMBER_DIM);
            }
        }

        int statDivY = menuBoxY + 2 + metrics.monthWeeks;
        if (rightAreaW > 0) fillLine(actX, statDivY, rightAreaW, L'\u2500', AMBER_DIM);

        std::wstringstream ss;
        ss << L"TOTAL: " << metrics.totalEntries
           << L"  |  STREAK: " << metrics.streak
           << L"  |  AVG: " << metrics.avgChars;
        writeAtColor(actX, statDivY + 1, ss.str(), AMBER);
    }

    MainPageLayout layout{};
    layout.boxX = boxX;
    layout.boxY = boxY;
    layout.boxW = boxW;
    layout.boxH = boxH;
    layout.innerX = innerX;

    layout.menuX = menuBoxX + 2;
    layout.menuY = menuBoxY + 1;
    layout.menuW = menuBoxW - 4;
    layout.menuH = menuBoxH - 2;

    {
        int metricsY = menuBoxY + 5 + metrics.monthWeeks;
        int metricsX = rightAreaX;
        writeAtColor(metricsX, metricsY, L"[ TEMPORAL_METRICS ]", AMBER_DIM);
        metricsY++;
        if (rightAreaW > 0) fillLine(metricsX, metricsY, rightAreaW, L'\u2500', AMBER_DIM);
        metricsY++;

        layout.dayProgX = metricsX;
        layout.dayProgY = metricsY;
        layout.dayProgW = rightAreaW;
        layout.dayProgBarX = metricsX + static_cast<int>(std::wstring(L"> DAY PROG [").size());
        layout.dayProgBarW = 18;
        renderDayProgressLine(layout, live.dayProgress);
        metricsY++;

        {
            std::wstringstream ss;
            ss << L"> YEAR PROG[";
            writeAtColor(metricsX, metricsY, ss.str(), AMBER);
            int barX = metricsX + static_cast<int>(ss.str().size());
            int barW = 18;
            drawProgressBar(barX, metricsY, barW, metrics.yearProgress);
            int pctX = barX + barW + 1;
            ss.str(L"");
            ss.clear();
            ss << L"] " << std::fixed << std::setprecision(0) << metrics.yearProgress << L"%";
            writeAtColor(pctX, metricsY, ss.str(), AMBER);
        }
        metricsY++;

        {
            std::wstringstream ss;
            ss << L"> WEEK LOG [";
            writeAtColor(metricsX, metricsY, ss.str(), AMBER);
            int cellX = metricsX + static_cast<int>(ss.str().size());
            for (int i = 0; i < 7; ++i) {
                writeAtColor(cellX, metricsY,
                             metrics.weekActivity[i] ? L"\u25CF " : L"\u25CB ",
                             metrics.weekActivity[i] ? AMBER : AMBER_DIM);
                cellX += 2;
            }
            writeAtColor(cellX, metricsY, L"]", AMBER);
            ss.str(L"");
            ss.clear();
            ss << L" (" << metrics.weekCount << L"/7 THIS WEEK)";
            writeAtColor(cellX + 1, metricsY, ss.str(), AMBER_DIM);
        }
    }

    layout.syncX = rightAreaX;
    layout.syncY = boxY + 9;
    layout.syncW = rightAreaW;

    layout.uptimeX = innerX;
    layout.uptimeY = boxY + boxH - 4;
    layout.uptimeW = 24;
    renderUptimeLine(layout, live.uptimeLine);
    writeAtColor(innerX, layout.uptimeY + 1, L"  STATUS IDLE", AMBER_DIM);

    int cmdY = boxY + boxH - 2;
    std::wstring prompt = L">> AWAITING_COMMAND...";
    writeAtColor(innerX, cmdY, prompt, AMBER);

    renderMenu(layout, mainMenuItems(), selectedIndex);
    return layout;
}

void updateLiveRegions(const MainPageLayout& layout,
                       const LiveMainSnapshot& prev,
                       const LiveMainSnapshot& next) {
    if (prev.uptimeLine != next.uptimeLine) {
        patchUptimeLine(layout, prev.uptimeLine, next.uptimeLine);
    }
    if (prev.syncLine != next.syncLine) {
        renderSyncLine(layout, next.syncLine);
    }
    if (prev.dayProgressPercent != next.dayProgressPercent) {
        renderDayProgressLine(layout, next.dayProgress);
    }
}

WaitOutcome waitForMainPageEvent(int& watchW, int& watchH, DWORD timeoutMs) {
    DWORD waitResult = WaitForSingleObject(g_hIn, timeoutMs);
    if (waitResult == WAIT_OBJECT_0) {
        INPUT_RECORD peek{};
        DWORD peeked = 0;
        if (PeekConsoleInputW(g_hIn, &peek, 1, &peeked) && peeked > 0 && peek.EventType == WINDOW_BUFFER_SIZE_EVENT) {
            INPUT_RECORD consumed{};
            DWORD read = 0;
            do {
                if (!ReadConsoleInputW(g_hIn, &consumed, 1, &read) || read == 0) break;
                peeked = 0;
            } while (PeekConsoleInputW(g_hIn, &peek, 1, &peeked) && peeked > 0 && peek.EventType == WINDOW_BUFFER_SIZE_EVENT);

            ConsoleViewport current = getConsoleViewport();
            watchW = current.w;
            watchH = current.h;
            return WaitOutcome::Resize;
        }
        return WaitOutcome::Input;
    }

    ConsoleViewport current = getConsoleViewport();
    if (current.w != watchW || current.h != watchH) {
        watchW = current.w;
        watchH = current.h;
        return WaitOutcome::Resize;
    }
    return WaitOutcome::Timeout;
}

}

int selectMainPage(const DiaryStore& store, const char* dataPath) {
    const std::vector<MenuItem>& items = mainMenuItems();
    int selected = 0;
    DiaryMetrics metrics = computeMetrics(store);
    MainPageLayout layout = renderMainPage(metrics, dataPath, selected);
    LiveMainSnapshot live = captureLiveSnapshot(metrics, dataPath);

    ConsoleViewport view = getConsoleViewport();
    int watchW = view.w;
    int watchH = view.h;

    CONSOLE_CURSOR_INFO oldCursorInfo{};
    GetConsoleCursorInfo(g_hOut, &oldCursorInfo);
    CONSOLE_CURSOR_INFO hiddenCursor = oldCursorInfo;
    hiddenCursor.bVisible = FALSE;
    SetConsoleCursorInfo(g_hOut, &hiddenCursor);

    while (true) {
        WaitOutcome outcome = waitForMainPageEvent(watchW, watchH, 100);
        if (outcome == WaitOutcome::Resize) {
            metrics = computeMetrics(store);
            layout = renderMainPage(metrics, dataPath, selected);
            live = captureLiveSnapshot(metrics, dataPath);
            continue;
        }

        LiveMainSnapshot nextLive = captureLiveSnapshot(metrics, dataPath);
        if (nextLive.currentYear != metrics.currentYear ||
            nextLive.currentMonth != metrics.currentMonth ||
            nextLive.currentDay != metrics.currentDay) {
            metrics = computeMetrics(store);
            layout = renderMainPage(metrics, dataPath, selected);
            live = captureLiveSnapshot(metrics, dataPath);
            continue;
        }
        updateLiveRegions(layout, live, nextLive);
        live = nextLive;

        if (outcome != WaitOutcome::Input) {
            continue;
        }

        INPUT_RECORD ir{};
        DWORD read = 0;
        if (!ReadConsoleInputW(g_hIn, &ir, 1, &read) || read == 0) continue;
        if (ir.EventType != KEY_EVENT || !ir.Event.KeyEvent.bKeyDown) continue;

        WORD vk = ir.Event.KeyEvent.wVirtualKeyCode;
        WCHAR ch = ir.Event.KeyEvent.uChar.UnicodeChar;

        if (vk == VK_UP || vk == VK_DOWN) {
            int oldSelected = selected;
            if (vk == VK_UP) {
                selected = (selected - 1 + static_cast<int>(items.size())) % static_cast<int>(items.size());
            } else {
                selected = (selected + 1) % static_cast<int>(items.size());
            }
            if (oldSelected != selected) {
                renderMenu(layout, items, selected);
            }
        } else if (vk == VK_RETURN) {
            SetConsoleCursorInfo(g_hOut, &oldCursorInfo);
            return selected;
        } else if (vk == VK_ESCAPE) {
            SetConsoleCursorInfo(g_hOut, &oldCursorInfo);
            return MENU_ESC;
        } else if (ch >= L'1' && ch <= L'7') {
            SetConsoleCursorInfo(g_hOut, &oldCursorInfo);
            return static_cast<int>(ch - L'1');
        }
    }
}
