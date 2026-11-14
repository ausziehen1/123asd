#include "store.h"
#include "merge.h"
#include <filesystem>
#include <chrono>
#include <fstream>
#include <iostream>
void require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
int main(int argc,char** argv){auto root=std::filesystem::temp_directory_path()/("repair-queue-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 try{
  require(argc==2,"Template path required");std::ifstream input(argv[1],std::ios::binary);std::string book((std::istreambuf_iterator<char>(input)),{});std::filesystem::create_directories(root);
  auto base=json::parse(R"({"format":"x-data-spreadsheet","version":1,"sheets":[{"name":"Card","rows":{"len":40,"0":{"cells":{"0":{"text":"initial"}}}},"cols":{"len":16},"styles":[],"merges":[]}]})");
  auto a=base,b=base;a["sheets"][0]["styles"]={{{"font",{{"bold",true}}},{"color","#112233"}}};a["sheets"][0]["rows"]["0"]["cells"]["1"]={{"text","first"},{"style",0}};
  b["sheets"][0]["styles"]={{{"font",{{"italic",true}}},{"bgcolor","#aabbcc"}}};b["sheets"][0]["rows"]["0"]["cells"]["2"]={{"text","second"},{"style",0}};
  auto merged=merge_documents(base,a,b);auto& sheet=merged["sheets"][0];auto& cells=sheet["rows"]["0"]["cells"];
  require(cells["1"]["text"]=="first"&&cells["2"]["text"]=="second","Independent cells lost");
  require(sheet["styles"][cells["1"]["style"].get<int>()]["font"]["bold"]==true&&sheet["styles"][cells["2"]["style"].get<int>()]["font"]["italic"]==true,"Colliding style indexes changed formatting");
  auto c=base;c["sheets"][0]["rows"]["0"]["cells"]["0"]["text"]="last";a["sheets"][0]["rows"]["0"]["cells"]["0"]["text"]="earlier";require(merge_documents(base,a,c)["sheets"][0]["rows"]["0"]["cells"]["0"]["text"]=="last","Last cell edit must win");
  Store store((root/"test.sqlite3").u8string());auto platform=store.create_platform("Queue")["id"].get<long long>();auto id=store.create_batch(platform,"V00001",1,book)["cards"][0]["id"].get<long long>();
  auto first=store.queue_document(id,0,base,a,"first");auto second=store.queue_document(id,0,base,b,"second");require(first["revision"]==1&&second["revision"]==2,"Queued revisions");
  require(second["document"]["sheets"][0]["rows"]["0"]["cells"]["1"]["text"]=="first","Stale snapshot overwrote another cell");
  require(store.queue_document(id,0,base,a,"first")["revision"]==1&&store.document(id)["revision"]==2,"Idempotent save was applied twice");
  require(store.history(id).size()==3,"Every version must remain in history");require(store.history_workbook(id,0)==book,"Original workbook history");
  store.restore_document(id,1);require(store.document(id)["revision"]==3&&store.document(id)["document"]==first["document"],"Restoration must create a new version");
  require(store.document_changes(id,3)["changed"]==false&&store.document_changes(id,2)["changed"]==true,"Live revision checks");
  require(store.workbook(id).substr(0,2)=="PK","Canonical XLSX missing");
  std::cout<<"Queue tests passed: cell merge, last edit wins, independent styles, idempotency, history, restore and live revisions\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';std::error_code ignored;std::filesystem::remove_all(root,ignored);return 1;}
 std::filesystem::remove_all(root);return 0;
}
