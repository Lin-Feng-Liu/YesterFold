#pragma once
#include "metrics.h"
#include "ui_render.h"

struct MainPageLayout {
    int menuX, menuY, menuW, menuH;  // 菜单区域（interior, 不含框线）
};

// 渲染主页面静态内容（框架、标题、ASCII Art、进度条、热力图、命令栏）
// 返回菜单区域坐标供 menuSelectInRegion 使用
MainPageLayout renderMainPage(const DiaryMetrics& m, const char* dataPath);
