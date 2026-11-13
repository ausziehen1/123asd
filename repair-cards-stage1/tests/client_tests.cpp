#include "httplib.h"
#include "api_client.h"
#include <iostream>
#include <stdexcept>
#include <thread>

using repair::Json;
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
template<class Exception, class Function>
void raises(Function f) {
    try { f(); } catch (const Exception&) { return; }
    throw std::runtime_error("Expected exception missing");
}
int main() {
    httplib::Server server;
    server.new_task_queue = [] { return new httplib::ThreadPool(2); };
    std::thread worker;
    try {
        require(repair::normalize_server_url("http://localhost:8080/") == "http://localhost:8080", "Trailing slash");
        require(repair::normalize_server_url("http://[::1]:8080") == "http://[::1]:8080", "IPv6 URL");
        for (const auto* url : {"https://localhost:8080", "http://localhost:0", "http://localhost:65536",
                                "http://user@localhost", "http://localhost/cards", "file:///x"})
            raises<repair::ApiError>([&] { repair::normalize_server_url(url); });
        server.Get("/api/health", [](const auto&, auto& res) { res.set_content(R"({"status":"ok"})", "application/json"); });
        server.Get("/api/platforms", [](const auto&, auto& res) {
            res.set_content(R"([{"id":1,"name":"Laptop","next_product_id":"V00006"}])", "application/json");
        });
        server.Get("/api/cards", [](const auto& req, auto& res) {
            if (req.get_param_value("platform_type_id") != "7" || req.get_param_value("after") != "100" ||
                req.get_param_value("limit") != "100") { res.status = 400; return; }
            res.set_content(R"([{"id":101,"product_id":"V00001"}])", "application/json");
        });
        server.Post("/api/platforms", [](const auto& req, auto& res) {
            auto data = Json::parse(req.body);
            if (data.at("name") != "Test platform") { res.status = 400; return; }
            res.set_content(R"({"id":2,"name":"Test platform"})", "application/json");
        });
        server.Post("/api/batches/preview", [](const auto& req, auto& res) {
            auto data = Json::parse(req.body);
            if (data.at("first_product_id") != "V00001" || data.at("count") != 5) { res.status = 400; return; }
            res.set_content(R"({"first_product_id":"V00001","last_product_id":"V00005"})", "application/json");
        });
        server.Post("/api/batches", [](const auto& req, auto& res) {
            auto data = Json::parse(req.body);
            if (data.at("platform_type_id") == 99) {
                res.status = 404; res.set_content(R"({"error":"Platform type not found"})", "application/json"); return;
            }
            if (data.at("platform_type_id") == 98) { res.set_content("broken", "text/plain"); return; }
            if (data.at("platform_type_id") != 1 || data.at("count") != 5) { res.status = 400; return; }
            res.status = 201;
            res.set_content(R"({"count":5,"next_product_id":"V00006"})", "application/json");
        });
        int port = server.bind_to_any_port("127.0.0.1");
        require(port > 0, "Cannot bind test server");
        worker = std::thread([&] { server.listen_after_bind(); });
        server.wait_until_ready();
        repair::ApiClient api("http://127.0.0.1:" + std::to_string(port));
        require(api.health().at("status") == "ok", "Health response");
        require(api.platforms().at(0).at("next_product_id") == "V00006", "Suggestion from server");
        require(api.cards(7, 100).at(0).at("id") == 101, "Filter and pagination");
        require(api.add_platform("Test platform").at("id") == 2, "Add platform");
        require(api.preview("V00001", 5).at("last_product_id") == "V00005", "Preview range");
        require(api.register_batch(1, "V00001", 5).at("next_product_id") == "V00006", "Registration");
        raises<repair::ApiError>([&] { api.register_batch(99, "V00001", 5); });
        raises<repair::RegistrationUncertain>([&] { api.register_batch(98, "V00001", 5); });
        server.stop(); worker.join();
        raises<repair::ConnectionError>([&] { api.health(); });
        raises<repair::RegistrationUncertain>([&] { api.register_batch(1, "V00001", 5); });
        std::cout << "Client tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        server.stop(); if (worker.joinable()) worker.join();
        std::cerr << "TEST FAILED: " << e.what() << '\n'; return 1;
    }
}
