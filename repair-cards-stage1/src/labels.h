#pragma once
#include "api_client.h"
#include <filesystem>
#include <string>
#include <vector>

namespace repair {
struct Label { long long id; std::string product_id, platform, url; };
struct Scan { std::string base; long long id; std::string url; };
std::string normalize_public_url(const std::string& url);
std::string card_url(const std::string& base, long long id);
Scan parse_scan(std::string input, const std::string& default_base);
std::vector<Label> make_labels(const Json& cards, const std::string& public_url);
std::string qr_svg(const std::string& text);
std::string labels_csv(const std::vector<Label>& labels);
std::filesystem::path write_label_job(const std::filesystem::path& parent, const std::vector<Label>& labels);
std::vector<std::string> bartender_arguments(const std::string& btw, const std::string& csv, bool print_dialog);
std::wstring quote_windows_argument(const std::wstring& argument);
}
