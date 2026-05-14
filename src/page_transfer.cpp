#include "page_transfer.h"

#include "diary_format.h"
#include "diary_store.h"
#include "ui_render.h"

#include <fstream>
#include <iostream>
#include <sstream>

namespace {

std::string readMultiLine(const std::string& prompt) {
    std::cout << prompt << std::endl;
    std::cout << "（输入完成后，在单独一行输入 :END 结束）" << std::endl;
    std::string result;
    std::string line;
    while (true) {
        std::getline(std::cin, line);
        if (line == ":END") break;
        if (line == ":end") break;
        result += line + "\n";
    }
    return result;
}

void exportDiary(const DiaryStore& store, const char* exportPath) {
    auto sorted = store.getSortedIndices();
    if (sorted.empty()) {
        wprintln(L"日记本为空，没有可导出的内容");
        return;
    }

    std::string output;
    for (size_t i = 0; i < sorted.size(); ++i) {
        output += formatDiaryPlain(store.entries()[sorted[i]]);
    }

    std::ofstream file(exportPath, std::ios::trunc);
    if (!file.is_open()) {
        wprintln(L"无法写入 " + utf8_to_wstring(exportPath));
        return;
    }
    file << output;
    file.close();

    wprintln(L"[已导出到 " + utf8_to_wstring(exportPath) + L"]");
    wprintln(L"共导出 " + std::to_wstring(sorted.size()) + L" 篇日记");
    wprintln(L"注意：该文件是明文，用完后请及时删除!");
}

void importDiary(DiaryStore& store,
                 const std::string& password,
                 const char* diaryPath,
                 const char* importPath) {
    std::vector<MenuItem> importModeItems = {
        {L"--- 导入方式 ---", false},
        {L"1. 从文件导入 (data\\import_diary.txt)", true},
        {L"2. 直接粘贴文本导入", true},
        {L"0. 返回", true},
    };
    int modeChoice = menuSelect(importModeItems, 1);
    if (modeChoice == 3 || modeChoice == MENU_ESC) return;

    std::string rawText;
    if (modeChoice == 1) {
        std::ifstream file(importPath);
        if (!file.is_open()) {
            wprintln(L"找不到 " + utf8_to_wstring(importPath));
            wprintln(L"请将日记文本放入该文件后再导入");
            return;
        }
        std::stringstream ss;
        ss << file.rdbuf();
        rawText = ss.str();
        file.close();
    } else {
        clearScreen();
        rawText = readMultiLine("请粘贴日记文本（格式需符合日记排版规范）");
    }

    if (rawText.empty()) {
        wprintln(L"导入文本为空");
        return;
    }

    // 解析导入文本
    std::vector<nlohmann::json> parsedEntries;
    std::string remaining = rawText;
    size_t pos = 0;

    // 按分隔线 "——" 分割
    while (true) {
        // 查找分隔线
        size_t sepStart = remaining.find("\xe2\x80\x94"); // UTF-8 EM DASH
        if (sepStart == std::string::npos) {
            // 也尝试查找连续的 "—"
            sepStart = remaining.find("——");
        }
        if (sepStart == std::string::npos) break;

        // 找到下一个分隔线（或文本末尾）
        size_t nextSep = sepStart + 1;
        while (nextSep < remaining.size()) {
            std::string next3 = remaining.substr(nextSep, 3);
            if (next3 == "\xe2\x80\x94" || remaining.substr(nextSep, 2) == "——") {
                break;
            }
            nextSep++;
        }

        std::string block;
        if (nextSep < remaining.size() - 1) {
            // 有分隔线，取出中间的内容
            size_t nextSepLine = remaining.find("\xE2\x80\x94", nextSep);
            if (nextSepLine == std::string::npos) {
                nextSepLine = remaining.find("——", nextSep + 1);
            }
            if (nextSepLine != std::string::npos) {
                // 找到下一个分隔线位置
                block = remaining.substr(sepStart, nextSepLine - sepStart);
                remaining = remaining.substr(nextSepLine);
            } else {
                block = remaining.substr(sepStart);
                remaining = "";
            }
        } else {
            block = remaining.substr(sepStart);
            remaining = "";
        }

        // 解析block
        std::istringstream blockStream(block);
        std::string line;
        std::string dateLine;
        nlohmann::json entry;
        nlohmann::json segs = nlohmann::json::array();

        int state = 0; // 0=找日期行, 1=等空行, 2=找时间行, 3=读内容
        std::string currentTime;
        std::string currentContent;

        bool gotDate = false;

        while (std::getline(blockStream, line)) {
            // 去除行尾\r
            if (!line.empty() && line.back() == '\r') line.pop_back();

            // 跳过分隔线本身
            if (line.find("——") == 0) continue;
            // 跳过全是"—"的行
            bool allDash = !line.empty();
            for (char c : line) {
                if (c != '\xE2' && c != '\x80' && c != '\x94' && c != '-' && c != '\xEF' && c != '\xBC' && c != '\x8D') {
                    allDash = false; break;
                }
            }
            if (allDash) continue;

            if (!gotDate) {
                // 寻找日期行: YYYY/M/D 或 YYYY/MM/DD
                int slashCount = 0;
                for (char c : line) if (c == '/') slashCount++;
                if (slashCount == 2 && !line.empty() && (line[0] >= '0' && line[0] <= '9')) {
                    dateLine = line;
                    // 解析日期
                    size_t s1 = line.find('/');
                    size_t s2 = line.find('/', s1 + 1);
                    if (s1 != std::string::npos && s2 != std::string::npos && s2 > s1 + 1) {
                        try {
                            int py = std::stoi(line.substr(0, s1));
                            int pm = std::stoi(line.substr(s1 + 1, s2 - s1 - 1));
                            int pd = std::stoi(line.substr(s2 + 1));
                            entry["year"] = py;
                            entry["month"] = pm;
                            entry["day"] = pd;
                            gotDate = true;
                            state = 2; // 跳过空行状态，直接等时间行
                        } catch (...) {
                            // 日期解析失败，跳过
                        }
                    }
                }
                continue;
            }

            if (state == 2) {
                // 查找时间行: HH:MM; 或 HH：MM；
                // 包含冒号（:）且以分号（;）或全角分号（；）结尾
                std::string fullColon = "\xEF\xBC\x9A";
                bool hasColon = (line.find(':') != std::string::npos || line.find(fullColon) != std::string::npos);
                bool endsWithSemi = false;
                if (!line.empty()) {
                    if (line.back() == ';') {
                        endsWithSemi = true;
                    } else if (line.size() >= 3 && line.substr(line.size() - 3) == "\xEF\xBC\x9B") {
                        endsWithSemi = true;
                    }
                }

                if (hasColon && endsWithSemi) {
                    // 保存上一个segment
                    if (!currentTime.empty()) {
                        nlohmann::json seg;
                        seg["time"] = currentTime;
                        seg["content"] = currentContent;
                        // 去除内容末尾多余空行
                        while (!seg["content"].get<std::string>().empty() && seg["content"].get<std::string>().back() == '\n') {
                            std::string c = seg["content"].get<std::string>();
                            c.pop_back();
                            seg["content"] = c;
                        }
                        segs.push_back(seg);
                        currentContent.clear();
                    }
                    // 解析时间
                    currentTime = line;
                    // 去除分号
                    if (!currentTime.empty() && currentTime.back() == ';') currentTime.pop_back();
                    // 去除全角分号
                    if (currentTime.size() >= 3) {
                        std::string tail = currentTime.substr(currentTime.size() - 3);
                        if (tail == "\xEF\xBC\x9B") currentTime = currentTime.substr(0, currentTime.size() - 3);
                    }
                    // 全角冒号转半角
                    size_t fc = currentTime.find("\xEF\xBC\x9A");
                    if (fc != std::string::npos) {
                        currentTime.replace(fc, 3, ":");
                    }
                    state = 3;
                } else if (!line.empty()) {
                    // 可能是内容没有时间标记，合并到上一个segment
                    if (!currentTime.empty() && !line.empty()) {
                        currentContent += line + "\n";
                    }
                }
                continue;
            }

            if (state == 3) {
                // 读取内容直到遇到下一个时间行或分隔线
                std::string fullColon2 = "\xEF\xBC\x9A";
                bool hasColon = (line.find(':') != std::string::npos || line.find(fullColon2) != std::string::npos);
                bool endsWithSemi = false;
                if (!line.empty()) {
                    if (line.back() == ';') {
                        endsWithSemi = true;
                    } else if (line.size() >= 3 && line.substr(line.size() - 3) == "\xEF\xBC\x9B") {
                        endsWithSemi = true;
                    }
                }

                if (hasColon && endsWithSemi) {
                    // 新时间行
                    if (!currentTime.empty()) {
                        nlohmann::json seg;
                        seg["time"] = currentTime;
                        seg["content"] = currentContent;
                        while (!seg["content"].get<std::string>().empty() && seg["content"].get<std::string>().back() == '\n') {
                            std::string c = seg["content"].get<std::string>();
                            c.pop_back();
                            seg["content"] = c;
                        }
                        segs.push_back(seg);
                        currentContent.clear();
                    }
                    currentTime = line;
                    if (!currentTime.empty() && currentTime.back() == ';') currentTime.pop_back();
                    if (currentTime.size() >= 3 && currentTime.substr(currentTime.size() - 3) == "\xEF\xBC\x9B")
                        currentTime = currentTime.substr(0, currentTime.size() - 3);
                    size_t fc2 = currentTime.find("\xEF\xBC\x9A");
                    if (fc2 != std::string::npos) currentTime.replace(fc2, 3, ":");
                } else {
                    currentContent += line + "\n";
                }
            }
        }

        // 保存最后一个segment
        if (!currentTime.empty()) {
            nlohmann::json seg;
            seg["time"] = currentTime;
            seg["content"] = currentContent;
            while (!seg["content"].get<std::string>().empty() && seg["content"].get<std::string>().back() == '\n') {
                std::string c = seg["content"].get<std::string>();
                c.pop_back();
                seg["content"] = c;
            }
            segs.push_back(seg);
        }

        if (gotDate && !segs.empty()) {
            entry["segments"] = segs;
            parsedEntries.push_back(entry);
        }
    }

    if (parsedEntries.empty()) {
        wprintln(L"未能从导入文本中解析出任何日记");
        wprintln(L"请确认格式正确（分隔线 + 日期行 + 时间行 + 内容）");
        return;
    }

    wprintln(L"\n解析到 " + std::to_wstring(parsedEntries.size()) + L" 篇日记");

    // 导入模式选择
    std::vector<MenuItem> conflictItems = {
        {L"--- 导入冲突处理 ---", false},
        {L"1. 完全替换当前日记库", true},
        {L"2. 合并导入，同日覆盖（导入数据优先）", true},
        {L"3. 合并导入，同日保留（合并segments）", true},
        {L"0. 取消导入", true},
    };
    int conflictChoice = menuSelect(conflictItems, 1);
    if (conflictChoice == 4 || conflictChoice == MENU_ESC) return;

    if (conflictChoice == 1) {
        // 完全替换
        store.entries() = nlohmann::json::array();
        for (auto& e : parsedEntries) {
            store.addEntry(e);
        }
    } else if (conflictChoice == 2) {
        // 同日覆盖
        for (auto& e : parsedEntries) {
            int ey = e.value("year", 0);
            int em = e.value("month", 0);
            int ed = e.value("day", 0);
            int fidx = store.findEntry(ey, em, ed);
            if (fidx >= 0) {
                store.updateEntry(fidx, e);
            } else {
                store.addEntry(e);
            }
        }
    } else if (conflictChoice == 3) {
        // 同日保留（合并segments）
        for (auto& e : parsedEntries) {
            int ey = e.value("year", 0);
            int em = e.value("month", 0);
            int ed = e.value("day", 0);
            int fidx = store.findEntry(ey, em, ed);
            if (fidx >= 0) {
                // 合并segments
                auto& existingSegs = store.entries()[fidx]["segments"];
                for (auto& seg : e["segments"]) {
                    existingSegs.push_back(seg);
                }
            } else {
                store.addEntry(e);
            }
        }
    }

    store.save(diaryPath, password);
    wprintln(L"[导入完成，已保存]  (^_^)" );
}

}

void exportImportMenu(DiaryStore& store,
                      const std::string& password,
                      const char* diaryPath,
                      const char* exportPath,
                      const char* importPath) {
    while (true) {
        CenteredRect shell = drawTerminalShell(L"TRANSFER.NODE // LOCAL_DIARY_ENV", true);
        int leftX = shell.x + 6;
        int topY = shell.y + 5;
        int leftW = 34;
        int rightX = leftX + leftW + 4;
        int rightW = shell.x + shell.w - rightX - 6;
        int panelH = 13;
        int menuY = topY + panelH + 1;
        int menuH = shell.y + shell.h - menuY - 4;
        if (menuH < 6) menuH = 6;

        drawSingleBox(leftX, topY, leftW, panelH);
        drawSingleBox(rightX, topY, rightW, panelH);
        drawSingleBox(leftX, menuY, shell.w - 12, menuH);

        writeAtColor(leftX + 2, topY - 1, L"[ TRANSFER_MENU ]", AMBER_DIM);
        writeAtColor(rightX + 2, topY - 1, L"[ PIPELINE_INFO ]", AMBER_DIM);
        writeAtColor(leftX + 2, menuY - 1, L"[ OPERATIONS ]", AMBER_DIM);

        writeAtColor(leftX + 2, topY + 2, L"导出 / 导入", AMBER_DIM);
        writeAtColor(leftX + 2, topY + 5, L"把日记导出为明文", AMBER);
        writeAtColor(leftX + 2, topY + 6, L"或从文本重新导回库中", AMBER);

        std::vector<std::wstring> infoLines = {
            L"导出路径 : " + utf8_to_wstring(exportPath),
            L"导入来源 : " + utf8_to_wstring(importPath) + L" / 直接粘贴",
            L"",
            L"提示 : 导入会修改当前日记库。",
            L"建议 : 导入前先导出一份作为备份。",
        };
        writeWrappedPanelLines(rightX + 2, topY + 1, rightW - 4, panelH - 2, infoLines, AMBER);

        setFooterHintRegion(shell.x + 1, shell.y + shell.h - 2, shell.w - 2);
        fillLine(shell.x + 1, shell.y + shell.h - 3, shell.w - 2, L' ', ATTR_NORMAL);
        writeAtColor(shell.x + 2, shell.y + shell.h - 3, L"Enter执行  Esc返回", AMBER_DIM);
        writeFooterHint(L">> TRANSFER READY", AMBER);

        std::vector<MenuItem> items = {
            {L"1. 导出日记", true},
            {L"2. 导入日记", true},
            {L"0. 返回", true},
        };
        int choice = menuSelectInRegion(leftX + 2, menuY + 1, shell.w - 16, menuH - 2, items, 0);

        if (choice == MENU_RESIZE) {
            resetFooterHintRegion();
            continue;
        }
        if (choice == MENU_ESC || choice == 2) {
            resetFooterHintRegion();
            break;
        }

        resetFooterHintRegion();
        clearScreen();
        if (choice == 0) {
            exportDiary(store, exportPath);
            pauseScreen();
        } else if (choice == 1) {
            importDiary(store, password, diaryPath, importPath);
            pauseScreen();
        } else {
            break;
        }
    }
}
