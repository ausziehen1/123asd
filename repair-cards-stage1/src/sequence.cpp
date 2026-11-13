#include "sequence.h"

std::string next_product_id(const std::string& id) {
    if (id.empty() || id.size() > 128 || id.find_first_of("\r\n\t") != std::string::npos)
        throw InputError("ID must contain 1..128 bytes and no control whitespace");
    size_t start = id.size();
    while (start > 0 && id[start - 1] >= '0' && id[start - 1] <= '9') --start;
    if (start == id.size()) throw InputError("ID must end with digits, for example V00001");
    std::string result = id;
    size_t pos = result.size();
    while (pos > start) {
        --pos;
        if (result[pos] != '9') { ++result[pos]; return result; }
        result[pos] = '0';
    }
    result.insert(start, 1, '1');
    if (result.size() > 128) throw InputError("Next ID exceeds 128 bytes");
    return result;
}

std::vector<std::string> make_sequence(const std::string& first, int count) {
    if (count < 1 || count > 1000) throw InputError("Count must be between 1 and 1000");
    next_product_id(first); // Validate before allocating records.
    std::vector<std::string> ids;
    ids.reserve(static_cast<size_t>(count));
    auto current = first;
    for (int i = 0; i < count; ++i) {
        ids.push_back(current);
        current = next_product_id(current);
    }
    return ids;
}
