#include "time_utils.h"

#include <windows.h>

void getCurrentDate(int& year, int& month, int& day) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    year = st.wYear;
    month = st.wMonth;
    day = st.wDay;
}

std::string getCurrentTimeStr() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d", st.wHour, st.wMinute);
    return std::string(buf);
}

std::string getCurrentTimestampStr() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d/%02d/%02d %02d:%02d:%02d",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return std::string(buf);
}

std::string getCurrentDateStr() {
    int y, m, d;
    getCurrentDate(y, m, d);
    return std::to_string(y) + "/" + std::to_string(m) + "/" + std::to_string(d);
}
