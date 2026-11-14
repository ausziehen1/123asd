#pragma once
#include <filesystem>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#endif

// A persistent lock file, held by the OS until process exit. Never unlink a
// live lock file: another process could then lock a different inode.
class ServerDatabaseLock {
#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    int handle_ = -1;
#endif
public:
    explicit ServerDatabaseLock(const std::filesystem::path& database) {
        auto path = std::filesystem::weakly_canonical(database);
#ifdef _WIN32
        if (path.wstring().rfind(L"\\\\", 0) == 0 || GetDriveTypeW(path.root_path().c_str()) == DRIVE_REMOTE)
            throw std::runtime_error("SQLite must be on a local server disk, not a network share.");
        path += L".server.lock";
        handle_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE)
#else
        path += ".server.lock";
        handle_ = open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
        if (handle_ < 0 || flock(handle_, LOCK_EX | LOCK_NB) != 0) {
            if (handle_ >= 0) ::close(handle_);
            handle_ = -1;
#endif
            throw std::runtime_error("Database is already owned by another Repair Cards server, or its lock file is inaccessible. Stop the other server and check folder permissions.");
#ifndef _WIN32
        }
#endif
    }
    ~ServerDatabaseLock() {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
#else
        if (handle_ >= 0) ::close(handle_);
#endif
    }
    ServerDatabaseLock(const ServerDatabaseLock&) = delete;
    ServerDatabaseLock& operator=(const ServerDatabaseLock&) = delete;
};
