#pragma once
#include <filesystem>
#include <string>
namespace repair::portable {
std::string identity(const std::string& payload);
// Caller holds the cache's interprocess lock during initial installation.
void install(const std::string& payload, const std::filesystem::path& destination,
             const std::string& role);
}
