#pragma once

#include <string>
#include <vector>

void showFullScreenMessage(const std::wstring& title,
                           const std::vector<std::wstring>& lines,
                           const std::wstring& prompt = L"按任意键继续...");
