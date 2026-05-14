#pragma once

#include <string>

class DiaryStore;

void exportImportMenu(DiaryStore& store,
                      const std::string& password,
                      const char* diaryPath,
                      const char* exportPath,
                      const char* importPath);
