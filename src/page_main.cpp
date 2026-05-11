#include "page_main.h"
#include <windows.h>
#include <string>
#include <sstream>
#include <iomanip>

static void drawMainFrame(int x, int y, int w, int h) {
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

static const wchar_t* monthNames[] = {
    L"", L"JANUARY", L"FEBRUARY", L"MARCH", L"APRIL",
    L"MAY", L"JUNE", L"JULY", L"AUGUST",
    L"SEPTEMBER", L"OCTOBER", L"NOVEMBER", L"DECEMBER"
};

MainPageLayout renderMainPage(const DiaryMetrics& m, const char* dataPath) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(g_hOut, &csbi);
    int screenW = csbi.dwSize.X;
    int screenH = csbi.dwSize.Y;

    int minW = 88;
    int boxW = (screenW < minW) ? minW : screenW;
    int boxH = screenH;

    int canvasMaxW = 132;
    int canvasW = boxW - 6;
    if (canvasW > canvasMaxW) canvasW = canvasMaxW;
    if (canvasW < 92) canvasW = 92;
    int canvasX = (boxW - canvasW) / 2;

    int leftAreaX = canvasX + 1;
    int leftAreaW = 44;
    int rightAreaX = canvasX + 49;
    int rightAreaW = canvasW - 49;
    if (rightAreaW < 34) {
        rightAreaW = 34;
        leftAreaW = rightAreaX - leftAreaX - 4;
    }

    clearScreen();
    fillRegion(0, 0, boxW, boxH, L' ', ATTR_NORMAL);

    drawMainFrame(0, 0, boxW, boxH);
    fillRegion(1, 3, boxW - 2, boxH - 4, L' ', ATTR_NORMAL);

    // Title bar
    {
        std::wstring left = L" [>_ SYS.TERMINAL // LOCAL_DIARY_ENV";
        std::wstring btns = L"[\u25A0][O][X] ";
        writeAtColor(canvasX, 1, left, AMBER);
        int btnX = boxW - 2 - static_cast<int>(btns.size());
        writeAtColor(btnX, 1, btns, AMBER_DIM);
    }

    drawDiaryTitle(leftAreaX, 4);

    // Vault panel
    {
        int vaultY = 4;
        int panelX = rightAreaX;
        writeAtColor(panelX, vaultY, L"[ VAULT_INTEGRITY ]", AMBER_DIM);
        if (rightAreaW > 0) fillLine(panelX, vaultY + 1, rightAreaW, L'\u2500', AMBER_DIM);

        WIN32_FILE_ATTRIBUTE_DATA fad;
        bool fileOk = GetFileAttributesExA(dataPath, GetFileExInfoStandard, &fad);
        if (fileOk) {
            ULONGLONG fsize = (static_cast<ULONGLONG>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
            std::wstringstream fss;
            if (fsize < 1024) fss << fsize << L" B";
            else if (fsize < 1048576) fss << std::fixed << std::setprecision(1) << (fsize / 1024.0) << L" KB";
            else fss << std::fixed << std::setprecision(1) << (fsize / 1048576.0) << L" MB";

            FILETIME ftNow;
            GetSystemTimeAsFileTime(&ftNow);
            ULARGE_INTEGER unow, ufile;
            unow.LowPart = ftNow.dwLowDateTime;  unow.HighPart = ftNow.dwHighDateTime;
            ufile.LowPart = fad.ftLastWriteTime.dwLowDateTime;
            ufile.HighPart = fad.ftLastWriteTime.dwHighDateTime;
            LONGLONG diff100ns = unow.QuadPart - ufile.QuadPart;
            int minAgo = static_cast<int>(diff100ns / 600000000LL);
            std::wstring syncStr;
            if (minAgo < 1) syncStr = L"just now";
            else if (minAgo < 60) {
                std::wstringstream t; t << minAgo << L" min ago"; syncStr = t.str();
            } else {
                std::wstringstream t; t << (minAgo / 60) << L" h " << (minAgo % 60) << L" m ago"; syncStr = t.str();
            }

            writeAtColor(panelX, vaultY + 2, L"PATH  : " + utf8_to_wstring(std::string(dataPath)), AMBER);
            writeAtColor(panelX, vaultY + 3, L"SIZE  : " + fss.str() + L" (Encrypted)", AMBER);
            writeAtColor(panelX, vaultY + 4, L"STATE : XChaCha20-Poly1305 [LOCKED]", AMBER);
            writeAtColor(panelX, vaultY + 5, L"SYNC  : Last save " + syncStr, AMBER);
        }
    }

    int menuBoxX = leftAreaX + 6;
    int menuBoxY = 11;
    int menuBoxW = 28;
    int menuBoxH = 12;
    drawSingleBox(menuBoxX, menuBoxY, menuBoxW, menuBoxH);

    // Activity log
    {
        int actX = rightAreaX;
        std::wstring label = L"[ " + std::wstring(monthNames[m.currentMonth]) + L" ACTIVITY LOG ]";
        writeAtColor(actX, menuBoxY, label, AMBER_DIM);
        if (rightAreaW > 0) fillLine(actX, menuBoxY + 1, rightAreaW, L'\u2500', AMBER_DIM);

        int dim = m.daysInCurrentMonth;
        int todayWeek = (m.currentDay - 1) / 7 + 1;
        for (int wk = 0; wk < m.monthWeeks; ++wk) {
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
                drawHeatmapCell(rowX, weekY, m.monthHeatmap[dayIdx]);
                rowX += 2;
            }
            writeAtColor(rowX, weekY, L"]", AMBER);

            if (wk + 1 == todayWeek) {
                writeAtColor(rowX + 2, weekY, L"<-- TODAY", AMBER_DIM);
            }
        }

        int statDivY = menuBoxY + 2 + m.monthWeeks;
        if (rightAreaW > 0) fillLine(actX, statDivY, rightAreaW, L'\u2500', AMBER_DIM);

        std::wstringstream ss;
        ss << L"TOTAL: " << m.totalEntries
           << L"  |  STREAK: " << m.streak
           << L"  |  AVG: " << m.avgChars;
        writeAtColor(actX, statDivY + 1, ss.str(), AMBER);
    }

    // Temporal metrics
    {
        int metricsY = menuBoxY + 5 + m.monthWeeks;
        if (metricsY + 4 <= boxH - 3) {
            int metricsX = rightAreaX;
            writeAtColor(metricsX, metricsY, L"[ TEMPORAL_METRICS ]", AMBER_DIM);
            metricsY++;
            if (rightAreaW > 0) fillLine(metricsX, metricsY, rightAreaW, L'\u2500', AMBER_DIM);
            metricsY++;

            {
                std::wstringstream ss;
                ss << L"> DAY PROG [";
                writeAtColor(metricsX, metricsY, ss.str(), AMBER);
                int barX = metricsX + static_cast<int>(ss.str().size());
                int barW = 18;
                drawProgressBar(barX, metricsY, barW, m.dayProgress);
                int pctX = barX + barW + 1;
                ss.str(L""); ss.clear();
                ss << L"] " << std::fixed << std::setprecision(0) << m.dayProgress << L"%";
                writeAtColor(pctX, metricsY, ss.str(), AMBER);
            }
            metricsY++;

            {
                std::wstringstream ss;
                ss << L"> YEAR PROG[";
                writeAtColor(metricsX, metricsY, ss.str(), AMBER);
                int barX = metricsX + static_cast<int>(ss.str().size());
                int barW = 18;
                drawProgressBar(barX, metricsY, barW, m.yearProgress);
                int pctX = barX + barW + 1;
                ss.str(L""); ss.clear();
                ss << L"] " << std::fixed << std::setprecision(0) << m.yearProgress << L"%";
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
                        m.weekActivity[i] ? L"\u25CF " : L"\u25CB ",
                        m.weekActivity[i] ? AMBER : AMBER_DIM);
                    cellX += 2;
                }
                writeAtColor(cellX, metricsY, L"]", AMBER);
                ss.str(L""); ss.clear();
                ss << L" (" << m.weekCount << L"/7 THIS WEEK)";
                writeAtColor(cellX + 1, metricsY, ss.str(), AMBER_DIM);
            }
        }
    }

    // Footer
    {
        int logY = boxH - 4;
        if (logY >= 4) {
            ULONGLONG elapsed = GetTickCount() - g_startTick;
            double secs = elapsed / 1000.0;

            wchar_t line1[80];
            swprintf(line1, 80, L"  UPTIME %.1fs", secs);
            writeAtColor(canvasX, logY, line1, AMBER_DIM);
            writeAtColor(canvasX, logY + 1, L"  STATUS IDLE", AMBER_DIM);
        }
    }

    int cmdY = boxH - 2;
    {
        std::wstring prompt = L">> AWAITING_COMMAND...";
        writeAtColor(canvasX, cmdY, prompt, AMBER);
        COORD cursorPos = {static_cast<SHORT>(canvasX + static_cast<int>(prompt.size())), static_cast<SHORT>(cmdY)};
        SetConsoleCursorPosition(g_hOut, cursorPos);
    }

    MainPageLayout layout;
    layout.menuX = menuBoxX + 2;
    layout.menuY = menuBoxY + 1;
    layout.menuW = menuBoxW - 4;
    layout.menuH = menuBoxH - 2;
    return layout;
}
