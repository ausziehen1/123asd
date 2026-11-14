#pragma once
#include "json.hpp"
#include "sqlite3.h"
#include <mutex>
#include <string>
#include <vector>

using json = nlohmann::json;
struct NotFound : std::runtime_error { using std::runtime_error::runtime_error; };
struct RevisionConflict : std::runtime_error { using std::runtime_error::runtime_error; };

class Store {
public:
    explicit Store(const std::string& filename);
    ~Store();
    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;
    json create_platform(const std::string& name);
    json platforms();
    json create_batch(long long platform, const std::string& first, int count,
                      const std::string& workbook);
    json cards(long long platform, int limit, long long after);
    json card(long long id);
    json card_set(const std::vector<long long>& ids);
    json assign_codes(const json& cards, const json& groups);
    std::string workbook(long long id);
    json document(long long id);
    json save_document(long long id, long long expected_revision, const json& document,
                       const std::string& workbook);
    std::string setting(const std::string& key);
    void set_setting(const std::string& key, const std::string& value, bool only_if_missing = false);
private:
    sqlite3* db_ = nullptr;
    std::mutex mutex_;
};
