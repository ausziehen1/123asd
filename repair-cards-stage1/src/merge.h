#pragma once
#include "json.hpp"
nlohmann::json merge_documents(const nlohmann::json& base, const nlohmann::json& current,
                               const nlohmann::json& incoming);
std::string document_workbook(const nlohmann::json& document);
