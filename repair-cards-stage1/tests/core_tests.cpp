#include "store.h"
#include "sequence.h"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class Exception, class Function>
void throws(Function function) {
    try { function(); }
    catch (const Exception&) { return; }
    throw std::runtime_error("Expected exception was not raised");
}
void execute_external(const std::string& file, const char* sql) {
    sqlite3* db = nullptr;
    if (sqlite3_open(file.c_str(), &db) != SQLITE_OK) throw std::runtime_error("Test DB open failed");
    int rc = sqlite3_exec(db, sql, nullptr, nullptr, nullptr);
    std::string error = sqlite3_errmsg(db);
    sqlite3_close(db);
    if (rc != SQLITE_OK) throw std::runtime_error(error);
}
int main() {
    auto dir = std::filesystem::temp_directory_path() /
        ("repair-core-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        require(next_product_id("V00001") == "V00002", "Leading zeroes");
        require(next_product_id("v00999") == "v01000", "Prefix case and carry");
        require(next_product_id("99999") == "100000", "Numeric width growth");
        require(next_product_id("A-B-009") == "A-B-010", "Compound prefix");
        auto sequence = make_sequence("V00001", 5);
        require(sequence.size() == 5 && sequence.back() == "V00005", "Batch range");
        throws<InputError>([] { make_sequence("V", 5); });
        throws<InputError>([] { make_sequence("V00001", 0); });
        throws<InputError>([] { make_sequence("V00001", 1001); });
        require(next_product_id("V999999999999999999999999999999") ==
                "V1000000000000000000000000000000", "No integer overflow");
        std::filesystem::create_directories(dir);
        auto file = (dir / "test.sqlite3").u8string();
        const std::string original("PK\x03\x04\0template", 13);
        {
            Store store(file);
            auto p = store.create_platform("Laptop 16.5").at("id").get<long long>();
            auto p2 = store.create_platform("Laptop 17").at("id").get<long long>();
            require(store.create_platform("Laptop 16.5").at("id") == p, "Reuse platform");
            auto batch = store.create_batch(p, "V00001", 5, original);
            require(batch.at("next_product_id") == "V00006", "Next suggestion");
            auto card_id = batch.at("cards").at(0).at("id").get<long long>();
            require(store.workbook(card_id) == original, "Binary workbook preserved");
            store.create_batch(p2, "V00001", 1, original);
            store.create_batch(p, "V00001", 1, original);
            require(store.cards(0, 100, 0).size() == 7, "Duplicate external IDs allowed");
            require(store.cards(p, 100, 0).size() == 6, "Platform filter");
            require(store.cards(0, 2, 2).at(0).at("id") == 3, "Pagination");
            throws<NotFound>([&] { store.create_batch(9999, "X001", 5, original); });
            require(store.cards(0, 100, 0).size() == 7, "Unknown platform atomicity");
            execute_external(file,
                "CREATE TRIGGER fail_batch BEFORE INSERT ON cards WHEN NEW.product_id='F002' "
                "BEGIN SELECT RAISE(ABORT,'simulated failure'); END;");
            throws<std::runtime_error>([&] { store.create_batch(p, "F001", 3, original); });
            require(store.cards(0, 100, 0).size() == 7, "Middle-of-batch rollback");
            require(store.platforms().at(0).at("next_product_id") == "V00002", "Counter rollback");
            execute_external(file, "UPDATE cards SET workbook=X'010203' WHERE id=1;");
            require(store.workbook(2) == original, "Independent workbook copies");
            throws<NotFound>([&] { store.card(999999); });
        }
        {
            Store reopened(file);
            require(reopened.cards(0, 100, 0).size() == 7, "Persistence after reopen");
            require(reopened.workbook(2) == original, "Workbook persistence");
        }
        std::filesystem::remove_all(dir);
        std::cout << "Core tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "TEST FAILED: " << e.what() << '\n';
        std::error_code ignored;
        std::filesystem::remove_all(dir, ignored);
        return 1;
    }
}
