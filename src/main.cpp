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
#include "page_status.h"
#include "page_auth.h"
#include "page_counter.h"
#include "page_archive.h"
#include "page_transfer.h"
#include "time_utils.h"
#include "diary_format.h"

static const char* DIARY_PATH       = "data\\diary.enc";
static const char* EXPORT_TXT_PATH  = "data\\export_diary.txt";
static const char* IMPORT_TXT_PATH  = "data\\import_diary.txt";

static const int MODE_EXIT   = 0;
static const int MODE_SWITCH = 1;

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
    int boxW;
    int boxH;
    int dateX;
    int dateW;
    int countX;
    int countW;
    int modeX;
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

    fillLine(layout.titleX, layout.menu.menuY - 2, layout.titleW, L' ', ATTR_NORMAL);
    writeAtColor(layout.titleX, layout.menu.menuY - 2, fitTextToWidth(L"[ " + listTitle + L" ]", layout.titleW), AMBER_DIM);

    for (int row = 0; row < layout.menu.menuH; ++row) {
        fillLine(layout.menu.menuX, layout.menu.menuY + row, layout.menu.menuW, L' ', ATTR_NORMAL);
    }

    writeWrappedPanelLines(layout.infoX, layout.infoY, layout.infoW, layout.infoH, infoLines, AMBER);

    fillLine(1, layout.menu.menuY + layout.menu.menuH + 1, layout.navW, L' ', ATTR_NORMAL);
    writeAtColor(3, layout.menu.menuY + layout.menu.menuH + 1,
                 fitTextToWidth(L"Enter进入  Esc返回  PgUp/PgDn翻页  Home/End首尾", layout.navW - 2),
                 AMBER_DIM);
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

    fillLine(1, boxH - 3, boxW - 2, L' ', ATTR_NORMAL);
    fillLine(1, boxH - 2, boxW - 2, L' ', ATTR_NORMAL);
    writeAtColor(3, boxH - 2, padOrTrimText(L">> ENTRY OPERATIONS READY", boxW - 6), AMBER);

    DateEntryPageLayout layout;
    layout.boxW = boxW;
    layout.boxH = boxH;
    layout.dateX = 4;
    layout.dateW = boxW - 8;
    layout.countX = 4;
    layout.countW = boxW - 8;
    layout.modeX = 4;
    layout.modeW = boxW - 8;
    layout.previewX = panelX + 2;
    layout.previewY = previewBoxY + 1;
    layout.previewW = panelW - 4;
    layout.previewH = previewBoxH - 2;
    layout.previewLabelX = panelX + 2;
    layout.previewLabelW = panelW - 4;
    layout.actionLabelX = panelX + 2;
    layout.actionLabelW = panelW - 4;
    layout.hintX = 3;
    layout.hintW = boxW - 6;
    layout.statusY = boxH - 2;
    layout.menu = {panelX + 2, actionBoxY + 1, panelW - 4, actionBoxH - 2};
    return layout;
}

static void updateDateEntryPageHeader(const DateEntryPageLayout& layout, const nlohmann::json& entry) {
    int y = entry.value("year", 0);
    int m = entry.value("month", 0);
    int d = entry.value("day", 0);
    int segCount = (entry.contains("segments") && entry["segments"].is_array())
        ? static_cast<int>(entry["segments"].size()) : 0;

    fillLine(layout.dateX, 7, layout.dateW, L' ', ATTR_NORMAL);
    fillLine(layout.countX, 8, layout.countW, L' ', ATTR_NORMAL);
    fillLine(layout.modeX, 9, layout.modeW, L' ', ATTR_NORMAL);
    writeAtColor(layout.dateX, 7, fitTextToWidth(L"DATE : " + formatDateW(y, m, d), layout.dateW), AMBER);
    writeAtColor(layout.countX, 8, fitTextToWidth(L"SEGMENTS : " + std::to_wstring(segCount), layout.countW), AMBER);
    writeAtColor(layout.modeX, 9, fitTextToWidth(L"MODE : PREVIEW + EDIT OPERATIONS", layout.modeW), AMBER_DIM);

    fillLine(layout.previewLabelX, layout.previewY - 2, layout.previewLabelW, L' ', ATTR_NORMAL);
    fillLine(layout.actionLabelX, layout.menu.menuY - 2, layout.actionLabelW, L' ', ATTR_NORMAL);
    writeAtColor(layout.previewLabelX, layout.previewY - 2, fitTextToWidth(L"[ ENTRY_LOG ]", layout.previewLabelW), AMBER_DIM);
    writeAtColor(layout.actionLabelX, layout.menu.menuY - 2, fitTextToWidth(L"[ SEGMENT_ACTIONS ]", layout.actionLabelW), AMBER_DIM);

    fillLine(layout.hintX, layout.statusY - 1, layout.hintW, L' ', ATTR_NORMAL);
    writeAtColor(layout.hintX, layout.statusY - 1, fitTextToWidth(L"Enter执行  Esc返回  PgUp/PgDn翻页", layout.hintW), AMBER_DIM);
}

static bool dateEntryViewportChanged(const DateEntryPageLayout& layout) {
    ConsoleViewport view = getConsoleViewport();
    return view.w != layout.boxW || view.h != layout.boxH;
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
    DateEntryPageLayout layout = renderDateEntryPage(store.entries()[entryIdx]);

    while (entryIdx < store.entryCount()) {
        auto& entry = store.entries()[entryIdx];
        updateDateEntryPageHeader(layout, entry);
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

        if (opChoice == MENU_RESIZE) {
            layout = renderDateEntryPage(entry);
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
                store.save(DIARY_PATH, password);
                showFullScreenMessage(L"SEGMENT UPDATED", {L"[记录已更新]"});
            } else {
                showFullScreenMessage(L"EDIT CANCELLED", {L"[已取消]"});
            }
            pauseScreen();
            if (dateEntryViewportChanged(layout)) layout = renderDateEntryPage(entry);
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
            if (dateEntryViewportChanged(layout)) layout = renderDateEntryPage(entry);
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
                if (dateEntryViewportChanged(layout)) layout = renderDateEntryPage(entry);
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

// ─── 修改密码 ───

// ─── 神秘计数器 ───

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
                exportImportMenu(store, password, DIARY_PATH, EXPORT_TXT_PATH, IMPORT_TXT_PATH);
                break;
            case 4:  // 修改密码
                changePasswordInteractive(password, DIARY_PATH);
                break;
            case 5:  // 神秘计数器
                clearScreen();
                counterPage(store, password, DIARY_PATH);
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
