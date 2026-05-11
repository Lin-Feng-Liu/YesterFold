#include "metrics.h"
#include "diary_store.h"
#include <windows.h>
#include <string>
#include <algorithm>

// ─── 工具 ───

static bool isLeapYear(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

static int daysInMonth(int year, int month) {
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) return 30;
    if (month == 2 && isLeapYear(year)) return 29;
    return days[month - 1];
}

static int dayOfYear(int year, int month, int day) {
    int doy = day;
    for (int m = 0; m < month - 1; ++m)
        doy += daysInMonth(year, m + 1);
    return doy;
}

static int daysInYearCount(int year) {
    return isLeapYear(year) ? 366 : 365;
}

static int entryTotalChars(const nlohmann::json& entry) {
    if (!entry.contains("segments") || !entry["segments"].is_array()) return 0;
    int total = 0;
    for (const auto& seg : entry["segments"]) {
        std::string content = seg.value("content", "");
        total += static_cast<int>(content.size());
    }
    return total;
}

// ─── 主计算函数 ───

DiaryMetrics computeMetrics(const DiaryStore& store) {
    DiaryMetrics m = {};

    // 获取当前时间
    SYSTEMTIME st;
    GetLocalTime(&st);
    m.currentYear = st.wYear;
    m.currentMonth = st.wMonth;
    m.currentDay = st.wDay;
    m.daysInCurrentMonth = daysInMonth(m.currentYear, m.currentMonth);

    // ── DAY PROG ──
    int secondsToday = st.wHour * 3600 + st.wMinute * 60 + st.wSecond;
    m.dayProgress = static_cast<double>(secondsToday) / 86400.0 * 100.0;

    // ── YEAR PROG ──
    int doy = dayOfYear(m.currentYear, m.currentMonth, m.currentDay);
    int yearDays = daysInYearCount(m.currentYear);
    m.yearProgress = static_cast<double>((doy - 1) * 86400 + secondsToday)
                     / static_cast<double>(yearDays * 86400) * 100.0;

    // ── WEEK LOG (最近 7 天) ──
    m.weekCount = 0;
    for (int i = 0; i < 7; ++i) {
        // 计算偏移日期：i=0 是 6 天前，i=6 是今天
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        // 将当前时间减去 (6-i) 天
        ULARGE_INTEGER uli;
        uli.LowPart = ft.dwLowDateTime;
        uli.HighPart = ft.dwHighDateTime;
        // 100纳秒 * 10000 * 1000 * 60 * 60 * 24 = 一天
        LONG64 delta = static_cast<LONG64>(6 - i) * 864000000000LL;
        uli.QuadPart -= delta;
        ft.dwLowDateTime = uli.LowPart;
        ft.dwHighDateTime = uli.HighPart;
        FileTimeToSystemTime(&ft, &st);

        int idx = store.findEntry(st.wYear, st.wMonth, st.wDay);
        m.weekActivity[i] = (idx >= 0);
        if (m.weekActivity[i]) m.weekCount++;
    }

    // ── MONTH HEATMAP ──
    int dim = m.daysInCurrentMonth;
    m.monthWeeks = (dim + 6) / 7;  // ceil(days/7)

    // 收集当月每天的活跃度
    int maxChars = 1;  // 避免除以零
    int dayChars[31] = {0};

    for (int d = 1; d <= dim; ++d) {
        int idx = store.findEntry(m.currentYear, m.currentMonth, d);
        if (idx >= 0) {
            const auto& entry = store.entries()[idx];
            int chars = entryTotalChars(entry);
            dayChars[d - 1] = chars;
            if (chars > maxChars) maxChars = chars;
        }
    }

    // 将字符数映射到 0-4 密度等级
    for (int d = 0; d < dim; ++d) {
        if (dayChars[d] == 0) {
            m.monthHeatmap[d] = 0;
        } else {
            // 按比例映射: ratio = chars/maxChars → level 1-4
            double ratio = static_cast<double>(dayChars[d]) / static_cast<double>(maxChars);
            if (ratio <= 0.25)      m.monthHeatmap[d] = 1;
            else if (ratio <= 0.50) m.monthHeatmap[d] = 2;
            else if (ratio <= 0.75) m.monthHeatmap[d] = 3;
            else                     m.monthHeatmap[d] = 4;
        }
    }

    // ── STREAK ──
    m.streak = 0;
    FILETIME ftNow;
    GetSystemTimeAsFileTime(&ftNow);
    ULARGE_INTEGER uliNow;
    uliNow.LowPart = ftNow.dwLowDateTime;
    uliNow.HighPart = ftNow.dwHighDateTime;

    for (int back = 0; back < 366; ++back) {
        ULARGE_INTEGER uli = uliNow;
        uli.QuadPart -= static_cast<LONG64>(back) * 864000000000LL;
        FILETIME ft2;
        ft2.dwLowDateTime = uli.LowPart;
        ft2.dwHighDateTime = uli.HighPart;
        SYSTEMTIME st2;
        FileTimeToSystemTime(&ft2, &st2);

        int idx = store.findEntry(st2.wYear, st2.wMonth, st2.wDay);
        if (idx >= 0) {
            m.streak++;
        } else {
            break;
        }
    }

    // ── TOTAL (当月有日记的天数) ──
    m.totalEntries = 0;
    int totalChars = 0;
    for (int d = 1; d <= dim; ++d) {
        int idx = store.findEntry(m.currentYear, m.currentMonth, d);
        if (idx >= 0) {
            m.totalEntries++;
            totalChars += entryTotalChars(store.entries()[idx]);
        }
    }

    // ── AVG (平均每篇日记字符数) ──
    if (m.totalEntries > 0) {
        m.avgChars = totalChars / m.totalEntries;
    } else {
        m.avgChars = 0;
    }

    return m;
}
