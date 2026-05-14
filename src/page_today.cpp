#include "page_today.h"

#include "diary_editor.h"
#include "diary_store.h"
#include "page_status.h"
#include "time_utils.h"
#include "ui_render.h"

#include <nlohmann/json.hpp>

void writeOrEditToday(DiaryStore& store, const std::string& password, const char* diaryPath) {
    int year, month, day;
    getCurrentDate(year, month, day);
    std::string currentTime = getCurrentTimeStr();

    int idx = store.findEntry(year, month, day);
    bool isNew = (idx < 0);
    nlohmann::json entryCopy;
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
        std::wstring trimmed = result.content;
        while (!trimmed.empty() && (trimmed.back() == L'\n' || trimmed.back() == L'\r' || trimmed.back() == L' ')) {
            trimmed.pop_back();
        }

        if (!trimmed.empty()) {
            store.entries()[idx]["segments"].back()["content"] = wstring_to_utf8(result.content);
            store.save(diaryPath, password);
            showFullScreenMessage(L"WRITE COMPLETE", {L"[日记已保存]"});
            pauseScreen();
        } else {
            auto& segs = store.entries()[idx]["segments"];
            segs.erase(segs.size() - 1);
            if (segs.empty()) {
                store.removeEntry(idx);
                showFullScreenMessage(L"WRITE CANCELLED", {L"[未输入内容，已取消]"});
            } else {
                showFullScreenMessage(L"WRITE SKIPPED", {L"[未输入新内容，保留已有记录]"});
            }
            store.save(diaryPath, password);
            pauseScreen();
        }
    } else {
        if (hasBackup) {
            store.updateEntry(idx, entryCopy);
        } else {
            store.removeEntry(idx);
        }
        showFullScreenMessage(L"WRITE CANCELLED", {L"[已取消]"});
        pauseScreen();
    }
}
