#include "page_date.h"

#include "diary_editor.h"
#include "diary_store.h"
#include "page_status.h"
#include "time_utils.h"
#include "ui_render.h"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <windows.h>

namespace {

struct RegionMenuLayout {
    int menuX;
    int menuY;
    int menuW;
    int menuH;
};

struct DateSelectorLayout {
    int pathX;
    int pathY;
    int pathW;
    int modeX;
    int modeY;
    int modeW;
    int navX;
    int navY;
    int navW;
    int titleX;
    int titleW;
    int infoX;
    int infoY;
    int infoW;
    int infoH;
    RegionMenuLayout menu;
};

struct DateEntryPageLayout {
    int boxX;
    int boxY;
    int boxW;
    int boxH;
    int dateX;
    int dateY;
    int dateW;
    int countX;
    int countY;
    int countW;
    int modeX;
    int modeY;
    int modeW;
    int previewX;
    int previewY;
    int previewW;
    int previewH;
    int previewLabelX;
    int previewLabelW;
    int actionLabelX;
    int actionLabelW;
    int hintX;
    int hintW;
    int statusY;
    RegionMenuLayout menu;
};

std::wstring formatDateW(int year, int month, int day) {
    return utf8_to_wstring(std::to_string(year) + "/" + std::to_string(month) + "/" + std::to_string(day));
}

std::vector<std::wstring> buildEntryPreviewLines(const nlohmann::json& entry) {
    std::vector<std::wstring> lines;
    int y = entry.value("year", 0);
    int m = entry.value("month", 0);
    int d = entry.value("day", 0);

    lines.push_back(L"DATE : " + formatDateW(y, m, d));
    lines.push_back(L"");

    if (entry.contains("segments") && entry["segments"].is_array()) {
        const auto& segs = entry["segments"];
        for (size_t i = 0; i < segs.size(); ++i) {
            const auto& seg = segs[i];
            std::wstring timeLine = L"[" + std::to_wstring(i + 1) + L"] " + utf8_to_wstring(seg.value("time", "")) + L";";
            lines.push_back(timeLine);

            std::vector<std::wstring> contentLines = splitDisplayLines(utf8_to_wstring(seg.value("content", "")), L"  ");
            if (contentLines.empty()) contentLines.push_back(L"  (空内容)");
            for (const auto& line : contentLines) {
                lines.push_back(line.empty() ? L"  " : line);
            }
            if (i + 1 < segs.size()) lines.push_back(L"");
        }
    }

    return lines;
}

std::vector<std::wstring> buildEntryHistoryLines(const nlohmann::json& entry, int skipSegIdx = -1) {
    std::vector<std::wstring> historyLines;
    if (!entry.contains("segments") || !entry["segments"].is_array()) return historyLines;

    const auto& segs = entry["segments"];
    for (size_t i = 0; i < segs.size(); ++i) {
        if (static_cast<int>(i) == skipSegIdx) continue;

        const auto& seg = segs[i];
        historyLines.push_back(L"[" + utf8_to_wstring(seg.value("time", "")) + L"]");
        std::vector<std::wstring> contentLines = splitDisplayLines(utf8_to_wstring(seg.value("content", "")), L"  ");
        if (contentLines.empty()) contentLines.push_back(L"  ");
        for (const auto& line : contentLines) historyLines.push_back(line);
        historyLines.push_back(L"");
    }

    return historyLines;
}

DateSelectorLayout renderDateSelectorPageFrame(bool preserveOuterShell = false) {
    CenteredRect shell = drawTerminalShell(
        L"DATE.SELECTOR // LOCAL_DIARY_ENV", false, !preserveOuterShell);
    int boxX = shell.x;
    int boxY = shell.y;
    int boxW = shell.w;
    int boxH = shell.h;

    writeAtColor(boxX + 4, boxY + 4, L"[ DATE_SELECTOR ]", AMBER_DIM);
    fillLine(boxX + 4, boxY + 5, boxW - 8, L'─', AMBER_DIM);

    int panelX = boxX + 4;
    int panelW = boxW - 8;
    int bodyY = boxY + 11;
    int contentBottom = boxY + boxH - 5;
    int bodyH = contentBottom - bodyY + 1;
    if (bodyH < 8) bodyH = 8;

    int leftBoxW = std::max(28, std::min(36, panelW / 3));
    int rightBoxX = panelX + leftBoxW + 2;
    int rightBoxW = panelW - leftBoxW - 2;

    drawSingleBox(panelX, bodyY, leftBoxW, bodyH);
    drawSingleBox(rightBoxX, bodyY, rightBoxW, bodyH);
    writeAtColor(rightBoxX + 2, bodyY - 1, L"[ CONTEXT ]", AMBER_DIM);

    fillLine(boxX + 1, boxY + boxH - 3, boxW - 2, L' ', ATTR_NORMAL);
    fillLine(boxX + 1, boxY + boxH - 2, boxW - 2, L' ', ATTR_NORMAL);
    writeAtColor(boxX + 3, boxY + boxH - 2,
                 padOrTrimText(L">> DATE INDEX READY", boxW - 6), AMBER);

    DateSelectorLayout layout;
    layout.pathX = boxX + 4;
    layout.pathY = boxY + 7;
    layout.pathW = boxW - 8;
    layout.modeX = boxX + 4;
    layout.modeY = boxY + 8;
    layout.modeW = boxW - 8;
    layout.navX = boxX + 4;
    layout.navY = boxY + 9;
    layout.navW = boxW - 8;
    layout.titleX = panelX + 2;
    layout.titleW = leftBoxW - 4;
    layout.infoX = rightBoxX + 2;
    layout.infoY = bodyY + 1;
    layout.infoW = rightBoxW - 4;
    layout.infoH = bodyH - 2;
    layout.menu = {panelX + 2, bodyY + 1, leftBoxW - 4, bodyH - 2};
    return layout;
}

void updateDateSelectorPage(const DateSelectorLayout& layout,
                            const std::wstring& listTitle,
                            const std::wstring& pathLine,
                            const std::vector<std::wstring>& infoLines) {
    fillLine(layout.pathX, layout.pathY, layout.pathW, L' ', ATTR_NORMAL);
    fillLine(layout.modeX, layout.modeY, layout.modeW, L' ', ATTR_NORMAL);
    fillLine(layout.navX, layout.navY, layout.navW, L' ', ATTR_NORMAL);
    writeAtColor(layout.pathX, layout.pathY,
                 fitTextToWidth(L"PATH : " + pathLine, layout.pathW), AMBER);
    writeAtColor(layout.modeX, layout.modeY,
                 fitTextToWidth(L"MODE : YEAR -> MONTH -> DAY", layout.modeW), AMBER);

    fillLine(layout.titleX, layout.menu.menuY - 2, layout.titleW, L' ', ATTR_NORMAL);
    writeAtColor(layout.titleX, layout.menu.menuY - 2, fitTextToWidth(L"[ " + listTitle + L" ]", layout.titleW), AMBER_DIM);

    for (int row = 0; row < layout.menu.menuH; ++row) {
        fillLine(layout.menu.menuX, layout.menu.menuY + row, layout.menu.menuW, L' ', ATTR_NORMAL);
    }

    writeWrappedPanelLines(layout.infoX, layout.infoY, layout.infoW, layout.infoH, infoLines, AMBER);

    fillLine(layout.navX - 3, layout.menu.menuY + layout.menu.menuH + 1,
             layout.navW, L' ', ATTR_NORMAL);
    writeAtColor(layout.navX - 1, layout.menu.menuY + layout.menu.menuH + 1,
                 fitTextToWidth(L"Enter进入  Esc返回  PgUp/PgDn翻页  Home/End首尾", layout.navW - 2),
                 AMBER_DIM);
}

DateEntryPageLayout renderDateEntryPage(const nlohmann::json& entry,
                                        bool preserveOuterShell = false) {
    CenteredRect shell = drawTerminalShell(
        L"DATE.ENTRY // LOCAL_DIARY_ENV", false, !preserveOuterShell);
    int boxX = shell.x;
    int boxY = shell.y;
    int boxW = shell.w;
    int boxH = shell.h;

    writeAtColor(boxX + 4, boxY + 4, L"[ DATE_ENTRY ]", AMBER_DIM);
    fillLine(boxX + 4, boxY + 5, boxW - 8, L'─', AMBER_DIM);
    int panelX = boxX + 4;
    int panelW = boxW - 8;
    int previewBoxY = boxY + 11;
    int actionBoxH = 8;
    int contentBottom = boxY + boxH - 5;
    int actionBoxY = contentBottom - actionBoxH + 1;
    if (actionBoxY <= previewBoxY + 6) actionBoxY = previewBoxY + 7;
    int previewBoxH = actionBoxY - previewBoxY - 1;
    if (previewBoxH < 6) previewBoxH = 6;

    drawSingleBox(panelX, previewBoxY, panelW, previewBoxH);
    drawSingleBox(panelX, actionBoxY, panelW, actionBoxH);

    fillLine(boxX + 1, boxY + boxH - 3, boxW - 2, L' ', ATTR_NORMAL);
    fillLine(boxX + 1, boxY + boxH - 2, boxW - 2, L' ', ATTR_NORMAL);
    writeAtColor(boxX + 3, boxY + boxH - 2,
                 padOrTrimText(L">> ENTRY OPERATIONS READY", boxW - 6), AMBER);

    DateEntryPageLayout layout;
    layout.boxX = boxX;
    layout.boxY = boxY;
    layout.boxW = boxW;
    layout.boxH = boxH;
    layout.dateX = boxX + 4;
    layout.dateY = boxY + 7;
    layout.dateW = boxW - 8;
    layout.countX = boxX + 4;
    layout.countY = boxY + 8;
    layout.countW = boxW - 8;
    layout.modeX = boxX + 4;
    layout.modeY = boxY + 9;
    layout.modeW = boxW - 8;
    layout.previewX = panelX + 2;
    layout.previewY = previewBoxY + 1;
    layout.previewW = panelW - 4;
    layout.previewH = previewBoxH - 2;
    layout.previewLabelX = panelX + 2;
    layout.previewLabelW = panelW - 4;
    layout.actionLabelX = panelX + 2;
    layout.actionLabelW = panelW - 4;
    layout.hintX = boxX + 3;
    layout.hintW = boxW - 6;
    layout.statusY = boxY + boxH - 2;
    layout.menu = {panelX + 2, actionBoxY + 1, panelW - 4, actionBoxH - 2};
    return layout;
}

void updateDateEntryPageHeader(const DateEntryPageLayout& layout, const nlohmann::json& entry) {
    int y = entry.value("year", 0);
    int m = entry.value("month", 0);
    int d = entry.value("day", 0);
    int segCount = (entry.contains("segments") && entry["segments"].is_array())
        ? static_cast<int>(entry["segments"].size()) : 0;

    fillLine(layout.dateX, layout.dateY, layout.dateW, L' ', ATTR_NORMAL);
    fillLine(layout.countX, layout.countY, layout.countW, L' ', ATTR_NORMAL);
    fillLine(layout.modeX, layout.modeY, layout.modeW, L' ', ATTR_NORMAL);
    writeAtColor(layout.dateX, layout.dateY,
                 fitTextToWidth(L"DATE : " + formatDateW(y, m, d), layout.dateW), AMBER);
    writeAtColor(layout.countX, layout.countY,
                 fitTextToWidth(L"SEGMENTS : " + std::to_wstring(segCount), layout.countW), AMBER);
    writeAtColor(layout.modeX, layout.modeY,
                 fitTextToWidth(L"MODE : PREVIEW + EDIT OPERATIONS", layout.modeW), AMBER_DIM);

    fillLine(layout.previewLabelX, layout.previewY - 2, layout.previewLabelW, L' ', ATTR_NORMAL);
    fillLine(layout.actionLabelX, layout.menu.menuY - 2, layout.actionLabelW, L' ', ATTR_NORMAL);
    writeAtColor(layout.previewLabelX, layout.previewY - 2, fitTextToWidth(L"[ ENTRY_LOG ]", layout.previewLabelW), AMBER_DIM);
    writeAtColor(layout.actionLabelX, layout.menu.menuY - 2, fitTextToWidth(L"[ SEGMENT_ACTIONS ]", layout.actionLabelW), AMBER_DIM);

    fillLine(layout.hintX, layout.statusY - 1, layout.hintW, L' ', ATTR_NORMAL);
    writeAtColor(layout.hintX, layout.statusY - 1, fitTextToWidth(L"Enter执行  Esc返回  PgUp/PgDn翻页", layout.hintW), AMBER_DIM);
}

void updateDateEntryPageStatus(const DateEntryPageLayout& layout,
                               const std::wstring& text,
                               WORD attr = AMBER) {
    fillLine(layout.hintX, layout.statusY, layout.hintW, L' ', ATTR_NORMAL);
    writeAtColor(layout.hintX, layout.statusY,
                 padOrTrimText(text, layout.hintW), attr);
}

bool dateEntryViewportChanged(const DateEntryPageLayout& layout) {
    ConsoleViewport view = getConsoleViewport();
    return view.x != layout.boxX || view.y != layout.boxY ||
           view.w != layout.boxW || view.h != layout.boxH;
}

int pickEntrySegment(const nlohmann::json& entry,
                     const std::wstring& title,
                     const DateEntryPageLayout& layout) {
    std::vector<MenuItem> items;
    items.push_back({L"--- 选择记录 ---", false});
    const auto& segs = entry["segments"];
    for (size_t i = 0; i < segs.size(); ++i) {
        std::wstring label = L"记录 " + std::to_wstring(i + 1) + L"  [" +
                             utf8_to_wstring(segs[i].value("time", "")) + L"]";
        items.push_back({label, true});
    }
    items.push_back({L"0. 返回", true});

    int boxW = std::min(72, layout.boxW - 12);
    int boxH = std::min(layout.boxH - 8,
                        std::max(10, static_cast<int>(items.size()) + 5));
    int boxX = layout.boxX + (layout.boxW - boxW) / 2;
    int boxY = layout.boxY + (layout.boxH - boxH) / 2;

    fillRegion(boxX, boxY, boxW, boxH, L' ', ATTR_NORMAL);
    drawSingleBox(boxX, boxY, boxW, boxH);
    writeAtColor(boxX + 2, boxY, L"[ " + title + L" ]", AMBER_DIM);
    writeAtColor(boxX + 2, boxY + 1,
                 L"Enter选择  Esc返回  PgUp/PgDn翻页", AMBER_DIM);

    int choice = menuSelectScrollableInRegion(
        boxX + 2, boxY + 3, boxW - 4, boxH - 4, items, 1);
    if (choice == MENU_RESIZE) return MENU_RESIZE;
    if (choice == MENU_ESC || choice == static_cast<int>(items.size()) - 1) return -1;
    return choice - 1;
}

int confirmDateEntryAction(const DateEntryPageLayout& layout,
                           const std::wstring& title,
                           const std::vector<std::wstring>& detailLines) {
    int boxW = std::min(68, layout.boxW - 12);
    int boxH = std::max(10, static_cast<int>(detailLines.size()) + 8);
    boxH = std::min(boxH, layout.boxH - 8);
    int boxX = layout.boxX + (layout.boxW - boxW) / 2;
    int boxY = layout.boxY + (layout.boxH - boxH) / 2;

    fillRegion(boxX, boxY, boxW, boxH, L' ', ATTR_NORMAL);
    drawSingleBox(boxX, boxY, boxW, boxH);
    writeAtColor(boxX + 2, boxY, L"[ " + title + L" ]", AMBER_DIM);
    writeWrappedPanelLines(boxX + 2, boxY + 2, boxW - 4,
                           std::max(1, boxH - 7), detailLines, AMBER);

    std::vector<MenuItem> items = {
        {L"1. 确认删除", true},
        {L"0. 取消", true},
    };
    return menuSelectInRegion(boxX + 2, boxY + boxH - 3,
                              boxW - 4, 2, items, 0);
}

void editDateEntryPage(DiaryStore& store, const std::string& password, const char* diaryPath, size_t entryIdx) {
    DateEntryPageLayout layout = renderDateEntryPage(store.entries()[entryIdx], true);
    bool pageInvalidated = false;
    std::wstring statusText = L">> ENTRY OPERATIONS READY";
    WORD statusAttr = AMBER;

    while (entryIdx < store.entryCount()) {
        auto& entry = store.entries()[entryIdx];
        bool viewportChanged = dateEntryViewportChanged(layout);
        if (pageInvalidated || viewportChanged) {
            layout = renderDateEntryPage(entry, !viewportChanged);
            pageInvalidated = false;
        }

        updateDateEntryPageHeader(layout, entry);
        writeWrappedPanelLines(layout.previewX, layout.previewY, layout.previewW, layout.previewH,
                               buildEntryPreviewLines(entry), AMBER);
        updateDateEntryPageStatus(layout, statusText, statusAttr);
        statusText = L">> ENTRY OPERATIONS READY";
        statusAttr = AMBER;

        std::vector<MenuItem> opItems;
        opItems.push_back({L"--- 操作 ---", false});
        if (entry.contains("segments") && entry["segments"].is_array()) {
            const auto& segs = entry["segments"];
            for (size_t si = 0; si < segs.size(); ++si) {
                std::wstring label = L"编辑记录 " + std::to_wstring(si + 1) + L" (" + utf8_to_wstring(segs[si].value("time", "")) + L")";
                opItems.push_back({label, true});
            }
        }
        size_t segCount = entry.contains("segments") && entry["segments"].is_array()
            ? entry["segments"].size() : 0;
        opItems.push_back({L"添加新记录", true});
        opItems.push_back({L"删除某条记录", true});
        opItems.push_back({L"删除整篇日记", true});
        opItems.push_back({L"0. 返回日期索引", true});

        int opChoice = menuSelectScrollableInRegion(
            layout.menu.menuX, layout.menu.menuY,
            layout.menu.menuW, layout.menu.menuH,
            opItems, segCount > 0 ? 1 : static_cast<int>(segCount + 1));

        if (opChoice == MENU_RESIZE) {
            pageInvalidated = true;
            continue;
        }
        if (opChoice == MENU_ESC || opChoice == static_cast<int>(opItems.size()) - 1) return;

        if (opChoice >= 1 && opChoice <= static_cast<int>(segCount)) {
            int segIdx = opChoice - 1;
            std::wstring oldW = utf8_to_wstring(entry["segments"][segIdx].value("content", ""));

            EditorScreenConfig cfg;
            cfg.panelTitle = L"EDIT DATE SEGMENT";
            cfg.dateLine = L"DATE : " + formatDateW(entry.value("year", 0), entry.value("month", 0), entry.value("day", 0));
            cfg.timeLine = L"SLOT : " + utf8_to_wstring(entry["segments"][segIdx].value("time", "")) + L";";
            cfg.modeLine = L"MODE : UPDATE EXISTING SEGMENT";
            cfg.historyLines = buildEntryHistoryLines(entry, segIdx);
            cfg.adaptiveHistory = true;
            cfg.emptyHistoryText = L"NO OTHER SEGMENTS FOR THIS DAY.";
            cfg.minEditInnerH = 5;

            EditorResult er = openDiaryEditor(oldW, cfg);
            if (er.confirmed) {
                entry["segments"][segIdx]["content"] = wstring_to_utf8(er.content);
                store.save(diaryPath, password);
                statusText = L">> SEGMENT UPDATED";
            } else {
                statusText = L">> EDIT CANCELLED";
                statusAttr = AMBER_DIM;
            }
            pageInvalidated = true;
            continue;
        }

        if (opChoice == static_cast<int>(segCount) + 1) {
            std::string newTime = getCurrentTimeStr();

            EditorScreenConfig cfg;
            cfg.panelTitle = L"ADD DATE SEGMENT";
            cfg.dateLine = L"DATE : " + formatDateW(entry.value("year", 0), entry.value("month", 0), entry.value("day", 0));
            cfg.timeLine = L"SLOT : " + utf8_to_wstring(newTime) + L";";
            cfg.modeLine = L"MODE : APPEND NEW SEGMENT";
            cfg.historyLines = buildEntryHistoryLines(entry);
            cfg.adaptiveHistory = true;
            cfg.emptyHistoryText = L"DAY LOG EMPTY. START WRITING.";
            cfg.minEditInnerH = 5;

            EditorResult er = openDiaryEditor(L"", cfg);
            if (er.confirmed) {
                std::wstring trimmed = er.content;
                while (!trimmed.empty() && (trimmed.back() == L'\n' || trimmed.back() == L'\r' || trimmed.back() == L' ')) {
                    trimmed.pop_back();
                }

                if (!trimmed.empty()) {
                    nlohmann::json newSeg;
                    newSeg["time"] = newTime;
                    newSeg["content"] = wstring_to_utf8(er.content);
                    entry["segments"].push_back(newSeg);
                    store.save(diaryPath, password);
                    statusText = L">> SEGMENT ADDED";
                } else {
                    statusText = L">> ADD CANCELLED: EMPTY INPUT";
                    statusAttr = AMBER_DIM;
                }
            } else {
                statusText = L">> ADD CANCELLED";
                statusAttr = AMBER_DIM;
            }
            pageInvalidated = true;
            continue;
        }

        if (opChoice == static_cast<int>(segCount) + 2) {
            if (segCount == 0) {
                statusText = L">> DELETE SKIPPED: NO SEGMENTS";
                statusAttr = AMBER_DIM;
                continue;
            }

            int segIdx = pickEntrySegment(entry, L"DELETE SEGMENT", layout);
            pageInvalidated = true;
            if (segIdx == MENU_RESIZE) continue;
            if (segIdx < 0) {
                statusText = L">> DELETE CANCELLED";
                statusAttr = AMBER_DIM;
                continue;
            }

            const auto& seg = entry["segments"][segIdx];
            int confirmChoice = confirmDateEntryAction(
                layout,
                L"CONFIRM DELETE",
                {
                    L"记录 " + std::to_wstring(segIdx + 1) + L"  [" +
                        utf8_to_wstring(seg.value("time", "")) + L"]",
                    L"删除后无法恢复。",
                });
            pageInvalidated = true;
            if (confirmChoice != 0) {
                statusText = L">> DELETE CANCELLED";
                statusAttr = AMBER_DIM;
                continue;
            }

            entry["segments"].erase(segIdx);
            if (entry["segments"].empty()) {
                store.removeEntry(entryIdx);
                store.save(diaryPath, password);
                return;
            }
            store.save(diaryPath, password);
            statusText = L">> SEGMENT DELETED";
            continue;
        }

        if (opChoice == static_cast<int>(segCount) + 3) {
            int confirmChoice = confirmDateEntryAction(
                layout,
                L"CONFIRM ENTRY DELETE",
                {
                    L"即将删除 " +
                        formatDateW(entry.value("year", 0),
                                    entry.value("month", 0),
                                    entry.value("day", 0)) +
                        L" 的整篇日记。",
                    L"全部分段都会被删除，且无法恢复。",
                });
            pageInvalidated = true;
            if (confirmChoice != 0) {
                statusText = L">> ENTRY DELETE CANCELLED";
                statusAttr = AMBER_DIM;
                continue;
            }

            store.removeEntry(entryIdx);
            store.save(diaryPath, password);
            return;
        }
    }
}

} // namespace

void editByDate(DiaryStore& store, const std::string& password, const char* diaryPath) {
    int stage = 0;
    int selYear = 0;
    int selMonth = 0;
    DateSelectorLayout selectorLayout = renderDateSelectorPageFrame();

    while (true) {
        auto sorted = store.getSortedIndices();
        if (sorted.empty()) {
            showFullScreenMessage(L"DATE INDEX EMPTY", {L"[当前没有可编辑的日记内容]"});
            pauseScreen();
            return;
        }

        std::vector<MenuItem> items;
        std::vector<int> values;
        std::vector<size_t> entryIndices;
        std::wstring listTitle;
        std::wstring pathLine = L"ROOT";
        std::vector<std::wstring> infoLines;

        if (stage == 0) {
            std::vector<int> years;
            for (size_t idx : sorted) {
                int y = store.entries()[idx].value("year", 0);
                if (std::find(years.begin(), years.end(), y) == years.end()) years.push_back(y);
            }
            std::sort(years.begin(), years.end());

            listTitle = L"YEAR INDEX";
            items.push_back({L"--- 选择年份 ---", false});
            for (int y : years) {
                items.push_back({utf8_to_wstring(std::to_string(y) + " 年"), true});
                values.push_back(y);
            }
            items.push_back({L"0. 返回主菜单", true});

            infoLines = {
                L"当前层级 : YEAR",
                L"收录年份 : " + std::to_wstring(years.size()),
                L"总日记数 : " + std::to_wstring(sorted.size()),
                L"",
                L"先定位年份，再继续进入月份与具体日期。",
            };
        } else if (stage == 1) {
            pathLine = utf8_to_wstring(std::to_string(selYear));
            std::vector<int> months;
            for (size_t idx : sorted) {
                const auto& e = store.entries()[idx];
                if (e.value("year", 0) == selYear) {
                    int m = e.value("month", 0);
                    if (std::find(months.begin(), months.end(), m) == months.end()) months.push_back(m);
                }
            }
            std::sort(months.begin(), months.end());
            if (months.empty()) {
                stage = 0;
                continue;
            }

            listTitle = L"MONTH INDEX";
            items.push_back({L"--- 选择月份 ---", false});
            for (int m : months) {
                items.push_back({utf8_to_wstring(std::to_string(m) + " 月"), true});
                values.push_back(m);
            }
            items.push_back({L"0. 返回年份", true});

            infoLines = {
                L"当前层级 : MONTH",
                L"年份节点 : " + utf8_to_wstring(std::to_string(selYear)),
                L"月份数量 : " + std::to_wstring(months.size()),
                L"",
                L"这个层级用来定位某一年中的具体月份。",
            };
        } else {
            pathLine = utf8_to_wstring(std::to_string(selYear) + " / " + std::to_string(selMonth));
            std::vector<int> days;
            for (size_t idx : sorted) {
                const auto& e = store.entries()[idx];
                if (e.value("year", 0) == selYear && e.value("month", 0) == selMonth) {
                    days.push_back(e.value("day", 0));
                    entryIndices.push_back(idx);
                }
            }
            if (days.empty()) {
                stage = 1;
                continue;
            }

            listTitle = L"DAY INDEX";
            items.push_back({L"--- 选择日期 ---", false});
            for (size_t i = 0; i < days.size(); ++i) {
                items.push_back({utf8_to_wstring(std::to_string(days[i]) + " 日"), true});
                values.push_back(days[i]);
            }
            items.push_back({L"0. 返回月份", true});

            infoLines = {
                L"当前层级 : DAY",
                L"月份节点 : " + utf8_to_wstring(std::to_string(selYear) + "/" + std::to_string(selMonth)),
                L"可编辑日期 : " + std::to_wstring(days.size()),
                L"",
                L"进入某一天后，可直接查看预览并编辑各段记录。",
            };
        }

        updateDateSelectorPage(selectorLayout, listTitle, pathLine, infoLines);
        int choice = menuSelectScrollableInRegion(
            selectorLayout.menu.menuX, selectorLayout.menu.menuY,
            selectorLayout.menu.menuW, selectorLayout.menu.menuH,
            items, 1);

        if (choice == MENU_RESIZE) {
            selectorLayout = renderDateSelectorPageFrame();
            continue;
        }
        if (choice == MENU_ESC) {
            if (stage == 0) return;
            stage--;
            continue;
        }

        if (choice == static_cast<int>(items.size()) - 1) {
            if (stage == 0) return;
            stage--;
            continue;
        }

        if (stage == 0) {
            selYear = values[choice - 1];
            stage = 1;
        } else if (stage == 1) {
            selMonth = values[choice - 1];
            stage = 2;
        } else {
            size_t entryIdx = entryIndices[choice - 1];
            editDateEntryPage(store, password, diaryPath, entryIdx);
            selectorLayout = renderDateSelectorPageFrame(true);
        }
    }
}
