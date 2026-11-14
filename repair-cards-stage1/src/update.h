#pragma once
#include "json.hpp"
#include <filesystem>
namespace repair::update {
nlohmann::json inspect(const std::filesystem::path& archive,const std::filesystem::path& installation);
void apply(const std::filesystem::path& archive,const std::filesystem::path& installation);
bool recover(const std::filesystem::path& installation);
void rollback(const std::filesystem::path& installation);
}
