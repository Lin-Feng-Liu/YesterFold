#pragma once

#include <string>

class DiaryStore;

void editByDate(DiaryStore& store, const std::string& password, const char* diaryPath);
