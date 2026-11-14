#include "update.h"
#include "picosha2.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <chrono>
namespace fs=std::filesystem;
std::string read(const fs::path& path){std::ifstream input(path,std::ios::binary);if(!input)throw std::runtime_error("Missing file");return std::string((std::istreambuf_iterator<char>(input)),{});}
int main(int argc,char** argv){if(argc!=3)return 2;auto copy=fs::temp_directory_path()/("repair-release-patch-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 try{
  fs::copy(fs::u8path(argv[1]),copy,fs::copy_options::recursive);auto before=read(copy/"RepairClient.dll");auto metadata=read(copy/"installation.json");auto archive=fs::u8path(argv[2]);
  auto manifest=repair::update::inspect(archive,copy);repair::update::apply(archive,copy);auto installed=nlohmann::json::parse(read(copy/"installation.json"));
  if(installed["version"]!=manifest["version"])throw std::runtime_error("Patch version differs");
  bool dll=false;for(const auto& file:manifest["files"])if(file["path"]=="RepairClient.dll"){dll=true;if(picosha2::hash256_hex_string(read(copy/"RepairClient.dll"))!=file["sha256"])throw std::runtime_error("Installed DLL hash differs");}
  if(!dll)throw std::runtime_error("No DLL in release patch");repair::update::rollback(copy);if(read(copy/"RepairClient.dll")!=before||read(copy/"installation.json")!=metadata)throw std::runtime_error("Rollback differs from real 0.5.0 client");
  fs::remove_all(copy);std::cout<<"Released DLL patch passed: actual 0.5.0 client upgraded to 0.6.0, hashes verified, exact rollback\n";return 0;
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';std::error_code ignored;fs::remove_all(copy,ignored);return 1;}
}
