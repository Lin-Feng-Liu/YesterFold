#include "diary_store.h"
#include "crypto.h"
#include <fstream>
#include <iostream>
#include <algorithm>

bool DiaryStore::load(const std::string& filePath, const std::string& password) {
    loaded_ = false;

    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;

    std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<unsigned char> buffer(fileSize);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), fileSize)) return false;
    file.close();

    auto plaintext = crypto::decryptFile(buffer.data(), buffer.size(), password.c_str());
    if (!plaintext.has_value()) return false;

    try {
        data_ = nlohmann::json::parse(plaintext.value());
        loaded_ = true;
        return true;
    } catch (const nlohmann::json::exception& e) {
        std::cerr << "JSON 解析错误: " << e.what() << std::endl;
        return false;
    }
}

bool DiaryStore::save(const std::string& filePath, const std::string& password) const {
    if (!loaded_) return false;

    std::string jsonStr = data_.dump(2);

    std::vector<unsigned char> encrypted;
    try {
        encrypted = crypto::encryptFile(jsonStr, password.c_str());
    } catch (const std::exception& e) {
        std::cerr << "加密失败: " << e.what() << std::endl;
        return false;
    }

    std::ofstream file(filePath, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) return false;

    file.write(reinterpret_cast<const char*>(encrypted.data()), encrypted.size());
    file.close();
    return file.good();
}

void DiaryStore::initEmpty() {
    data_ = nlohmann::json::object();
    data_["version"] = 1;
    data_["entries"] = nlohmann::json::array();
    loaded_ = true;
}

int DiaryStore::findEntry(int year, int month, int day) const {
    const auto& ents = entries();
    for (size_t i = 0; i < ents.size(); ++i) {
        const auto& e = ents[i];
        if (e.value("year", 0) == year &&
            e.value("month", 0) == month &&
            e.value("day", 0) == day) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void DiaryStore::addEntry(nlohmann::json entry) {
    entries().push_back(std::move(entry));
}

bool DiaryStore::removeEntry(size_t index) {
    if (index >= entries().size()) return false;
    entries().erase(index);
    return true;
}

bool DiaryStore::updateEntry(size_t index, const nlohmann::json& entry) {
    if (index >= entries().size()) return false;
    entries()[index] = entry;
    return true;
}

std::vector<size_t> DiaryStore::getSortedIndices() const {
    std::vector<size_t> indices(entries().size());
    for (size_t i = 0; i < indices.size(); ++i) indices[i] = i;

    std::sort(indices.begin(), indices.end(), [this](size_t a, size_t b) {
        const auto& ea = entries()[a];
        const auto& eb = entries()[b];
        int ya = ea.value("year", 0), ma = ea.value("month", 0), da = ea.value("day", 0);
        int yb = eb.value("year", 0), mb = eb.value("month", 0), db = eb.value("day", 0);
        if (ya != yb) return ya < yb;
        if (ma != mb) return ma < mb;
        return da < db;
    });
    return indices;
}
