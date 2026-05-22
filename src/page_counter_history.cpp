#include "page_counter_history.h"

#include "diary_store.h"
#include "page_status.h"
#include "ui_render.h"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace {

struct CounterHistoryLayout {
    int pathX;
    int pathW;
    int modeX;
    int modeW;
    int infoX;
    int infoY;
    int infoW;
    int infoH;
    int listX;
    int listY;
    int listW;
    int listH;
    int hintX;
    int hintW;
    int statusY;
};

struct HistoryItem {
    size_t index;
    int number;
    int year;
    int month;
    std::string timestamp;
};

struct ParsedStamp {
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
};

bool parseStamp(const std::string& stamp, ParsedStamp& out) {
    int y, m, d, hh, mm, ss;
    if (std::sscanf(stamp.c_str(), "%d/%d/%d %d:%d:%d", &y, &m, &d, &hh, &mm, &ss) != 6) return false;
    out.year = y;
    out.month = m;
    out.day = d;
    out.hour = hh;
    out.minute = mm;
    out.second = ss;
    return true;
}

std::wstring formatMonthLine(int year, int month) {
    return utf8_to_wstring(std::to_string(year) + "/" + std::to_string(month));
}

int showDeleteConfirmModal(const HistoryItem& item) {
    ConsoleViewport view = getConsoleViewport();
    int boxW = std::min(std::max(42, view.w - 20), 64);
    int boxH = 10;
    if (boxH > view.h - 4) boxH = std::max(8, view.h - 4);
    int boxX = view.x + std::max(0, (view.w - boxW) / 2);
    int boxY = view.y + std::max(0, (view.h - boxH) / 2);

    drawSingleBox(boxX, boxY, boxW, boxH);
    fillRegion(boxX + 1, boxY + 1, boxW - 2, boxH - 2, L' ', ATTR_NORMAL);
    writeAtColor(boxX + 2, boxY - 1, L"[ DELETE RECORD ]", AMBER_DIM);

    std::vector<std::wstring> lines = {
        L"即将删除以下触发记录：",
        L"#" + std::to_wstring(item.number) + L" : " + utf8_to_wstring(item.timestamp),
    };
    writeWrappedPanelLines(boxX + 2, boxY + 2, boxW - 4, 3, lines, AMBER);

    std::vector<MenuItem> items = {
        {L"1. 确认删除", true},
        {L"0. 取消", true},
    };
    int choice = menuSelectInRegion(boxX + 2, boxY + boxH - 3, boxW - 4, 2, items, 0);
    fillRegion(boxX, boxY - 1, boxW, boxH + 1, L' ', ATTR_NORMAL);
    return choice;
}

CounterHistoryLayout renderCounterHistoryFrame() {
    ConsoleViewport view = getConsoleViewport();
    int boxW = view.w < 88 ? 88 : view.w;
    int boxH = view.h < 24 ? 24 : view.h;

    drawTerminalShell(L"COUNTER.HISTORY // LOCAL_DIARY_ENV");

    writeAtColor(4, 4, L"[ OVERALL_HISTORY ]", AMBER_DIM);
    fillLine(4, 5, boxW - 8, L'─', AMBER_DIM);

    int panelX = 4;
    int panelW = boxW - 8;
    int topY = 7;
    int infoH = 4;
    int listY = topY + infoH + 1;
    int listH = boxH - listY - 5;
    if (listH < 8) listH = 8;

    drawSingleBox(panelX, topY, panelW, infoH);
    drawSingleBox(panelX, listY, panelW, listH);

    fillLine(1, boxH - 3, boxW - 2, L' ', ATTR_NORMAL);
    fillLine(1, boxH - 2, boxW - 2, L' ', ATTR_NORMAL);
    writeAtColor(3, boxH - 2, padOrTrimText(L">> COUNTER HISTORY READY", boxW - 6), AMBER);

    CounterHistoryLayout layout{};
    layout.pathX = 6;
    layout.pathW = boxW - 12;
    layout.modeX = 6;
    layout.modeW = boxW - 12;
    layout.infoX = panelX + 2;
    layout.infoY = topY + 1;
    layout.infoW = panelW - 4;
    layout.infoH = infoH - 2;
    layout.listX = panelX + 2;
    layout.listY = listY + 1;
    layout.listW = panelW - 4;
    layout.listH = listH - 2;
    layout.hintX = 3;
    layout.hintW = boxW - 6;
    layout.statusY = boxH - 2;
    return layout;
}

void clearCounterHistoryBody(const CounterHistoryLayout& layout) {
    fillLine(4, 7, layout.pathW, L' ', ATTR_NORMAL);
    fillLine(4, 8, layout.pathW, L' ', ATTR_NORMAL);
    fillLine(layout.infoX, layout.infoY, layout.infoW, L' ', ATTR_NORMAL);
    fillLine(layout.infoX, layout.infoY + 1, layout.infoW, L' ', ATTR_NORMAL);
    fillLine(layout.listX, layout.listY, layout.listW, L' ', ATTR_NORMAL);
    for (int row = 1; row < layout.listH; ++row) {
        fillLine(layout.listX, layout.listY + row, layout.listW, L' ', ATTR_NORMAL);
    }
}

void updateCounterHistoryHeader(const CounterHistoryLayout& layout,
                                const std::wstring& pathLine,
                                const std::wstring& modeLine,
                                const std::vector<std::wstring>& infoLines) {
    fillLine(layout.pathX, 7, layout.pathW, L' ', ATTR_NORMAL);
    fillLine(layout.modeX, 8, layout.modeW, L' ', ATTR_NORMAL);
    writeAtColor(layout.pathX, 7, fitTextToWidth(L"PATH : " + pathLine, layout.pathW), AMBER);
    writeAtColor(layout.modeX, 8, fitTextToWidth(L"MODE : " + modeLine, layout.modeW), AMBER);
    writeWrappedPanelLines(layout.infoX, layout.infoY, layout.infoW, layout.infoH, infoLines, AMBER);
    fillLine(layout.hintX, layout.statusY - 1, layout.hintW, L' ', ATTR_NORMAL);
    writeAtColor(layout.hintX, layout.statusY - 1, fitTextToWidth(L"Enter删除  Esc返回  PgUp/PgDn翻页  Home/End首尾", layout.hintW), AMBER_DIM);
}

std::vector<HistoryItem> buildHistoryItems(const DiaryStore& store) {
    std::vector<HistoryItem> items;
    auto history = store.getCounterHistory();
    int count = store.getCounter();
    int total = static_cast<int>(history.size());
    for (int i = 0; i < total; ++i) {
        int recordNumber = count - (total - 1 - i);
        if (recordNumber <= 0) continue;
        ParsedStamp stamp{};
        if (!parseStamp(history[i], stamp)) continue;
        items.push_back({static_cast<size_t>(i), recordNumber, stamp.year, stamp.month, history[i]});
    }
    return items;
}

} // namespace

void browseCounterHistory(DiaryStore& store, const std::string& password, const char* diaryPath) {
    int stage = 0;
    int selYear = 0;
    int selMonth = 0;
    CounterHistoryLayout layout = renderCounterHistoryFrame();

    while (true) {
        auto items = buildHistoryItems(store);

        if (items.empty()) {
            showFullScreenMessage(L"HISTORY EMPTY", {L"[还没有可回望的触发记录]"});
            pauseScreen();
            return;
        }

        std::vector<int> years;
        for (const auto& item : items) {
            if (std::find(years.begin(), years.end(), item.year) == years.end()) years.push_back(item.year);
        }
        std::sort(years.begin(), years.end());

        if (stage > 0 && std::find(years.begin(), years.end(), selYear) == years.end()) {
            stage = 0;
            selYear = 0;
            selMonth = 0;
        }

        bool needResizeRebuild = false;

        while (true) {
            std::vector<MenuItem> menuItems;
            std::vector<int> values;
            std::vector<HistoryItem> monthItems;
            std::wstring pathLine = L"ROOT";
            std::wstring modeLine;
            std::vector<std::wstring> infoLines;

            if (stage == 0) {
                menuItems.push_back({L"--- 选择年份 ---", false});
                for (int y : years) {
                    menuItems.push_back({utf8_to_wstring(std::to_string(y) + " 年"), true});
                    values.push_back(y);
                }
                menuItems.push_back({L"0. 返回", true});
                pathLine = L"YEAR INDEX";
                modeLine = L"YEAR -> MONTH -> RECORDS";
                infoLines = {L"当前层级 : YEAR", L"可用年份 : " + std::to_wstring(years.size())};
            } else {
                std::vector<int> months;
                for (const auto& item : items) {
                    if (item.year == selYear && std::find(months.begin(), months.end(), item.month) == months.end()) {
                        months.push_back(item.month);
                    }
                }
                std::sort(months.begin(), months.end());
                if (stage == 2 && std::find(months.begin(), months.end(), selMonth) == months.end()) {
                    stage = 1;
                }
                if (stage == 1) {
                    menuItems.push_back({L"--- 选择月份 ---", false});
                    for (int m : months) {
                        menuItems.push_back({utf8_to_wstring(std::to_string(m) + " 月"), true});
                        values.push_back(m);
                    }
                    menuItems.push_back({L"0. 返回年份", true});
                    pathLine = utf8_to_wstring(std::to_string(selYear));
                    modeLine = L"YEAR -> MONTH -> RECORDS";
                    infoLines = {L"当前层级 : MONTH", L"年份节点 : " + utf8_to_wstring(std::to_string(selYear)), L"可用月份 : " + std::to_wstring(months.size())};
                } else {
                    for (const auto& item : items) {
                        if (item.year == selYear && item.month == selMonth) monthItems.push_back(item);
                    }
                    menuItems.push_back({L"--- 选择记录 ---", false});
                    for (const auto& item : monthItems) {
                        ParsedStamp stamp{};
                        parseStamp(item.timestamp, stamp);
                        std::wstring label = L"#" + std::to_wstring(item.number) + L"  " + utf8_to_wstring(std::to_string(stamp.day) + "日 " + item.timestamp.substr(11));
                        menuItems.push_back({label, true});
                    }
                    menuItems.push_back({L"0. 返回月份", true});
                    pathLine = formatMonthLine(selYear, selMonth);
                    modeLine = L"RECORDS -> DELETE ENABLED";
                    infoLines = {
                        L"当前层级 : RECORDS",
                        L"月份节点 : " + formatMonthLine(selYear, selMonth),
                        L"记录数量 : " + std::to_wstring(monthItems.size()),
                        L"选择后可删除该条触发记录。",
                    };
                }
            }

            updateCounterHistoryHeader(layout, pathLine, modeLine, infoLines);
            int choice = menuSelectScrollableInRegion(layout.listX, layout.listY, layout.listW, layout.listH, menuItems, 1);

            if (choice == MENU_RESIZE) {
                needResizeRebuild = true;
                break;
            }
            if (choice == MENU_ESC || choice == static_cast<int>(menuItems.size()) - 1) {
                if (stage == 0) return;
                --stage;
                continue;
            }

            if (stage == 0) {
                selYear = values[choice - 1];
                stage = 1;
            } else if (stage == 1) {
                selMonth = values[choice - 1];
                stage = 2;
            } else {
                int recordIdx = choice - 1;
                if (recordIdx < 0 || recordIdx >= static_cast<int>(monthItems.size())) continue;
                int confirm = showDeleteConfirmModal(monthItems[recordIdx]);
                if (confirm == MENU_RESIZE) {
                    break;
                }
                if (confirm != 0) continue;

                if (!store.removeCounterHistoryAt(monthItems[recordIdx].index)) continue;
                int newCount = store.getCounter() - 1;
                if (newCount < 0) newCount = 0;
                store.setCounter(newCount);
                store.save(diaryPath, password);
                showFullScreenMessage(L"RECORD DELETED", {L"[该条触发记录已删除，计数器已扣减]"});
                pauseScreen();
                break;
            }
        }

        if (needResizeRebuild) {
            layout = renderCounterHistoryFrame();
        }
    }
}
