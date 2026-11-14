#include "store.h"
#include "sequence.h"
#include "document.h"
#include "one_c.h"
#include <set>
#include <limits>
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
    "SELECT c.id,c.product_id,c.platform_type_id,p.name,c.created_at,c.one_c_code,c.document_revision "
    "FROM cards c JOIN platform_types p ON p.id=c.platform_type_id ";
json read_card(const Statement& q) {
    auto id = q.int_at(0);
    return {{"id", id}, {"product_id", q.text_at(1)}, {"platform_type_id", q.int_at(2)},
            {"platform_name", q.text_at(3)}, {"created_at", q.text_at(4)},
            {"one_c_code",q.text_at(5)},{"document_revision",q.int_at(6)},
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
        long long v = 0;
        { Statement version(db_, "PRAGMA user_version"); version.step(); v = version.int_at(0); }
        if (v > 4) throw std::runtime_error("Database belongs to a newer application version");
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
        if (v < 2) {
            exec(db_, "BEGIN IMMEDIATE;"
                "ALTER TABLE cards ADD COLUMN document_json TEXT;"
                "ALTER TABLE cards ADD COLUMN document_revision INTEGER NOT NULL DEFAULT 0;"
                "ALTER TABLE cards ADD COLUMN document_updated_at TEXT;"
                "PRAGMA user_version=2; COMMIT;");
        }
        if(v<3)exec(db_,"BEGIN IMMEDIATE;CREATE TABLE app_settings(key TEXT PRIMARY KEY,value TEXT NOT NULL);PRAGMA user_version=3;COMMIT;");
        if(v<4){
            exec(db_,"BEGIN IMMEDIATE;ALTER TABLE cards ADD COLUMN one_c_code TEXT NOT NULL DEFAULT '';");
            Statement rows(db_,"SELECT id,document_json,workbook FROM cards");
            while(rows.step()){
                std::optional<std::string> code;auto raw=rows.text_at(1);auto document=raw.empty()?json():json::parse(raw);
                if(!document.is_null())code=document_one_c(document);
                if(!code){auto p=static_cast<const char*>(sqlite3_column_blob(rows.value,2));auto n=sqlite3_column_bytes(rows.value,2);try{code=workbook_one_c(std::string(p?p:"",n));}catch(const InputError&){} }
                Statement update(db_,"UPDATE cards SET one_c_code=?,document_json=? WHERE id=?");update.text(1,code.value_or(""));
                if(document.is_null())sqlite3_bind_null(update.value,2);else update.text(2,document.dump());update.integer(3,rows.int_at(0));update.step();
            }
            exec(db_,"PRAGMA user_version=4;COMMIT;");
        }
    } catch (...) { sqlite3_close(db_); db_ = nullptr; throw; }
}
Store::~Store() { sqlite3_close(db_); }
std::string Store::setting(const std::string& key) {
    std::lock_guard<std::mutex> guard(mutex_);
    Statement query(db_,"SELECT value FROM app_settings WHERE key=?");query.text(1,key);
    return query.step()?query.text_at(0):std::string();
}
void Store::set_setting(const std::string& key,const std::string& value,bool only_if_missing) {
    std::lock_guard<std::mutex> guard(mutex_);
    Statement update(db_,only_if_missing?"INSERT INTO app_settings(key,value) VALUES(?,?) ON CONFLICT(key) DO NOTHING":
        "INSERT INTO app_settings(key,value) VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value");
    update.text(1,key);update.text(2,value);update.step();
}

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
    std::string initialCode;try{initialCode=workbook_one_c(workbook).value_or("");}catch(const InputError&){}
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
            Statement insert(db_, "INSERT INTO cards(product_id,platform_type_id,workbook,one_c_code) VALUES(?,?,?,?)");
            insert.text(1, product); insert.integer(2, platform); insert.blob(3, workbook); insert.text(4,initialCode);insert.step();
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
json Store::card_set(const std::vector<long long>& ids) {
    if(ids.empty()||ids.size()>1000)throw InputError("Select 1..1000 cards");
    std::lock_guard<std::mutex> guard(mutex_);
    auto sql=std::string(card_select)+"WHERE c.id=?";
    Statement query(db_,sql.c_str());json result=json::array();
    for(auto id:ids){
        if(id<=0)throw InputError("Invalid card ID");
        sqlite3_reset(query.value);sqlite3_clear_bindings(query.value);query.integer(1,id);
        if(!query.step())throw NotFound("Одна из выбранных карточек не найдена. Обновите список перед печатью.");
        result.push_back(read_card(query));
    }
    return result;
}
std::string Store::workbook(long long id) {
    std::lock_guard<std::mutex> guard(mutex_);
    Statement q(db_, "SELECT workbook FROM cards WHERE id=?"); q.integer(1, id);
    if (!q.step()) throw NotFound("Card not found");
    auto p = static_cast<const char*>(sqlite3_column_blob(q.value, 0));
    int n = sqlite3_column_bytes(q.value, 0);
    return p && n ? std::string(p, n) : std::string();
}
json Store::document(long long id) {
    std::lock_guard<std::mutex> guard(mutex_);
    Statement q(db_, "SELECT document_json,document_revision,document_updated_at FROM cards WHERE id=?");
    q.integer(1, id);
    if (!q.step()) throw NotFound("Card not found");
    auto raw = q.text_at(0), updated = q.text_at(2);
    return {{"revision", q.int_at(1)}, {"document", raw.empty() ? json(nullptr) : json::parse(raw)},
            {"updated_at", updated.empty() ? json(nullptr) : json(updated)}};
}
json Store::save_document(long long id, long long expected_revision, const json& document,
                          const std::string& workbook) {
    if (expected_revision < 0 || expected_revision == std::numeric_limits<long long>::max())
        throw InputError("Invalid document revision");
    validate_document(document);
    if (workbook.size() < 4 || workbook.size() > 10 * 1024 * 1024 || workbook.substr(0, 4) != std::string("PK\x03\x04", 4))
        throw InputError("Workbook must be an XLSX ZIP file");
    auto normalized=document;auto code=document_one_c(normalized);
    auto raw=normalized.dump();
    auto bytes=code?set_workbook_one_c(workbook,*code):workbook;
    std::lock_guard<std::mutex> guard(mutex_);
    exec(db_, "BEGIN IMMEDIATE");
    try {
        Statement exists(db_, "SELECT document_revision,one_c_code FROM cards WHERE id=?");
        exists.integer(1, id);
        if (!exists.step()) throw NotFound("Card not found");
        if(!code&&!exists.text_at(1).empty())throw InputError("В бланке удалено поле «Код 1С». Восстановите его или сначала очистите значение в привязанной ячейке.");
        if (exists.int_at(0) != expected_revision)
            throw RevisionConflict("Document was changed by another editor. Reload the saved version before saving.");
        Statement update(db_, "UPDATE cards SET document_json=?,workbook=?,document_revision=document_revision+1,"
                              "document_updated_at=strftime('%Y-%m-%dT%H:%M:%fZ','now'),one_c_code=COALESCE(?,one_c_code) WHERE id=? AND document_revision=?");
        update.text(1,raw);update.blob(2,bytes);if(code)update.text(3,*code);else sqlite3_bind_null(update.value,3);update.integer(4,id);update.integer(5,expected_revision);
        update.step();
        if (sqlite3_changes(db_) != 1) throw RevisionConflict("Document revision changed");
        Statement q(db_, "SELECT document_revision,document_updated_at,one_c_code FROM cards WHERE id=?");
        q.integer(1, id); q.step();
        json result = {{"revision", q.int_at(0)}, {"updated_at", q.text_at(1)},{"one_c_code",q.text_at(2)}};
        exec(db_, "COMMIT");
        return result;
    } catch (...) { sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr); throw; }
}

json Store::assign_codes(const json& cards,const json& groups){
    if(!cards.is_array()||cards.empty()||cards.size()>1000||!groups.is_array()||groups.empty()||groups.size()>1000)throw InputError("Выберите 1–1000 карточек и задайте группы кодов 1С.");
    std::vector<std::string> codes;
    for(const auto& group:groups){
        if(!group.is_object()||!group.contains("count")||!group["count"].is_number_integer()||group["count"]<1||group["count"]>1000||!group.contains("code")||!group["code"].is_string())throw InputError("В каждой группе нужны положительное количество и строка кода 1С.");
        auto code=validate_one_c(group["code"].get<std::string>());auto count=group["count"].get<size_t>();
        if(codes.size()+count>cards.size())throw InputError("Сумма количеств кодов 1С превышает число выбранных номеров.");
        codes.insert(codes.end(),count,code);
    }
    if(codes.size()!=cards.size())throw InputError("Распределите код 1С для всех выбранных номеров. Код может быть пустым.");
    std::set<long long> unique;std::vector<std::pair<long long,long long>> expected;
    for(const auto& c:cards){
        if(!c.is_object()||!c.contains("id")||!c["id"].is_number_integer()||c["id"]<=0||c["id"]>std::numeric_limits<long long>::max()||!c.contains("revision")||!c["revision"].is_number_integer()||c["revision"]<0||c["revision"]>=std::numeric_limits<long long>::max())throw InputError("Некорректная карточка или версия бланка.");
        auto id=c["id"].get<long long>();if(!unique.insert(id).second)throw InputError("Карточка выбрана дважды.");expected.emplace_back(id,c["revision"].get<long long>());
    }
    std::lock_guard<std::mutex> guard(mutex_);exec(db_,"BEGIN IMMEDIATE");json result=json::array();
    try{for(size_t i=0;i<expected.size();i++){
        auto [id,revision]=expected[i];Statement row(db_,"SELECT one_c_code,document_revision,document_json,workbook FROM cards WHERE id=?");row.integer(1,id);
        if(!row.step())throw NotFound("Одна из карточек не найдена.");
        if(row.int_at(1)!=revision)throw RevisionConflict("Один из бланков изменен. Обновите список выбранных номеров и повторите распределение кодов 1С.");
        if(row.text_at(0)!=codes[i]){
            auto p=static_cast<const char*>(sqlite3_column_blob(row.value,3));auto n=sqlite3_column_bytes(row.value,3);auto bytes=set_workbook_one_c(std::string(p?p:"",n),codes[i]);
            auto raw=row.text_at(2);json document=raw.empty()?json():json::parse(raw);if(!document.is_null()&&!set_document_one_c(document,codes[i]))throw InputError("В сохраненном бланке не найдено поле «Код 1С». Восстановите поле в редакторе.");
            Statement update(db_,"UPDATE cards SET one_c_code=?,workbook=?,document_json=?,document_revision=document_revision+1,document_updated_at=strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE id=?");
            update.text(1,codes[i]);update.blob(2,bytes);if(document.is_null())sqlite3_bind_null(update.value,3);else update.text(3,document.dump());update.integer(4,id);update.step();
        }
        auto sql=std::string(card_select)+"WHERE c.id=?";Statement selected(db_,sql.c_str());selected.integer(1,id);selected.step();result.push_back(read_card(selected));
    }exec(db_,"COMMIT");return result;}catch(...){sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
}
