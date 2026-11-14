#include "portable_package.h"
#include "json.hpp"
#include "miniz.h"
#include <filesystem>
#include <fstream>
#include <map>
#include <iostream>
#include <chrono>
namespace fs = std::filesystem;
using Json = nlohmann::json;
void require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
std::string payload(std::map<std::string,std::string> files,bool corrupt=false){
 Json manifest={{"product","RepairCardsPortable"},{"role","Reader"},{"abi",1},{"architecture","x64"},{"files",Json::array()}};
 for(const auto& f:files)manifest["files"].push_back({{"path",f.first},{"sha256",corrupt?std::string(64,'0'):repair::portable::identity(f.second)}});
 files["payload.json"]=manifest.dump();mz_zip_archive zip{};require(mz_zip_writer_init_heap(&zip,0,0),"ZIP init");
 for(const auto& f:files)require(mz_zip_writer_add_mem(&zip,f.first.c_str(),f.second.data(),f.second.size(),MZ_BEST_SPEED),"ZIP file");
 void* data=nullptr;size_t n=0;require(mz_zip_writer_finalize_heap_archive(&zip,&data,&n),"ZIP finish");std::string out(static_cast<char*>(data),n);mz_free(data);mz_zip_writer_end(&zip);return out;
}
int main(){auto root=fs::temp_directory_path()/("repair-portable-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 try{
  std::map<std::string,std::string> files={{"Reader.exe","launcher"},{"RepairClient.dll","original"},{"WebView2Loader.dll","loader"},{"Updater.exe","updater"},{"installation.json",R"({"version":"0.6.0"})"},{"docs/guide.md","guide"}};
  auto bytes=payload(files);auto cache=root/"cache";repair::portable::install(bytes,cache,"Reader");require(fs::exists(cache/"Reader.exe"),"First extraction");
  {std::ofstream out(cache/"RepairClient.dll");out<<"patched";}
  repair::portable::install(bytes,cache,"Reader");std::ifstream in(cache/"RepairClient.dll");std::string changed;in>>changed;in.close();require(changed=="patched","Relaunch must preserve a DLL patch");
  auto rejected=[&](const std::string& data,const fs::path& path){bool failed=false;try{repair::portable::install(data,path,"Reader");}catch(const std::exception&){failed=true;}require(failed,"Invalid payload accepted");};
  rejected(payload(files,true),root/"bad-hash");require(!fs::exists(root/"bad-hash"),"Bad hash published");
  files["../outside.txt"]="escape";rejected(payload(files),root/"bad-path");require(!fs::exists(root/"outside.txt"),"Path escaped");files.erase("../outside.txt");
  rejected("broken",root/"broken");rejected(payload(files,true),cache);
  fs::remove(cache/"RepairClient.dll");rejected(bytes,cache);require(!fs::exists(cache/"RepairClient.dll"),"Broken patched cache silently overwritten");
  fs::remove_all(root);std::cout<<"Portable tests passed: first extraction, relaunch, preserved DLL patch, hashes, traversal, damaged cache\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';std::error_code ignored;fs::remove_all(root,ignored);return 1;}
}
