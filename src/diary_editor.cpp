#include "diary_editor.h"

#include "ui_render.h"

#include <algorithm>
#include <cwchar>
#include <windows.h>

namespace {

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

struct LineInfo {
    size_t startIdx;
    int displayLen;
};

EditorShellLayout renderEditorShell(const EditorScreenConfig& cfg) {
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

std::vector<LineInfo> calcEditorLines(const std::wstring& buf, int screenW) {
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
    if (lines.empty()) {
        LineInfo li;
        li.startIdx = 0;
        li.displayLen = 0;
        lines.push_back(li);
    }
    if (!buf.empty() && buf.back() == L'\n') {
        LineInfo li;
        li.startIdx = buf.size();
        li.displayLen = 0;
        lines.push_back(li);
    }
    return lines;
}

void findCursorPos(const std::wstring& buf, size_t cursor,
                   const std::vector<LineInfo>& lines, int screenW,
                   int& outRow, int& outCol) {
    outRow = (int)lines.size() - 1;
    outCol = 0;

    if (cursor >= buf.size()) {
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

size_t rowColToIndex(const std::wstring& buf, const std::vector<LineInfo>& lines,
                     int targetRow, int targetCol, int screenW) {
    if (lines.empty()) return 0;
    if (targetRow < 0) targetRow = 0;
    if (targetRow >= (int)lines.size()) targetRow = (int)lines.size() - 1;

    size_t lineStart = lines[targetRow].startIdx;
    size_t lineEnd = (targetRow + 1 < (int)lines.size()) ? lines[targetRow + 1].startIdx : buf.size();
    if (lineEnd > buf.size()) lineEnd = buf.size();

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

} // namespace

EditorResult openDiaryEditor(const std::wstring& initialContent,
                             const EditorScreenConfig& cfg) {
    HANDLE hOut = g_hOut;
    HANDLE hIn  = g_hIn;

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hOut, &csbi);
    int screenW = csbi.dwSize.X;
    int screenH = csbi.dwSize.Y;
    if (screenW < 20) screenW = 80;

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
        auto lines = calcEditorLines(buf, editWidth);
        int cursorRow, cursorCol;
        findCursorPos(buf, cursor, lines, editWidth, cursorRow, cursorCol);

        int scrollOff = 0;
        if (cursorRow >= maxEditLines) scrollOff = cursorRow - maxEditLines + 1;

        std::vector<RenderedLine> nextFrame(maxEditLines);

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

    bool inConfirm = false;
    bool vtCollecting = false;
    std::wstring vtNum;
    int vtKey = 0;
    bool vtReadingMod = false;

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

        if (vtCollecting) {
            if (ch >= L'0' && ch <= L'9') {
                vtNum += ch;
            } else if (ch == L'[' && vtNum.empty()) {
            } else if (ch == L';' && !vtNum.empty()) {
                vtKey = _wtoi(vtNum.c_str());
                vtNum.clear();
                vtReadingMod = true;
            } else if (ch == L'u' && vtReadingMod) {
                int mod = vtNum.empty() ? 1 : _wtoi(vtNum.c_str());
                vtCollecting = false;
                if (vtKey == 13 && (mod & 2)) {
                    buf.insert(buf.begin() + cursor, L'\n');
                    cursor++;
                    redrawPanels();
                }
            } else {
                vtCollecting = false;
            }
            continue;
        }

        if (ch == 0x1B) {
            DWORD avail = 0;
            GetNumberOfConsoleInputEvents(hIn, &avail);
            if (avail > 0) {
                INPUT_RECORD peek;
                DWORD peekRead;
                if (PeekConsoleInputW(hIn, &peek, 1, &peekRead) && peekRead > 0) {
                    if (peek.EventType == KEY_EVENT && peek.Event.KeyEvent.bKeyDown &&
                        peek.Event.KeyEvent.uChar.UnicodeChar == L'[') {
                        ReadConsoleInputW(hIn, &peek, 1, &peekRead);
                        vtCollecting = true;
                        vtNum.clear();
                        vtKey = 0;
                        vtReadingMod = false;
                        continue;
                    }
                }
            }
            if (inConfirm) {
                inConfirm = false;
                showConfirmBar(false);
                redrawPanels();
                continue;
            }
            SetConsoleMode(hIn, oldInMode);
            return {false, L""};
        }

        if (inConfirm) {
            if (vk == VK_RETURN) {
                showConfirmBar(false);
                SetConsoleMode(hIn, oldInMode);
                return {true, buf};
            }
            if (vk == VK_ESCAPE) {
                inConfirm = false;
                showConfirmBar(false);
                redrawPanels();
            }
            continue;
        }

        if (vk == VK_RETURN && (ctrl & SHIFT_PRESSED)) {
            buf.insert(buf.begin() + cursor, L'\n');
            cursor++;
            redrawPanels();
            continue;
        }

        if (vk == VK_RETURN && (ctrl & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))) {
            buf.insert(buf.begin() + cursor, L'\n');
            cursor++;
            redrawPanels();
            continue;
        }

        if (vk == VK_RETURN) {
            inConfirm = true;
            showConfirmBar(true);
            continue;
        }

        if (vk == VK_ESCAPE) {
            SetConsoleMode(hIn, oldInMode);
            return {false, L""};
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

        if (vk == VK_LEFT) {
            if (cursor > 0) {
                cursor--;
                redrawPanels();
            }
            continue;
        }

        if (vk == VK_RIGHT) {
            if (cursor < buf.size()) {
                cursor++;
                redrawPanels();
            }
            continue;
        }

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

        if (vk == VK_HOME) {
            auto lines = calcEditorLines(buf, editWidth);
            int cr, cc;
            findCursorPos(buf, cursor, lines, editWidth, cr, cc);
            size_t lineStart = lines[cr].startIdx;
            cursor = lineStart;
            redrawPanels();
            continue;
        }

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

        if (vk == VK_BACK) {
            if (cursor > 0) {
                cursor--;
                buf.erase(buf.begin() + cursor);
                redrawPanels();
            }
            continue;
        }

        if (vk == VK_DELETE) {
            if (cursor < buf.size()) {
                buf.erase(buf.begin() + cursor);
                redrawPanels();
            }
            continue;
        }

        if (ch >= L' ' || (ch >= 0x4E00 && ch <= 0x9FFF) || (ch >= 0x3000 && ch <= 0x303F) || (ch >= 0xFF00 && ch <= 0xFFEF)) {
            buf.insert(buf.begin() + cursor, ch);
            cursor++;
            redrawPanels();
            continue;
        }
    }
}
