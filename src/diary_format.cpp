#include "diary_format.h"

#include "ui_render.h"

void displaySegments(const nlohmann::json& entry) {
    if (!entry.contains("segments") || !entry["segments"].is_array()) return;
    for (const auto& seg : entry["segments"]) {
        std::string time = seg.value("time", "");
        std::string content = seg.value("content", "");
        wprint(L"  ");
        wprint(utf8_to_wstring(time));
        wprintln(L";");
        if (!content.empty()) {
            wprintln(utf8_to_wstring(content));
        }
        wprintln();
    }
}

std::string formatDiaryPlain(const nlohmann::json& entry) {
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

void printDiary(const nlohmann::json& entry) {
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
            wprint(utf8_to_wstring(time));
            wprintln(L";");
            if (!content.empty()) {
                wprintln(utf8_to_wstring(content));
            }
            wprintln();
        }
    }
}
