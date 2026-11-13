#pragma once
#include "json.hpp"
#include <stdexcept>
#include <string>

namespace repair {
using Json = nlohmann::json;
struct ConnectionError : std::runtime_error { using std::runtime_error::runtime_error; };
struct ApiError : std::runtime_error { using std::runtime_error::runtime_error; };
// A failed response to a mutation must not be retried automatically.
struct RegistrationUncertain : std::runtime_error { using std::runtime_error::runtime_error; };

std::string normalize_server_url(std::string url);
class ApiClient {
public:
    explicit ApiClient(std::string server_url);
    Json health() const;
    Json platforms() const;
    Json add_platform(const std::string& name) const;
    Json cards(long long platform, long long after = 0) const;
    Json preview(const std::string& first, int count) const;
    Json register_batch(long long platform, const std::string& first, int count) const;
private:
    std::string base_;
    Json request(const std::string& path, const Json* body = nullptr, bool registration = false) const;
};
}
