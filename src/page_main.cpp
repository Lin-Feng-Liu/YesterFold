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
        writeAtColor(x, row, L"\u2551", AMBER);
        writeAtColor(x + w - 1, row, L"\u2551", AMBER);
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

MainPageLayout renderMainPage(const DiaryMetrics& m) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(g_hOut, &csbi);
    int screenW = csbi.dwSize.X;
    int screenH = csbi.dwSize.Y;

    int minW = 88, minH = 25;
    int boxW = (screenW < minW) ? minW : screenW;
    int boxH = (screenH < minH) ? minH : screenH;

    clearScreen();
    fillRegion(0, 0, boxW, boxH, L' ', ATTR_NORMAL);

    drawMainFrame(0, 0, boxW, boxH);
    fillRegion(1, 3, boxW - 2, boxH - 4, L' ', ATTR_NORMAL);

    // ── 标题栏 ──
    int titleY = 1;
    int innerW = boxW - 2;
    {
        std::wstring left = L" [>_ SYS.TERMINAL // LOCAL_DIARY_ENV";
        std::wstring btns = L"[\u25A0][O][X] ";  // ■
        writeAtColor(2, titleY, left, AMBER);
        int btnX = boxW - 2 - static_cast<int>(btns.size());
        writeAtColor(btnX, titleY, btns, AMBER_DIM);
    }

    // ── ASCII Art DIARY (行 4-9) ──
    drawDiaryTitle(3, 4);

    // ── 右侧指标区 ──
    int metricsX = 46;
    int metricsRow = 4;

    writeAtColor(metricsX, metricsRow, L"[ TEMPORAL_METRICS ]", AMBER_DIM);
    metricsRow++;

    // DAY PROG
    {
        std::wstringstream ss;
        ss << L"> DAY PROG [";
        writeAtColor(metricsX, metricsRow, ss.str(), AMBER);
        int barX = metricsX + static_cast<int>(ss.str().size());
        int barW = 18;
        drawProgressBar(barX, metricsRow, barW, m.dayProgress);
        int pctX = barX + barW + 1;
        ss.str(L""); ss.clear();
        ss << L"] " << std::fixed << std::setprecision(0) << m.dayProgress << L"%";
        writeAtColor(pctX, metricsRow, ss.str(), AMBER);
    }
    metricsRow++;

    // YEAR PROG
    {
        std::wstringstream ss;
        ss << L"> YEAR PROG[";
        writeAtColor(metricsX, metricsRow, ss.str(), AMBER);
        int barX = metricsX + static_cast<int>(ss.str().size());
        int barW = 18;
        drawProgressBar(barX, metricsRow, barW, m.yearProgress);
        int pctX = barX + barW + 1;
        ss.str(L""); ss.clear();
        ss << L"] " << std::fixed << std::setprecision(0) << m.yearProgress << L"%";
        writeAtColor(pctX, metricsRow, ss.str(), AMBER);
    }
    metricsRow++;

    // WEEK LOG
    {
        std::wstringstream ss;
        ss << L"> WEEK LOG  [";
        writeAtColor(metricsX, metricsRow, ss.str(), AMBER);
        int cellX = metricsX + static_cast<int>(ss.str().size());
        for (int i = 0; i < 7; ++i) {
            writeAtColor(cellX, metricsRow,
                m.weekActivity[i] ? L"\u25CF " : L"\u25CB ",  // ● / ○
                m.weekActivity[i] ? AMBER : AMBER_DIM);
            cellX += 2;
        }
        writeAtColor(cellX, metricsRow, L"]", AMBER);
        ss.str(L""); ss.clear();
        ss << L" (" << m.weekCount << L"/7 DAYS)";
        writeAtColor(cellX + 1, metricsRow, ss.str(), AMBER_DIM);
    }

    // ── 菜单框 + 活动日志区 ──
    int menuBoxX = 3;
    int menuBoxY = 11;
    int menuBoxW = 28;
    int menuBoxH = 12;

    drawSingleBox(menuBoxX, menuBoxY, menuBoxW, menuBoxH);

    // 活动日志标签
    {
        int actX = 46;
        std::wstring label = L"[ " + std::wstring(monthNames[m.currentMonth]) + L" ACTIVITY LOG ]";
        writeAtColor(actX, menuBoxY, label, AMBER_DIM);
    }

    // 分隔线
    {
        int actX = 46;
        int divLen = boxW - actX - 2;
        if (divLen > 0) fillLine(actX, menuBoxY + 1, divLen, L'\u2500', AMBER_DIM);
    }

    // 热力图 WEEK 行
    int dim = m.daysInCurrentMonth;
    int todayWeek = (m.currentDay - 1) / 7 + 1;
    for (int wk = 0; wk < m.monthWeeks; ++wk) {
        int weekY = menuBoxY + 2 + wk;
        int actX = 46;

        std::wstringstream ss;
        ss << L"WEEK " << (wk + 1) << L": ";
        writeAtColor(actX, weekY, ss.str(), AMBER);
        actX += static_cast<int>(ss.str().size());

        writeAtColor(actX, weekY, L"[", AMBER);
        actX++;

        for (int d = 0; d < 7; ++d) {
            int dayIdx = wk * 7 + d;
            if (dayIdx >= dim) break;
            drawHeatmapCell(actX, weekY, m.monthHeatmap[dayIdx]);
            actX += 2;
        }

        writeAtColor(actX, weekY, L"]", AMBER);

        if (wk + 1 == todayWeek) {
            writeAtColor(actX + 2, weekY, L"<-- TODAY", AMBER_DIM);
        }
    }

    // 统计分隔线
    {
        int statDivY = menuBoxY + 2 + m.monthWeeks;
        int actX = 46;
        int divLen = boxW - actX - 2;
        if (divLen > 0) fillLine(actX, statDivY, divLen, L'\u2500', AMBER_DIM);
    }

    // TOTAL / STREAK / AVG
    {
        int statY = menuBoxY + 3 + m.monthWeeks;
        int actX = 46;
        std::wstringstream ss;
        ss << L"TOTAL: " << m.totalEntries
           << L"  |  STREAK: " << m.streak
           << L"  |  AVG: " << m.avgChars;
        writeAtColor(actX, statY, ss.str(), AMBER);
    }

    // ── 命令栏 ──
    int cmdY = boxH - 2;
    {
        std::wstring prompt = L">> AWAITING_COMMAND...";
        writeAtColor(3, cmdY, prompt, AMBER);
    }

    // 返回菜单区域坐标（内边距）
    MainPageLayout layout;
    layout.menuX = menuBoxX + 2;
    layout.menuY = menuBoxY + 1;
    layout.menuW = menuBoxW - 4;
    layout.menuH = menuBoxH - 2;
    return layout;
}
