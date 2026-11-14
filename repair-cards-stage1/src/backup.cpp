#include "backup.h"
#include "sqlite3.h"
#include <chrono>
#include <thread>
#include <stdexcept>
#include <random>
#ifdef _WIN32
#include <windows.h>
#endif
void backup_database(const std::filesystem::path& source, const std::filesystem::path& destination) {
    namespace fs = std::filesystem;
    if (!fs::is_regular_file(source)) throw std::runtime_error("Source database does not exist");
    if (fs::exists(destination)) throw std::runtime_error("Backup destination already exists; choose a new filename");
    fs::create_directories(fs::absolute(destination).parent_path());
    auto temporary = destination; temporary += "." + std::to_string(std::random_device{}()) + ".tmp";
    sqlite3 *input = nullptr, *output = nullptr;
    try {
        if (sqlite3_open_v2(source.u8string().c_str(), &input, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) throw std::runtime_error("Cannot open source database");
        if (sqlite3_open_v2(temporary.u8string().c_str(), &output, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) throw std::runtime_error("Cannot open backup file");
        sqlite3_busy_timeout(input, 5000); sqlite3_busy_timeout(output, 5000);
        auto* backup = sqlite3_backup_init(output, "main", input, "main");
        if (!backup) throw std::runtime_error(sqlite3_errmsg(output));
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        int result;
        do {
            result = sqlite3_backup_step(backup, 256);
            if (result == SQLITE_BUSY || result == SQLITE_LOCKED) std::this_thread::sleep_for(std::chrono::milliseconds(50));
        } while ((result == SQLITE_OK || result == SQLITE_BUSY || result == SQLITE_LOCKED) && std::chrono::steady_clock::now() < deadline);
        auto finished = sqlite3_backup_finish(backup);
        if (result != SQLITE_DONE || finished != SQLITE_OK) throw std::runtime_error("Backup could not complete within 60 seconds; no destination file was published");
        sqlite3_stmt* check = nullptr;
        if (sqlite3_prepare_v2(output, "PRAGMA integrity_check", -1, &check, nullptr) != SQLITE_OK) throw std::runtime_error("Cannot check backup integrity");
        bool valid = sqlite3_step(check) == SQLITE_ROW && std::string(reinterpret_cast<const char*>(sqlite3_column_text(check, 0))) == "ok";
        sqlite3_finalize(check);
        if (!valid) throw std::runtime_error("Backup integrity check failed");
        sqlite3_close(output); output = nullptr; sqlite3_close(input); input = nullptr;
        // Never replace another backup, including a concurrently created file.
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("Cannot publish backup; destination may already exist");
#else
        fs::create_hard_link(temporary, destination); fs::remove(temporary);
#endif
    } catch (...) {
        if (output) sqlite3_close(output); if (input) sqlite3_close(input);
        std::error_code ignored; fs::remove(temporary, ignored); throw;
    }
}
