#pragma once

#include <string>
#include <vector>

struct EditorScreenConfig {
    std::wstring screenLabel = L"WRITE.BUFFER // LOCAL_DIARY_ENV";
    std::wstring panelTitle = L"WRITE / EDIT";
    std::wstring dateLine = L"DATE : --/--/--";
    std::wstring timeLine = L"SLOT : --:--";
    std::wstring modeLine = L"MODE : EDIT";
    std::vector<std::wstring> historyLines;
    std::wstring emptyHistoryText = L"NO PRIOR SEGMENTS.";
    bool adaptiveHistory = false;
    bool projectBufferToHistory = false;
    std::wstring livePreviewTime = L"";
    int minEditInnerH = 8;
    bool confirmExitWhenBufferNotEmpty = false;
};

struct EditorResult {
    bool confirmed;
    std::wstring content;
};

EditorResult openDiaryEditor(const std::wstring& initialContent,
                             const EditorScreenConfig& cfg = EditorScreenConfig());
