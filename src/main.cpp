#include <iostream>
#include <string>
#include <vector>
#include <windows.h>
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
#include "page_today.h"
#include "page_date.h"

static const char* DIARY_PATH       = "data\\diary.enc";
static const char* EXPORT_TXT_PATH  = "data\\export_diary.txt";
static const char* IMPORT_TXT_PATH  = "data\\import_diary.txt";

static const int MODE_EXIT   = 0;
static const int MODE_SWITCH = 1;

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
                writeOrEditToday(store, password, DIARY_PATH);
                break;
            case 1:  // 查看全部
                clearScreen();
                viewAllDiaries(store);
                break;
            case 2:  // 按日期编辑
                clearScreen();
                editByDate(store, password, DIARY_PATH);
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
