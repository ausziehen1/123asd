#include "labels.h"
#include "qrcodegen.hpp"
#include <algorithm>
#include <charconv>
#include <cctype>
#include <chrono>
#include <fstream>
#include <limits>
#include <random>
#include <regex>
#include <set>
#include <sstream>

namespace repair {
namespace {
std::string trim(std::string s) {
    auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    return s.substr(begin, s.find_last_not_of(" \t\r\n") - begin + 1);
}
long long positive(const std::string& s) {
    long long id = 0;
    auto r = std::from_chars(s.data(), s.data()+s.size(), id);
    if (r.ec != std::errc{} || r.ptr != s.data()+s.size() || id <= 0)
        throw ApiError("Некорректный внутренний номер карточки.");
    return id;
}
std::string html(const std::string& s) {
    std::string out;
    for (auto c:s) switch(c) {
        case '&':out+="&amp;";break;case '<':out+="&lt;";break;case '>':out+="&gt;";break;
        case '"':out+="&quot;";break;case '\'':out+="&#39;";break;default:out+=c;
    }
    return out;
}
void file(const std::filesystem::path& path, const std::string& data) {
    std::ofstream out(path,std::ios::binary);
    if (!out || !out.write(data.data(),static_cast<std::streamsize>(data.size())))
        throw ApiError("Не удалось записать файл задания: "+path.u8string());
}
}
std::string normalize_public_url(const std::string& input) {
    if(trim(input).empty())throw ApiError("Выберите или введите сетевой адрес ПК с сервером в поле «Сетевой адрес в QR», например http://192.168.1.50:8080.");
    auto base = normalize_server_url(trim(input));
    auto host = base.substr(7);
    std::transform(host.begin(),host.end(),host.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    if (host == "localhost" || host.rfind("localhost:",0)==0 || host.rfind("127.",0)==0 ||
        host == "0.0.0.0" || host.rfind("0.0.0.0:",0)==0 || host.rfind("[::1]",0)==0 || host.rfind("[::]",0)==0)
        throw ApiError("Для QR нужен сетевой адрес ПК с сервером, например http://192.168.1.50:8080. localhost и 127.0.0.1 на телефоне не ведут к вашему ПК.");
    return base;
}
std::string card_url(const std::string& base, long long id) {
    if (id <= 0) throw ApiError("Некорректный внутренний номер карточки.");
    return normalize_server_url(base)+"/cards/"+std::to_string(id);
}
Scan parse_scan(std::string input, const std::string& default_base) {
    if(input.size()>2048)throw ApiError("Содержимое QR слишком длинное.");
    input=trim(input);
    if (input.size()>3 && input[0]==']' && input[1]=='Q' && input[2]>='0' && input[2]<='9') input.erase(0,3);
    if (input.empty()) throw ApiError("Сканируйте QR или введите внутренний номер карточки.");
    if (input.find_first_not_of("0123456789")==std::string::npos) {
        auto base=normalize_server_url(default_base);auto id=positive(input);return{base,id,card_url(base,id)};
    }
    static const std::regex pattern(R"(^(http://(?:[A-Za-z0-9._-]+|\[[0-9A-Fa-f:]+\])(?::[0-9]{1,5})?)/cards/([1-9][0-9]*)/?$)");
    std::smatch match;
    if (!std::regex_match(input,match,pattern))
        throw ApiError("Это не QR-ссылка просмотра карточки. Нужна ссылка http://сервер:порт/cards/номер. ID продукции без QR может повторяться.");
    auto base=normalize_server_url(match[1].str());auto id=positive(match[2].str());
    return{base,id,card_url(base,id)};
}
std::vector<Label> make_labels(const Json& cards,const std::string& public_url) {
    auto base=normalize_public_url(public_url);
    if (!cards.is_array() || cards.empty() || cards.size()>1000) throw ApiError("Выберите от 1 до 1000 карточек.");
    std::set<long long> seen;std::vector<Label> result;
    for (const auto& card:cards) {
        if (!card.is_object() || !card.at("id").is_number_integer() || card.at("id")<=0 ||
            card.at("id")>std::numeric_limits<long long>::max()) throw ApiError("Некорректный внутренний номер.");
        auto id=card.at("id").get<long long>();
        if (!seen.insert(id).second) throw ApiError("В задании одна карточка выбрана дважды.");
        result.push_back({id,card.at("product_id").get<std::string>(),card.at("platform_name").get<std::string>(),card_url(base,id)});
    }
    return result;
}
std::string qr_svg(const std::string& text) {
    const auto qr=qrcodegen::QrCode::encodeText(text.c_str(),qrcodegen::QrCode::Ecc::MEDIUM);
    const int border=4, size=qr.getSize()+border*2;
    std::ostringstream out;
    out<<"<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 "<<size<<' '<<size<<"\" shape-rendering=\"crispEdges\"><rect width=\"100%\" height=\"100%\" fill=\"white\"/><path d=\"";
    for(int y=0;y<qr.getSize();y++)for(int x=0;x<qr.getSize();x++)if(qr.getModule(x,y))
        out<<'M'<<x+border<<','<<y+border<<"h1v1h-1z ";
    out<<"\" fill=\"black\"/></svg>";return out.str();
}
std::string labels_csv(const std::vector<Label>& labels) {
    auto quote=[](const std::string& value){std::string out="\"";for(char c:value){out+=c;if(c=='"')out+='"';}return out+'"';};
    std::string out="\xEF\xBB\xBF\"InternalID\",\"ProductID\",\"Platform\",\"QRUrl\"\r\n";
    for(const auto& label:labels)out+=quote(std::to_string(label.id))+","+quote(label.product_id)+","+quote(label.platform)+","+quote(label.url)+"\r\n";
    return out;
}
std::filesystem::path write_label_job(const std::filesystem::path& parent,const std::vector<Label>& labels) {
    if(labels.empty())throw ApiError("Не выбраны карточки.");
    std::filesystem::create_directories(parent);
    auto directory=parent/("job-"+std::to_string(std::chrono::system_clock::now().time_since_epoch().count())+"-"+std::to_string(std::random_device{}()));
    if(!std::filesystem::create_directory(directory))throw ApiError("Не удалось создать папку задания.");
    std::string page="<!doctype html><html lang='ru'><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>QR карточек</title><style>body{font:16px Arial;margin:24px}section{display:inline-block;width:280px;padding:16px;border:1px solid #bbb;margin:8px;vertical-align:top;overflow-wrap:anywhere}img{width:220px;height:220px}small{font-size:12px}</style><h1>QR карточек</h1><p>Это проверка ссылок. Точный макет и расположение для печати открываются в BarTender.</p>";
    Json manifest=Json::array();
    for(size_t i=0;i<labels.size();i++) {
        const auto& label=labels[i];auto name="qr-"+std::to_string(i+1)+".svg";
        file(directory/name,qr_svg(label.url));
        page+="<section><img src='"+name+"' alt='QR'><p><b>"+html(label.product_id)+"</b><br>"+html(label.platform)+"</p><small>Внутренний ID: "+std::to_string(label.id)+"<br><a href='"+html(label.url)+"'>"+html(label.url)+"</a></small></section>";
        manifest.push_back({{"internal_id",label.id},{"product_id",label.product_id},{"platform",label.platform},{"qr_url",label.url}});
    }
    file(directory/"labels.csv",labels_csv(labels));file(directory/"preview.html",page+"</html>");file(directory/"manifest.json",manifest.dump(2));
    return directory;
}
std::vector<std::string> bartender_arguments(const std::string& btw,const std::string& csv,bool print_dialog) {
    if(btw.empty() || (print_dialog&&csv.empty()))throw ApiError("Укажите шаблон BarTender и файл задания.");
    std::vector<std::string> args={"/MAX","/AF="+btw};
    if(!csv.empty())args.insert(args.end(),{"/D="+csv,"/DbTextHeader=1","/C=1","/S=1"});
    if(print_dialog)args.push_back("/FPD");
    return args;
}
std::wstring quote_windows_argument(const std::wstring& s) {
    std::wstring result=L"\"";size_t slashes=0;
    for(wchar_t c:s){if(c==L'\\'){slashes++;continue;}if(c==L'"'){result.append(slashes*2+1,L'\\');result+=c;}else{result.append(slashes,L'\\');result+=c;}slashes=0;}
    result.append(slashes*2,L'\\');return result+L'"';
}
std::wstring bartender_command_line(const std::wstring& executable,const std::vector<std::wstring>& arguments) {
    auto valid=[](const std::wstring& s){
        return !s.empty()&&s.find_first_of(L"\"\r\n") == std::wstring::npos&&s.find(L'\0')==std::wstring::npos;
    };
    if(!valid(executable))throw ApiError("Некорректный путь BarTender.");
    std::wstring command=quote_windows_argument(executable);
    for(const auto& arg:arguments){
        // BarTender parses the raw command line itself. Keep the slash and
        // parameter name outside quotes: /AF="path", never "/AF=path".
        if(arg.rfind(L"/AF=",0)==0||arg.rfind(L"/D=",0)==0){
            auto equal=arg.find(L'=');auto value=arg.substr(equal+1);
            if(!valid(value))throw ApiError("Некорректный путь шаблона или задания BarTender.");
            command+=L" "+arg.substr(0,equal+1)+quote_windows_argument(value);
        }else if(arg==L"/MAX"||arg==L"/FPD"||arg==L"/DbTextHeader=1"||arg==L"/C=1"||arg==L"/S=1"){
            command+=L" "+arg;
        }else throw ApiError("Неподдерживаемый параметр BarTender.");
    }
    return command;
}
}
