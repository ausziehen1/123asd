#include "backup.h"
#include <iostream>
#include <string>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif
int main(int argc, char** argv) {
    try {
        std::filesystem::path source, target;
#ifdef _WIN32
        auto args = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (!args) throw std::runtime_error("Cannot read arguments");
        if (argc == 5 && std::wstring(args[1]) == L"--database" && std::wstring(args[3]) == L"--output") {source=args[2];target=args[4];}
        LocalFree(args);
#else
        if (argc == 5 && std::string(argv[1]) == "--database" && std::string(argv[3]) == "--output") {source=std::filesystem::u8path(argv[2]);target=std::filesystem::u8path(argv[4]);}
#endif
        if (source.empty() || target.empty()) throw std::runtime_error("Usage: repair_backup --database path/repair.sqlite3 --output path/new-backup.sqlite3");
        backup_database(source, target);
        std::cout << "Backup completed; integrity_check: ok\n"; return 0;
    } catch (const std::exception& error) {std::cerr << error.what() << '\n';return 1;}
}
