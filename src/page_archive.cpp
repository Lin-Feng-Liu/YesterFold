#include "page_archive.h"

#include "diary_store.h"
#include "ui_render.h"

#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

void viewAllDiaries(const DiaryStore& store) {
    auto sorted = store.getSortedIndices();

    std::vector<std::wstring> logicalLines;
    logicalLines.reserve(sorted.size() * 8 + 4);

    if (sorted.empty()) {
        logicalLines.push_back(L"NO_DIARY_ENTRIES_FOUND.");
        logicalLines.push_back(L"");
        logicalLines.push_back(L"这个档案库现在还是空的。按 Esc 返回。");
    } else {
        for (size_t i = 0; i < sorted.size(); ++i) {
            const auto& entry = store.entries()[sorted[i]];
            int y = entry.value("year", 0);
            int m = entry.value("month", 0);
            int d = entry.value("day", 0);

            logicalLines.push_back(L"————————————————————————");
            logicalLines.push_back(utf8_to_wstring(std::to_string(y) + "/" + std::to_string(m) + "/" + std::to_string(d)));
            logicalLines.push_back(L"");

            if (entry.contains("segments") && entry["segments"].is_array()) {
                for (const auto& seg : entry["segments"]) {
                    std::string time = seg.value("time", "");
                    std::string content = seg.value("content", "");
                    logicalLines.push_back(utf8_to_wstring(time + ";"));

                    std::vector<std::wstring> contentLines = splitDisplayLines(utf8_to_wstring(content));
                    if (contentLines.empty()) contentLines.push_back(L"");
                    logicalLines.insert(logicalLines.end(), contentLines.begin(), contentLines.end());
                    logicalLines.push_back(L"");
                }
            }
        }
    }

    struct ArchiveLayout {
        int bodyX;
        int bodyY;
        int bodyW;
        int bodyH;
        int infoY;
        int promptY;
        int statusY;
    };

    DWORD oldInMode = 0;
    GetConsoleMode(g_hIn, &oldInMode);
    SetConsoleMode(g_hIn, ENABLE_EXTENDED_FLAGS | ENABLE_WINDOW_INPUT);

    CONSOLE_CURSOR_INFO oldCursorInfo{};
    GetConsoleCursorInfo(g_hOut, &oldCursorInfo);
    CONSOLE_CURSOR_INFO hiddenCursor = oldCursorInfo;
    hiddenCursor.bVisible = FALSE;
    SetConsoleCursorInfo(g_hOut, &hiddenCursor);

    int screenW = 0;
    int screenH = 0;
    int scroll = 0;
    ArchiveLayout layout{};
    std::vector<std::wstring> wrappedLines;
    std::vector<RenderedLine> prevBodyFrame;
    bool bodyFrameInit = false;
    std::wstring searchQuery;
    std::wstring searchDraft;
    std::vector<int> matches;
    int activeMatch = -1;
    bool searchMode = false;
    bool searchDraftSelected = false;

    auto toSearchKey = [](const std::wstring& text) {
        std::wstring key;
        key.reserve(text.size());
        for (wchar_t ch : text) {
            key.push_back(static_cast<wchar_t>(std::towlower(ch)));
        }
        return key;
    };

    auto rebuildMatches = [&]() {
        matches.clear();
        if (searchQuery.empty()) {
            activeMatch = -1;
            return;
        }

        std::wstring needle = toSearchKey(searchQuery);
        for (int i = 0; i < static_cast<int>(wrappedLines.size()); ++i) {
            if (toSearchKey(wrappedLines[i]).find(needle) != std::wstring::npos) {
                matches.push_back(i);
            }
        }

        if (matches.empty()) {
            activeMatch = -1;
        } else if (activeMatch < 0) {
            activeMatch = 0;
        } else if (activeMatch >= static_cast<int>(matches.size())) {
            activeMatch = static_cast<int>(matches.size()) - 1;
        }
    };

    auto centerOnActiveMatch = [&]() {
        if (activeMatch < 0 || activeMatch >= static_cast<int>(matches.size())) return;
        int targetLine = matches[activeMatch];
        int total = static_cast<int>(wrappedLines.size());
        int maxScroll = std::max(0, total - layout.bodyH);
        int desired = targetLine - std::max(0, layout.bodyH / 2);
        if (desired < 0) desired = 0;
        if (desired > maxScroll) desired = maxScroll;
        scroll = desired;
    };

    auto jumpToMatch = [&](int delta) {
        if (searchQuery.empty()) return false;
        rebuildMatches();
        if (matches.empty()) return false;

        if (activeMatch < 0 || activeMatch >= static_cast<int>(matches.size())) {
            activeMatch = 0;
        } else if (delta != 0) {
            int count = static_cast<int>(matches.size());
            activeMatch = (activeMatch + delta) % count;
            if (activeMatch < 0) activeMatch += count;
        }

        centerOnActiveMatch();
        return true;
    };

    auto paintRenderedFrame = [&](int x, int y, int width,
                                  const std::vector<RenderedLine>& nextFrame,
                                  std::vector<RenderedLine>& prevFrame,
                                  bool& initialized) {
        if (!initialized || prevFrame.size() != nextFrame.size()) {
            prevFrame.assign(nextFrame.size(), RenderedLine{});
            initialized = false;
        }

        for (size_t i = 0; i < nextFrame.size(); ++i) {
            if (!initialized || prevFrame[i].text != nextFrame[i].text || prevFrame[i].attr != nextFrame[i].attr) {
                fillLine(x, y + static_cast<int>(i), width, L' ', ATTR_NORMAL);
                writeAtColor(x, y + static_cast<int>(i), padOrTrimText(nextFrame[i].text, width), nextFrame[i].attr);
            }
        }

        prevFrame = nextFrame;
        initialized = true;
    };

    auto rebuildWrappedLines = [&]() {
        wrappedLines.clear();
        int wrapW = getAdaptiveWrapWidth(layout.bodyW);
        for (const auto& raw : logicalLines) {
            std::vector<std::wstring> wrapped = wrapDisplayText(raw, wrapW);
            if (wrapped.empty()) wrapped.push_back(L"");
            wrappedLines.insert(wrappedLines.end(), wrapped.begin(), wrapped.end());
        }
        if (wrappedLines.empty()) wrappedLines.push_back(L"");
        rebuildMatches();
    };

    auto renderShell = [&]() {
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        GetConsoleScreenBufferInfo(g_hOut, &csbi);
        screenW = csbi.dwSize.X;
        screenH = csbi.dwSize.Y;
        if (screenW < 20) screenW = 80;

        drawTerminalShell(L"ARCHIVE.READER // LOCAL_DIARY_ENV");

        int boxW = csbi.dwSize.X;
        int boxH = csbi.dwSize.Y;
        if (boxW < 88) boxW = 88;

        writeAtColor(4, 4, L"[ VIEW ALL ENTRIES ]", AMBER_DIM);
        fillLine(4, 5, boxW - 8, L'─', AMBER_DIM);
        writeAtColor(4, 7, L"TOTAL : " + std::to_wstring(sorted.size()) + L" ENTRIES", AMBER);
        writeAtColor(4, 8, L"MODE  : ARCHIVE READER", AMBER);

        int panelX = 4;
        int panelW = boxW - 8;
        int bodyBoxY = 11;
        int contentBottom = boxH - 6;
        int bodyBoxH = contentBottom - bodyBoxY + 1;
        if (bodyBoxH < 6) bodyBoxH = 6;

        drawSingleBox(panelX, bodyBoxY, panelW, bodyBoxH);
        writeAtColor(panelX + 2, bodyBoxY - 1, L"[ DIARY_ARCHIVE ]", AMBER_DIM);

        layout.bodyX = panelX + 2;
        layout.bodyY = bodyBoxY + 1;
        layout.bodyW = panelW - 4;
        layout.bodyH = bodyBoxH - 2;
        layout.infoY = boxH - 4;
        layout.promptY = boxH - 3;
        layout.statusY = boxH - 2;

        prevBodyFrame.assign(layout.bodyH, RenderedLine{});
        bodyFrameInit = false;
        rebuildWrappedLines();
    };

    auto renderArchive = [&]() {
        int total = static_cast<int>(wrappedLines.size());
        int maxScroll = std::max(0, total - layout.bodyH);
        if (scroll < 0) scroll = 0;
        if (scroll > maxScroll) scroll = maxScroll;

        std::vector<RenderedLine> nextFrame(layout.bodyH);
        for (int i = 0; i < layout.bodyH; ++i) {
            int lineIdx = scroll + i;
            if (lineIdx >= total) continue;

            const std::wstring& line = wrappedLines[lineIdx];
            WORD attr = AMBER;
            if (!line.empty() && line[0] == L'—') {
                attr = AMBER_DIM;
            } else if (std::count(line.begin(), line.end(), L'/') == 2 && line.find(L' ') == std::wstring::npos) {
                attr = 0x0F;
            }
            if (activeMatch >= 0 &&
                activeMatch < static_cast<int>(matches.size()) &&
                lineIdx == matches[activeMatch]) {
                attr = 0x70;
            }

            nextFrame[i].text = line;
            nextFrame[i].attr = attr;
        }

        paintRenderedFrame(layout.bodyX, layout.bodyY, layout.bodyW,
                           nextFrame, prevBodyFrame, bodyFrameInit);

        int visibleEnd = std::min(total, scroll + layout.bodyH);
        std::wstring info = L"LINES " + std::to_wstring(total == 0 ? 0 : scroll + 1) +
                            L"-" + std::to_wstring(visibleEnd) +
                            L" / " + std::to_wstring(total) +
                            L"    ENTRIES " + std::to_wstring(sorted.size());
        if (!searchQuery.empty()) {
            if (matches.empty()) {
                info += L"    NO MATCH: \"" + searchQuery + L"\"";
            } else {
                info += L"    SEARCH \"" + searchQuery + L"\"  " +
                        std::to_wstring(activeMatch + 1) + L" / " +
                        std::to_wstring(matches.size());
            }
        }
        std::wstring controls = L"Ctrl+F搜索  Ctrl+A上一条  Ctrl+E下一条  PgUp/PgDn翻页  Up/Down滚动  Home/End首尾  Esc返回";
        fillLine(1, layout.infoY, screenW - 2, L' ', ATTR_NORMAL);
        fillLine(1, layout.promptY, screenW - 2, L' ', ATTR_NORMAL);
        fillLine(1, layout.statusY, screenW - 2, L' ', ATTR_NORMAL);
        writeAtColor(3, layout.infoY, fitTextToWidth(info, screenW - 6), AMBER_DIM);
        writeAtColor(3, layout.promptY, fitTextToWidth(controls, screenW - 6), AMBER_DIM);
        if (searchMode) {
            writeAtColor(3, layout.statusY, fitTextToWidth(L"SEARCH: " + searchDraft, screenW - 6), 0x70);
        } else {
            writeAtColor(3, layout.statusY, L">> ARCHIVE READY", AMBER);
        }
    };

    renderShell();
    scroll = std::max(0, static_cast<int>(wrappedLines.size()) - layout.bodyH);
    renderArchive();

    while (true) {
        if (!waitForConsoleInputOrResize(g_hIn, screenW, screenH)) {
            int oldActiveMatch = activeMatch;
            renderShell();
            if (!searchQuery.empty()) {
                rebuildMatches();
                if (matches.empty()) {
                    activeMatch = -1;
                } else if (oldActiveMatch < 0) {
                    activeMatch = 0;
                } else if (oldActiveMatch >= static_cast<int>(matches.size())) {
                    activeMatch = static_cast<int>(matches.size()) - 1;
                } else {
                    activeMatch = oldActiveMatch;
                }
                centerOnActiveMatch();
            }
            renderArchive();
            continue;
        }

        INPUT_RECORD ir{};
        DWORD read = 0;
        if (!ReadConsoleInputW(g_hIn, &ir, 1, &read) || read == 0) continue;
        if (ir.EventType != KEY_EVENT || !ir.Event.KeyEvent.bKeyDown) continue;

        WORD vk = ir.Event.KeyEvent.wVirtualKeyCode;
        DWORD ctrl = ir.Event.KeyEvent.dwControlKeyState;
        WCHAR ch = ir.Event.KeyEvent.uChar.UnicodeChar;
        bool changed = false;

        bool ctrlPressed = (ctrl & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
        if (searchMode) {
            if (vk == VK_ESCAPE) {
                searchMode = false;
                searchDraft = searchQuery;
                searchDraftSelected = false;
                changed = true;
            } else if (vk == VK_BACK) {
                if (searchDraftSelected) {
                    searchDraft.clear();
                    searchDraftSelected = false;
                    changed = true;
                    if (changed) renderArchive();
                    continue;
                }
                if (!searchDraft.empty()) {
                    searchDraft.pop_back();
                }
                changed = true;
            } else if (vk == VK_RETURN) {
                if (searchDraft.empty()) {
                    searchQuery.clear();
                    matches.clear();
                    activeMatch = -1;
                } else if (searchDraft == searchQuery && !matches.empty()) {
                    jumpToMatch(1);
                } else {
                    searchQuery = searchDraft;
                    activeMatch = -1;
                    jumpToMatch(0);
                }
                searchMode = false;
                searchDraftSelected = false;
                changed = true;
            } else if (ch >= L' ') {
                if (searchDraftSelected) {
                    searchDraft.clear();
                    searchDraftSelected = false;
                }
                searchDraft.push_back(ch);
                changed = true;
            }

            if (changed) renderArchive();
            continue;
        }

        if (ctrlPressed && vk == L'F') {
            searchMode = true;
            searchDraft = searchQuery;
            searchDraftSelected = !searchDraft.empty();
            renderArchive();
            continue;
        }

        if (vk == VK_RETURN && !searchQuery.empty()) {
            changed = jumpToMatch(1);
            if (changed) renderArchive();
            continue;
        }

        if (ctrlPressed && vk == L'A') {
            changed = jumpToMatch(-1);
            if (changed) renderArchive();
            continue;
        }

        if (ctrlPressed && vk == L'E') {
            changed = jumpToMatch(1);
            if (changed) renderArchive();
            continue;
        }

        if (vk == VK_ESCAPE) {
            break;
        }
        if (vk == VK_UP) {
            scroll--;
            changed = true;
        } else if (vk == VK_DOWN) {
            scroll++;
            changed = true;
        } else if (vk == VK_PRIOR) {
            scroll -= std::max(1, layout.bodyH - 2);
            changed = true;
        } else if (vk == VK_NEXT) {
            scroll += std::max(1, layout.bodyH - 2);
            changed = true;
        } else if (vk == VK_HOME) {
            scroll = 0;
            changed = true;
        } else if (vk == VK_END) {
            scroll = static_cast<int>(wrappedLines.size());
            changed = true;
        }

        if (changed) renderArchive();
    }

    SetConsoleCursorInfo(g_hOut, &oldCursorInfo);
    SetConsoleMode(g_hIn, oldInMode);
}
