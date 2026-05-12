#include <algorithm>
#include <iostream>
#include <string>
#include <vector>
#include <limits>
#include <fstream>
#include <sstream>
#include <windows.h>
#include <conio.h>
#include <sodium.h>
#include "crypto.h"
#include "diary_store.h"
#include "ui_render.h"
#include "metrics.h"
#include "page_main.h"

static const char* DIARY_PATH       = "data\\diary.enc";
static const char* EXPORT_TXT_PATH  = "data\\export_diary.txt";
static const char* IMPORT_TXT_PATH  = "data\\import_diary.txt";

static const int MODE_EXIT   = 0;
static const int MODE_SWITCH = 1;

constexpr WORD ACCESS_WARN_ATTR   = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
constexpr WORD ACCESS_DANGER_ATTR = FOREGROUND_RED | FOREGROUND_INTENSITY;

static void pauseScreen() {
    FlushConsoleInputBuffer(g_hIn);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(g_hOut, &csbi);
    int pauseY = std::max(1, static_cast<int>(csbi.dwSize.Y) - 2);
    fillLine(1, pauseY, csbi.dwSize.X - 2, L' ', ATTR_NORMAL);
    writeAtColor(3, pauseY, L"按任意键继续...", AMBER);
    DWORD oldMode; GetConsoleMode(g_hIn, &oldMode);
    SetConsoleMode(g_hIn, ENABLE_EXTENDED_FLAGS | ENABLE_WINDOW_INPUT);
    while (true) {
        INPUT_RECORD ir; DWORD read;
        if (!ReadConsoleInputW(g_hIn, &ir, 1, &read) || read == 0) continue;
        if (ir.EventType == KEY_EVENT && ir.Event.KeyEvent.bKeyDown) break;
    }
    SetConsoleMode(g_hIn, oldMode);
    fillLine(1, pauseY, csbi.dwSize.X - 2, L' ', ATTR_NORMAL);
}

static std::string readPassword(const std::string& prompt);

static std::string readLine(const std::string& prompt, bool allowEmpty = false) {
    std::string input;
    while (true) {
        std::cout << prompt; std::cout.flush(); std::getline(std::cin, input);
        if (!input.empty() || allowEmpty) break;
        std::cout << "输入不能为空" << std::endl;
    }
    return input;
}

// ─── 可取消输入（支持 Esc 返回） ───

struct InputResult {
    bool cancelled;
    std::string value;
    bool resized;
};

struct AccessPageLayout {
    int fieldX;
    int fieldY;
    int fieldW;
};

struct EditorScreenConfig {
    std::wstring screenLabel = L"WRITE.BUFFER // LOCAL_DIARY_ENV";
    std::wstring panelTitle = L"WRITE / EDIT";
    std::wstring dateLine = L"DATE : --/--/--";
    std::wstring timeLine = L"SLOT : --:--";
    std::wstring modeLine = L"MODE : EDIT";
    std::vector<std::wstring> historyLines;
    std::wstring emptyHistoryText = L"NO PRIOR SEGMENTS.";
    bool adaptiveHistory = false;
    bool projectBufferToHistory = false;
    std::wstring livePreviewTime = L"";
    int minEditInnerH = 8;
};

struct EditorShellLayout {
    int historyX;
    int historyY;
    int historyW;
    int historyH;
    int editX;
    int editY;
    int editW;
    int editH;
    int statusY;
    int contentBottom;
    std::vector<std::wstring> wrappedHistoryLines;
};

struct RegionMenuLayout {
    int menuX;
    int menuY;
    int menuW;
    int menuH;
};

struct DateSelectorLayout {
    int pathX;
    int pathW;
    int modeX;
    int modeW;
    int navX;
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
    int previewX;
    int previewY;
    int previewW;
    int previewH;
    int statusY;
    RegionMenuLayout menu;
};

struct RenderedLine {
    std::wstring text;
    WORD attr = ATTR_NORMAL;
};

static AccessPageLayout renderAccessPage(const std::wstring& headline,
                                         const std::wstring& promptLabel,
                                         const std::vector<std::wstring>& statusLines,
                                         const std::wstring& modeLabel,
                                         WORD promptBoxAttr = AMBER);
static InputResult readAccessPasswordPage(const std::wstring& headline,
                                          const std::wstring& promptLabel,
                                          const std::vector<std::wstring>& statusLines,
                                          const std::wstring& modeLabel,
                                          WORD promptBoxAttr,
                                          bool allowEscape);
static std::string readLoginPassword(int failedAttempts, int maxAttempts);

static std::wstring fitTextToWidth(const std::wstring& text, int maxWidth) {
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

static std::vector<std::wstring> wrapDisplayText(const std::wstring& text, int maxWidth) {
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

static std::vector<std::wstring> splitDisplayLines(const std::wstring& text,
                                                   const std::wstring& prefix = L"") {
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

static std::wstring padOrTrimText(const std::wstring& text, int width) {
    if (width <= 0) return L"";
    std::wstring fitted = fitTextToWidth(text, width);
    int used = 0;
    for (wchar_t ch : fitted) used += wcharWidth(ch);
    if (used < width) fitted.append(width - used, L' ');
    return fitted;
}

static int getAdaptiveWrapWidth(int availableWidth) {
    if (availableWidth <= 0) return 0;
    if (availableWidth <= 12) return availableWidth;
    return availableWidth - ((availableWidth >= 36) ? 2 : 1);
}

static CenteredRect drawTerminalShell(const std::wstring& envLabel, bool fixedCentered = false) {
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

static void writeWrappedPanelLines(int x, int y, int w, int h,
                                   const std::vector<std::wstring>& logicalLines,
                                   WORD attr = AMBER) {
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

static int menuSelectScrollableInRegion(int x, int y, int w, int h,
                                        const std::vector<MenuItem>& items,
                                        int startIdx = 0) {
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

static InputResult readPasswordFieldAt(int x, int y, bool allowEscape) {
    ConsoleViewport view = getConsoleViewport();
    int fieldW = (view.x + view.w) - x - 2;
    if (fieldW < 8) fieldW = 8;

    DWORD oldMode;
    GetConsoleMode(g_hIn, &oldMode);
    SetConsoleMode(g_hIn, ENABLE_EXTENDED_FLAGS | ENABLE_WINDOW_INPUT);

    std::wstring wpass;
    COORD startPos = {static_cast<SHORT>(x), static_cast<SHORT>(y)};
    SetConsoleCursorPosition(g_hOut, startPos);
    while (true) {
        INPUT_RECORD ir; DWORD read;
        if (!ReadConsoleInputW(g_hIn, &ir, 1, &read) || read == 0) continue;
        if (ir.EventType == WINDOW_BUFFER_SIZE_EVENT) {
            SetConsoleMode(g_hIn, oldMode);
            return {false, "", true};
        }
        if (ir.EventType != KEY_EVENT || !ir.Event.KeyEvent.bKeyDown) continue;

        WORD vk = ir.Event.KeyEvent.wVirtualKeyCode;
        WCHAR ch = ir.Event.KeyEvent.uChar.UnicodeChar;
        if (vk == VK_RETURN) break;
        if (allowEscape && vk == VK_ESCAPE) {
            SetConsoleMode(g_hIn, oldMode);
            return {true, "", false};
        }
        if (ch == 3) {
            SetConsoleMode(g_hIn, oldMode);
            exit(0);
        }
        if (vk == VK_BACK) {
            if (!wpass.empty()) wpass.pop_back();
        } else if (ch >= L' ') {
            wpass.push_back(ch);
        }

        fillLine(x, y, fieldW, L' ', ATTR_NORMAL);
        std::wstring mask(wpass.size(), L'*');
        writeAtColor(x, y, fitTextToWidth(mask, fieldW), AMBER);
        COORD cursorPos = {static_cast<SHORT>(x + std::min<int>(static_cast<int>(mask.size()), fieldW)), static_cast<SHORT>(y)};
        SetConsoleCursorPosition(g_hOut, cursorPos);
    }

    SetConsoleMode(g_hIn, oldMode);
    return {false, wstring_to_utf8(wpass), false};
}

static InputResult readPasswordFieldAt(int x, int y, int fieldW, bool allowEscape) {
    if (fieldW < 8) fieldW = 8;

    DWORD oldMode;
    GetConsoleMode(g_hIn, &oldMode);
    SetConsoleMode(g_hIn, ENABLE_EXTENDED_FLAGS | ENABLE_WINDOW_INPUT);

    std::wstring wpass;
    COORD startPos = {static_cast<SHORT>(x), static_cast<SHORT>(y)};
    SetConsoleCursorPosition(g_hOut, startPos);
    while (true) {
        INPUT_RECORD ir; DWORD read;
        if (!ReadConsoleInputW(g_hIn, &ir, 1, &read) || read == 0) continue;
        if (ir.EventType == WINDOW_BUFFER_SIZE_EVENT) {
            SetConsoleMode(g_hIn, oldMode);
            return {false, "", true};
        }
        if (ir.EventType != KEY_EVENT || !ir.Event.KeyEvent.bKeyDown) continue;

        WORD vk = ir.Event.KeyEvent.wVirtualKeyCode;
        WCHAR ch = ir.Event.KeyEvent.uChar.UnicodeChar;
        if (vk == VK_RETURN) break;
        if (allowEscape && vk == VK_ESCAPE) {
            SetConsoleMode(g_hIn, oldMode);
            return {true, "", false};
        }
        if (ch == 3) {
            SetConsoleMode(g_hIn, oldMode);
            exit(0);
        }
        if (vk == VK_BACK) {
            if (!wpass.empty()) wpass.pop_back();
        } else if (ch >= L' ') {
            wpass.push_back(ch);
        }

        fillLine(x, y, fieldW, L' ', ATTR_NORMAL);
        std::wstring mask(wpass.size(), L'*');
        writeAtColor(x, y, fitTextToWidth(mask, fieldW), AMBER);
        COORD cursorPos = {static_cast<SHORT>(x + std::min<int>(static_cast<int>(mask.size()), fieldW)), static_cast<SHORT>(y)};
        SetConsoleCursorPosition(g_hOut, cursorPos);
    }

    SetConsoleMode(g_hIn, oldMode);
    return {false, wstring_to_utf8(wpass), false};
}

static std::string readPassword(const std::string& prompt) {
    auto result = readAccessPasswordPage(
        L"VAULT LOCKED // CREDENTIAL REQUIRED",
        utf8_to_wstring(prompt),
        {
            L"TARGET: data\\diary.enc",
            L"STATE : XChaCha20-Poly1305 [LOCKED]",
            L"NOTICE: local-only encrypted diary",
        },
        L"LOGIN",
        AMBER,
        false
    );
    return result.value;
}

static std::string readLoginPassword(int failedAttempts, int maxAttempts) {
    WORD promptAttr = AMBER;
    if (failedAttempts == 1) promptAttr = ACCESS_WARN_ATTR;
    else if (failedAttempts >= 2) promptAttr = ACCESS_DANGER_ATTR;

    std::vector<std::wstring> status = {
        L"TARGET: data\\diary.enc",
        L"STATE : XChaCha20-Poly1305 [LOCKED]",
    };
    if (failedAttempts <= 0) {
        status.push_back(L"NOTICE: local-only encrypted diary");
    } else {
        int remain = maxAttempts - failedAttempts;
        status.push_back(L"ALERT : invalid credential detected");
        status.push_back(L"REMAIN: " + std::to_wstring(remain) + L" attempt(s)");
    }

    auto result = readAccessPasswordPage(
        L"VAULT LOCKED // CREDENTIAL REQUIRED",
        L"输入密码:",
        status,
        L"LOGIN",
        promptAttr,
        false
    );
    return result.value;
}

static InputResult readAccessPasswordPage(const std::wstring& headline,
                                          const std::wstring& promptLabel,
                                          const std::vector<std::wstring>& statusLines,
                                          const std::wstring& modeLabel,
                                          WORD promptBoxAttr,
                                          bool allowEscape) {
    while (true) {
        AccessPageLayout layout = renderAccessPage(headline, promptLabel, statusLines, modeLabel, promptBoxAttr);
        InputResult result = readPasswordFieldAt(layout.fieldX, layout.fieldY, layout.fieldW, allowEscape);
        if (result.resized) continue;
        return result;
    }
}

static AccessPageLayout renderAccessPage(const std::wstring& headline,
                                         const std::wstring& promptLabel,
                                         const std::vector<std::wstring>& statusLines,
                                         const std::wstring& modeLabel,
                                         WORD promptBoxAttr) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(g_hOut, &csbi);
    CenteredRect shell = drawTerminalShell(L"AUTH.ACCESS // LOCAL_DIARY_ENV", true);

    drawDiaryTitle(shell.x + 4, shell.y + 4);

    int panelX = shell.x + 56;
    int panelW = shell.w - 58;
    writeAtColor(panelX, shell.y + 4, L"[ ACCESS_GATE ]", AMBER_DIM);
    fillLine(panelX, shell.y + 5, panelW, L'─', AMBER_DIM);
    writeAtColor(panelX, shell.y + 7, headline, AMBER);
    writeAtColor(panelX, shell.y + 8, L"MODE  : " + modeLabel, AMBER);
    for (size_t i = 0; i < statusLines.size() && i < 5; ++i) {
        writeAtColor(panelX, shell.y + 10 + static_cast<int>(i), fitTextToWidth(statusLines[i], panelW - 1), AMBER);
    }

    int boxX = shell.x + 6;
    int boxY = shell.y + 13;
    int promptBoxW = shell.w - 12;
    int promptBoxH = 7;
    if (boxY + promptBoxH >= shell.y + shell.h - 2) boxY = shell.y + shell.h - promptBoxH - 3;
    drawSingleBox(boxX, boxY, promptBoxW, promptBoxH);
    writeAtColor(boxX, boxY, L"┌", promptBoxAttr);
    writeAtColor(boxX + promptBoxW - 1, boxY, L"┐", promptBoxAttr);
    writeAtColor(boxX, boxY + promptBoxH - 1, L"└", promptBoxAttr);
    writeAtColor(boxX + promptBoxW - 1, boxY + promptBoxH - 1, L"┘", promptBoxAttr);
    fillLine(boxX + 1, boxY, promptBoxW - 2, L'─', promptBoxAttr);
    fillLine(boxX + 1, boxY + promptBoxH - 1, promptBoxW - 2, L'─', promptBoxAttr);
    for (int row = 1; row < promptBoxH - 1; ++row) {
        writeAtColor(boxX, boxY + row, L"│", promptBoxAttr);
        if (row == 3) {
            writeAtColor(boxX + promptBoxW - 1, boxY + row, L" ", ATTR_NORMAL);
        } else {
            writeAtColor(boxX + promptBoxW - 1, boxY + row, L"│", promptBoxAttr);
        }
    }
    writeAtColor(boxX + 2, boxY - 1, L"[ PASSWORD_ENTRY ]", AMBER_DIM);
    writeAtColor(boxX + 2, boxY + 1, promptLabel, AMBER);
    writeAtColor(boxX + 2, boxY + 3, L">> ", AMBER);
    fillLine(boxX + 5, boxY + 3, promptBoxW - 7, L' ', ATTR_NORMAL);
    writeAtColor(boxX + 2, boxY + 5, L"Enter=提交  Backspace=删除  Esc=取消(若可用)", AMBER_DIM);

    writeAtColor(shell.x + 2, shell.y + shell.h - 2, L">> AWAITING_CREDENTIAL...", AMBER);

    AccessPageLayout layout;
    layout.fieldX = boxX + 5;
    layout.fieldY = boxY + 3;
    layout.fieldW = promptBoxW - 7;
    return layout;
}

static EditorShellLayout renderEditorShell(const EditorScreenConfig& cfg) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(g_hOut, &csbi);
    int boxW = csbi.dwSize.X;
    int boxH = csbi.dwSize.Y;
    if (boxW < 88) boxW = 88;

    drawTerminalShell(cfg.screenLabel);

    writeAtColor(4, 4, L"[ " + cfg.panelTitle + L" ]", AMBER_DIM);
    fillLine(4, 5, boxW - 8, L'─', AMBER_DIM);
    writeAtColor(4, 7, cfg.dateLine, AMBER);
    writeAtColor(4, 8, cfg.timeLine, AMBER);
    writeAtColor(4, 9, cfg.modeLine, AMBER);

    int panelX = 4;
    int panelW = boxW - 8;
    int contentTop = 11;
    int contentBottom = boxH - 5;
    int availableContentH = contentBottom - contentTop + 1;
    if (availableContentH < 10) availableContentH = 10;

    int innerW = panelW - 4;
    int historyWrapW = getAdaptiveWrapWidth(innerW);
    std::vector<std::wstring> wrappedHistoryLines;
    if (cfg.historyLines.empty()) {
        wrappedHistoryLines.push_back(cfg.emptyHistoryText);
    } else {
        for (const auto& raw : cfg.historyLines) {
            std::vector<std::wstring> wrapped = wrapDisplayText(raw, historyWrapW);
            wrappedHistoryLines.insert(wrappedHistoryLines.end(), wrapped.begin(), wrapped.end());
        }
    }

    int minEditInnerH = cfg.minEditInnerH;
    int reservedGap = 3;
    int maxHistoryInnerH = std::max(4, availableContentH - minEditInnerH - reservedGap);
    int historyInnerH = std::max(6, maxHistoryInnerH);
    if (!cfg.adaptiveHistory) {
        historyInnerH = std::min<int>(std::max(6, availableContentH / 2), maxHistoryInnerH);
    }
    historyInnerH = std::min(historyInnerH, maxHistoryInnerH);

    int historyBoxH = historyInnerH + 2;
    int editBoxY = contentTop + historyBoxH + 1;
    int editBoxH = contentBottom - editBoxY + 1;
    if (editBoxH < minEditInnerH + 2) {
        editBoxH = minEditInnerH + 2;
        historyBoxH = contentBottom - editBoxH - contentTop;
        if (historyBoxH < 6) historyBoxH = 6;
        historyInnerH = historyBoxH - 2;
        editBoxY = contentTop + historyBoxH + 1;
        editBoxH = contentBottom - editBoxY + 1;
    }

    drawSingleBox(panelX, contentTop, panelW, historyBoxH);
    drawSingleBox(panelX, editBoxY, panelW, editBoxH);
    writeAtColor(panelX + 2, contentTop - 1, L"[ TODAY_LOG ]", AMBER_DIM);
    writeAtColor(panelX + 2, editBoxY - 1, L"[ INPUT ]", AMBER_DIM);

    writeAtColor(3, boxH - 3, L"TODAY_LOG显示完整日志流  |  PgUp/PgDn翻页  Alt+Up/Down细滚动  Ctrl+Enter / Shift+Enter=换行", AMBER_DIM);
    writeAtColor(3, boxH - 2, L">> INPUT READY", AMBER);

    EditorShellLayout layout;
    layout.historyX = panelX + 2;
    layout.historyY = contentTop + 1;
    layout.historyW = panelW - 4;
    layout.historyH = historyInnerH;
    layout.editX = panelX + 2;
    layout.editY = editBoxY + 1;
    layout.editW = panelW - 4;
    layout.editH = editBoxH - 2;
    layout.statusY = boxH - 2;
    layout.contentBottom = contentBottom;
    layout.wrappedHistoryLines = wrappedHistoryLines;
    return layout;
}

static InputResult readPasswordCancelable(const std::string& prompt) {
    return readAccessPasswordPage(
        L"SECURE ACTION // IDENTITY CHECK",
        utf8_to_wstring(prompt),
        {
            L"ESC enabled for safe abort",
            L"INPUT remains hidden in memory",
            L"RETURN to continue authentication",
        },
        L"VERIFY",
        AMBER,
        true
    );
}

static InputResult readLineCancelable(const std::string& prompt, bool allowEmpty = false) {
    DWORD oldMode; GetConsoleMode(g_hIn, &oldMode);
    while (true) {
        std::cout << prompt; std::cout.flush();
        SetConsoleMode(g_hIn, ENABLE_EXTENDED_FLAGS | ENABLE_WINDOW_INPUT);
        std::wstring line;
        while (true) {
            INPUT_RECORD ir; DWORD read;
            if (!ReadConsoleInputW(g_hIn, &ir, 1, &read) || read == 0) continue;
            if (ir.EventType != KEY_EVENT || !ir.Event.KeyEvent.bKeyDown) continue;
            WORD vk = ir.Event.KeyEvent.wVirtualKeyCode;
            WCHAR ch = ir.Event.KeyEvent.uChar.UnicodeChar;
            if (vk == VK_RETURN) break;
            if (vk == VK_ESCAPE) {
                SetConsoleMode(g_hIn, oldMode); std::cout << std::endl;
                return {true, "", false};
            }
            if (vk == VK_BACK) {
                if (!line.empty()) { line.pop_back(); std::cout << "\b \b"; }
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

// ─── 多行粘贴近 ───

static std::string readMultiLine(const std::string& prompt) {
    std::cout << prompt << std::endl;
    std::cout << "（输入完成后，在单独一行输入 :END 结束）" << std::endl;
    std::string result;
    std::string line;
    while (true) {
        std::getline(std::cin, line);
        if (line == ":END") break;
        if (line == ":end") break;
        result += line + "\n";
    }
    return result;
}

// ─── 退出确认条 ───

// 返回 true=确认退出, false=取消
static bool confirmExitBar() {
    FlushConsoleInputBuffer(g_hIn);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(g_hOut, &csbi);
    int barLine = csbi.dwSize.Y - 1;
    COORD barPos = {0, (SHORT)barLine};
    DWORD written;
    FillConsoleOutputCharacterW(g_hOut, L' ', csbi.dwSize.X, barPos, &written);
    FillConsoleOutputAttribute(g_hOut, csbi.wAttributes, csbi.dwSize.X, barPos, &written);
    SetConsoleCursorPosition(g_hOut, barPos);
    SetConsoleTextAttribute(g_hOut, 0x70);
    wprint(L"  Enter=保存退出    Esc=取消  ");
    SetConsoleTextAttribute(g_hOut, 0x07);

    DWORD oldMode; GetConsoleMode(g_hIn, &oldMode);
    SetConsoleMode(g_hIn, ENABLE_EXTENDED_FLAGS | ENABLE_WINDOW_INPUT);
    while (true) {
        INPUT_RECORD ir; DWORD read;
        if (!ReadConsoleInputW(g_hIn, &ir, 1, &read) || read == 0) continue;
        if (ir.EventType != KEY_EVENT || !ir.Event.KeyEvent.bKeyDown) continue;
        WORD vk = ir.Event.KeyEvent.wVirtualKeyCode;
        if (vk == VK_RETURN) {
            SetConsoleMode(g_hIn, oldMode);
            FillConsoleOutputCharacterW(g_hOut, L' ', csbi.dwSize.X, barPos, &written);
            return true;
        }
        if (vk == VK_ESCAPE) {
            SetConsoleMode(g_hIn, oldMode);
            FillConsoleOutputCharacterW(g_hOut, L' ', csbi.dwSize.X, barPos, &written);
            return false;
        }
    }
}

// ─── 多行编辑器 ───

struct EditorResult {
    bool confirmed;
    std::wstring content;
};

struct LineInfo {
    size_t startIdx;  // wstring中行起始索引
    int displayLen;   // 屏幕列数(缓存)
};

// 计算编辑器buffer的行信息
static std::vector<LineInfo> calcEditorLines(const std::wstring& buf, int screenW) {
    std::vector<LineInfo> lines;
    if (screenW <= 0) return lines;

    size_t i = 0;
    while (i < buf.size()) {
        LineInfo li;
        li.startIdx = i;
        int lineW = 0;
        while (i < buf.size()) {
            if (buf[i] == L'\n') {
                i++; break;
            }
            int cw = wcharWidth(buf[i]);
            if (cw == 0) { i++; continue; }
            if (lineW + cw > screenW) break;
            lineW += cw;
            i++;
        }
        li.displayLen = lineW;
        lines.push_back(li);
    }
    // 确保至少有一行（即使buffer为空）
    if (lines.empty()) {
        LineInfo li;
        li.startIdx = 0;
        li.displayLen = 0;
        lines.push_back(li);
    }
    // 如果buffer最后是换行，追加一个空行
    if (!buf.empty() && buf.back() == L'\n') {
        LineInfo li;
        li.startIdx = buf.size();
        li.displayLen = 0;
        lines.push_back(li);
    }
    return lines;
}

// 找到光标所在行号和列号
static void findCursorPos(const std::wstring& buf, size_t cursor,
                          const std::vector<LineInfo>& lines, int screenW,
                          int& outRow, int& outCol) {
    // 按行查找光标位置
    outRow = (int)lines.size() - 1;
    outCol = 0;

    if (cursor >= buf.size()) {
        // 光标在末尾
        if (!lines.empty()) {
            outRow = (int)lines.size() - 1;
            outCol = lines.back().displayLen;
        }
        return;
    }

    for (size_t li = 0; li < lines.size(); ++li) {
        size_t lineStart = lines[li].startIdx;
        size_t lineEnd = (li + 1 < lines.size()) ? lines[li + 1].startIdx : buf.size();
        if (lineEnd > buf.size()) lineEnd = buf.size();

        if (cursor >= lineStart && cursor < lineEnd) {
            // 光标在这一行
            outRow = (int)li;
            int col = 0;
            size_t idx = lineStart;
            while (idx < cursor && idx < lineEnd) {
                if (buf[idx] == L'\n') break;
                col += wcharWidth(buf[idx]);
                idx++;
            }
            outCol = col;
            return;
        }
    }
}

// 将(row, col)转回buffer中的索引
static size_t rowColToIndex(const std::wstring& buf, const std::vector<LineInfo>& lines,
                            int targetRow, int targetCol, int screenW) {
    if (lines.empty()) return 0;
    if (targetRow < 0) targetRow = 0;
    if (targetRow >= (int)lines.size()) targetRow = (int)lines.size() - 1;

    size_t lineStart = lines[targetRow].startIdx;
    size_t lineEnd = (targetRow + 1 < (int)lines.size()) ? lines[targetRow + 1].startIdx : buf.size();
    if (lineEnd > buf.size()) lineEnd = buf.size();

    // 跳过行尾的\n
    if (lineEnd > lineStart && buf[lineEnd - 1] == L'\n') lineEnd--;

    int col = 0;
    size_t i = lineStart;
    while (i < lineEnd) {
        if (buf[i] == L'\n') break;
        int cw = wcharWidth(buf[i]);
        if (col + cw > targetCol) break;
        col += cw;
        i++;
    }
    return i;
}

static EditorResult openDiaryEditor(const std::wstring& initialContent,
                                    const EditorScreenConfig& cfg = EditorScreenConfig()) {
    HANDLE hOut = g_hOut;
    HANDLE hIn  = g_hIn;

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hOut, &csbi);
    int screenW = csbi.dwSize.X;
    int screenH = csbi.dwSize.Y;
    if (screenW < 20) screenW = 80;

    // 回显模式: 读取输入并显示
    DWORD oldInMode;
    GetConsoleMode(hIn, &oldInMode);
    SetConsoleMode(hIn, ENABLE_EXTENDED_FLAGS | ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT);

    std::wstring buf = initialContent;
    size_t cursor = buf.size();

    int historyScroll = 0;
    EditorShellLayout shell = renderEditorShell(cfg);
    int editStartLine = shell.editY;
    int editStartCol = shell.editX;
    int editWidth = shell.editW;
    int maxEditLines = shell.editH;
    if (editWidth < 10) editWidth = screenW - 4;
    if (maxEditLines < 3) maxEditLines = std::max(3, screenH - editStartLine - 3);

    std::vector<RenderedLine> prevHistoryFrame(shell.historyH);
    std::vector<RenderedLine> prevEditorFrame(maxEditLines);
    bool historyFrameInit = false;
    bool editorFrameInit = false;

    auto rebuildLayout = [&]() {
        CONSOLE_SCREEN_BUFFER_INFO nowCsbi;
        GetConsoleScreenBufferInfo(hOut, &nowCsbi);
        screenW = nowCsbi.dwSize.X;
        screenH = nowCsbi.dwSize.Y;
        if (screenW < 20) screenW = 80;

        shell = renderEditorShell(cfg);
        editStartLine = shell.editY;
        editStartCol = shell.editX;
        editWidth = shell.editW;
        maxEditLines = shell.editH;
        if (editWidth < 10) editWidth = screenW - 4;
        if (maxEditLines < 3) maxEditLines = std::max(3, screenH - editStartLine - 3);

        prevHistoryFrame.assign(shell.historyH, RenderedLine{});
        prevEditorFrame.assign(maxEditLines, RenderedLine{});
        historyFrameInit = false;
        editorFrameInit = false;
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

    auto renderHistory = [&]() {
        std::vector<std::wstring> lines = shell.wrappedHistoryLines;
        if (cfg.projectBufferToHistory) {
            lines.push_back(L"");
            std::wstring liveTitle = cfg.livePreviewTime.empty()
                ? L"[CURRENT INPUT]"
                : L"[" + cfg.livePreviewTime + L"]";
            lines.push_back(liveTitle);

            std::vector<std::wstring> projected = splitDisplayLines(buf, L"  ");
            if (projected.empty()) projected.push_back(L"  ");
            int historyWrapW = getAdaptiveWrapWidth(shell.historyW);
            for (const auto& rawLine : projected) {
                std::vector<std::wstring> wrapped = wrapDisplayText(rawLine, historyWrapW);
                if (wrapped.empty()) wrapped.push_back(L"");
                for (const auto& line : wrapped) {
                    lines.push_back(line);
                }
            }
        }

        int total = static_cast<int>(lines.size());
        int visible = shell.historyH;
        int maxScroll = std::max(0, total - visible);
        if (historyScroll < 0) historyScroll = 0;
        if (historyScroll > maxScroll) historyScroll = maxScroll;

        std::vector<RenderedLine> nextFrame(visible);

        for (int i = 0; i < visible && i + historyScroll < total; ++i) {
            bool isEmptyHint = cfg.historyLines.empty() && !cfg.projectBufferToHistory;
            WORD attr = isEmptyHint ? AMBER_DIM : AMBER;
            nextFrame[i].text = lines[i + historyScroll];
            nextFrame[i].attr = attr;
        }

        if (maxScroll > 0) {
            std::wstring hint = L"HISTORY " + std::to_wstring(historyScroll + 1) + L"-" +
                                std::to_wstring(std::min(total, historyScroll + visible)) +
                                L" / " + std::to_wstring(total) + L"  PgUp/PgDn翻页  Alt+Up/Down细滚动";
            nextFrame[shell.historyH - 1].text = hint;
            nextFrame[shell.historyH - 1].attr = AMBER_DIM;
        }

        paintRenderedFrame(shell.historyX, shell.historyY, shell.historyW,
                           nextFrame, prevHistoryFrame, historyFrameInit);
    };

    auto renderEditor = [&]() {
        // 计算行信息
        auto lines = calcEditorLines(buf, editWidth);
        int cursorRow, cursorCol;
        findCursorPos(buf, cursor, lines, editWidth, cursorRow, cursorCol);

        // 滚动: 确保光标行可见
        int scrollOff = 0;
        if (cursorRow >= maxEditLines) scrollOff = cursorRow - maxEditLines + 1;

        std::vector<RenderedLine> nextFrame(maxEditLines);

        // 绘制可见行
        for (int li = scrollOff; li < (int)lines.size() && (li - scrollOff) < maxEditLines; ++li) {
            size_t lineStart = lines[li].startIdx;
            size_t lineEnd = (li + 1 < (int)lines.size()) ? lines[li + 1].startIdx : buf.size();
            if (lineEnd > buf.size()) lineEnd = buf.size();

            std::wstring lineText;
            for (size_t ci = lineStart; ci < lineEnd; ++ci) {
                if (buf[ci] == L'\n') break;
                lineText += buf[ci];
            }

            int frameIdx = li - scrollOff;
            nextFrame[frameIdx].text = lineText;
            nextFrame[frameIdx].attr = AMBER;
        }

        paintRenderedFrame(editStartCol, editStartLine, editWidth,
                           nextFrame, prevEditorFrame, editorFrameInit);

        // 设置光标位置
        int cursorScreenRow = editStartLine + cursorRow - scrollOff;
        if (cursorScreenRow < editStartLine) cursorScreenRow = editStartLine;
        COORD cursorPos = {(SHORT)(editStartCol + cursorCol), (SHORT)cursorScreenRow};
        SetConsoleCursorPosition(hOut, cursorPos);
    };

    auto setStatusLine = [&](const std::wstring& text, WORD attr) {
        fillLine(1, shell.statusY, screenW - 2, L' ', ATTR_NORMAL);
        writeAtColor(3, shell.statusY, fitTextToWidth(text, screenW - 6), attr);
    };

    auto redrawPanels = [&]() {
        renderHistory();
        renderEditor();
    };

    // 在编辑区域底部显示确认提示
    auto showConfirmBar = [&](bool visible) {
        if (visible) {
            fillLine(1, shell.statusY, screenW - 2, L' ', ATTR_NORMAL);
            writeAtColor(3, shell.statusY, L"SAVE_BUFFER?  Enter=确认保存    Esc=继续编辑", 0x70);
        } else {
            setStatusLine(L">> INPUT READY", AMBER);
        }
    };

    rebuildLayout();
    redrawPanels();
    showConfirmBar(false);
    renderEditor();

    // 主输入循环
    bool inConfirm = false;

    // VT序列状态机：用于解析 CSI u 协议序列 (ESC [ <key> ; <mod> u)
    bool vtCollecting = false;  // 正在收集VT序列
    std::wstring vtNum;         // 当前正在读取的数字
    int vtKey = 0;              // VT序列的key code
    bool vtReadingMod = false;  // 是否正在读取modifier部分

    while (true) {
        if (!waitForConsoleInputOrResize(hIn, screenW, screenH)) {
            rebuildLayout();
            redrawPanels();
            showConfirmBar(inConfirm);
            renderEditor();
            continue;
        }

        INPUT_RECORD ir;
        DWORD read;
        if (!ReadConsoleInputW(hIn, &ir, 1, &read) || read == 0) continue;

        if (ir.EventType != KEY_EVENT || !ir.Event.KeyEvent.bKeyDown) continue;

        WORD vk = ir.Event.KeyEvent.wVirtualKeyCode;
        DWORD ctrl = ir.Event.KeyEvent.dwControlKeyState;
        WCHAR ch = ir.Event.KeyEvent.uChar.UnicodeChar;

        // ── VT序列收集模式 ──
        if (vtCollecting) {
            if (ch >= L'0' && ch <= L'9') {
                vtNum += ch;
            } else if (ch == L'[' && vtNum.empty()) {
                // 第二个字符，跳过（已经在ESC检测时处理了'['）
            } else if (ch == L';' && !vtNum.empty()) {
                vtKey = _wtoi(vtNum.c_str());
                vtNum.clear();
                vtReadingMod = true;
            } else if (ch == L'u' && vtReadingMod) {
                // 序列完整，解析modifier
                int mod = vtNum.empty() ? 1 : _wtoi(vtNum.c_str());
                vtCollecting = false;
                // Shift+Enter: key=13, modifier bit 1 (shift) = 2
                if (vtKey == 13 && (mod & 2)) {
                    buf.insert(buf.begin() + cursor, L'\n');
                    cursor++;
                    redrawPanels();
                }
                // 其他VT序列一律丢弃
            } else {
                // 非预期字符，终止收集
                vtCollecting = false;
            }
            continue;
        }

        // ── ESC字符检测：区分ESC键和VT序列开头 ──
        if (ch == 0x1B) {
            DWORD avail = 0;
            GetNumberOfConsoleInputEvents(hIn, &avail);
            if (avail > 0) {
                INPUT_RECORD peek;
                DWORD peekRead;
                if (PeekConsoleInputW(hIn, &peek, 1, &peekRead) && peekRead > 0) {
                    if (peek.EventType == KEY_EVENT && peek.Event.KeyEvent.bKeyDown &&
                        peek.Event.KeyEvent.uChar.UnicodeChar == L'[') {
                        // 后面紧跟着'['，是VT序列开头，消费掉'['并开始收集
                        ReadConsoleInputW(hIn, &peek, 1, &peekRead);
                        vtCollecting = true;
                        vtNum.clear();
                        vtKey = 0;
                        vtReadingMod = false;
                        continue;
                    }
                }
            }
            // 没有紧跟字符，当作普通ESC键处理
            if (inConfirm) {
                inConfirm = false;
                showConfirmBar(false);
                redrawPanels();
                continue;
            }
            SetConsoleMode(hIn, oldInMode);
            EditorResult result;
            result.confirmed = false;
            result.content = L"";
            return result;
        }

        // 确认模式：只接受 Enter 和 Esc
        if (inConfirm) {
            if (vk == VK_RETURN) {
                showConfirmBar(false);
                SetConsoleMode(hIn, oldInMode);
                EditorResult result;
                result.confirmed = true;
                result.content = buf;
                return result;
            }
            if (vk == VK_ESCAPE) {
                inConfirm = false;
                showConfirmBar(false);
                redrawPanels();
            }
            continue;
        }

        // Shift+Enter → 插入换行
        if (vk == VK_RETURN && (ctrl & SHIFT_PRESSED)) {
            buf.insert(buf.begin() + cursor, L'\n');
            cursor++;
            redrawPanels();
            continue;
        }

        // Ctrl+Enter → 插入换行（备选）
        if (vk == VK_RETURN && (ctrl & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))) {
            buf.insert(buf.begin() + cursor, L'\n');
            cursor++;
            redrawPanels();
            continue;
        }

        // 普通 Enter → 进入确认模式
        if (vk == VK_RETURN) {
            inConfirm = true;
            showConfirmBar(true);
            continue;
        }

        // Esc → 取消
        if (vk == VK_ESCAPE) {
            SetConsoleMode(hIn, oldInMode);
            EditorResult result;
            result.confirmed = false;
            result.content = L"";
            return result;
        }

        if (vk == VK_PRIOR) {
            historyScroll -= std::max(1, shell.historyH - 2);
            redrawPanels();
            continue;
        }

        if (vk == VK_NEXT) {
            historyScroll += std::max(1, shell.historyH - 2);
            redrawPanels();
            continue;
        }

        if ((ctrl & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) && vk == VK_UP) {
            historyScroll--;
            redrawPanels();
            continue;
        }

        if ((ctrl & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) && vk == VK_DOWN) {
            historyScroll++;
            redrawPanels();
            continue;
        }

        // 左箭头
        if (vk == VK_LEFT) {
            if (cursor > 0) {
                cursor--;
                redrawPanels();
            }
            continue;
        }

        // 右箭头
        if (vk == VK_RIGHT) {
            if (cursor < buf.size()) {
                cursor++;
                redrawPanels();
            }
            continue;
        }

        // 上箭头
        if (vk == VK_UP) {
            auto lines = calcEditorLines(buf, editWidth);
            int cr, cc;
            findCursorPos(buf, cursor, lines, editWidth, cr, cc);
            if (cr > 0) {
                cursor = rowColToIndex(buf, lines, cr - 1, cc, editWidth);
                redrawPanels();
            }
            continue;
        }

        // 下箭头
        if (vk == VK_DOWN) {
            auto lines = calcEditorLines(buf, editWidth);
            int cr, cc;
            findCursorPos(buf, cursor, lines, editWidth, cr, cc);
            if (cr < (int)lines.size() - 1) {
                cursor = rowColToIndex(buf, lines, cr + 1, cc, editWidth);
                redrawPanels();
            }
            continue;
        }

        // Home
        if (vk == VK_HOME) {
            auto lines = calcEditorLines(buf, editWidth);
            int cr, cc;
            findCursorPos(buf, cursor, lines, editWidth, cr, cc);
            size_t lineStart = lines[cr].startIdx;
            cursor = lineStart;
            redrawPanels();
            continue;
        }

        // End
        if (vk == VK_END) {
            auto lines = calcEditorLines(buf, editWidth);
            int cr, cc;
            findCursorPos(buf, cursor, lines, editWidth, cr, cc);
            size_t lineEnd = (cr + 1 < (int)lines.size()) ? lines[cr + 1].startIdx : buf.size();
            if (lineEnd > buf.size()) lineEnd = buf.size();
            if (lineEnd > lines[cr].startIdx && buf[lineEnd - 1] == L'\n') lineEnd--;
            cursor = lineEnd;
            redrawPanels();
            continue;
        }

        // Backspace
        if (vk == VK_BACK) {
            if (cursor > 0) {
                cursor--;
                buf.erase(buf.begin() + cursor);
                redrawPanels();
            }
            continue;
        }

        // Delete
        if (vk == VK_DELETE) {
            if (cursor < buf.size()) {
                buf.erase(buf.begin() + cursor);
                redrawPanels();
            }
            continue;
        }

        // 可打印字符或中文字符
        if (ch >= L' ' || (ch >= 0x4E00 && ch <= 0x9FFF) || (ch >= 0x3000 && ch <= 0x303F) || (ch >= 0xFF00 && ch <= 0xFFEF)) {
            buf.insert(buf.begin() + cursor, ch);
            cursor++;
            redrawPanels();
            continue;
        }
    }
}

static void showFullScreenMessage(const std::wstring& title,
                                  const std::vector<std::wstring>& lines,
                                  const std::wstring& prompt = L"按任意键继续...") {
    drawTerminalShell(L"STATUS.PANEL // LOCAL_DIARY_ENV");
    int boxX = 6;
    int boxY = 6;

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(g_hOut, &csbi);
    int boxW = csbi.dwSize.X - 12;
    int boxH = std::max(8, static_cast<int>(lines.size()) + 6);
    if (boxY + boxH >= csbi.dwSize.Y - 2) boxH = csbi.dwSize.Y - boxY - 3;

    drawSingleBox(boxX, boxY, boxW, boxH);
    writeAtColor(boxX + 2, boxY - 1, L"[ " + title + L" ]", AMBER_DIM);

    int y = boxY + 2;
    for (const auto& line : lines) {
        if (y >= boxY + boxH - 2) break;
        writeAtColor(boxX + 2, y, fitTextToWidth(line, boxW - 4), AMBER);
        y++;
    }

    fillLine(1, csbi.dwSize.Y - 2, csbi.dwSize.X - 2, L' ', ATTR_NORMAL);
    writeAtColor(3, csbi.dwSize.Y - 2, prompt, AMBER);
}

// ─── 日期工具 ───

static void getCurrentDate(int& year, int& month, int& day) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    year = st.wYear;
    month = st.wMonth;
    day = st.wDay;
}

static std::string getCurrentTimeStr() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d", st.wHour, st.wMinute);
    return std::string(buf);
}

static std::string getCurrentTimestampStr() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d/%02d/%02d %02d:%02d:%02d",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return std::string(buf);
}

static std::vector<std::wstring> buildCounterHistoryLines(const std::vector<std::string>& history) {
    std::vector<std::wstring> lines;
    lines.push_back(L"TRACE WINDOW");
    lines.push_back(L"────────────────────");

    if (history.empty()) {
        lines.push_back(L"NO TRIGGER RECORDED");
        lines.push_back(L"");
        lines.push_back(L"第一次触发后，这里会出现最近几次的时间链。");
        return lines;
    }

    static const wchar_t* labels[] = {L"LATEST", L"PREV-1", L"PREV-2", L"PREV-3", L"PREV-4", L"PREV-5"};
    int shown = 0;
    for (auto it = history.rbegin(); it != history.rend() && shown < 5; ++it, ++shown) {
        std::wstring line = std::wstring(labels[shown]) + L" : " + utf8_to_wstring(*it);
        lines.push_back(line);
    }

    if (history.size() >= 2) {
        lines.push_back(L"");
        lines.push_back(L"latest pulse stays at the top.");
    }
    return lines;
}

static std::string getCurrentDateStr() {
    int y, m, d;
    getCurrentDate(y, m, d);
    return std::to_string(y) + "/" + std::to_string(m) + "/" + std::to_string(d);
}

// ─── 显示函数 ───

static void displaySegments(const nlohmann::json& entry) {
    if (!entry.contains("segments") || !entry["segments"].is_array()) return;
    for (const auto& seg : entry["segments"]) {
        std::string time = seg.value("time", "");
        std::string content = seg.value("content", "");
        wprint(L"  "); wprint(utf8_to_wstring(time)); wprintln(L";");
        if (!content.empty()) {
            wprintln(utf8_to_wstring(content));
        }
        wprintln();
    }
}

static std::string formatDiaryPlain(const nlohmann::json& entry) {
    int y = entry.value("year", 0);
    int m = entry.value("month", 0);
    int d = entry.value("day", 0);
    std::string result;
    result += "————————————————————————\n";
    result += std::to_string(y) + "/" + std::to_string(m) + "/" + std::to_string(d) + "\n";
    result += "\n";

    if (entry.contains("segments") && entry["segments"].is_array()) {
        for (const auto& seg : entry["segments"]) {
            std::string time = seg.value("time", "");
            std::string content = seg.value("content", "");
            result += time + ";\n";
            if (!content.empty()) {
                result += content + "\n";
            }
            result += "\n";
        }
    }
    return result;
}

// ─── 打印日记（纯文本格式） ───

static void printDiary(const nlohmann::json& entry) {
    int y = entry.value("year", 0);
    int m = entry.value("month", 0);
    int d = entry.value("day", 0);

    wprintln(L"————————————————————————");
    wprintln(utf8_to_wstring(std::to_string(y) + "/" + std::to_string(m) + "/" + std::to_string(d)));
    wprintln();

    if (entry.contains("segments") && entry["segments"].is_array()) {
        for (const auto& seg : entry["segments"]) {
            std::string time = seg.value("time", "");
            std::string content = seg.value("content", "");
            wprint(utf8_to_wstring(time)); wprintln(L";");
            if (!content.empty()) {
                wprintln(utf8_to_wstring(content));
            }
            wprintln();
        }
    }
}

// ─── 查看全部 ───
static void viewAllDiaries(const DiaryStore& store) {
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
        writeAtColor(4, 9, L"SCROLL: PgUp/PgDn=翻页  Up/Down=滚动  Home/End=首尾  Esc=返回", AMBER_DIM);

        int panelX = 4;
        int panelW = boxW - 8;
        int bodyBoxY = 11;
        int contentBottom = boxH - 5;
        int bodyBoxH = contentBottom - bodyBoxY + 1;
        if (bodyBoxH < 6) bodyBoxH = 6;

        drawSingleBox(panelX, bodyBoxY, panelW, bodyBoxH);
        writeAtColor(panelX + 2, bodyBoxY - 1, L"[ DIARY_ARCHIVE ]", AMBER_DIM);

        layout.bodyX = panelX + 2;
        layout.bodyY = bodyBoxY + 1;
        layout.bodyW = panelW - 4;
        layout.bodyH = bodyBoxH - 2;
        layout.infoY = boxH - 3;
        layout.promptY = boxH - 2;

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
        fillLine(1, layout.infoY, screenW - 2, L' ', ATTR_NORMAL);
        fillLine(1, layout.promptY, screenW - 2, L' ', ATTR_NORMAL);
        writeAtColor(3, layout.infoY, fitTextToWidth(info, screenW - 6), AMBER_DIM);
        writeAtColor(3, layout.promptY, padOrTrimText(L">> ARCHIVE READY", screenW - 6), AMBER);
    };

    renderShell();
    scroll = std::max(0, static_cast<int>(wrappedLines.size()) - layout.bodyH);
    renderArchive();

    while (true) {
        if (!waitForConsoleInputOrResize(g_hIn, screenW, screenH)) {
            renderShell();
            renderArchive();
            continue;
        }

        INPUT_RECORD ir{};
        DWORD read = 0;
        if (!ReadConsoleInputW(g_hIn, &ir, 1, &read) || read == 0) continue;
        if (ir.EventType != KEY_EVENT || !ir.Event.KeyEvent.bKeyDown) continue;

        WORD vk = ir.Event.KeyEvent.wVirtualKeyCode;
        bool changed = false;

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

// ─── 写入 / 编辑今日 ───

static void writeOrEditToday(DiaryStore& store, const std::string& password) {
    int year, month, day;
    getCurrentDate(year, month, day);
    std::string currentTime = getCurrentTimeStr();

    int idx = store.findEntry(year, month, day);
    bool isNew = (idx < 0);
    nlohmann::json entryCopy; // 备份（用于编辑已有日记时回滚）
    bool hasBackup = false;

    if (isNew) {
        nlohmann::json entry;
        entry["year"] = year;
        entry["month"] = month;
        entry["day"] = day;
        entry["segments"] = nlohmann::json::array();
        nlohmann::json seg;
        seg["time"] = currentTime;
        seg["content"] = "";
        entry["segments"].push_back(seg);
        store.addEntry(entry);
        idx = static_cast<int>(store.entryCount()) - 1;
    } else {
        entryCopy = store.entries()[idx];
        hasBackup = true;
        // 添加新segment
        nlohmann::json seg;
        seg["time"] = currentTime;
        seg["content"] = "";
        store.entries()[idx]["segments"].push_back(seg);
    }

    auto& entry = store.entries()[idx];
    size_t segCount = entry["segments"].size();

    std::vector<std::wstring> historyLines;
    for (size_t si = 0; si + 1 < segCount; ++si) {
        const auto& seg = entry["segments"][si];
        std::wstring timeLine = L"[" + utf8_to_wstring(seg.value("time", "")) + L"]";
        historyLines.push_back(timeLine);

        std::wstring contentW = utf8_to_wstring(seg.value("content", ""));
        std::vector<std::wstring> contentLines = splitDisplayLines(contentW, L"  ");
        if (contentLines.empty()) contentLines.push_back(L"  ");
        for (const auto& line : contentLines) {
            historyLines.push_back(line);
        }
        historyLines.push_back(L"");
    }

    EditorScreenConfig cfg;
    cfg.panelTitle = isNew ? L"WRITE TODAY" : L"APPEND TODAY";
    cfg.dateLine = L"DATE : " + utf8_to_wstring(getCurrentDateStr());
    cfg.timeLine = L"SLOT : " + utf8_to_wstring(currentTime) + L";";
    cfg.modeLine = isNew ? L"MODE : CREATE NEW ENTRY" : L"MODE : APPEND NEW SEGMENT";
    cfg.historyLines = historyLines;
    cfg.adaptiveHistory = true;
    cfg.emptyHistoryText = L"TODAY LOG EMPTY. START WRITING.";
    cfg.projectBufferToHistory = true;
    cfg.livePreviewTime = utf8_to_wstring(currentTime);
    cfg.minEditInnerH = 5;

    EditorResult result = openDiaryEditor(L"", cfg);

    if (result.confirmed) {
        // 用户确认
        std::wstring trimmed = result.content;
        // 去除首尾空白
        while (!trimmed.empty() && (trimmed.back() == L'\n' || trimmed.back() == L'\r' || trimmed.back() == L' ')) {
            trimmed.pop_back();
        }

        if (!trimmed.empty()) {
            // 更新最后一个segment
            store.entries()[idx]["segments"].back()["content"] = wstring_to_utf8(result.content);
            store.save(DIARY_PATH, password);
            showFullScreenMessage(L"WRITE COMPLETE", {L"[日记已保存]"});
            pauseScreen();
        } else {
            // 内容为空，删除自动添加的segment
            auto& segs = store.entries()[idx]["segments"];
            segs.erase(segs.size() - 1);
            if (segs.empty()) {
                store.removeEntry(idx);
                showFullScreenMessage(L"WRITE CANCELLED", {L"[未输入内容，已取消]"});
            } else {
                showFullScreenMessage(L"WRITE SKIPPED", {L"[未输入新内容，保留已有记录]"});
            }
            store.save(DIARY_PATH, password);
            pauseScreen();
        }
    } else {
        // 用户取消（Esc）
        if (hasBackup) {
            store.updateEntry(idx, entryCopy);
        } else {
            store.removeEntry(idx);
        }
        showFullScreenMessage(L"WRITE CANCELLED", {L"[已取消]"});
        pauseScreen();
    }
}

static std::wstring formatDateW(int year, int month, int day) {
    return utf8_to_wstring(std::to_string(year) + "/" + std::to_string(month) + "/" + std::to_string(day));
}

static std::vector<std::wstring> buildEntryPreviewLines(const nlohmann::json& entry) {
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

static std::vector<std::wstring> buildEntryHistoryLines(const nlohmann::json& entry, int skipSegIdx = -1) {
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

static DateSelectorLayout renderDateSelectorPageFrame() {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(g_hOut, &csbi);
    int boxW = csbi.dwSize.X;
    int boxH = csbi.dwSize.Y;
    if (boxW < 88) boxW = 88;

    drawTerminalShell(L"DATE.SELECTOR // LOCAL_DIARY_ENV");

    writeAtColor(4, 4, L"[ DATE_SELECTOR ]", AMBER_DIM);
    fillLine(4, 5, boxW - 8, L'─', AMBER_DIM);

    int panelX = 4;
    int panelW = boxW - 8;
    int bodyY = 11;
    int contentBottom = boxH - 5;
    int bodyH = contentBottom - bodyY + 1;
    if (bodyH < 8) bodyH = 8;

    int leftBoxW = std::max(28, std::min(36, panelW / 3));
    int rightBoxX = panelX + leftBoxW + 2;
    int rightBoxW = panelW - leftBoxW - 2;

    drawSingleBox(panelX, bodyY, leftBoxW, bodyH);
    drawSingleBox(rightBoxX, bodyY, rightBoxW, bodyH);
    writeAtColor(rightBoxX + 2, bodyY - 1, L"[ CONTEXT ]", AMBER_DIM);

    fillLine(1, boxH - 3, boxW - 2, L' ', ATTR_NORMAL);
    fillLine(1, boxH - 2, boxW - 2, L' ', ATTR_NORMAL);
    writeAtColor(3, boxH - 3, L"SELECT a time branch and enter its node.", AMBER_DIM);
    writeAtColor(3, boxH - 2, padOrTrimText(L">> DATE INDEX READY", boxW - 6), AMBER);

    DateSelectorLayout layout;
    layout.pathX = 4;
    layout.pathW = boxW - 8;
    layout.modeX = 4;
    layout.modeW = boxW - 8;
    layout.navX = 4;
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

static void updateDateSelectorPage(const DateSelectorLayout& layout,
                                   const std::wstring& listTitle,
                                   const std::wstring& pathLine,
                                   const std::vector<std::wstring>& infoLines) {
    fillLine(layout.pathX, 7, layout.pathW, L' ', ATTR_NORMAL);
    fillLine(layout.modeX, 8, layout.modeW, L' ', ATTR_NORMAL);
    fillLine(layout.navX, 9, layout.navW, L' ', ATTR_NORMAL);
    writeAtColor(layout.pathX, 7, fitTextToWidth(L"PATH : " + pathLine, layout.pathW), AMBER);
    writeAtColor(layout.modeX, 8, fitTextToWidth(L"MODE : YEAR -> MONTH -> DAY", layout.modeW), AMBER);
    writeAtColor(layout.navX, 9, fitTextToWidth(L"NAV  : Enter进入  Esc返回  PgUp/PgDn翻页  Home/End首尾", layout.navW), AMBER_DIM);

    fillLine(layout.titleX, layout.menu.menuY - 2, layout.titleW, L' ', ATTR_NORMAL);
    writeAtColor(layout.titleX, layout.menu.menuY - 2, fitTextToWidth(L"[ " + listTitle + L" ]", layout.titleW), AMBER_DIM);

    for (int row = 0; row < layout.menu.menuH; ++row) {
        fillLine(layout.menu.menuX, layout.menu.menuY + row, layout.menu.menuW, L' ', ATTR_NORMAL);
    }

    writeWrappedPanelLines(layout.infoX, layout.infoY, layout.infoW, layout.infoH, infoLines, AMBER);
}

static DateEntryPageLayout renderDateEntryPage(const nlohmann::json& entry) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(g_hOut, &csbi);
    int boxW = csbi.dwSize.X;
    int boxH = csbi.dwSize.Y;
    if (boxW < 88) boxW = 88;

    int y = entry.value("year", 0);
    int m = entry.value("month", 0);
    int d = entry.value("day", 0);
    int segCount = (entry.contains("segments") && entry["segments"].is_array())
        ? static_cast<int>(entry["segments"].size()) : 0;

    drawTerminalShell(L"DATE.ENTRY // LOCAL_DIARY_ENV");

    writeAtColor(4, 4, L"[ DATE_ENTRY ]", AMBER_DIM);
    fillLine(4, 5, boxW - 8, L'─', AMBER_DIM);
    writeAtColor(4, 7, L"DATE : " + formatDateW(y, m, d), AMBER);
    writeAtColor(4, 8, L"SEGMENTS : " + std::to_wstring(segCount), AMBER);
    writeAtColor(4, 9, L"MODE : PREVIEW + EDIT OPERATIONS", AMBER_DIM);

    int panelX = 4;
    int panelW = boxW - 8;
    int previewBoxY = 11;
    int actionBoxH = 8;
    int contentBottom = boxH - 5;
    int actionBoxY = contentBottom - actionBoxH + 1;
    if (actionBoxY <= previewBoxY + 6) actionBoxY = previewBoxY + 7;
    int previewBoxH = actionBoxY - previewBoxY - 1;
    if (previewBoxH < 6) previewBoxH = 6;

    drawSingleBox(panelX, previewBoxY, panelW, previewBoxH);
    drawSingleBox(panelX, actionBoxY, panelW, actionBoxH);
    writeAtColor(panelX + 2, previewBoxY - 1, L"[ ENTRY_LOG ]", AMBER_DIM);
    writeAtColor(panelX + 2, actionBoxY - 1, L"[ SEGMENT_ACTIONS ]", AMBER_DIM);

    fillLine(1, boxH - 3, boxW - 2, L' ', ATTR_NORMAL);
    fillLine(1, boxH - 2, boxW - 2, L' ', ATTR_NORMAL);
    writeAtColor(3, boxH - 3, L"TIP : preview follows window size; action list may scroll independently.", AMBER_DIM);
    writeAtColor(3, boxH - 2, padOrTrimText(L">> ENTRY OPERATIONS READY", boxW - 6), AMBER);

    DateEntryPageLayout layout;
    layout.previewX = panelX + 2;
    layout.previewY = previewBoxY + 1;
    layout.previewW = panelW - 4;
    layout.previewH = previewBoxH - 2;
    layout.statusY = boxH - 2;
    layout.menu = {panelX + 2, actionBoxY + 1, panelW - 4, actionBoxH - 2};
    return layout;
}

static int pickEntrySegment(const nlohmann::json& entry, const std::wstring& title) {
    while (true) {
        CenteredRect shell = drawTerminalShell(L"SEGMENT.SELECT // LOCAL_DIARY_ENV", true);
        int boxX = shell.x + 6;
        int boxY = shell.y + 5;
        int boxW = shell.w - 12;
        int boxH = shell.h - 10;
        if (boxH < 10) boxH = 10;

        drawSingleBox(boxX, boxY, boxW, boxH);
        writeAtColor(boxX + 2, boxY - 1, L"[ " + title + L" ]", AMBER_DIM);
        writeAtColor(boxX + 2, boxY + 1, L"Enter选择  Esc返回  PgUp/PgDn翻页", AMBER_DIM);

        std::vector<MenuItem> items;
        items.push_back({L"--- 选择记录 ---", false});
        const auto& segs = entry["segments"];
        for (size_t i = 0; i < segs.size(); ++i) {
            std::wstring label = L"记录 " + std::to_wstring(i + 1) + L"  [" + utf8_to_wstring(segs[i].value("time", "")) + L"]";
            items.push_back({label, true});
        }
        items.push_back({L"0. 返回", true});

        int choice = menuSelectScrollableInRegion(boxX + 2, boxY + 3, boxW - 4, boxH - 5, items, 1);
        if (choice == MENU_RESIZE) continue;
        if (choice == MENU_ESC || choice == static_cast<int>(items.size()) - 1) return -1;
        return choice - 1;
    }
}

static void editDateEntryPage(DiaryStore& store, const std::string& password, size_t entryIdx) {
    while (entryIdx < store.entryCount()) {
        auto& entry = store.entries()[entryIdx];
        DateEntryPageLayout layout = renderDateEntryPage(entry);
        writeWrappedPanelLines(layout.previewX, layout.previewY, layout.previewW, layout.previewH,
                               buildEntryPreviewLines(entry), AMBER);

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

        if (opChoice == MENU_RESIZE) continue;
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
                store.save(DIARY_PATH, password);
                showFullScreenMessage(L"SEGMENT UPDATED", {L"[记录已更新]"});
            } else {
                showFullScreenMessage(L"EDIT CANCELLED", {L"[已取消]"});
            }
            pauseScreen();
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
                    store.save(DIARY_PATH, password);
                    showFullScreenMessage(L"SEGMENT ADDED", {L"[记录已添加]"});
                } else {
                    showFullScreenMessage(L"ADD CANCELLED", {L"[未输入内容，已取消]"});
                }
            } else {
                showFullScreenMessage(L"ADD CANCELLED", {L"[已取消]"});
            }
            pauseScreen();
            continue;
        }

        if (opChoice == static_cast<int>(segCount) + 2) {
            if (segCount == 0) {
                showFullScreenMessage(L"DELETE SKIPPED", {L"[没有可删除的记录]"});
                pauseScreen();
                continue;
            }

            int segIdx = pickEntrySegment(entry, L"DELETE SEGMENT");
            if (segIdx < 0) continue;

            auto cfmRes = readLineCancelable("确认删除记录 " + std::to_string(segIdx + 1) + "? (输入 yes 确认): ");
            if (cfmRes.cancelled) continue;
            if (cfmRes.value == "yes") {
                entry["segments"].erase(segIdx);
                if (entry["segments"].empty()) {
                    store.removeEntry(entryIdx);
                    store.save(DIARY_PATH, password);
                    showFullScreenMessage(L"ENTRY REMOVED", {L"[该日记已清空，整篇已删除]"});
                    pauseScreen();
                    return;
                }
                store.save(DIARY_PATH, password);
                showFullScreenMessage(L"SEGMENT DELETED", {L"[记录已删除]"});
                pauseScreen();
            }
            continue;
        }

        if (opChoice == static_cast<int>(segCount) + 3) {
            auto cfmRes = readLineCancelable("确认删除这篇日记? (输入 yes 确认): ");
            if (cfmRes.cancelled) continue;
            if (cfmRes.value == "yes") {
                store.removeEntry(entryIdx);
                store.save(DIARY_PATH, password);
                showFullScreenMessage(L"ENTRY REMOVED", {L"[日记已删除]"});
                pauseScreen();
                return;
            }
        }
    }
}

// ─── 按日期编辑 ───

static void editByDate(DiaryStore& store, const std::string& password) {
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
            editDateEntryPage(store, password, entryIdx);
            selectorLayout = renderDateSelectorPageFrame();
        }
    }
}

// ─── 导出 ───

static void exportDiary(const DiaryStore& store) {
    auto sorted = store.getSortedIndices();
    if (sorted.empty()) {
        wprintln(L"日记本为空，没有可导出的内容");
        return;
    }

    std::string output;
    for (size_t i = 0; i < sorted.size(); ++i) {
        output += formatDiaryPlain(store.entries()[sorted[i]]);
    }

    std::ofstream file(EXPORT_TXT_PATH, std::ios::trunc);
    if (!file.is_open()) {
        wprintln(L"无法写入 " + utf8_to_wstring(EXPORT_TXT_PATH));
        return;
    }
    file << output;
    file.close();

    wprintln(L"[已导出到 " + utf8_to_wstring(EXPORT_TXT_PATH) + L"]");
    wprintln(L"共导出 " + std::to_wstring(sorted.size()) + L" 篇日记");
    wprintln(L"注意：该文件是明文，用完后请及时删除!");
}

// ─── 导入 ───

static void importDiary(DiaryStore& store, const std::string& password) {
    std::vector<MenuItem> importModeItems = {
        {L"--- 导入方式 ---", false},
        {L"1. 从文件导入 (data\\import_diary.txt)", true},
        {L"2. 直接粘贴文本导入", true},
        {L"0. 返回", true},
    };
    int modeChoice = menuSelect(importModeItems, 1);
    if (modeChoice == 3 || modeChoice == MENU_ESC) return;

    std::string rawText;
    if (modeChoice == 1) {
        std::ifstream file(IMPORT_TXT_PATH);
        if (!file.is_open()) {
            wprintln(L"找不到 " + utf8_to_wstring(IMPORT_TXT_PATH));
            wprintln(L"请将日记文本放入该文件后再导入");
            return;
        }
        std::stringstream ss;
        ss << file.rdbuf();
        rawText = ss.str();
        file.close();
    } else {
        clearScreen();
        rawText = readMultiLine("请粘贴日记文本（格式需符合日记排版规范）");
    }

    if (rawText.empty()) {
        wprintln(L"导入文本为空");
        return;
    }

    // 解析导入文本
    std::vector<nlohmann::json> parsedEntries;
    std::string remaining = rawText;
    size_t pos = 0;

    // 按分隔线 "——" 分割
    while (true) {
        // 查找分隔线
        size_t sepStart = remaining.find("\xe2\x80\x94"); // UTF-8 EM DASH
        if (sepStart == std::string::npos) {
            // 也尝试查找连续的 "—"
            sepStart = remaining.find("——");
        }
        if (sepStart == std::string::npos) break;

        // 找到下一个分隔线（或文本末尾）
        size_t nextSep = sepStart + 1;
        while (nextSep < remaining.size()) {
            std::string next3 = remaining.substr(nextSep, 3);
            if (next3 == "\xe2\x80\x94" || remaining.substr(nextSep, 2) == "——") {
                break;
            }
            nextSep++;
        }

        std::string block;
        if (nextSep < remaining.size() - 1) {
            // 有分隔线，取出中间的内容
            size_t nextSepLine = remaining.find("\xE2\x80\x94", nextSep);
            if (nextSepLine == std::string::npos) {
                nextSepLine = remaining.find("——", nextSep + 1);
            }
            if (nextSepLine != std::string::npos) {
                // 找到下一个分隔线位置
                block = remaining.substr(sepStart, nextSepLine - sepStart);
                remaining = remaining.substr(nextSepLine);
            } else {
                block = remaining.substr(sepStart);
                remaining = "";
            }
        } else {
            block = remaining.substr(sepStart);
            remaining = "";
        }

        // 解析block
        std::istringstream blockStream(block);
        std::string line;
        std::string dateLine;
        nlohmann::json entry;
        nlohmann::json segs = nlohmann::json::array();

        int state = 0; // 0=找日期行, 1=等空行, 2=找时间行, 3=读内容
        std::string currentTime;
        std::string currentContent;

        bool gotDate = false;

        while (std::getline(blockStream, line)) {
            // 去除行尾\r
            if (!line.empty() && line.back() == '\r') line.pop_back();

            // 跳过分隔线本身
            if (line.find("——") == 0) continue;
            // 跳过全是"—"的行
            bool allDash = !line.empty();
            for (char c : line) {
                if (c != '\xE2' && c != '\x80' && c != '\x94' && c != '-' && c != '\xEF' && c != '\xBC' && c != '\x8D') {
                    allDash = false; break;
                }
            }
            if (allDash) continue;

            if (!gotDate) {
                // 寻找日期行: YYYY/M/D 或 YYYY/MM/DD
                int slashCount = 0;
                for (char c : line) if (c == '/') slashCount++;
                if (slashCount == 2 && !line.empty() && (line[0] >= '0' && line[0] <= '9')) {
                    dateLine = line;
                    // 解析日期
                    size_t s1 = line.find('/');
                    size_t s2 = line.find('/', s1 + 1);
                    if (s1 != std::string::npos && s2 != std::string::npos && s2 > s1 + 1) {
                        try {
                            int py = std::stoi(line.substr(0, s1));
                            int pm = std::stoi(line.substr(s1 + 1, s2 - s1 - 1));
                            int pd = std::stoi(line.substr(s2 + 1));
                            entry["year"] = py;
                            entry["month"] = pm;
                            entry["day"] = pd;
                            gotDate = true;
                            state = 2; // 跳过空行状态，直接等时间行
                        } catch (...) {
                            // 日期解析失败，跳过
                        }
                    }
                }
                continue;
            }

            if (state == 2) {
                // 查找时间行: HH:MM; 或 HH：MM；
                // 包含冒号（:）且以分号（;）或全角分号（；）结尾
                std::string fullColon = "\xEF\xBC\x9A";
                bool hasColon = (line.find(':') != std::string::npos || line.find(fullColon) != std::string::npos);
                bool endsWithSemi = false;
                if (!line.empty()) {
                    if (line.back() == ';') {
                        endsWithSemi = true;
                    } else if (line.size() >= 3 && line.substr(line.size() - 3) == "\xEF\xBC\x9B") {
                        endsWithSemi = true;
                    }
                }

                if (hasColon && endsWithSemi) {
                    // 保存上一个segment
                    if (!currentTime.empty()) {
                        nlohmann::json seg;
                        seg["time"] = currentTime;
                        seg["content"] = currentContent;
                        // 去除内容末尾多余空行
                        while (!seg["content"].get<std::string>().empty() && seg["content"].get<std::string>().back() == '\n') {
                            std::string c = seg["content"].get<std::string>();
                            c.pop_back();
                            seg["content"] = c;
                        }
                        segs.push_back(seg);
                        currentContent.clear();
                    }
                    // 解析时间
                    currentTime = line;
                    // 去除分号
                    if (!currentTime.empty() && currentTime.back() == ';') currentTime.pop_back();
                    // 去除全角分号
                    if (currentTime.size() >= 3) {
                        std::string tail = currentTime.substr(currentTime.size() - 3);
                        if (tail == "\xEF\xBC\x9B") currentTime = currentTime.substr(0, currentTime.size() - 3);
                    }
                    // 全角冒号转半角
                    size_t fc = currentTime.find("\xEF\xBC\x9A");
                    if (fc != std::string::npos) {
                        currentTime.replace(fc, 3, ":");
                    }
                    state = 3;
                } else if (!line.empty()) {
                    // 可能是内容没有时间标记，合并到上一个segment
                    if (!currentTime.empty() && !line.empty()) {
                        currentContent += line + "\n";
                    }
                }
                continue;
            }

            if (state == 3) {
                // 读取内容直到遇到下一个时间行或分隔线
                std::string fullColon2 = "\xEF\xBC\x9A";
                bool hasColon = (line.find(':') != std::string::npos || line.find(fullColon2) != std::string::npos);
                bool endsWithSemi = false;
                if (!line.empty()) {
                    if (line.back() == ';') {
                        endsWithSemi = true;
                    } else if (line.size() >= 3 && line.substr(line.size() - 3) == "\xEF\xBC\x9B") {
                        endsWithSemi = true;
                    }
                }

                if (hasColon && endsWithSemi) {
                    // 新时间行
                    if (!currentTime.empty()) {
                        nlohmann::json seg;
                        seg["time"] = currentTime;
                        seg["content"] = currentContent;
                        while (!seg["content"].get<std::string>().empty() && seg["content"].get<std::string>().back() == '\n') {
                            std::string c = seg["content"].get<std::string>();
                            c.pop_back();
                            seg["content"] = c;
                        }
                        segs.push_back(seg);
                        currentContent.clear();
                    }
                    currentTime = line;
                    if (!currentTime.empty() && currentTime.back() == ';') currentTime.pop_back();
                    if (currentTime.size() >= 3 && currentTime.substr(currentTime.size() - 3) == "\xEF\xBC\x9B")
                        currentTime = currentTime.substr(0, currentTime.size() - 3);
                    size_t fc2 = currentTime.find("\xEF\xBC\x9A");
                    if (fc2 != std::string::npos) currentTime.replace(fc2, 3, ":");
                } else {
                    currentContent += line + "\n";
                }
            }
        }

        // 保存最后一个segment
        if (!currentTime.empty()) {
            nlohmann::json seg;
            seg["time"] = currentTime;
            seg["content"] = currentContent;
            while (!seg["content"].get<std::string>().empty() && seg["content"].get<std::string>().back() == '\n') {
                std::string c = seg["content"].get<std::string>();
                c.pop_back();
                seg["content"] = c;
            }
            segs.push_back(seg);
        }

        if (gotDate && !segs.empty()) {
            entry["segments"] = segs;
            parsedEntries.push_back(entry);
        }
    }

    if (parsedEntries.empty()) {
        wprintln(L"未能从导入文本中解析出任何日记");
        wprintln(L"请确认格式正确（分隔线 + 日期行 + 时间行 + 内容）");
        return;
    }

    wprintln(L"\n解析到 " + std::to_wstring(parsedEntries.size()) + L" 篇日记");

    // 导入模式选择
    std::vector<MenuItem> conflictItems = {
        {L"--- 导入冲突处理 ---", false},
        {L"1. 完全替换当前日记库", true},
        {L"2. 合并导入，同日覆盖（导入数据优先）", true},
        {L"3. 合并导入，同日保留（合并segments）", true},
        {L"0. 取消导入", true},
    };
    int conflictChoice = menuSelect(conflictItems, 1);
    if (conflictChoice == 4 || conflictChoice == MENU_ESC) return;

    if (conflictChoice == 1) {
        // 完全替换
        store.entries() = nlohmann::json::array();
        for (auto& e : parsedEntries) {
            store.addEntry(e);
        }
    } else if (conflictChoice == 2) {
        // 同日覆盖
        for (auto& e : parsedEntries) {
            int ey = e.value("year", 0);
            int em = e.value("month", 0);
            int ed = e.value("day", 0);
            int fidx = store.findEntry(ey, em, ed);
            if (fidx >= 0) {
                store.updateEntry(fidx, e);
            } else {
                store.addEntry(e);
            }
        }
    } else if (conflictChoice == 3) {
        // 同日保留（合并segments）
        for (auto& e : parsedEntries) {
            int ey = e.value("year", 0);
            int em = e.value("month", 0);
            int ed = e.value("day", 0);
            int fidx = store.findEntry(ey, em, ed);
            if (fidx >= 0) {
                // 合并segments
                auto& existingSegs = store.entries()[fidx]["segments"];
                for (auto& seg : e["segments"]) {
                    existingSegs.push_back(seg);
                }
            } else {
                store.addEntry(e);
            }
        }
    }

    store.save(DIARY_PATH, password);
    wprintln(L"[导入完成，已保存]  (^_^)");
}

// ─── 导出导入子菜单 ───

static void exportImportMenu(DiaryStore& store, const std::string& password) {
    while (true) {
        clearScreen();
        std::vector<MenuItem> items = {
            {L"--- 导出 / 导入 ---", false},
            {L"1. 导出日记", true},
            {L"2. 导入日记", true},
            {L"0. 返回", true},
        };
        int choice = menuSelect(items, 1);
        clearScreen();
        if (choice == MENU_ESC || choice == 3) {
            break;
        }
        if (choice == 1) {
            exportDiary(store);
            pauseScreen();
        } else if (choice == 2) {
            importDiary(store, password);
            pauseScreen();
        } else {
            break;
        }
    }
}

// ─── 修改密码 ───

static void changePasswordInteractive(const std::string& currentPass) {
    clearScreen();
    wprintln(L"══════════ 修改密码 ══════════");

    auto vRes = readPasswordCancelable("输入当前密码确认身份 (Esc 取消): ");
    if (vRes.cancelled) return;
    if (vRes.value != currentPass) {
        wprintln(L"身份验证失败!");
        sodium_memzero(vRes.value.data(), vRes.value.size());
        return;
    }
    sodium_memzero(vRes.value.data(), vRes.value.size());

    auto nRes = readPasswordCancelable("输入新密码 (Esc 取消): ");
    if (nRes.cancelled) return;
    std::string newPass = nRes.value;

    auto cRes = readPasswordCancelable("确认新密码 (Esc 取消): ");
    if (cRes.cancelled) { sodium_memzero(newPass.data(), newPass.size()); return; }
    std::string newPassConfirm = cRes.value;

    if (newPass != newPassConfirm) {
        wprintln(L"两次密码不一致!");
        sodium_memzero(newPass.data(), newPass.size());
        sodium_memzero(newPassConfirm.data(), newPassConfirm.size());
        return;
    }
    sodium_memzero(newPassConfirm.data(), newPassConfirm.size());

    // 用旧密码解密，新密码加密
    DiaryStore store;
    if (store.load(DIARY_PATH, currentPass)) {
        if (store.save(DIARY_PATH, newPass)) {
            wprintln(L"[密码已修改]  (^_^)");
        } else {
            wprintln(L"[保存失败!]");
        }
    } else {
        wprintln(L"[解密失败，密码修改未完成]");
    }
    sodium_memzero(newPass.data(), newPass.size());
}

// ─── 神秘计数器 ───

static void counterPage(DiaryStore& store, const std::string& password) {
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

        writeAtColor(leftX + 2, topY + 2, L"CURRENT VALUE", AMBER_DIM);
        std::wstring countText = std::to_wstring(count);
        int countX = leftX + std::max(2, (leftW - static_cast<int>(countText.size())) / 2);
        writeAtColor(countX, topY + 6, countText, 0x0F);
        writeAtColor(leftX + 2, topY + heroH - 3, L"Tap history is archived on the right.", AMBER_DIM);

        std::vector<std::wstring> infoLines = buildCounterHistoryLines(history);
        writeWrappedPanelLines(rightX + 2, topY + 1, rightW - 4, heroH - 2, infoLines, AMBER);

        fillLine(shell.x + 1, shell.y + shell.h - 3, shell.w - 2, L' ', ATTR_NORMAL);
        fillLine(shell.x + 1, shell.y + shell.h - 2, shell.w - 2, L' ', ATTR_NORMAL);
        writeAtColor(shell.x + 2, shell.y + shell.h - 3, L"Enter执行  Esc返回  数值变更会立即保存", AMBER_DIM);
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
            store.save(DIARY_PATH, password);
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

            store.setCounter(newVal);
            store.pushCounterHistory(getCurrentTimestampStr());
            store.save(DIARY_PATH, password);
            showFullScreenMessage(L"COUNTER UPDATED", {L"[计数器已更新]"});
            pauseScreen();
        }
    }
}

// ─── 主循环 ───

static void mainLoop(DiaryStore& store, const std::string& password) {
    while (true) {
        DiaryMetrics m = computeMetrics(store);
        MainPageLayout layout = renderMainPage(m, DIARY_PATH);

        std::vector<MenuItem> menuItems = {
            {L"1. 写入 / 编辑今日", true},
            {L"2. 查看全部", true},
            {L"3. 按日期编辑", true},
            {L"4. 导出 / 导入", true},
            {L"5. 修改密码", true},
            {L"6. 神秘计数器", true},
            {L"7. 保存并退出", true},
        };

        int choice = menuSelectInRegion(
            layout.menuX, layout.menuY,
            layout.menuW, layout.menuH,
            menuItems, 0);

        if (choice == MENU_RESIZE) {
            continue;
        }

        if (choice == MENU_ESC) {
            continue;
        }

        switch (choice) {
            case 0:  // 写入/编辑今日
                clearScreen();
                writeOrEditToday(store, password);
                break;
            case 1:  // 查看全部
                clearScreen();
                viewAllDiaries(store);
                break;
            case 2:  // 按日期编辑
                clearScreen();
                editByDate(store, password);
                break;
            case 3:  // 导出/导入
                exportImportMenu(store, password);
                break;
            case 4:  // 修改密码
                changePasswordInteractive(password);
                pauseScreen();
                break;
            case 5:  // 神秘计数器
                clearScreen();
                counterPage(store, password);
                break;
            case 6:  // 保存并退出
                store.save(DIARY_PATH, password);
                wprintln(L"\n[日记已保存，再见!]  (^_^)");
                return;
        }
    }
}

// ─── 首次设置 ───

static void firstTimeSetup() {
    std::string pass = readPassword("设置日记密码 (不回显): ");
    std::string passConfirm = readPassword("确认密码: ");
    if (pass != passConfirm) {
        wprintln(L"两次密码不一致，设置失败。");
        exit(1);
    }

    DiaryStore store;
    store.initEmpty();
    if (!store.save(DIARY_PATH, pass)) {
        wprintln(L"保存失败!");
        exit(1);
    }

    sodium_memzero(pass.data(), pass.size());
    sodium_memzero(passConfirm.data(), passConfirm.size());
    wprintln(L"\n设置完成! 请重新运行程序登录。");
}

// ─── 文件工具 ───

static bool fileExists(const char* path) {
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(path, &fd);
    if (h != INVALID_HANDLE_VALUE) { FindClose(h); return true; }
    return false;
}

// ─── 登录 ───

static int loginLoop() {
    bool diaryExists = fileExists(DIARY_PATH);
    if (!diaryExists) { firstTimeSetup(); pauseScreen(); return MODE_EXIT; }

    const int MAX_ATTEMPTS = 3;
    for (int attempt = 0; attempt < MAX_ATTEMPTS; ++attempt) {
        std::string password = readLoginPassword(attempt, MAX_ATTEMPTS);

        DiaryStore store;
        if (store.load(DIARY_PATH, password)) {
            mainLoop(store, password);
            sodium_memzero(password.data(), password.size());
            return MODE_EXIT;
        }

        showFullScreenMessage(L"ACCESS WARNING", {L"密码错误!"}, L"按任意键继续...");
        sodium_memzero(password.data(), password.size());
        if (attempt < MAX_ATTEMPTS - 1) pauseScreen();
    }
    showFullScreenMessage(L"ACCESS DENIED", {L"尝试次数过多，程序退出。"}, L"按任意键继续...");
    pauseScreen();
    return MODE_EXIT;
}

// ─── 入口 ───

int main() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    g_hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    g_hIn  = GetStdHandle(STD_INPUT_HANDLE);
    g_startTick = GetTickCount();

    if (!crypto::init()) {
        std::cerr << "加密库初始化失败!" << std::endl;
        pauseScreen();
        return 1;
    }
    CreateDirectoryA("data", nullptr);

    loginLoop();
    return 0;
}
