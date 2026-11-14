#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include "portable_package.h"
#ifndef PORTABLE_ROLE
#define PORTABLE_ROLE "Reader"
#endif
namespace {
std::wstring wide(const std::string& text) {
    auto n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(n, L'\0'); MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n); return out;
}
std::wstring quote(const std::wstring& text) {
    std::wstring out = L"\""; size_t slashes = 0;
    for (auto c : text) { if (c == L'\\') { ++slashes; continue; } out.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\'); out += c; slashes = 0; }
    out.append(slashes * 2, L'\\'); return out + L'"';
}
struct Handle { HANDLE value = INVALID_HANDLE_VALUE; ~Handle() { if (value != INVALID_HANDLE_VALUE && value) CloseHandle(value); } };
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    try {
        const auto resource = FindResourceW(instance, MAKEINTRESOURCEW(201), RT_RCDATA);
        if (!resource) throw std::runtime_error("В EXE отсутствует встроенный клиент.");
        const auto length = SizeofResource(instance, resource);
        const auto data = LockResource(LoadResource(instance, resource));
        if (!data || !length) throw std::runtime_error("Не удалось прочитать встроенный клиент.");
        const std::string payload(static_cast<const char*>(data), length);
        const std::string role = PORTABLE_ROLE;
        PWSTR path = nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path))) throw std::runtime_error("LocalAppData недоступен.");
        auto parent = std::filesystem::path(path) / L"RepairCards" / L"clients" / wide(role); CoTaskMemFree(path);
        std::filesystem::create_directories(parent);
        const auto destination = parent / wide(repair::portable::identity(payload));
        int argc = 0; auto args = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (!args) throw std::runtime_error("Не удалось прочитать параметры запуска.");
        std::vector<std::wstring> arguments;for(int i=1;i<argc;++i)arguments.emplace_back(args[i]);LocalFree(args);
        const bool update=arguments.size()==1&&arguments[0]==L"--update";
        const bool files=arguments.size()==1&&arguments[0]==L"--files";
        auto lock_path = destination; lock_path += L".extract.lock";
        Handle lock;
        const auto started = GetTickCount64();
        for (;;) {
            lock.value = CreateFileW(lock_path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (lock.value != INVALID_HANDLE_VALUE) break;
            if (GetLastError() != ERROR_SHARING_VIOLATION || GetTickCount64() - started > 30000) throw std::runtime_error("Не удалось открыть кэш клиента. Проверьте права LocalAppData; другой запуск может еще готовить файлы.");
            Sleep(50);
        }
        if(!(update&&std::filesystem::is_regular_file(destination/L"Updater.exe")))repair::portable::install(payload, destination, role);
        CloseHandle(lock.value); lock.value = INVALID_HANDLE_VALUE;
        if(files){auto result=ShellExecuteW(nullptr,L"open",destination.c_str(),nullptr,nullptr,SW_SHOWNORMAL);if(reinterpret_cast<INT_PTR>(result)<=32)throw std::runtime_error("Не удалось открыть папку клиента.");return 0;}
        const auto executable = destination / (update?L"Updater.exe":wide(role + ".exe"));
        auto command = quote(executable.wstring());
        if(!update)for(const auto& argument:arguments)command+=L" "+quote(argument);
        STARTUPINFOW startup{}; startup.cb = sizeof(startup); startup.dwFlags = STARTF_USESHOWWINDOW; startup.wShowWindow = static_cast<WORD>(show);
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, destination.c_str(), &startup, &process)) throw std::runtime_error("Не удалось запустить локальный клиент. Проверьте антивирус и права на кэш.");
        Handle child; child.value = process.hProcess; CloseHandle(process.hThread);
        WaitForSingleObject(child.value, INFINITE);
        DWORD status = 1; GetExitCodeProcess(child.value, &status); return static_cast<int>(status);
    } catch (const std::exception& error) { MessageBoxW(nullptr, wide(error.what()).c_str(), L"Repair Cards — запуск", MB_OK | MB_ICONERROR); return 1; }
}
