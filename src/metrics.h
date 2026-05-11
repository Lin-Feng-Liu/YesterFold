#pragma once

struct DiaryMetrics {
    int currentYear;
    int currentMonth;
    int currentDay;
    int daysInCurrentMonth;

    double dayProgress;     // 0-100
    double yearProgress;    // 0-100

    bool weekActivity[7];   // [0]=6天前, [6]=今天
    int weekCount;          // 本周有日记的天数

    int monthHeatmap[31];   // 当月每天活跃度 0-4
    int monthWeeks;         // 当月有几行 (4 或 5)

    int streak;             // 连续写日记天数
    int totalEntries;       // 当月有日记的天数
    int avgChars;           // 当月平均每篇日记字符数
};

// 通过 DiaryStore 前向声明
class DiaryStore;

DiaryMetrics computeMetrics(const DiaryStore& store);
