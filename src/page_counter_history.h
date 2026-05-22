#pragma once

#include <string>

class DiaryStore;

void browseCounterHistory(DiaryStore& store, const std::string& password, const char* diaryPath);
