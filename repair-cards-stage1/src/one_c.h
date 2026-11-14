#pragma once
#include "json.hpp"
#include <optional>
#include <string>
std::string validate_one_c(std::string code);
std::optional<std::string> document_one_c(nlohmann::json& document);
bool set_document_one_c(nlohmann::json& document,const std::string& code);
std::optional<std::string> workbook_one_c(const std::string& workbook);
std::string set_workbook_one_c(const std::string& workbook,const std::string& code);
