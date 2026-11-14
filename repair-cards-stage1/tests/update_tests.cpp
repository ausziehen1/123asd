#include "update.h"
#include "picosha2.h"
#include "miniz.h"
#include <fstream>
#include <iostream>
#include <chrono>
#include <map>
using Json=nlohmann::json;namespace fs=std::filesystem;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void write(const fs::path& p,const std::string& data){fs::create_directories(p.parent_path());std::ofstream out(p,std::ios::binary);out<<data;}
std::string read(const fs::path& p){std::ifstream in(p,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
template<class F>void rejected(F fn){try{fn();}catch(const std::exception&){return;}throw std::runtime_error("Invalid patch was accepted");}
void package(const fs::path& path,const std::map<std::string,std::string>& files,bool bad=false){Json m={{"product","RepairCardsClients"},{"abi",1},{"architecture","x64"},{"from_version","0.5.0"},{"version","0.5.1"},{"files",Json::array()}};for(const auto& p:files)m["files"].push_back({{"path",p.first},{"sha256",bad?std::string(64,'0'):picosha2::hash256_hex_string(p.second)}});mz_zip_archive z{};require(mz_zip_writer_init_heap(&z,0,0),"ZIP init");for(const auto& p:files)require(mz_zip_writer_add_mem(&z,p.first.c_str(),p.second.data(),p.second.size(),MZ_BEST_SPEED),"ZIP file");auto text=m.dump();require(mz_zip_writer_add_mem(&z,"manifest.json",text.data(),text.size(),MZ_BEST_SPEED),"ZIP manifest");void* ptr=nullptr;size_t size=0;require(mz_zip_writer_finalize_heap_archive(&z,&ptr,&size),"ZIP finish");write(path,std::string(static_cast<char*>(ptr),size));mz_free(ptr);mz_zip_writer_end(&z);}
int main(){auto root=fs::temp_directory_path()/("repair-update-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));try{
 auto install=root/"client";write(install/"installation.json",R"({"product":"RepairCardsClients","version":"0.5.0","abi":1,"architecture":"x64"})");write(install/"RepairClient.dll","old");write(install/"data/user.db","user data");write(install/"labels/RepairCards.btw","user template");
 auto patch=root/"patch.zip";package(patch,{{"RepairClient.dll","new"},{"ui/test.css","new style"}});
 require(repair::update::inspect(patch,install)["version"]=="0.5.1","Inspect local patch");repair::update::apply(patch,install);
 require(read(install/"RepairClient.dll")=="new"&&read(install/"ui/test.css")=="new style","Patched DLL and new UI file");require(Json::parse(read(install/"installation.json"))["version"]=="0.5.1","Version updated");
 require(read(install/"data/user.db")=="user data"&&read(install/"labels/RepairCards.btw")=="user template","Working data untouched");rejected([&]{repair::update::apply(patch,install);});
 repair::update::rollback(install);require(read(install/"RepairClient.dll")=="old"&&!fs::exists(install/"ui/test.css"),"Rollback restores old files and removes new ones");require(Json::parse(read(install/"installation.json"))["version"]=="0.5.0","Rollback version");
 package(root/"bad.zip",{{"RepairClient.dll","bad"}},true);rejected([&]{repair::update::apply(root/"bad.zip",install);});require(read(install/"RepairClient.dll")=="old","Bad hash changes no file");
 package(root/"traversal.zip",{{"../escaped","bad"}});rejected([&]{repair::update::apply(root/"traversal.zip",install);});require(!fs::exists(root/"escaped"),"No traversal");
 package(root/"data.zip",{{"data/user.db","bad"}});rejected([&]{repair::update::apply(root/"data.zip",install);});
 repair::update::apply(patch,install);auto pointer=read(install/".updates/last.json");write(install/"RepairClient.dll","interrupted");write(install/".updates/pending.json",pointer);require(repair::update::recover(install),"Interrupted update detected");require(read(install/"RepairClient.dll")=="old"&&!fs::exists(install/".updates/pending.json"),"Interrupted update recovered");
 std::cout<<"Update tests passed: hashes, versions, DLL/UI update, data preservation, rollback and interruption recovery\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}std::error_code ec;fs::remove_all(root,ec);return 0;}
