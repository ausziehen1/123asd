#include "httplib.h"
#include "store.h"
#include "server_lock.h"
#include "sequence.h"
#include "document.h"
#include "labels.h"
#include "network.h"
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <random>

namespace fs = std::filesystem;

std::string read_file(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot read file: " + path.u8string());
    std::ostringstream out; out << f.rdbuf();
    if (f.bad()) throw std::runtime_error("File read failed: " + path.u8string());
    return out.str();
}
long long number(const std::string& s) {
    long long value = 0;
    auto result = std::from_chars(s.data(), s.data() + s.size(), value);
    if (s.empty() || result.ec != std::errc{} || result.ptr != s.data() + s.size() || value < 0)
        throw InputError("Expected a non-negative integer");
    return value;
}
long long integer_field(const json& body, const char* key) {
    const auto& value = body.at(key);
    if (!value.is_number_integer() ||
        (value.is_number_unsigned() && value.get<unsigned long long>() >
            static_cast<unsigned long long>(std::numeric_limits<long long>::max())))
        throw InputError(std::string(key) + " must be an integer");
    return value.get<long long>();
}
int batch_count(const json& body) {
    auto count = integer_field(body, "count");
    if (count < 1 || count > 1000) throw InputError("Count must be between 1 and 1000");
    return static_cast<int>(count);
}
std::string escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\'': out += "&#39;"; break;
        default: out += c;
        }
    }
    return out;
}
std::string page(const std::string& title, const std::string& body) {
    return "<!doctype html><html lang='ru'><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'><title>" + escape(title) +
        "</title><style>body{font:16px Arial,sans-serif;margin:24px;color:#18202a;background:#f5f6f8}"
        "main{max-width:1200px;margin:auto;background:white;padding:24px;border-radius:12px}"
        "table{border-collapse:collapse;width:100%;margin-top:20px}th,td{border:1px solid #aaa;padding:10px;text-align:left}"
        "th{background:#eee}a{color:#145a9c}select,button{font:inherit;padding:8px}.scroll{overflow:auto}"
        ".note{color:#59616d;line-height:1.5}.blank{min-width:900px}.blank td{height:50px;vertical-align:top}"
        ".heading{background:#ddd;text-align:center;font-weight:bold;padding:12px}"
        ".fields{display:grid;grid-template-columns:2fr 1fr;gap:24px;padding:24px 8px}"
        "@media(max-width:600px){body{margin:8px}main{padding:12px}}</style><main>" + body + "</main></html>";
}
void send(httplib::Response& response, const json& body, int status = 200) {
    response.status = status;
    response.set_content(body.dump(), "application/json; charset=utf-8");
}
template<class Function>
auto safe(Function function) {
    return [function](const httplib::Request& request, httplib::Response& response) {
        try { function(request, response); }
        catch (const InputError& e) { send(response, {{"error", e.what()}}, 400); }
        catch (const repair::ApiError& e) { send(response, {{"error", e.what()}}, 400); }
        catch (const RevisionConflict& e) { send(response, {{"error", e.what()}}, 409); }
        catch (const DatabaseBusy& e) { response.set_header("Retry-After", "2"); send(response, {{"error", e.what()}}, 503); }
        catch (const NotFound& e) { send(response, {{"error", e.what()}}, 404); }
        catch (const json::exception&) { send(response, {{"error", "Invalid JSON body or field types"}}, 400); }
        catch (const std::exception& e) {
            std::cerr << "Request failed: " << e.what() << '\n';
            send(response, {{"error", "Internal server error; see server console"}}, 500);
        }
    };
}

int main(int argc, char** argv) {
    try {
        fs::path executable_parent;
#ifdef _WIN32
        std::wstring executable(32768,L'\0');
        auto executable_size=GetModuleFileNameW(nullptr,executable.data(),static_cast<DWORD>(executable.size()));
        if(!executable_size||executable_size==executable.size())throw std::runtime_error("Cannot resolve executable path");
        executable.resize(executable_size);executable_parent=fs::path(executable).parent_path();
#else
        executable_parent=fs::absolute(fs::u8path(argv[0])).parent_path();
#endif
        fs::path config_path = fs::u8path("config.example.json");
        if(argc==1&&fs::exists(executable_parent/"config.example.json"))config_path=executable_parent/"config.example.json";
        if (argc == 3 && std::string(argv[1]) == "--config") config_path = fs::u8path(argv[2]);
        else if (argc != 1) throw InputError("Usage: repair_server [--config path.json]");
        config_path = fs::absolute(config_path);
        auto config = json::parse(read_file(config_path));
        auto host = config.at("host").get<std::string>();
        auto port_value = integer_field(config, "port");
        if (port_value < 1 || port_value > 65535) throw InputError("Port must be between 1 and 65535");
        int port = static_cast<int>(port_value);
        auto database = config_path.parent_path() / fs::u8path(config.at("database").get<std::string>());
        auto template_path = config_path.parent_path() / fs::u8path(config.at("template").get<std::string>());
        auto web_root = config_path.parent_path() / fs::u8path(config.value("web", "web"));
        if (!config.contains("web") && !fs::exists(web_root / "document.html"))
            web_root = executable_parent / "web";
        auto document_page = read_file(web_root / "document.html");
        auto workbook = read_file(template_path);
        if (workbook.size() < 4 || workbook.substr(0, 4) != std::string("PK\x03\x04", 4))
            throw InputError("Template must be an XLSX ZIP file");
        if (workbook.size() > 10 * 1024 * 1024) throw InputError("Template exceeds 10 MiB");
        fs::create_directories(database.parent_path());
        ServerDatabaseLock database_lock(database);
        Store store(database.u8string());
        if(store.setting("server_id").empty()){
            std::ostringstream identity;std::random_device random;
            for(int i=0;i<4;i++)identity<<std::hex<<random();
            store.set_setting("server_id",identity.str(),true);
        }
        auto configured_url=config.value("public_url",std::string());
        if(!configured_url.empty())store.set_setting("public_url",repair::normalize_public_url(configured_url),true);
        const bool local_only=host=="127.0.0.1"||host=="localhost"||host=="::1";
        auto server_info=[&]{return json{{"version","0.6.0"},{"capabilities",json::array({"one_c_v1","embedded_view_v1","multi_user_v1","batch_guard_v1","queued_save_v1","live_documents_v1"})},{"server_id",store.setting("server_id")},
            {"public_url",store.setting("public_url")},{"local_only",local_only},{"listen_host",host},{"port",port},
            {"suggested_urls",network_urls(port)}};};
        httplib::Server server;
        // One process owns this address/port. httplib's SO_REUSEPORT default
        // could otherwise distribute requests between two different databases.
        server.set_socket_options([](socket_t socket) {
            int enabled=1;
#ifdef _WIN32
            constexpr int option=SO_EXCLUSIVEADDRUSE;
#else
            constexpr int option=SO_REUSEADDR;
#endif
            if(setsockopt(socket,SOL_SOCKET,option,reinterpret_cast<const char*>(&enabled),sizeof(enabled))!=0)
                throw std::runtime_error("Cannot configure listening socket");
        });
        server.new_task_queue = [] { return new httplib::ThreadPool(8); };
        server.set_payload_max_length(26 * 1024 * 1024);
        server.set_read_timeout(10, 0);
        server.set_write_timeout(10, 0);
        server.set_default_headers({{"Cache-Control", "no-store"}, {"X-Content-Type-Options", "nosniff"},
                                   {"Content-Security-Policy", "default-src 'none'; script-src 'self'; style-src 'self' 'unsafe-inline'; connect-src 'self'; img-src 'self' data:; font-src 'self' data:; form-action 'self'; base-uri 'none'; frame-ancestors 'none'"}});
        server.Get(R"(/assets/([a-zA-Z0-9_.-]+))", safe([&](const auto& req, auto& res) {
            auto name = req.matches[1].str();
            if (name != "xspreadsheet.js" && name != "xspreadsheet.css" && name != "exceljs.min.js" &&
                name != "locale-ru.js" && name != "editor.js" && name != "merge.js" && name != "editor.css" &&
                name != "58eaeb4e52248a5c75936c6f4c33a370.svg") throw NotFound("Asset not found");
            auto type = name.size() >= 4 && name.substr(name.size()-4) == ".css" ? "text/css; charset=utf-8" :
                (name.size() >= 4 && name.substr(name.size()-4) == ".svg" ? "image/svg+xml" : "application/javascript; charset=utf-8");
            res.set_content(read_file(web_root / "assets" / name), type);
        }));
        server.Get("/api/health", safe([&](const auto&, auto& res) {
            send(res, {{"status", "ok"}, {"version", "0.6.0"}, {"schema", 5}, {"sqlite", sqlite3_libversion()}});
        }));
        server.Get("/api/server-info",safe([&](const auto&,auto& res){send(res,server_info());}));
        server.Post("/api/labels/cards",safe([&](const auto& req,auto& res){
            auto body=json::parse(req.body);const auto& list=body.at("ids");
            if(!list.is_array()||list.empty()||list.size()>1000)throw InputError("Select 1..1000 cards");
            std::vector<long long> ids;
            for(const auto& value:list){json wrapped={{"id",value}};ids.push_back(integer_field(wrapped,"id"));}
            send(res,store.card_set(ids));
        }));
        server.Post("/api/labels/assign",safe([&](const auto& req,auto& res){auto body=json::parse(req.body);send(res,store.assign_codes(body.at("cards"),body.at("groups")));}));
        server.Post("/api/labels/assign-queued",safe([&](const auto& req,auto& res){auto body=json::parse(req.body);send(res,store.assign_codes(body.at("cards"),body.at("groups"),true));}));
        server.Put("/api/settings/public-url",safe([&](const auto& req,auto& res){
            if(local_only)throw InputError("Для доступа по сети установите host = 0.0.0.0 в конфиге и перезапустите сервер.");
            auto body=json::parse(req.body);
            auto url=repair::normalize_public_url(body.at("public_url").template get<std::string>());
            store.set_setting("public_url",url);send(res,server_info());
        }));
        server.Get(R"(/api/cards/([0-9]+)/qr.svg)",safe([&](const auto& req,auto& res){
            auto id=number(req.matches[1].str());store.card(id);
            auto url=repair::normalize_public_url(store.setting("public_url"));
            res.set_content(repair::qr_svg(repair::card_url(url,id)),"image/svg+xml");
        }));
        server.Get("/api/platforms", safe([&](const auto&, auto& res) { send(res, store.platforms()); }));
        server.Post("/api/platforms", safe([&](const auto& req, auto& res) {
            auto body = json::parse(req.body);
            send(res, store.create_platform(body.at("name").template get<std::string>()), 201);
        }));
        server.Post("/api/batches/preview", safe([&](const auto& req, auto& res) {
            auto body = json::parse(req.body);
            auto ids = make_sequence(body.at("first_product_id").template get<std::string>(), batch_count(body));
            send(res, {{"count", ids.size()}, {"first_product_id", ids.front()},
                       {"last_product_id", ids.back()}, {"next_product_id", next_product_id(ids.back())}});
        }));
        server.Post("/api/batches", safe([&](const auto& req, auto& res) {
            auto body = json::parse(req.body);
            json options = json::object();
            for (const auto* key : {"request_id", "expected_allocation_revision", "queued"}) if (body.contains(key)) options[key] = body[key];
            send(res, store.create_batch(integer_field(body, "platform_type_id"),
                 body.at("first_product_id").template get<std::string>(), batch_count(body), workbook, options), 201);
        }));
        server.Get("/api/cards", safe([&](const auto& req, auto& res) {
            auto platform = req.has_param("platform_type_id") ? number(req.get_param_value("platform_type_id")) : 0;
            auto after = req.has_param("after") ? number(req.get_param_value("after")) : 0;
            auto limit = req.has_param("limit") ? number(req.get_param_value("limit")) : 100;
            if (limit < 1 || limit > 1000) throw InputError("Limit must be between 1 and 1000");
            send(res, store.cards(platform, static_cast<int>(limit), after));
        }));
        server.Get(R"(/api/cards/([0-9]+))", safe([&](const auto& req, auto& res) {
            send(res, store.card(number(req.matches[1].str())));
        }));
        server.Get(R"(/api/cards/([0-9]+)/workbook)", safe([&](const auto& req, auto& res) {
            auto id = number(req.matches[1].str());
            res.set_header("Content-Disposition", "attachment; filename=card-" + std::to_string(id) + ".xlsx");
            res.set_content(store.workbook(id), "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet");
        }));
        server.Get(R"(/api/cards/([0-9]+)/document)", safe([&](const auto& req, auto& res) {
            if(req.has_param("after"))send(res,store.document_changes(number(req.matches[1].str()),number(req.get_param_value("after"))));
            else send(res, store.document(number(req.matches[1].str())));
        }));
        server.Post(R"(/api/cards/([0-9]+)/save)",safe([&](const auto& req,auto& res){auto body=json::parse(req.body);send(res,store.queue_document(number(req.matches[1].str()),integer_field(body,"revision"),body.at("base_document"),body.at("document"),body.at("request_id").template get<std::string>()));}));
        server.Get(R"(/api/cards/([0-9]+)/history)",safe([&](const auto& req,auto& res){send(res,store.history(number(req.matches[1].str())));}));
        server.Get(R"(/api/cards/([0-9]+)/history/([0-9]+)/workbook)",safe([&](const auto& req,auto& res){auto id=number(req.matches[1].str()),revision=number(req.matches[2].str());res.set_header("Content-Disposition","attachment; filename=card-"+std::to_string(id)+"-v"+std::to_string(revision)+".xlsx");res.set_content(store.history_workbook(id,revision),"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet");}));
        server.Post(R"(/api/cards/([0-9]+)/restore)",safe([&](const auto& req,auto& res){auto body=json::parse(req.body);send(res,store.restore_document(number(req.matches[1].str()),integer_field(body,"revision"),body.value("document",json(nullptr))));}));
        server.Put(R"(/api/cards/([0-9]+)/document)", safe([&](const auto& req, auto& res) {
            auto body = json::parse(req.body);
            auto bytes = decode_workbook(body.at("workbook_base64").template get<std::string>());
            send(res, store.save_document(number(req.matches[1].str()), integer_field(body, "revision"), body.at("document"), bytes));
        }));
        server.Get("/", safe([&](const auto& req, auto& res) {
            auto platform = req.has_param("platform_type_id") ? number(req.get_param_value("platform_type_id")) : 0;
            auto after = req.has_param("after") ? number(req.get_param_value("after")) : 0;
            std::string html = "<h1>Карточки оборудования</h1><p class='note'>Регистрация партий и открытие редактора доступны в приложении Креатор. "
                               "Ссылки ниже открывают бланки для просмотра.</p>"
                               "<form method='get'><label>Тип платформы: <select name='platform_type_id'>"
                               "<option value='0'>Все типы</option>";
            for (const auto& p : store.platforms()) {
                auto id = p.at("id").template get<long long>();
                html += "<option value='" + std::to_string(id) + "'" + (id == platform ? " selected" : "") + ">" +
                        escape(p.at("name").template get<std::string>()) + "</option>";
            }
            html += "</select></label> <button>Показать</button></form><div class='scroll'><table>"
                    "<tr><th>Unique ID</th><th>Table ID</th><th>Тип платформы</th><th>1C Code</th><th>Бланк</th></tr>";
            auto rows = store.cards(platform, 100, after);
            for (const auto& c : rows) {
                auto id = std::to_string(c.at("id").template get<long long>());
                html += "<tr><td>" + id + "</td><td>" + escape(c.at("product_id").template get<std::string>()) +
                        "</td><td>" + escape(c.at("platform_name").template get<std::string>()) +
                        "</td><td>" + escape(c.at("one_c_code").template get<std::string>()) + "</td><td><a href='/cards/" + id + "'>Открыть</a></td></tr>";
            }
            html += "</table></div>";
            if (rows.empty()) html += "<p>Записей пока нет. Для регистрации партии запустите repair_creator.</p>";
            if (rows.size() == 100) html += "<p><a href='/?platform_type_id=" + std::to_string(platform) +
                "&amp;after=" + std::to_string(rows.back().at("id").template get<long long>()) + "'>Следующие записи</a></p>";
            res.set_content(page("Карточки оборудования", html), "text/html; charset=utf-8");
        }));
        auto show_document = [&](bool editable, const httplib::Request& req, httplib::Response& res) {
            auto id = number(req.matches[1].str());
            store.card(id);
            auto html = document_page;
            auto replace = [&](const std::string& from, const std::string& to) {
                size_t pos = 0;
                while ((pos = html.find(from, pos)) != std::string::npos) { html.replace(pos, from.size(), to); pos += to.size(); }
            };
            replace("{{CARD_ID}}", std::to_string(id));
            replace("{{MODE}}", editable ? "edit" : "read");
            replace("{{MODE_CLASS}}", editable ? "editor" : "reader");
            res.set_content(html, "text/html; charset=utf-8");
        };
        server.Get(R"(/cards/([0-9]+))", safe([&](const auto& req, auto& res) { show_document(false, req, res); }));
        server.Get(R"(/edit/([0-9]+))", safe([&](const auto& req, auto& res) { show_document(true, req, res); }));
        server.set_error_handler([](const auto&, auto& res) {
            if (res.body.empty()) send(res, {{"error", "Route not found"}}, res.status);
        });
        if(!server.bind_to_port(host,port))throw std::runtime_error("Cannot listen on configured host/port");
        std::cout << "Repair Cards 0.6.0\nDatabase: " << database.u8string()
                  << "\nListen: http://" << host << ':' << port << "\nStop: Ctrl+C\n" << std::flush;
        if(host=="0.0.0.0")std::cout<<"Local browser: http://127.0.0.1:"<<port<<"\nQR address is configured in Creator.\n"<<std::flush;
        if (!server.listen_after_bind()) throw std::runtime_error("Server stopped unexpectedly");
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Startup failed: " << e.what() << '\n';
        return 1;
    }
}
