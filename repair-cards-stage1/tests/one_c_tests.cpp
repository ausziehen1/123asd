#include "store.h"
#include "one_c.h"
#include "sequence.h"
#include <filesystem>
#include <fstream>
#include <chrono>
#include <iostream>
void require(bool v,const char* m){if(!v)throw std::runtime_error(m);}
template<class E,class F>void raises(F f){try{f();}catch(const E&){return;}throw std::runtime_error("Expected failure");}
std::string read(const std::filesystem::path& p){std::ifstream in(p,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
int main(int argc,char** argv){auto root=std::filesystem::temp_directory_path()/("repair-onec-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));try{
 require(argc==2,"Template argument");std::filesystem::create_directories(root);auto original=read(argv[1]);require(workbook_one_c(original)=="","Original blank 1C field");
 require(workbook_one_c(set_workbook_one_c(original,"__"))=="__","Literal underscore code differs from template placeholder");
 auto coded=set_workbook_one_c(original,"00123");require(workbook_one_c(coded)=="00123","Leading zeros preserved in XLSX");require(workbook_one_c(set_workbook_one_c(coded,""))=="","Clearing code in XLSX");
 auto doc=nlohmann::json::parse(R"({"format":"x-data-spreadsheet","version":1,"sheets":[{"name":"Card","rows":{"len":50,"14":{"cells":{"12":{"text":"Код 1С: 00123"}}}},"cols":{"len":20}}]})");
 require(document_one_c(doc)=="00123"&&doc["sheets"][0]["rows"]["14"]["cells"]["12"]["repairField"]=="one_c_code","Semantic binding independent of original coordinate");
 doc["sheets"][0]["rows"]["14"]["cells"]["12"]["text"]="007";require(document_one_c(doc)=="007","Binding survives label text edit");
 Store store((root/"cards.db").u8string());auto platform=store.create_platform("Test")["id"].get<long long>();store.create_batch(platform,"V00001",10,original);
 nlohmann::json ids=nlohmann::json::array();for(int n=1;n<=10;n++)ids.push_back({{"id",n},{"revision",0}});
 auto groups=nlohmann::json::parse(R"([{"count":2,"code":"123"},{"count":7,"code":"1234"},{"count":1,"code":"12345"}])");
 auto assigned=store.assign_codes(ids,groups);for(int n=1;n<=10;n++){auto expected=n<=2?"123":n<=9?"1234":"12345";require(store.card(n)["one_c_code"]==expected,"2/7/1 SQLite mapping");require(workbook_one_c(store.workbook(n))==expected,"2/7/1 workbook mapping");require(assigned[n-1]["document_revision"]==1,"Revision advanced for assigned code");}
 raises<RevisionConflict>([&]{store.save_document(1,0,doc,coded);});
 auto fresh=nlohmann::json::array({{{"id",1},{"revision",1}},{{"id",2},{"revision",0}}});raises<RevisionConflict>([&]{store.assign_codes(fresh,nlohmann::json::array({{{"count",2},{"code","changed"}}}));});require(store.card(1)["one_c_code"]=="123","Conflict rolls back earlier assignment");
 auto missing=nlohmann::json::array({{{"id",1},{"revision",1}},{{"id",999},{"revision",0}}});raises<NotFound>([&]{store.assign_codes(missing,nlohmann::json::array({{{"count",2},{"code","changed"}}}));});require(store.card(1)["one_c_code"]=="123","Missing card rolls back batch");
 auto first=nlohmann::json::array({{{"id",1},{"revision",1}}});store.assign_codes(first,nlohmann::json::array({{{"count",1},{"code",""}}}));require(store.card(1)["one_c_code"]==""&&workbook_one_c(store.workbook(1))=="","Blank code is explicit valid assignment");
 auto bad=groups;bad[0]["count"]=1;raises<InputError>([&]{store.assign_codes(ids,bad);});bad=groups;bad[0]["count"]=0;raises<InputError>([&]{store.assign_codes(ids,bad);});
 auto current=store.document(2)["revision"].get<long long>();store.save_document(2,current,doc,coded);require(store.card(2)["one_c_code"]=="007"&&workbook_one_c(store.workbook(2))=="007","Editor save synchronizes SQLite and XLSX");
 std::cout<<"1C tests passed: XLSX, semantic binding, 2/7/1 allocation, revision conflicts, rollback, empty codes and editor save\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}std::error_code error;std::filesystem::remove_all(root,error);return 0;}
