#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

class DiaryStore {
public:
    DiaryStore() : loaded_(false) {}

    bool load(const std::string& filePath, const std::string& password);
    bool save(const std::string& filePath, const std::string& password) const;
    bool isLoaded() const { return loaded_; }

    void initEmpty();

    nlohmann::json& entries() { return data_["entries"]; }
    const nlohmann::json& entries() const { return data_["entries"]; }
    size_t entryCount() const { return entries().size(); }

    // 查找指定日期的条目索引，未找到返回 -1
    int findEntry(int year, int month, int day) const;

    void addEntry(nlohmann::json entry);
    bool removeEntry(size_t index);
    bool updateEntry(size_t index, const nlohmann::json& entry);

    // 神秘计数器
    int getCounter() const;
    void setCounter(int value);

    // 返回按日期排序的条目索引
    std::vector<size_t> getSortedIndices() const;

private:
    nlohmann::json data_;
    bool loaded_;
};
