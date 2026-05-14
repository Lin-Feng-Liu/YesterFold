#include "page_auth.h"

#include "diary_store.h"
#include "page_status.h"

#include <algorithm>
#include <sodium.h>

namespace {

constexpr WORD ACCESS_WARN_ATTR   = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
constexpr WORD ACCESS_DANGER_ATTR = FOREGROUND_RED | FOREGROUND_INTENSITY;

struct AccessPageLayout {
    int fieldX;
    int fieldY;
    int fieldW;
};

InputResult readPasswordFieldAt(int x, int y, int fieldW, bool allowEscape) {
    if (fieldW < 8) fieldW = 8;

    DWORD oldMode;
    GetConsoleMode(g_hIn, &oldMode);
    SetConsoleMode(g_hIn, ENABLE_EXTENDED_FLAGS | ENABLE_WINDOW_INPUT);

    std::wstring wpass;
    COORD startPos = {static_cast<SHORT>(x), static_cast<SHORT>(y)};
    SetConsoleCursorPosition(g_hOut, startPos);
    while (true) {
        INPUT_RECORD ir;
        DWORD read;
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

AccessPageLayout renderAccessPage(const std::wstring& headline,
                                  const std::wstring& promptLabel,
                                  const std::vector<std::wstring>& statusLines,
                                  const std::wstring& modeLabel,
                                  WORD promptBoxAttr,
                                  bool allowEscape) {
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
    std::wstring hint = allowEscape
        ? L"Enter=提交  Backspace=删除  Esc=取消"
        : L"Enter=提交  Backspace=删除";
    writeAtColor(boxX + 2, boxY + 5, hint, AMBER_DIM);

    writeAtColor(shell.x + 2, shell.y + shell.h - 2, L">> AWAITING_CREDENTIAL...", AMBER);

    return {boxX + 5, boxY + 3, promptBoxW - 7};
}

InputResult readAccessPasswordPage(const std::wstring& headline,
                                   const std::wstring& promptLabel,
                                   const std::vector<std::wstring>& statusLines,
                                   const std::wstring& modeLabel,
                                   WORD promptBoxAttr,
                                   bool allowEscape) {
    while (true) {
        AccessPageLayout layout = renderAccessPage(headline, promptLabel, statusLines, modeLabel, promptBoxAttr, allowEscape);
        InputResult result = readPasswordFieldAt(layout.fieldX, layout.fieldY, layout.fieldW, allowEscape);
        if (result.resized) continue;
        return result;
    }
}

}

std::string readPassword(const std::string& prompt) {
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

std::string readLoginPassword(int failedAttempts, int maxAttempts) {
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

InputResult readPasswordCancelable(const std::string& prompt) {
    return readAccessPasswordPage(
        L"SECURE ACTION // IDENTITY CHECK",
        utf8_to_wstring(prompt),
        {
            L"Esc 已启用，可安全取消当前操作",
            L"输入过程始终隐藏，不会明文回显",
            L"按 Enter 继续身份验证",
        },
        L"VERIFY",
        AMBER,
        true
    );
}

void changePasswordInteractive(const std::string& currentPass, const char* diaryPath) {
    CenteredRect shell = drawTerminalShell(L"PASSWORD.UPDATE // LOCAL_DIARY_ENV", true);
    setFooterHintRegion(shell.x + 1, shell.y + shell.h - 2, shell.w - 2);

    auto vRes = readPasswordCancelable("输入当前密码确认身份 (Esc 取消): ");
    if (vRes.cancelled) { resetFooterHintRegion(); return; }
    if (vRes.value != currentPass) {
        showFullScreenMessage(L"VERIFY FAILED", {L"身份验证失败!"});
        sodium_memzero(vRes.value.data(), vRes.value.size());
        resetFooterHintRegion();
        return;
    }
    sodium_memzero(vRes.value.data(), vRes.value.size());

    auto nRes = readPasswordCancelable("输入新密码 (Esc 取消): ");
    if (nRes.cancelled) { resetFooterHintRegion(); return; }
    std::string newPass = nRes.value;

    auto cRes = readPasswordCancelable("确认新密码 (Esc 取消): ");
    if (cRes.cancelled) { sodium_memzero(newPass.data(), newPass.size()); resetFooterHintRegion(); return; }
    std::string newPassConfirm = cRes.value;

    if (newPass != newPassConfirm) {
        showFullScreenMessage(L"MISMATCH", {L"两次密码不一致!"});
        sodium_memzero(newPass.data(), newPass.size());
        sodium_memzero(newPassConfirm.data(), newPassConfirm.size());
        resetFooterHintRegion();
        return;
    }
    sodium_memzero(newPassConfirm.data(), newPassConfirm.size());

    DiaryStore store;
    if (store.load(diaryPath, currentPass)) {
        if (store.save(diaryPath, newPass)) {
            showFullScreenMessage(L"PASSWORD UPDATED", {L"[密码已修改]  (^_^)"});
        } else {
            showFullScreenMessage(L"SAVE FAILED", {L"[保存失败!]"});
        }
    } else {
        showFullScreenMessage(L"DECRYPT FAILED", {L"[解密失败，密码修改未完成]"});
    }
    sodium_memzero(newPass.data(), newPass.size());
    resetFooterHintRegion();
}
