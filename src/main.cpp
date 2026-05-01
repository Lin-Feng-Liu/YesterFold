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

static const char* DIARY_PATH       = "data\\diary.enc";
static const char* EXPORT_TXT_PATH  = "data\\export_diary.txt";
static const char* IMPORT_TXT_PATH  = "data\\import_diary.txt";

static const int MODE_EXIT   = 0;
static const int MODE_SWITCH = 1;
static const int MENU_ESC    = -2;

static HANDLE g_hOut = nullptr;
static HANDLE g_hIn  = nullptr;

// ─── UTF-8 与 wstring 互转 ───

static std::wstring utf8_to_wstring(const std::string& utf8) {
    if (utf8.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
    if (len <= 0) return L"";
    std::wstring result(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), &result[0], len);
    return result;
}

static std::string wstring_to_utf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return "";
    std::string result(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.size()), &result[0], len, nullptr, nullptr);
    return result;
}

// ─── 宽字符屏幕宽度 ───

static int wcharWidth(wchar_t ch) {
    if (ch == L'\n' || ch == L'\r') return 0;
    if (ch < 0x20) return 0;
    // 中文等全角字符
    if (ch >= 0x4E00 && ch <= 0x9FFF) return 2;
    if (ch >= 0x3000 && ch <= 0x303F) return 2; // CJK标点
    if (ch >= 0xFF00 && ch <= 0xFFEF) return 2; // 全角形式
    if (ch >= 0x2000 && ch <= 0x206F) return 2; // 通用标点(含EM DASH)
    if (ch == 0x2014) return 2;  // EM DASH —
    if (ch < 0x80) return 1;
    return 2; // 其他非ASCII默认宽字符
}

// ─── 直接输出宽字符（不依赖 C++ 流 locale） ───

static void wprint(const std::wstring& s) {
    DWORD written;
    WriteConsoleW(g_hOut, s.c_str(), (DWORD)s.size(), &written, NULL);
}

static void wprintln(const std::wstring& s) {
    wprint(s);
    wprint(L"\r\n");
}

static void wprintln() {
    wprint(L"\r\n");
}

// ─── 控制台工具 ───

static void clearScreen() { system("cls"); }

static void pauseScreen() {
    FlushConsoleInputBuffer(g_hIn);
    wprint(L"\n按任意键继续...");
    DWORD oldMode; GetConsoleMode(g_hIn, &oldMode);
    SetConsoleMode(g_hIn, ENABLE_EXTENDED_FLAGS | ENABLE_WINDOW_INPUT);
    while (true) {
        INPUT_RECORD ir; DWORD read;
        if (!ReadConsoleInputW(g_hIn, &ir, 1, &read) || read == 0) continue;
        if (ir.EventType == KEY_EVENT && ir.Event.KeyEvent.bKeyDown) break;
    }
    SetConsoleMode(g_hIn, oldMode);
}

static std::string readPassword(const std::string& prompt) {
    std::cout << prompt; std::cout.flush();
    DWORD oldMode; GetConsoleMode(g_hIn, &oldMode);
    SetConsoleMode(g_hIn, oldMode & ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT));
    std::wstring wpass;
    while (true) {
        WCHAR ch; DWORD read; ReadConsoleW(g_hIn, &ch, 1, &read, nullptr);
        if (ch == L'\r' || ch == L'\n') break;
        if (ch == L'\b' || ch == 127) {
            if (!wpass.empty()) { wpass.pop_back(); std::cout << "\b \b"; }
        } else if (ch == 3) {
            SetConsoleMode(g_hIn, oldMode); std::cout << std::endl; exit(0);
        } else {
            wpass.push_back(ch);
            wprint(L"*");
        }
    }
    SetConsoleMode(g_hIn, oldMode); wprintln();
    return wstring_to_utf8(wpass);
}

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
};

static InputResult readPasswordCancelable(const std::string& prompt) {
    std::cout << prompt; std::cout.flush();
    DWORD oldMode; GetConsoleMode(g_hIn, &oldMode);
    SetConsoleMode(g_hIn, ENABLE_EXTENDED_FLAGS | ENABLE_WINDOW_INPUT);
    std::wstring wpass;
    while (true) {
        INPUT_RECORD ir; DWORD read;
        if (!ReadConsoleInputW(g_hIn, &ir, 1, &read) || read == 0) continue;
        if (ir.EventType != KEY_EVENT || !ir.Event.KeyEvent.bKeyDown) continue;
        WORD vk = ir.Event.KeyEvent.wVirtualKeyCode;
        WCHAR ch = ir.Event.KeyEvent.uChar.UnicodeChar;
        if (vk == VK_RETURN) break;
        if (vk == VK_ESCAPE) {
            SetConsoleMode(g_hIn, oldMode); std::cout << std::endl;
            return {true, ""};
        }
        if (vk == VK_BACK) {
            if (!wpass.empty()) { wpass.pop_back(); std::cout << "\b \b"; }
        } else if (ch >= L' ') {
            wpass.push_back(ch);
            std::cout << '*';
        }
        std::cout.flush();
    }
    SetConsoleMode(g_hIn, oldMode); std::cout << std::endl;
    return {false, wstring_to_utf8(wpass)};
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
                return {true, ""};
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
        if (!result.empty() || allowEmpty) return {false, result};
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

// ─── 方向键菜单 (复用旧项目逻辑) ───

struct MenuItem {
    std::wstring text;
    bool selectable;
};

static int menuSelect(const std::vector<MenuItem>& items, int startIdx = 0) {
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

    // 用 Wide 函数操作控制台
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
            // Esc → 返回上一级
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

static EditorResult openDiaryEditor(const std::wstring& initialContent) {
    HANDLE hOut = g_hOut;
    HANDLE hIn  = g_hIn;

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hOut, &csbi);
    SHORT screenW = csbi.dwSize.X;
    if (screenW < 20) screenW = 80;

    // 回显模式: 读取输入并显示
    DWORD oldInMode;
    GetConsoleMode(hIn, &oldInMode);
    SetConsoleMode(hIn, ENABLE_EXTENDED_FLAGS | ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT);

    std::wstring buf = initialContent;
    size_t cursor = buf.size();

    // 编辑器屏幕布局
    int headerLines = 4; // 标题 + 日期 + 分隔 + 提示
    int footerLines = 2; // 底部提示
    int editStartLine = csbi.dwCursorPosition.Y;
    int maxEditLines = csbi.dwSize.Y - editStartLine - footerLines;
    if (maxEditLines < 3) maxEditLines = 3;

    auto renderEditor = [&]() {
        // 计算行信息
        auto lines = calcEditorLines(buf, screenW - 2);
        int cursorRow, cursorCol;
        findCursorPos(buf, cursor, lines, screenW - 2, cursorRow, cursorCol);

        // 清空编辑区域
        COORD clearPos = {0, (SHORT)editStartLine};
        DWORD toClear = (csbi.dwSize.Y - editStartLine) * csbi.dwSize.X;
        DWORD written;
        FillConsoleOutputCharacterW(hOut, L' ', toClear, clearPos, &written);
        FillConsoleOutputAttribute(hOut, csbi.wAttributes, toClear, clearPos, &written);

        // 滚动: 确保光标行可见
        int scrollOff = 0;
        if (cursorRow >= maxEditLines) scrollOff = cursorRow - maxEditLines + 1;

        // 绘制可见行
        for (int li = scrollOff; li < (int)lines.size() && (li - scrollOff) < maxEditLines; ++li) {
            COORD pos = {0, (SHORT)(editStartLine + li - scrollOff)};
            SetConsoleCursorPosition(hOut, pos);

            size_t lineStart = lines[li].startIdx;
            size_t lineEnd = (li + 1 < (int)lines.size()) ? lines[li + 1].startIdx : buf.size();
            if (lineEnd > buf.size()) lineEnd = buf.size();

            std::wstring lineText;
            for (size_t ci = lineStart; ci < lineEnd; ++ci) {
                if (buf[ci] == L'\n') break;
                lineText += buf[ci];
            }
            wprint(lineText);
        }

        // 设置光标位置
        int cursorScreenRow = editStartLine + cursorRow - scrollOff;
        if (cursorScreenRow < editStartLine) cursorScreenRow = editStartLine;
        COORD cursorPos = {(SHORT)cursorCol, (SHORT)cursorScreenRow};
        SetConsoleCursorPosition(hOut, cursorPos);
    };

    // 在编辑区域底部显示确认提示
    auto showConfirmBar = [&](bool visible) {
        int barLine = csbi.dwSize.Y - 1;
        COORD barPos = {0, (SHORT)barLine};
        DWORD written;
        FillConsoleOutputCharacterW(hOut, L' ', csbi.dwSize.X, barPos, &written);
        FillConsoleOutputAttribute(hOut, csbi.wAttributes, csbi.dwSize.X, barPos, &written);
        if (visible) {
            SetConsoleCursorPosition(hOut, barPos);
            SetConsoleTextAttribute(hOut, 0x70);
            wprint(L"  Enter=确认保存    Esc=继续编辑  ");
            SetConsoleTextAttribute(hOut, 0x07);
        }
    };

    renderEditor();
    showConfirmBar(false);

    // 主输入循环
    bool inConfirm = false;
    while (true) {
        INPUT_RECORD ir;
        DWORD read;
        if (!ReadConsoleInputW(hIn, &ir, 1, &read) || read == 0) continue;

        if (ir.EventType != KEY_EVENT || !ir.Event.KeyEvent.bKeyDown) continue;

        WORD vk = ir.Event.KeyEvent.wVirtualKeyCode;
        DWORD ctrl = ir.Event.KeyEvent.dwControlKeyState;
        WCHAR ch = ir.Event.KeyEvent.uChar.UnicodeChar;

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
                renderEditor();
            }
            continue;
        }

        // Shift+Enter → 插入换行
        if (vk == VK_RETURN && (ctrl & SHIFT_PRESSED)) {
            buf.insert(buf.begin() + cursor, L'\n');
            cursor++;
            renderEditor();
            continue;
        }

        // Ctrl+Enter → 插入换行（备选）
        if (vk == VK_RETURN && (ctrl & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))) {
            buf.insert(buf.begin() + cursor, L'\n');
            cursor++;
            renderEditor();
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

        // 左箭头
        if (vk == VK_LEFT) {
            if (cursor > 0) {
                cursor--;
                renderEditor();
            }
            continue;
        }

        // 右箭头
        if (vk == VK_RIGHT) {
            if (cursor < buf.size()) {
                cursor++;
                renderEditor();
            }
            continue;
        }

        // 上箭头
        if (vk == VK_UP) {
            auto lines = calcEditorLines(buf, screenW - 2);
            int cr, cc;
            findCursorPos(buf, cursor, lines, screenW - 2, cr, cc);
            if (cr > 0) {
                cursor = rowColToIndex(buf, lines, cr - 1, cc, screenW - 2);
                renderEditor();
            }
            continue;
        }

        // 下箭头
        if (vk == VK_DOWN) {
            auto lines = calcEditorLines(buf, screenW - 2);
            int cr, cc;
            findCursorPos(buf, cursor, lines, screenW - 2, cr, cc);
            if (cr < (int)lines.size() - 1) {
                cursor = rowColToIndex(buf, lines, cr + 1, cc, screenW - 2);
                renderEditor();
            }
            continue;
        }

        // Home
        if (vk == VK_HOME) {
            auto lines = calcEditorLines(buf, screenW - 2);
            int cr, cc;
            findCursorPos(buf, cursor, lines, screenW - 2, cr, cc);
            size_t lineStart = lines[cr].startIdx;
            cursor = lineStart;
            renderEditor();
            continue;
        }

        // End
        if (vk == VK_END) {
            auto lines = calcEditorLines(buf, screenW - 2);
            int cr, cc;
            findCursorPos(buf, cursor, lines, screenW - 2, cr, cc);
            size_t lineEnd = (cr + 1 < (int)lines.size()) ? lines[cr + 1].startIdx : buf.size();
            if (lineEnd > buf.size()) lineEnd = buf.size();
            if (lineEnd > lines[cr].startIdx && buf[lineEnd - 1] == L'\n') lineEnd--;
            cursor = lineEnd;
            renderEditor();
            continue;
        }

        // Backspace
        if (vk == VK_BACK) {
            if (cursor > 0) {
                cursor--;
                buf.erase(buf.begin() + cursor);
                renderEditor();
            }
            continue;
        }

        // Delete
        if (vk == VK_DELETE) {
            if (cursor < buf.size()) {
                buf.erase(buf.begin() + cursor);
                renderEditor();
            }
            continue;
        }

        // 可打印字符或中文字符
        if (ch >= L' ' || (ch >= 0x4E00 && ch <= 0x9FFF) || (ch >= 0x3000 && ch <= 0x303F) || (ch >= 0xFF00 && ch <= 0xFFEF)) {
            buf.insert(buf.begin() + cursor, ch);
            cursor++;
            renderEditor();
            continue;
        }
    }
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
    if (sorted.empty()) {
        wprintln(L"日记本为空");
        return;
    }
    wprintln(L"\n======= 全部日记 (" + std::to_wstring(sorted.size()) + L" 篇) =======");
    for (size_t i = 0; i < sorted.size(); ++i) {
        printDiary(store.entries()[sorted[i]]);
    }
    wprintln(L"========================================");
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

    // 显示已有segment
    clearScreen();
    wprintln(L"══════════════ 写入日记 ══════════════");
    wprintln(L"日期: " + utf8_to_wstring(getCurrentDateStr()));
    wprintln(L"────────────────────────────────────");

    auto& entry = store.entries()[idx];
    size_t segCount = entry["segments"].size();
    if (segCount > 1) {
        wprintln(L"[已有记录]");
        for (size_t si = 0; si < segCount - 1; ++si) {
            const auto& seg = entry["segments"][si];
            std::string timeStr = seg.value("time", "");
            std::string content = seg.value("content", "");
            wprint(L"  "); wprint(utf8_to_wstring(timeStr)); wprintln(L";");
            if (!content.empty()) {
                wprintln(utf8_to_wstring(content));
            }
            wprintln();
        }
    }

    wprintln(L">> 新增时间: " + utf8_to_wstring(currentTime));
    wprintln(L"────────────────────────────────────");
    wprintln(L"══════════════════════════════════════");

    // 获取当前光标位置作为编辑区域起始
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(g_hOut, &csbi);
    // 确保编辑区域有足够空间
    if (csbi.dwCursorPosition.Y >= csbi.dwSize.Y - 4) {
        // 编辑区域不够，重新设置
        csbi.dwCursorPosition.Y = (SHORT)(csbi.dwSize.Y - 10);
        SetConsoleCursorPosition(g_hOut, csbi.dwCursorPosition);
    }

    // 打开编辑器
    EditorResult result = openDiaryEditor(L"");

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
            wprintln(L"\n[日记已保存]  (^_^)");
        } else {
            // 内容为空，删除自动添加的segment
            auto& segs = store.entries()[idx]["segments"];
            segs.erase(segs.size() - 1);
            if (segs.empty()) {
                store.removeEntry(idx);
                wprintln(L"\n[未输入内容，已取消]");
            } else {
                wprintln(L"\n[未输入新内容，保留已有记录]");
            }
            store.save(DIARY_PATH, password);
        }
    } else {
        // 用户取消（Esc）
        if (hasBackup) {
            store.updateEntry(idx, entryCopy);
        } else {
            store.removeEntry(idx);
        }
        wprintln(L"\n[已取消]");
    }
}

// ─── 按日期编辑 ───

static void editByDate(DiaryStore& store, const std::string& password) {
    auto sorted = store.getSortedIndices();
    if (sorted.empty()) {
        wprintln(L"日记本为空，没有可编辑的内容");
        return;
    }

    // 收集年份
    std::vector<int> years;
    for (size_t i : sorted) {
        int y = store.entries()[i].value("year", 0);
        if (std::find(years.begin(), years.end(), y) == years.end()) years.push_back(y);
    }

    // 子菜单结构 (标签用于 Esc 返回上一级)
sel_year:
    std::vector<MenuItem> yearItems;
    yearItems.push_back({L"--- 选择年份 ---", false});
    std::vector<int> yearVal;
    for (int y : years) {
        yearItems.push_back({utf8_to_wstring(std::to_string(y) + " 年"), true});
        yearVal.push_back(y);
    }
    yearItems.push_back({L"0. 返回", true});

    int yearChoice = menuSelect(yearItems, 1);
    if (yearChoice == (int)yearItems.size() - 1 || yearChoice == MENU_ESC) return;
    int selYear = yearVal[yearChoice - 1];
    clearScreen();

sel_month:
    std::vector<int> months;
    for (size_t i : sorted) {
        auto& e = store.entries()[i];
        if (e.value("year", 0) == selYear) {
            int m = e.value("month", 0);
            if (std::find(months.begin(), months.end(), m) == months.end()) months.push_back(m);
        }
    }
    std::sort(months.begin(), months.end());

    std::vector<MenuItem> monthItems;
    monthItems.push_back({L"--- 选择月份 ---", false});
    std::vector<int> monthVal;
    for (int m : months) {
        monthItems.push_back({utf8_to_wstring(std::to_string(m) + " 月"), true});
        monthVal.push_back(m);
    }
    monthItems.push_back({L"0. 返回", true});

    int monthChoice = menuSelect(monthItems, 1);
    if (monthChoice == (int)monthItems.size() - 1) goto sel_year;
    if (monthChoice == MENU_ESC) goto sel_year;
    int selMonth = monthVal[monthChoice - 1];
    clearScreen();

sel_day:
    std::vector<int> days;
    std::vector<size_t> dayEntryIndices;
    for (size_t i : sorted) {
        auto& e = store.entries()[i];
        if (e.value("year", 0) == selYear && e.value("month", 0) == selMonth) {
            int d = e.value("day", 0);
            days.push_back(d);
            dayEntryIndices.push_back(i);
        }
    }

    std::vector<MenuItem> dayItems;
    dayItems.push_back({L"--- 选择日期 ---", false});
    std::vector<int> dayVal;
    for (int d : days) {
        dayItems.push_back({utf8_to_wstring(std::to_string(d) + " 日"), true});
        dayVal.push_back(d);
    }
    dayItems.push_back({L"0. 返回", true});

    int dayChoice = menuSelect(dayItems, 1);
    if (dayChoice == (int)dayItems.size() - 1) goto sel_month;
    if (dayChoice == MENU_ESC) goto sel_month;
    int selDay = dayVal[dayChoice - 1];
    size_t entryIdx = dayEntryIndices[dayChoice - 1];
    clearScreen();

    // ── 显示日记并提供操作 ──
    auto& entry = store.entries()[entryIdx];
    int ey = entry.value("year", 0), em = entry.value("month", 0), ed = entry.value("day", 0);

    while (true) {
        clearScreen();
        wprintln(L"══════════════ 查看/编辑日记 ══════════════");
        wprintln(L"日期: " + utf8_to_wstring(std::to_string(ey) + "/" + std::to_string(em) + "/" + std::to_string(ed)));
        wprintln(L"────────────────────────────────────────");

        auto& segs = entry["segments"];
        wprintln(L"共 " + std::to_wstring(segs.size()) + L" 条记录：");
        for (size_t si = 0; si < segs.size(); ++si) {
            const auto& seg = segs[si];
            std::string timeStr = seg.value("time", "");
            std::string content = seg.value("content", "");
            wprintln(L"\n[" + std::to_wstring(si + 1) + L"] " + utf8_to_wstring(timeStr) + L";");
            if (!content.empty()) {
                wprintln(utf8_to_wstring(content));
            } else {
                wprintln(L"(空内容)");
            }
        }
        wprintln(L"────────────────────────────────────────");

        std::vector<MenuItem> opItems;
        opItems.push_back({L"--- 操作 ---", false});
        opItems.push_back({L"查看日记（只读）", true});
        for (size_t si = 0; si < segs.size(); ++si) {
            std::string timeStr = segs[si].value("time", "");
            std::wstring label = L"编辑记录 " + std::to_wstring(si + 1) + L" (" + utf8_to_wstring(timeStr) + L")";
            opItems.push_back({label, true});
        }
        opItems.push_back({L"添加新记录", true});
        opItems.push_back({L"删除某条记录", true});
        opItems.push_back({L"删除整篇日记", true});
        opItems.push_back({L"0. 返回", true});

        int opChoice = menuSelect(opItems, 1);

        // Esc → 返回日期选择
        if (opChoice == MENU_ESC) goto sel_day;

        size_t segCount = segs.size();
        // 查看日记
        if (opChoice == 1) {
            clearScreen();
            printDiary(entry);
            pauseScreen();
        }
        // 编辑某条记录
        else if (opChoice >= 2 && opChoice <= (int)segCount + 1) {
            int segIdx = opChoice - 2;
            std::string oldContent = segs[segIdx].value("content", "");
            std::wstring oldW = utf8_to_wstring(oldContent);

            clearScreen();
            wprintln(L"══════ 编辑记录 " + std::to_wstring(segIdx + 1) + L" ══════");
            wprintln(L"时间: " + utf8_to_wstring(segs[segIdx].value("time", "")) + L";");
            wprintln(L"────────────────────────────────────");
            wprintln(L"（修改后的内容）");
            wprintln(L"══════════════════════════════════════");

            CONSOLE_SCREEN_BUFFER_INFO csbi2;
            GetConsoleScreenBufferInfo(g_hOut, &csbi2);

            EditorResult er = openDiaryEditor(oldW);

            if (er.confirmed) {
                std::wstring trimmed = er.content;
                while (!trimmed.empty() && (trimmed.back() == L'\n' || trimmed.back() == L'\r' || trimmed.back() == L' ')) {
                    trimmed.pop_back();
                }
                segs[segIdx]["content"] = wstring_to_utf8(er.content);
                store.save(DIARY_PATH, password);
                wprintln(L"\n[记录已更新]  (^_^)");
            } else {
                wprintln(L"\n[已取消]");
            }
        }
        // 添加新记录
        else if (opChoice == (int)segCount + 2) {
            std::string newTime = getCurrentTimeStr();
            nlohmann::json newSeg;
            newSeg["time"] = newTime;
            newSeg["content"] = "";
            segs.push_back(newSeg);

            clearScreen();
            wprintln(L"══════ 添加新记录 ══════");
            wprintln(L"日期: " + utf8_to_wstring(std::to_string(ey) + "/" + std::to_string(em) + "/" + std::to_string(ed)));
            wprintln(L"新增时间: " + utf8_to_wstring(newTime) + L";");
            wprintln(L"────────────────────────────────────");
            wprintln(L"══════════════════════════════════════");

            CONSOLE_SCREEN_BUFFER_INFO csbi3;
            GetConsoleScreenBufferInfo(g_hOut, &csbi3);

            EditorResult er = openDiaryEditor(L"");

            if (er.confirmed) {
                std::wstring trimmed = er.content;
                while (!trimmed.empty() && (trimmed.back() == L'\n' || trimmed.back() == L'\r' || trimmed.back() == L' ')) {
                    trimmed.pop_back();
                }
                if (!trimmed.empty()) {
                    segs.back()["content"] = wstring_to_utf8(er.content);
                    store.save(DIARY_PATH, password);
                    wprintln(L"\n[记录已添加]");
                } else {
                    segs.erase(segs.size() - 1);
                    wprintln(L"\n[未输入内容，已取消]");
                }
            } else {
                segs.erase(segs.size() - 1);
                wprintln(L"\n[已取消]");
            }
        }
        // 删除某条记录
        else if (opChoice == (int)segCount + 3) {
            if (segs.empty()) { wprintln(L"没有可删除的记录"); pauseScreen(); continue; }
            auto scRes = readLineCancelable("删除第几条记录? (1-" + std::to_string(segs.size()) + ", 0取消): ");
            if (scRes.cancelled || scRes.value.empty()) continue;
            size_t si;
            try { si = std::stoull(scRes.value); } catch (...) { continue; }
            if (si == 0 || si > segs.size()) continue;
            auto cfmRes = readLineCancelable("确认删除记录 " + std::to_string(si) + "? (输入 yes 确认): ");
            if (cfmRes.cancelled) continue;
            if (cfmRes.value == "yes") {
                segs.erase(si - 1);
                if (segs.empty()) {
                    store.removeEntry(entryIdx);
                    store.save(DIARY_PATH, password);
                    wprintln(L"[日记已清空，整篇已删除]");
                    pauseScreen();
                    return;
                }
                store.save(DIARY_PATH, password);
                wprintln(L"[记录已删除]");
            }
        }
        // 删除整篇日记
        else if (opChoice == (int)segCount + 4) {
            auto cfmRes = readLineCancelable("确认删除这篇日记? (输入 yes 确认): ");
            if (cfmRes.cancelled) continue;
            if (cfmRes.value == "yes") {
                store.removeEntry(entryIdx);
                store.save(DIARY_PATH, password);
                wprintln(L"[日记已删除]");
                pauseScreen();
                return;
            }
        }
        // 返回
        else if (opChoice == (int)segCount + 5) {
            goto sel_day;
        }
        pauseScreen();
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

// ─── 主循环 ───

static void mainLoop(DiaryStore& store, const std::string& password) {
    while (true) {
        clearScreen();
        std::vector<MenuItem> items = {
            {L"==========================", false},
            {L"|       日记本           |", false},
            {L"==========================", false},
            {L"1. 写入 / 编辑今日", true},
            {L"2. 查看全部", true},
            {L"3. 按日期编辑", true},
            {L"4. 导出 / 导入", true},
            {L"5. 修改密码", true},
            {L"6. 保存并退出", true},
            {L"==========================", false},
        };

        int choice = menuSelect(items, 3);

        // Esc → 退出确认
        if (choice == MENU_ESC) {
            if (confirmExitBar()) {
                store.save(DIARY_PATH, password);
                wprintln(L"\n[日记已保存，再见!]  (^_^)");
                return;
            }
            continue;
        }

        switch (choice) {
            case 3:  // 写入/编辑今日
                clearScreen();
                writeOrEditToday(store, password);
                pauseScreen();
                break;
            case 4:  // 查看全部
                clearScreen();
                viewAllDiaries(store);
                pauseScreen();
                break;
            case 5:  // 按日期编辑
                clearScreen();
                editByDate(store, password);
                break;
            case 6:  // 导出/导入
                exportImportMenu(store, password);
                break;
            case 7:  // 修改密码
                changePasswordInteractive(password);
                pauseScreen();
                break;
            case 8:  // 保存并退出
                store.save(DIARY_PATH, password);
                wprintln(L"\n[日记已保存，再见!]  (^_^)");
                return;
        }
    }
}

// ─── 首次设置 ───

static void firstTimeSetup() {
    clearScreen();
    wprintln(L"========================================");
    wprintln(L"|       欢迎使用日记本 - 首次设置       |");
    wprintln(L"========================================");
    wprintln(L"这是你第一次运行，需要设置日记密码。");
    wprintln();

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
        clearScreen();
        wprintln(L"------------------");
        wprintln(L"| 欢迎来到日记本 |");
        wprintln(L"------------------");
        wprintln();
        std::string password = readPassword("输入密码 (" + std::to_string(MAX_ATTEMPTS - attempt) + "次机会): ");

        DiaryStore store;
        if (store.load(DIARY_PATH, password)) {
            mainLoop(store, password);
            sodium_memzero(password.data(), password.size());
            return MODE_EXIT;
        }

        wprintln(L"密码错误!");
        sodium_memzero(password.data(), password.size());
        if (attempt < MAX_ATTEMPTS - 1) pauseScreen();
    }
    wprintln(L"\n尝试次数过多，程序退出。");
    pauseScreen();
    return MODE_EXIT;
}

// ─── 入口 ───

int main() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    g_hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    g_hIn  = GetStdHandle(STD_INPUT_HANDLE);

    if (!crypto::init()) {
        std::cerr << "加密库初始化失败!" << std::endl;
        pauseScreen();
        return 1;
    }
    CreateDirectoryA("data", nullptr);

    loginLoop();
    return 0;
}
