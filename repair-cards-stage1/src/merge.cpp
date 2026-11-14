#include "merge.h"
#include "document.h"
#include <set>
namespace {
using Json = nlohmann::json;
Json merge(const Json& base,const Json& current,const Json& local) {
    if (local == base) return current;
    if (current == base || !base.is_object() || !current.is_object() || !local.is_object()) return local;
    Json result = current;
    std::set<std::string> keys;for(const auto& v:base.items())keys.insert(v.key());for(const auto& v:local.items())keys.insert(v.key());
    for(const auto& key:keys){
        if(!local.contains(key)){if(base.contains(key))result.erase(key);continue;}
        if(base.contains(key)&&local[key]==base[key])continue;
        if(!current.contains(key)){result[key]=local[key];continue;}
        result[key]=merge(base.value(key,Json::object()),current[key],local[key]);
    }
    return result;
}
Json expanded(Json sheet) {
    auto styles=sheet.value("styles",Json::array());sheet.erase("styles");
    for(auto& row:sheet["rows"].items())if(row.value().is_object()&&row.value().contains("cells"))for(auto& cell:row.value()["cells"].items()){
        if(cell.value().contains("style")){auto n=cell.value()["style"].get<size_t>();cell.value()["_expandedStyle"]=styles.at(n);cell.value().erase("style");}
    }
    return sheet;
}
Json compact(Json sheet) {
    Json styles=Json::array();
    for(auto& row:sheet["rows"].items())if(row.value().is_object()&&row.value().contains("cells"))for(auto& cell:row.value()["cells"].items()){
        if(cell.value().contains("_expandedStyle")){
            auto style=cell.value()["_expandedStyle"];size_t i=0;while(i<styles.size()&&styles[i]!=style)++i;
            if(i==styles.size())styles.push_back(style);cell.value()["style"]=i;cell.value().erase("_expandedStyle");
        }
    }
    sheet["styles"]=styles;return sheet;
}
bool structure_changed(const Json& a,const Json& b){return a.at("rows").at("len")!=b.at("rows").at("len")||a.at("cols").at("len")!=b.at("cols").at("len")||a.value("merges",Json::array())!=b.value("merges",Json::array());}
}
nlohmann::json merge_documents(const Json& base,const Json& current,const Json& incoming){
    validate_document(base);validate_document(current);validate_document(incoming);
    if(incoming==base)return current;
    if(current==base)return incoming;
    if(base["sheets"].size()!=current["sheets"].size()||base["sheets"].size()!=incoming["sheets"].size())return incoming;
    Json result=incoming;
    for(size_t i=0;i<base["sheets"].size();i++){
        auto b=base["sheets"][i],c=current["sheets"][i],l=incoming["sheets"][i];
        if(l==b){result["sheets"][i]=c;continue;}
        // Changing sheet order, row/column structure or merged regions is a
        // whole-sheet operation. The later queued operation wins that sheet.
        if(b["name"]!=c["name"]||b["name"]!=l["name"]||structure_changed(b,c)||structure_changed(b,l)){result["sheets"][i]=l;continue;}
        result["sheets"][i]=compact(merge(expanded(b),expanded(c),expanded(l)));
    }
    validate_document(result);return result;
}
