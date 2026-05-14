#pragma once

#include <nlohmann/json.hpp>

#include <string>

void displaySegments(const nlohmann::json& entry);
std::string formatDiaryPlain(const nlohmann::json& entry);
void printDiary(const nlohmann::json& entry);
