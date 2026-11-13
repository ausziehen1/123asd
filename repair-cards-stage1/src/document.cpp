#include "document.h"
#include "sequence.h"
#include <charconv>
#include <limits>

namespace {
int integer(const nlohmann::json& value, int lo, int hi) {
    if (!value.is_number_integer() || value < lo || value > hi) throw InputError("Invalid document dimension or index");
    return value.get<int>();
}
int index(const std::string& text, int limit) {
    int result = 0;
    auto r = std::from_chars(text.data(), text.data() + text.size(), result);
    if (r.ec != std::errc{} || r.ptr != text.data() + text.size() || result < 0 || result >= limit)
        throw InputError("Document cell index exceeds sheet dimensions");
    return result;
}
void dimension(const nlohmann::json& value) {
    if (!value.is_number() || value < 0 || value > 3000) throw InputError("Invalid row height or column width");
}
void safe_keys(const nlohmann::json& value) {
    if (value.is_object()) for (const auto& entry : value.items()) {
        if (entry.key() == "__proto__" || entry.key() == "prototype" || entry.key() == "constructor")
            throw InputError("Invalid document property");
        safe_keys(entry.value());
    }
    else if (value.is_array()) for (const auto& item : value) safe_keys(item);
}
}
void validate_document(const nlohmann::json& document) {
    safe_keys(document);
    if (!document.is_object() || document.value("format", "") != "x-data-spreadsheet" ||
        !document.contains("version") || integer(document["version"], 1, 1) != 1 ||
        !document.contains("sheets") || !document["sheets"].is_array() ||
        document["sheets"].empty() || document["sheets"].size() > 16)
        throw InputError("Unsupported document format");
    if (document.dump().size() > 10 * 1024 * 1024) throw InputError("Document exceeds 10 MiB");
    for (const auto& sheet : document["sheets"]) {
        if (!sheet.is_object() || !sheet.contains("name") || !sheet["name"].is_string() ||
            sheet["name"].get_ref<const std::string&>().size() > 200 ||
            !sheet.contains("rows") || !sheet["rows"].is_object() ||
            !sheet.contains("cols") || !sheet["cols"].is_object()) throw InputError("Invalid sheet");
        for (const auto& entry : sheet.items()) {
            if (entry.key() != "name" && entry.key() != "freeze" && entry.key() != "styles" &&
                entry.key() != "merges" && entry.key() != "rows" && entry.key() != "cols" &&
                entry.key() != "validations" && entry.key() != "autofilter") throw InputError("Unknown sheet property");
        }
        const auto& rows = sheet["rows"]; const auto& cols = sheet["cols"];
        if (!rows.contains("len") || !cols.contains("len")) throw InputError("Missing sheet dimensions");
        int nr = integer(rows["len"], 1, 20000), nc = integer(cols["len"], 1, 400);
        size_t styles = 0;
        if (sheet.contains("styles")) {
            if (!sheet["styles"].is_array() || sheet["styles"].size() > 10000) throw InputError("Invalid styles");
            styles = sheet["styles"].size();
            for (const auto& style : sheet["styles"]) if (!style.is_object()) throw InputError("Invalid cell style");
        }
        for (const auto& item : cols.items()) {
            if (item.key() == "len") continue;
            index(item.key(), nc);
            if (!item.value().is_object()) throw InputError("Invalid column");
            if (item.value().contains("width")) dimension(item.value()["width"]);
        }
        for (const auto& item : rows.items()) {
            if (item.key() == "len") continue;
            index(item.key(), nr);
            const auto& row = item.value();
            if (!row.is_object()) throw InputError("Invalid row");
            if (row.contains("height")) dimension(row["height"]);
            if (!row.contains("cells")) continue;
            if (!row["cells"].is_object()) throw InputError("Invalid cells");
            for (const auto& entry : row["cells"].items()) {
                index(entry.key(), nc);
                const auto& cell = entry.value();
                if (!cell.is_object()) throw InputError("Invalid cell");
                if (cell.contains("text") && !(cell["text"].is_primitive())) throw InputError("Invalid cell text");
                if (cell.contains("text") && cell["text"].dump().size() > 100000) throw InputError("Cell text is too large");
                if (cell.contains("style")) {
                    if (styles == 0) throw InputError("Cell references missing style");
                    integer(cell["style"], 0, static_cast<int>(styles) - 1);
                }
                if (cell.contains("merge")) {
                    if (!cell["merge"].is_array() || cell["merge"].size() != 2) throw InputError("Invalid merged cell");
                    integer(cell["merge"][0], 0, nr - 1); integer(cell["merge"][1], 0, nc - 1);
                }
            }
        }
        if (sheet.contains("merges") && (!sheet["merges"].is_array() || sheet["merges"].size() > 20000))
            throw InputError("Invalid merged ranges");
    }
}
std::string decode_workbook(const std::string& base64) {
    if (base64.empty() || base64.size() % 4 != 0 || base64.size() > 14 * 1024 * 1024)
        throw InputError("Invalid workbook size");
    auto digit = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::string result; result.reserve(base64.size() / 4 * 3);
    for (size_t i = 0; i < base64.size(); i += 4) {
        int a = digit(base64[i]), b = digit(base64[i + 1]);
        bool pad_c = base64[i + 2] == '=', pad_d = base64[i + 3] == '=';
        int c = pad_c ? 0 : digit(base64[i + 2]), d = pad_d ? 0 : digit(base64[i + 3]);
        if (a < 0 || b < 0 || c < 0 || d < 0 || (pad_c && !pad_d) ||
            ((pad_c || pad_d) && i + 4 != base64.size()) || (pad_c && (b & 15)) || (pad_d && !pad_c && (c & 3)))
            throw InputError("Invalid workbook encoding");
        result.push_back(static_cast<char>((a << 2) | (b >> 4)));
        if (!pad_c) result.push_back(static_cast<char>((b << 4) | (c >> 2)));
        if (!pad_d) result.push_back(static_cast<char>((c << 6) | d));
    }
    if (result.size() < 4 || result.size() > 10 * 1024 * 1024 || result.substr(0, 4) != std::string("PK\x03\x04", 4))
        throw InputError("Workbook must be an XLSX ZIP file");
    return result;
}
