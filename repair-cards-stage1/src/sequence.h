#pragma once
#include <stdexcept>
#include <string>
#include <vector>

struct InputError : std::runtime_error { using std::runtime_error::runtime_error; };
std::string next_product_id(const std::string& id);
std::vector<std::string> make_sequence(const std::string& first, int count);
