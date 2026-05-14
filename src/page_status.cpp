#include "page_status.h"
#include "ui_render.h"

#include <algorithm>

void showFullScreenMessage(const std::wstring& title,
                           const std::vector<std::wstring>& lines,
                           const std::wstring& prompt) {
    CenteredRect shell = drawTerminalShell(L"STATUS.PANEL // LOCAL_DIARY_ENV", true);
    int boxX = shell.x + 6;
    int boxY = shell.y + 6;
    int boxW = shell.w - 12;
    int boxH = std::max(8, static_cast<int>(lines.size()) + 6);
    if (boxY + boxH >= shell.y + shell.h - 2) boxH = shell.y + shell.h - boxY - 3;

    drawSingleBox(boxX, boxY, boxW, boxH);
    writeAtColor(boxX + 2, boxY - 1, L"[ " + title + L" ]", AMBER_DIM);

    int y = boxY + 2;
    for (const auto& line : lines) {
        if (y >= boxY + boxH - 2) break;
        writeAtColor(boxX + 2, y, fitTextToWidth(line, boxW - 4), AMBER);
        y++;
    }

    setFooterHintRegion(shell.x + 1, shell.y + shell.h - 2, shell.w - 2);
    writeFooterHint(prompt, AMBER);
}
