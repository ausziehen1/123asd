#include "store.h"
#include "document.h"
#include "sequence.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>

void require(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
template<class Exception, class Function> void raises(Function fn) {
    try { fn(); } catch (const Exception&) { return; }
    throw std::runtime_error("Expected exception was not raised");
}
void sql(const std::string& file, const char* query) {
    sqlite3* db = nullptr;
    if (sqlite3_open(file.c_str(), &db) != SQLITE_OK) throw std::runtime_error("Test database open failed");
    int result = sqlite3_exec(db, query, nullptr, nullptr, nullptr);
    auto reason = std::string(sqlite3_errmsg(db)); sqlite3_close(db);
    if (result != SQLITE_OK) throw std::runtime_error(reason);
}
int main() {
    auto dir = std::filesystem::temp_directory_path() / ("repair-document-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        std::filesystem::create_directories(dir);
        auto file = (dir / "migration.sqlite3").u8string();
        // Real schema from stage 1, with a registered card and its own workbook.
        sql(file, "CREATE TABLE platform_types(id INTEGER PRIMARY KEY AUTOINCREMENT,name TEXT NOT NULL UNIQUE,"
                  "next_product_id TEXT,created_at TEXT NOT NULL DEFAULT 'old-platform');"
                  "CREATE TABLE cards(id INTEGER PRIMARY KEY AUTOINCREMENT,product_id TEXT NOT NULL,"
                  "platform_type_id INTEGER NOT NULL REFERENCES platform_types(id),workbook BLOB NOT NULL,"
                  "created_at TEXT NOT NULL DEFAULT 'old-card');"
                  "INSERT INTO platform_types(name,next_product_id) VALUES('Focusline 16','V00000026');"
                  "INSERT INTO cards(product_id,platform_type_id,workbook) VALUES('V00000024',1,X'504B0304');"
                  "PRAGMA user_version=1;");
        const std::string workbook("PK\x03\x04", 4), edited("PK\x03\x04\x05", 5);
        auto state = json::parse(R"({"format":"x-data-spreadsheet","version":1,"sheets":[{"name":"Card","freeze":"A1","rows":{"len":40,"8":{"height":25,"cells":{"5":{"text":"Fault description","style":0}}}},"cols":{"len":16,"5":{"width":200}},"styles":[{"font":{"name":"Calibri","size":11}}],"merges":[]}]})");
        validate_document(state);
        require(decode_workbook("UEsDBA==") == workbook, "Base64 round trip");
        for (const auto* bad : {"", "AAAA", "UEsDBA=", "UEsDB?==", "UEsDBB=="})
            raises<InputError>([&] { decode_workbook(bad); });
        {
            Store store(file);
            require(store.card(1).at("product_id") == "V00000024", "Existing identity survives migration");
            require(store.platforms().at(0).at("next_product_id") == "V00000026", "Sequence survives migration");
            require(store.workbook(1) == workbook, "Existing workbook survives migration");
            require(store.document(1).at("document").is_null() && store.document(1).at("revision") == 0, "Initial revision");
            auto saved = store.save_document(1, 0, state, edited);
            require(saved.at("revision") == 1, "Revision after first save");
            require(store.document(1).at("document") == state && store.workbook(1) == edited, "State and workbook saved together");
            raises<RevisionConflict>([&] { store.save_document(1, 0, state, workbook); });
            require(store.workbook(1) == edited, "Stale editor cannot overwrite workbook");
            auto bad = state; bad["sheets"][0]["settings"] = {{"mode", "edit"}};
            raises<InputError>([&] { store.save_document(1, 1, bad, workbook); });
            bad = state; bad["sheets"][0]["styles"][0]["__proto__"] = {{"polluted", true}};
            raises<InputError>([&] { store.save_document(1, 1, bad, workbook); });
            bad = state; bad["sheets"][0]["rows"]["len"] = 0;
            raises<InputError>([&] { store.save_document(1, 1, bad, workbook); });
            sql(file, "CREATE TRIGGER save_failure BEFORE UPDATE ON cards BEGIN SELECT RAISE(ABORT,'save failure'); END;");
            raises<std::runtime_error>([&] { store.save_document(1, 1, state, workbook); });
            require(store.document(1).at("revision") == 1 && store.workbook(1) == edited, "Failed save is atomic");
            sql(file, "DROP TRIGGER save_failure;");
            std::atomic<int> success{0}, conflicts{0};
            auto save = [&] {
                try { store.save_document(1, 1, state, edited); ++success; }
                catch (const RevisionConflict&) { ++conflicts; }
            };
            std::thread a(save), b(save); a.join(); b.join();
            require(success == 1 && conflicts == 1, "Only one simultaneous editor can save a revision");
            raises<NotFound>([&] { store.save_document(999, 0, state, workbook); });
        }
        {
            Store reopened(file);
            require(reopened.document(1).at("revision") == 2, "Document revision persists after restart");
            require(reopened.document(1).at("document") == state && reopened.workbook(1) == edited, "Saved document persists after restart");
        }
        std::filesystem::remove_all(dir);
        std::cout << "Document tests passed\n"; return 0;
    } catch (const std::exception& e) {
        std::cerr << "TEST FAILED: " << e.what() << '\n';
        std::error_code ignored; std::filesystem::remove_all(dir, ignored); return 1;
    }
}
