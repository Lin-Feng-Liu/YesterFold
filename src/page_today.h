#pragma once

#include <string>

class DiaryStore;

void writeOrEditToday(DiaryStore& store, const std::string& password, const char* diaryPath);
