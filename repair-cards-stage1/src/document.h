#pragma once
#include "json.hpp"
#include <string>

void validate_document(const nlohmann::json& document);
std::string decode_workbook(const std::string& base64);
