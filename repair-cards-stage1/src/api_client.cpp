#include "httplib.h"
#include "api_client.h"
#include <regex>

namespace repair {
std::string normalize_server_url(std::string url) {
    if(url.size()>512)throw ApiError("Адрес сервера слишком длинный.");
    while (!url.empty() && url.back() == '/') url.pop_back();
    // This installation uses an HTTP server in the local network.
    static const std::regex pattern(R"(^http://([A-Za-z0-9._-]+|\[[0-9A-Fa-f:]+\])(:([0-9]{1,5}))?$)");
    std::smatch match;
    if (!std::regex_match(url, match, pattern))
        throw ApiError("Адрес сервера должен иметь вид http://127.0.0.1:8080");
    if (match[3].matched) {
        int port = std::stoi(match[3].str());
        if (port < 1 || port > 65535) throw ApiError("Порт сервера должен быть от 1 до 65535");
    }
    return url;
}
ApiClient::ApiClient(std::string server_url) : base_(normalize_server_url(std::move(server_url))) {}
Json ApiClient::request(const std::string& path, const Json* body, bool registration, bool put) const {
    httplib::Client client(base_);
    client.set_connection_timeout(2, 0);
    client.set_read_timeout(registration ? 60 : 5, 0);
    client.set_write_timeout(5, 0);
    auto response = body ? (put?client.Put(path,body->dump(),"application/json; charset=utf-8"):
        client.Post(path, body->dump(), "application/json; charset=utf-8")) : client.Get(path);
    if (!response) {
        if (registration) throw RegistrationUncertain(
            "Ответ на регистрацию не получен. Партия могла сохраниться. "
            "Обновите список и проверьте записи перед повторной регистрацией.");
        throw ConnectionError("Нет ответа сервера. Проверьте адрес и запущен ли repair_server.");
    }
    auto data = Json::parse(response->body, nullptr, false);
    if (data.is_discarded()) {
        if (registration) throw RegistrationUncertain(
            "Ответ на регистрацию поврежден. Обновите список перед повторной регистрацией.");
        throw ApiError("Сервер вернул некорректный JSON.");
    }
    if (response->status < 200 || response->status >= 300) {
        auto error = data.is_object() && data.contains("error") && data["error"].is_string()
            ? data["error"].get<std::string>() : "Ошибка запроса";
        throw ApiError("HTTP " + std::to_string(response->status) + ": " + error);
    }
    return data;
}
Json ApiClient::health() const {
    auto data = request("/api/health");
    if (!data.is_object() || data.value("status", "") != "ok")
        throw ApiError("По этому адресу не найден работающий сервер карточек.");
    return data;
}
Json ApiClient::assign_codes(const Json& cards,const Json& groups) const {Json body={{"cards",cards},{"groups",groups}};try{return request("/api/labels/assign-queued",&body,true);}catch(const RegistrationUncertain&){throw ApiError("Ответ на распределение кодов 1С не получен. Изменения могли сохраниться. Закройте окно подготовки QR и откройте выбранные карточки заново перед повтором.");}}
Json ApiClient::platforms() const { return request("/api/platforms"); }
Json ApiClient::server_info() const { return request("/api/server-info"); }
Json ApiClient::card(long long id) const { return request("/api/cards/"+std::to_string(id)); }
Json ApiClient::resolve_cards(const Json& ids) const {
    Json body={{"ids",ids}};return request("/api/labels/cards",&body);
}
Json ApiClient::set_public_url(const std::string& url) const {
    Json body={{"public_url",url}};return request("/api/settings/public-url",&body,false,true);
}
Json ApiClient::add_platform(const std::string& name) const {
    Json body = {{"name", name}};
    return request("/api/platforms", &body);
}
Json ApiClient::cards(long long platform, long long after) const {
    return request("/api/cards?platform_type_id=" + std::to_string(platform) + "&after=" +
                   std::to_string(after) + "&limit=100");
}
Json ApiClient::preview(const std::string& first, int count) const {
    Json body = {{"first_product_id", first}, {"count", count}};
    return request("/api/batches/preview", &body);
}
Json ApiClient::register_batch(long long platform, const std::string& first, int count) const {
    Json body = {{"platform_type_id", platform}, {"first_product_id", first}, {"count", count}};
    return request("/api/batches", &body, true);
}
Json ApiClient::register_batch(long long platform, const std::string& first, int count,
                                long long expected_allocation_revision, const std::string& request_id) const {
    Json body = {{"platform_type_id", platform}, {"first_product_id", first}, {"count", count},
                 {"expected_allocation_revision", expected_allocation_revision}, {"request_id", request_id}, {"queued",true}};
    return request("/api/batches", &body, true);
}
}
