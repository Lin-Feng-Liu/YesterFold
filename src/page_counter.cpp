#include "page_counter.h"

#include "diary_store.h"
#include "page_status.h"
#include "ui_render.h"

#include <algorithm>
#include <iostream>
#include <windows.h>

namespace {

std::string getCurrentTimestampStr() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d/%02d/%02d %02d:%02d:%02d",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return std::string(buf);
}

InputResult readLineCancelable(const std::string& prompt, bool allowEmpty = false) {
    DWORD oldMode;
    GetConsoleMode(g_hIn, &oldMode);
    while (true) {
        std::cout << prompt;
        std::cout.flush();
        SetConsoleMode(g_hIn, ENABLE_EXTENDED_FLAGS | ENABLE_WINDOW_INPUT);
        std::wstring line;
        while (true) {
            INPUT_RECORD ir;
            DWORD read;
            if (!ReadConsoleInputW(g_hIn, &ir, 1, &read) || read == 0) continue;
            if (ir.EventType != KEY_EVENT || !ir.Event.KeyEvent.bKeyDown) continue;
            WORD vk = ir.Event.KeyEvent.wVirtualKeyCode;
            WCHAR ch = ir.Event.KeyEvent.uChar.UnicodeChar;
            if (vk == VK_RETURN) break;
            if (vk == VK_ESCAPE) {
                SetConsoleMode(g_hIn, oldMode);
                std::cout << std::endl;
                return {true, "", false};
            }
            if (vk == VK_BACK) {
                if (!line.empty()) {
                    line.pop_back();
                    std::cout << "\b \b";
                }
            } else if (ch >= L' ') {
                line.push_back(ch);
                std::wcout << ch;
            }
            std::cout.flush();
        }
        std::cout << std::endl;
        SetConsoleMode(g_hIn, oldMode);
        std::string result = wstring_to_utf8(line);
        if (!result.empty() || allowEmpty) return {false, result, false};
        std::cout << "输入不能为空" << std::endl;
    }
}

std::vector<std::wstring> buildCounterHistoryLines(const std::vector<std::string>& history,
                                                   int currentCount) {
    std::vector<std::wstring> lines;
    lines.push_back(L"最近触发记录");
    lines.push_back(L"────────────────────");

    if (history.empty()) {
        lines.push_back(L"还没有触发记录");
        return lines;
    }

    int shown = 0;
    for (auto it = history.rbegin(); it != history.rend() && shown < 5; ++it, ++shown) {
        int recordNumber = currentCount - shown;
        if (recordNumber < 0) break;
        std::wstring line = L"#" + std::to_wstring(recordNumber) + L" : " + utf8_to_wstring(*it);
        lines.push_back(line);
    }

    if (lines.empty()) lines.push_back(L"还没有可显示的记录");
    return lines;
}

}

void counterPage(DiaryStore& store, const std::string& password, const char* diaryPath) {
    while (true) {
        int count = store.getCounter();
        std::vector<std::string> history = store.getCounterHistory();

        CenteredRect shell = drawTerminalShell(L"COUNTER.NODE // LOCAL_DIARY_ENV", true);
        int leftX = shell.x + 6;
        int topY = shell.y + 5;
        int leftW = 48;
        int rightX = leftX + leftW + 4;
        int rightW = shell.x + shell.w - rightX - 6;
        int heroH = 13;
        int menuY = topY + heroH + 1;
        int menuH = shell.y + shell.h - menuY - 4;
        if (menuH < 6) menuH = 6;

        drawSingleBox(leftX, topY, leftW, heroH);
        drawSingleBox(rightX, topY, rightW, heroH);
        drawSingleBox(leftX, menuY, shell.w - 12, menuH);

        writeAtColor(leftX + 2, topY - 1, L"[ MYSTERY_COUNTER ]", AMBER_DIM);
        writeAtColor(rightX + 2, topY - 1, L"[ RECENT_PULSES ]", AMBER_DIM);
        writeAtColor(leftX + 2, menuY - 1, L"[ OPERATIONS ]", AMBER_DIM);

        writeAtColor(leftX + 2, topY + 2, L"当前计数", AMBER_DIM);
        std::wstring countText = std::to_wstring(count);
        int countX = leftX + std::max(2, (leftW - static_cast<int>(countText.size())) / 2);
        writeAtColor(countX, topY + 6, countText, 0x0F);

        std::vector<std::wstring> infoLines = buildCounterHistoryLines(history, count);
        writeWrappedPanelLines(rightX + 2, topY + 1, rightW - 4, heroH - 2, infoLines, AMBER);

        fillLine(shell.x + 1, shell.y + shell.h - 3, shell.w - 2, L' ', ATTR_NORMAL);
        fillLine(shell.x + 1, shell.y + shell.h - 2, shell.w - 2, L' ', ATTR_NORMAL);
        writeAtColor(shell.x + 2, shell.y + shell.h - 3, L"Enter执行  Esc返回", AMBER_DIM);
        writeAtColor(shell.x + 2, shell.y + shell.h - 2, padOrTrimText(L">> COUNTER READY", shell.w - 6), AMBER);

        std::vector<MenuItem> items = {
            {L"1. 次数加一", true},
            {L"2. 手动设置次数", true},
            {L"0. 返回", true},
        };

        int choice = menuSelectInRegion(leftX + 2, menuY + 1, shell.w - 16, menuH - 2, items, 0);

        if (choice == MENU_ESC || choice == 2) break;
        if (choice == MENU_RESIZE) continue;

        if (choice == 0) {
            store.setCounter(count + 1);
            store.pushCounterHistory(getCurrentTimestampStr());
            store.save(diaryPath, password);
        } else if (choice == 1) {
            auto scRes = readLineCancelable("输入新次数 (非负整数, Esc 取消): ");
            if (scRes.cancelled || scRes.value.empty()) continue;

            int newVal;
            try {
                newVal = std::stoi(scRes.value);
            } catch (...) {
                wprintln(L"输入无效，请输入整数");
                pauseScreen();
                continue;
            }

            if (newVal < 0) {
                wprintln(L"次数不能为负数");
                pauseScreen();
                continue;
            }

            if (newVal < count) {
                size_t removeCount = static_cast<size_t>(count - newVal);
                if (removeCount > history.size()) removeCount = history.size();
                store.trimCounterHistoryTail(removeCount);
            }

            store.setCounter(newVal);
            store.pushCounterHistory(getCurrentTimestampStr());
            store.save(diaryPath, password);
            showFullScreenMessage(L"COUNTER UPDATED", {L"[计数器已更新]"});
            pauseScreen();
        }
    }
}
