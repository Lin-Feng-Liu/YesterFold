#pragma once

#include "ui_render.h"

#include <string>

std::string readPassword(const std::string& prompt);
std::string readLoginPassword(int failedAttempts, int maxAttempts);
InputResult readPasswordCancelable(const std::string& prompt);
void changePasswordInteractive(const std::string& currentPass, const char* diaryPath);
