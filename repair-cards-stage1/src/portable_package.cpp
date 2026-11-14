#include "portable_package.h"
#include "json.hpp"
#include "miniz.h"
#include "picosha2.h"
#include <fstream>
#include <map>
#include <set>
namespace repair::portable {
namespace {
namespace fs = std::filesystem;
using Json = nlohmann::json;
void write(const fs::path& path, const std::string& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    if (!stream || !stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size())))
        throw std::runtime_error("Не удалось записать локальный кэш клиента.");
    stream.close();
    if (!stream) throw std::runtime_error("Ошибка завершения записи клиента.");
}
bool safe_name(const std::string& name) {
    if (name.empty() || name.front() == '/' || name.back() == '/' || name.find_first_of("\\:\r\n") != name.npos || name.find('\0') != name.npos) return false;
    for (const auto& part : fs::u8path(name)) if (part == "." || part == ".." || part.empty()) return false;
    return true;
}
}
std::string identity(const std::string& payload) { return picosha2::hash256_hex_string(payload); }
void install(const std::string& payload, const fs::path& destination, const std::string& role) {
    if (role != "Creator" && role != "Reader" && role != "Repair") throw std::runtime_error("Invalid client role");
    const auto payload_id = identity(payload);
    if (fs::exists(destination)) {
        std::ifstream input(destination / ".portable-ready.json");
        const auto marker = Json::parse(input, nullptr, false);
        if (!marker.is_object() || marker.value("payload", "") != payload_id || marker.value("role", "") != role)
            throw std::runtime_error("Локальный кэш клиента поврежден. Не удаляйте рабочие данные; инструкция восстановления находится в руководстве.");
        for (const auto& name : {role + ".exe", std::string("RepairClient.dll"), std::string("WebView2Loader.dll"), std::string("Updater.exe"), std::string("installation.json")})
            if (!fs::is_regular_file(destination / fs::u8path(name))) throw std::runtime_error("В кэше клиента отсутствуют файлы. Запустите Updater для восстановления или переустановите кэш по инструкции.");
        // DLL patches and user theme changes must survive relaunching the same EXE.
        return;
    }
    if (payload.size() > 64 * 1024 * 1024) throw std::runtime_error("Portable payload too large");
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, payload.data(), payload.size(), 0)) throw std::runtime_error("Поврежден встроенный комплект клиента.");
    std::map<std::string, std::string> files;
    try {
        auto count = mz_zip_reader_get_num_files(&zip);
        if (count > 256) throw std::runtime_error("Too many portable files");
        size_t total = 0;
        for (mz_uint i = 0; i < count; ++i) {
            mz_zip_archive_file_stat stat{};
            if (!mz_zip_reader_file_stat(&zip, i, &stat) || stat.m_is_directory || !safe_name(stat.m_filename)) throw std::runtime_error("Invalid portable file path");
            if (stat.m_uncomp_size > 64 * 1024 * 1024 || total + stat.m_uncomp_size > 96 * 1024 * 1024) throw std::runtime_error("Portable files too large");
            total += static_cast<size_t>(stat.m_uncomp_size);
            size_t length = 0; auto* data = mz_zip_reader_extract_to_heap(&zip, i, &length, 0);
            if (!data) throw std::runtime_error("Portable extraction failed");
            auto inserted = files.emplace(stat.m_filename, std::string(static_cast<char*>(data), length)).second;
            mz_free(data);
            if (!inserted) throw std::runtime_error("Duplicate portable file");
        }
        mz_zip_reader_end(&zip);
    } catch (...) { mz_zip_reader_end(&zip); throw; }
    if (!files.count("payload.json")) throw std::runtime_error("Missing portable manifest");
    auto manifest = Json::parse(files.at("payload.json")); files.erase("payload.json");
    if (manifest.value("product", "") != "RepairCardsPortable" || manifest.value("role", "") != role || manifest.value("abi", 0) != 1 || manifest.value("architecture", "") != "x64" || manifest.at("files").size() != files.size()) throw std::runtime_error("Invalid portable manifest");
    std::set<std::string> seen;
    for (const auto& entry : manifest.at("files")) {
        auto name = entry.at("path").get<std::string>();
        if (!safe_name(name) || !seen.insert(name).second || !files.count(name) || identity(files.at(name)) != entry.at("sha256")) throw std::runtime_error("Неверная контрольная сумма встроенного клиента.");
    }
    for (const auto& name : {role + ".exe", std::string("RepairClient.dll"), std::string("WebView2Loader.dll"), std::string("Updater.exe"), std::string("installation.json")})
        if (!files.count(name)) throw std::runtime_error("Incomplete portable payload");
    auto staged = destination; staged += ".staging";
    if (fs::exists(staged)) fs::remove_all(staged);
    try {
        for (const auto& file : files) write(staged / fs::u8path(file.first), file.second);
        write(staged / ".portable-ready.json", Json{{"payload", payload_id}, {"role", role}}.dump());
        fs::rename(staged, destination);
    } catch (...) { std::error_code ignored; fs::remove_all(staged, ignored); throw; }
}
}
