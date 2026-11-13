#include "store.h"
#include "sequence.h"
#include <memory>

namespace {
void exec(sqlite3* db, const char* sql) {
    if (sqlite3_exec(db, sql, nullptr, nullptr, nullptr) != SQLITE_OK)
        throw std::runtime_error(sqlite3_errmsg(db));
}
class Statement {
    sqlite3* db_;
public:
    sqlite3_stmt* value = nullptr;
    Statement(sqlite3* db, const char* sql) : db_(db) {
        if (sqlite3_prepare_v2(db, sql, -1, &value, nullptr) != SQLITE_OK)
            throw std::runtime_error(sqlite3_errmsg(db));
    }
    ~Statement() { sqlite3_finalize(value); }
    void text(int i, const std::string& s) {
        if (sqlite3_bind_text(value, i, s.data(), static_cast<int>(s.size()), SQLITE_TRANSIENT) != SQLITE_OK)
            throw std::runtime_error(sqlite3_errmsg(db_));
    }
    void integer(int i, long long n) {
        if (sqlite3_bind_int64(value, i, n) != SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db_));
    }
    void blob(int i, const std::string& s) {
        if (sqlite3_bind_blob(value, i, s.data(), static_cast<int>(s.size()), SQLITE_TRANSIENT) != SQLITE_OK)
            throw std::runtime_error(sqlite3_errmsg(db_));
    }
    bool step() {
        int rc = sqlite3_step(value);
        if (rc == SQLITE_ROW) return true;
        if (rc == SQLITE_DONE) return false;
        throw std::runtime_error(sqlite3_errmsg(db_));
    }
    std::string text_at(int i) const {
        auto p = sqlite3_column_text(value, i);
        return p ? reinterpret_cast<const char*>(p) : "";
    }
    long long int_at(int i) const { return sqlite3_column_int64(value, i); }
};
const char* card_select =
    "SELECT c.id,c.product_id,c.platform_type_id,p.name,c.created_at "
    "FROM cards c JOIN platform_types p ON p.id=c.platform_type_id ";
json read_card(const Statement& q) {
    auto id = q.int_at(0);
    return {{"id", id}, {"product_id", q.text_at(1)}, {"platform_type_id", q.int_at(2)},
            {"platform_name", q.text_at(3)}, {"created_at", q.text_at(4)},
            {"view_path", "/cards/" + std::to_string(id)}};
}
}

Store::Store(const std::string& filename) {
    if (sqlite3_open_v2(filename.c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                        SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK) {
        std::string error = db_ ? sqlite3_errmsg(db_) : "Cannot open SQLite";
        sqlite3_close(db_); db_ = nullptr;
        throw std::runtime_error(error);
    }
    try {
        sqlite3_busy_timeout(db_, 5000);
        exec(db_, "PRAGMA foreign_keys=ON; PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL;");
        Statement version(db_, "PRAGMA user_version");
        version.step();
        const auto v = version.int_at(0);
        if (v > 1) throw std::runtime_error("Database belongs to a newer application version");
        if (v == 0) {
            exec(db_,
                "BEGIN IMMEDIATE;"
                "CREATE TABLE platform_types("
                "id INTEGER PRIMARY KEY AUTOINCREMENT,name TEXT NOT NULL UNIQUE,"
                "next_product_id TEXT,created_at TEXT NOT NULL DEFAULT(strftime('%Y-%m-%dT%H:%M:%fZ','now')));"
                "CREATE TABLE cards("
                "id INTEGER PRIMARY KEY AUTOINCREMENT,product_id TEXT NOT NULL,"
                "platform_type_id INTEGER NOT NULL REFERENCES platform_types(id),"
                "workbook BLOB NOT NULL,"
                "created_at TEXT NOT NULL DEFAULT(strftime('%Y-%m-%dT%H:%M:%fZ','now')));"
                "CREATE INDEX cards_platform_id ON cards(platform_type_id,id);"
                "CREATE INDEX cards_product_id ON cards(product_id);"
                "PRAGMA user_version=1; COMMIT;");
        }
    } catch (...) { sqlite3_close(db_); db_ = nullptr; throw; }
}
Store::~Store() { sqlite3_close(db_); }

json Store::create_platform(const std::string& name) {
    if (name.empty() || name.size() > 200 || name.find_first_not_of(" \t\r\n") == std::string::npos)
        throw InputError("Platform name must contain 1..200 bytes and cannot be blank");
    std::lock_guard<std::mutex> guard(mutex_);
    Statement insert(db_, "INSERT INTO platform_types(name) VALUES(?) ON CONFLICT(name) DO NOTHING");
    insert.text(1, name); insert.step();
    Statement query(db_, "SELECT id,name,next_product_id FROM platform_types WHERE name=?");
    query.text(1, name); query.step();
    return {{"id", query.int_at(0)}, {"name", query.text_at(1)},
            {"next_product_id", query.text_at(2)}};
}
json Store::platforms() {
    std::lock_guard<std::mutex> guard(mutex_);
    Statement q(db_, "SELECT p.id,p.name,p.next_product_id,count(c.id) FROM platform_types p "
                     "LEFT JOIN cards c ON c.platform_type_id=p.id GROUP BY p.id ORDER BY p.id");
    json rows = json::array();
    while (q.step()) rows.push_back({{"id", q.int_at(0)}, {"name", q.text_at(1)},
                                    {"next_product_id", q.text_at(2)}, {"card_count", q.int_at(3)}});
    return rows;
}
json Store::create_batch(long long platform, const std::string& first, int count,
                         const std::string& workbook) {
    auto ids = make_sequence(first, count);
    if (workbook.empty()) throw InputError("Workbook template is empty");
    std::lock_guard<std::mutex> guard(mutex_);
    exec(db_, "BEGIN IMMEDIATE");
    try {
        {
            Statement check(db_, "SELECT id FROM platform_types WHERE id=?");
            check.integer(1, platform);
            if (!check.step()) throw NotFound("Platform type not found");
        }
        json records = json::array();
        for (const auto& product : ids) {
            Statement insert(db_, "INSERT INTO cards(product_id,platform_type_id,workbook) VALUES(?,?,?)");
            insert.text(1, product); insert.integer(2, platform); insert.blob(3, workbook); insert.step();
            auto id = sqlite3_last_insert_rowid(db_);
            records.push_back({{"id", id}, {"product_id", product},
                               {"view_path", "/cards/" + std::to_string(id)}});
        }
        auto next = next_product_id(ids.back());
        Statement update(db_, "UPDATE platform_types SET next_product_id=? WHERE id=?");
        update.text(1, next); update.integer(2, platform); update.step();
        exec(db_, "COMMIT");
        return {{"count", count}, {"first_product_id", first}, {"last_product_id", ids.back()},
                {"next_product_id", next}, {"cards", records}};
    } catch (...) { sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr); throw; }
}
json Store::cards(long long platform, int limit, long long after) {
    if (platform < 0 || limit < 1 || limit > 1000 || after < 0) throw InputError("Invalid list parameters");
    std::lock_guard<std::mutex> guard(mutex_);
    auto sql = std::string(card_select) + "WHERE (?=0 OR c.platform_type_id=?) AND c.id>? ORDER BY c.id LIMIT ?";
    Statement q(db_, sql.c_str());
    q.integer(1, platform); q.integer(2, platform); q.integer(3, after); q.integer(4, limit);
    json rows = json::array();
    while (q.step()) rows.push_back(read_card(q));
    return rows;
}
json Store::card(long long id) {
    std::lock_guard<std::mutex> guard(mutex_);
    auto sql = std::string(card_select) + "WHERE c.id=?";
    Statement q(db_, sql.c_str()); q.integer(1, id);
    if (!q.step()) throw NotFound("Card not found");
    return read_card(q);
}
std::string Store::workbook(long long id) {
    std::lock_guard<std::mutex> guard(mutex_);
    Statement q(db_, "SELECT workbook FROM cards WHERE id=?"); q.integer(1, id);
    if (!q.step()) throw NotFound("Card not found");
    auto p = static_cast<const char*>(sqlite3_column_blob(q.value, 0));
    int n = sqlite3_column_bytes(q.value, 0);
    return p && n ? std::string(p, n) : std::string();
}
