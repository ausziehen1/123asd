#include "update.h"
#include "miniz.h"
#include "picosha2.h"
#include <fstream>
#include <set>
#include <map>
#include <charconv>
#include <array>
#include <chrono>
#include <random>
namespace repair::update {
namespace {
using Json=nlohmann::json;namespace fs=std::filesystem;
std::string read(const fs::path& p,size_t limit=128*1024*1024){std::ifstream in(p,std::ios::binary);if(!in)throw std::runtime_error("Не удалось открыть файл: "+p.u8string());auto size=fs::file_size(p);if(size>limit)throw std::runtime_error("Файл превышает допустимый размер.");std::string data(static_cast<size_t>(size),'\0');if(!in.read(data.data(),static_cast<std::streamsize>(data.size())))throw std::runtime_error("Ошибка чтения файла.");return data;}
void write(const fs::path& p,const std::string& data){fs::create_directories(p.parent_path());std::ofstream out(p,std::ios::binary);if(!out||!out.write(data.data(),static_cast<std::streamsize>(data.size())))throw std::runtime_error("Ошибка записи файла: "+p.u8string());out.flush();if(!out)throw std::runtime_error("Ошибка завершения записи.");}
void atomic(const fs::path& p,const std::string& data){auto tmp=p;tmp+=L".tmp";write(tmp,data);if(fs::exists(p))fs::remove(p);fs::rename(tmp,p);}
std::string hash(const std::string& data){return picosha2::hash256_hex_string(data);}
bool allowed(const std::string& p,bool internal=false){
 if(internal&&p=="installation.json")return true;
 if(p=="RepairClient.dll"||p=="WebView2Loader.dll"||p=="Creator.exe"||p=="Reader.exe"||p=="Repair.exe"||p=="theme.json")return true;
 if(p.rfind("ui/",0)!=0)return false;
 if(p.find("..")!=p.npos||p.find_first_of("\\:\r\n")!=p.npos||p.find('\0')!=p.npos||p.back()=='/')return false;
 auto ext=fs::path(p).extension().string();return ext==".css"||ext==".js"||ext==".json";
}
fs::path target(const fs::path& root,const std::string& name){if(!allowed(name,true))throw std::runtime_error("Недопустимый путь в обновлении: "+name);auto p=root;for(const auto& part:fs::u8path(name)){p/=part;if(fs::is_symlink(p))throw std::runtime_error("Обновление через символические ссылки не поддерживается.");}return p;}
std::array<int,3> version(const std::string& s){std::array<int,3> out{};size_t start=0;for(int n=0;n<3;n++){auto end=s.find('.',start);if(n<2&&end==s.npos)throw std::runtime_error("Неверная версия пакета.");if(n==2){if(end!=s.npos)throw std::runtime_error("Неверная версия пакета.");end=s.size();}auto r=std::from_chars(s.data()+start,s.data()+end,out[n]);if(r.ec!=std::errc{}||r.ptr!=s.data()+end||out[n]<0||out[n]>1000000)throw std::runtime_error("Неверная версия пакета.");start=end+1;}return out;}
Json installation(const fs::path& root){auto data=Json::parse(read(root/"installation.json",65536));if(data.value("product","")!="RepairCardsClients"||data.value("abi",0)!=1||data.value("architecture","")!="x64")throw std::runtime_error("Выберите папку установленного клиента Repair Cards x64.");version(data.at("version").get<std::string>());return data;}
struct Package{Json manifest;std::map<std::string,std::string> files;};
Package load(const fs::path& archive,const fs::path& root){
 auto current=installation(root);auto bytes=read(archive,64*1024*1024);mz_zip_archive zip{};if(!mz_zip_reader_init_mem(&zip,bytes.data(),bytes.size(),0))throw std::runtime_error("Поврежден ZIP обновления.");Package p;
 try{auto count=mz_zip_reader_get_num_files(&zip);if(count>512)throw std::runtime_error("Слишком много файлов в пакете.");size_t total=0;
  for(mz_uint n=0;n<count;n++){mz_zip_archive_file_stat info{};if(!mz_zip_reader_file_stat(&zip,n,&info))throw std::runtime_error("Поврежден ZIP.");if(info.m_is_directory)continue;std::string name=info.m_filename;
   if(name!="manifest.json"&&!allowed(name))throw std::runtime_error("Пакет содержит недопустимый файл: "+name);
   target(root,name=="manifest.json"?"installation.json":name);
   if(info.m_uncomp_size>64*1024*1024||total+info.m_uncomp_size>128*1024*1024)throw std::runtime_error("Превышен размер распакованного обновления.");total+=static_cast<size_t>(info.m_uncomp_size);
   size_t size=0;void* data=mz_zip_reader_extract_to_heap(&zip,n,&size,0);if(!data)throw std::runtime_error("Не удалось распаковать обновление.");auto ok=p.files.emplace(name,std::string(static_cast<char*>(data),size));mz_free(data);if(!ok.second)throw std::runtime_error("Повтор имени файла в пакете.");
  }mz_zip_reader_end(&zip);
 }catch(...){mz_zip_reader_end(&zip);throw;}
 if(!p.files.count("manifest.json"))throw std::runtime_error("В пакете нет manifest.json.");p.manifest=Json::parse(p.files.at("manifest.json"));p.files.erase("manifest.json");
 auto& m=p.manifest;if(m.value("product","")!="RepairCardsClients"||m.value("abi",0)!=1||m.value("architecture","")!="x64"||m.value("from_version","")!=current.at("version"))throw std::runtime_error("Патч предназначен для другой версии или архитектуры клиента.");
 if(version(m.at("version").get<std::string>())<=version(current.at("version").get<std::string>()))throw std::runtime_error("Патч должен повышать версию клиента.");
 if(!m.at("files").is_array()||m.at("files").empty()||m.at("files").size()!=p.files.size())throw std::runtime_error("Неверный список файлов обновления.");
 std::set<std::string> unique;for(const auto& f:m.at("files")){auto name=f.at("path").get<std::string>();if(!allowed(name)||!unique.insert(name).second||!p.files.count(name)||hash(p.files.at(name))!=f.at("sha256"))throw std::runtime_error("Контрольная сумма или имя файла обновления не совпадает.");}
 return p;
}
fs::path backup_path(const fs::path& root,const Json& pointer){auto name=pointer.at("backup").get<std::string>();if(name.rfind("backup-",0)!=0||name.find_first_not_of("0123456789-abcdefghijklmnopqrstuvwxyz")!=name.npos)throw std::runtime_error("Неверный журнал обновления.");auto p=root/".updates"/name;if(fs::is_symlink(root/".updates")||fs::is_symlink(p))throw std::runtime_error("Неверная папка восстановления.");return p;}
void restore(const fs::path& root,const Json& pointer){auto backup=backup_path(root,pointer);auto journal=Json::parse(read(backup/"journal.json",65536));
 for(const auto& f:journal.at("files")){auto name=f.at("path").get<std::string>();target(root,name);if(f.at("existed").get<bool>()){auto bytes=read(target(backup,name));if(hash(bytes)!=f.at("sha256"))throw std::runtime_error("Повреждена резервная копия обновления.");}}
 for(const auto& f:journal.at("files")){auto name=f.at("path").get<std::string>();auto dest=target(root,name);if(f.at("existed").get<bool>())atomic(dest,read(target(backup,name)));else if(fs::exists(dest))fs::remove(dest);}
}
}
Json inspect(const fs::path& archive,const fs::path& root){return load(archive,root).manifest;}
bool recover(const fs::path& root){auto pending=root/".updates/pending.json";if(!fs::exists(pending))return false;restore(root,Json::parse(read(pending,65536)));fs::remove(pending);return true;}
void rollback(const fs::path& root){recover(root);auto last=root/".updates/last.json";if(!fs::exists(last))throw std::runtime_error("Нет предыдущего обновления для отката.");auto pointer=Json::parse(read(last,65536));restore(root,pointer);fs::remove(last);}
void apply(const fs::path& archive,const fs::path& root){
 recover(root);auto package=load(archive,root);if(fs::is_symlink(root/".updates"))throw std::runtime_error("Недопустимая папка обновления.");
 auto suffix=std::to_string(std::chrono::system_clock::now().time_since_epoch().count())+"-"+std::to_string(std::random_device{}());auto backup=root/".updates"/("backup-"+suffix);fs::create_directories(backup);
 auto current=installation(root);current["version"]=package.manifest.at("version");package.files["installation.json"]=current.dump(2);
 Json journal={{"files",Json::array()}};
 for(const auto& file:package.files){auto dest=target(root,file.first);bool existed=fs::exists(dest);if(existed&&!fs::is_regular_file(dest))throw std::runtime_error("Путь обновляемого файла занят папкой.");Json entry={{"path",file.first},{"existed",existed}};if(existed){auto before=read(dest);write(target(backup,file.first),before);entry["sha256"]=hash(before);}journal["files"].push_back(entry);write(target(backup/"staged",file.first),file.second);}
 write(backup/"journal.json",journal.dump(2));Json pointer={{"backup",backup.filename().string()}};auto pending=root/".updates/pending.json";atomic(pending,pointer.dump());
 try{for(const auto& file:package.files)atomic(target(root,file.first),read(target(backup/"staged",file.first)));atomic(root/".updates/last.json",pointer.dump());fs::remove(pending);}
 catch(...){try{restore(root,pointer);fs::remove(pending);}catch(...){throw std::runtime_error("Обновление прервано. Файлы требуют восстановления: повторно запустите Updater.exe.");}throw;}
}
}
