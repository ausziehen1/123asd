#include "one_c.h"
#include "sequence.h"
#include "miniz.h"
#include "pugixml.hpp"
#include <algorithm>
#include <cstdlib>
#include <map>
#include <sstream>
#include <vector>
namespace {
using Json=nlohmann::json;
std::string trim(std::string s){auto p=s.find_first_not_of(" \t\r\n");return p==s.npos?std::string():s.substr(p,s.find_last_not_of(" \t\r\n")-p+1);}
std::optional<std::string> label(const std::string& text,bool placeholder=true){
 auto s=trim(text);for(auto prefix:{std::string("Код 1С"),std::string("Код 1C")})if(s.rfind(prefix,0)==0){s=trim(s.substr(prefix.size()));if(!s.empty()&&s[0]==':')s=trim(s.substr(1));if(placeholder&&s.find_first_not_of("_ \t")==s.npos)s.clear();return validate_one_c(s);}return{};
}
struct Zip {
 mz_zip_archive zip{};std::map<std::string,std::string> files;
 explicit Zip(const std::string& bytes){
  if(!mz_zip_reader_init_mem(&zip,bytes.data(),bytes.size(),0))throw InputError("Не удалось прочитать XLSX.");
  try{size_t total=0;auto count=mz_zip_reader_get_num_files(&zip);if(count>4096)throw InputError("Too many XLSX entries");
   for(mz_uint i=0;i<count;i++){mz_zip_archive_file_stat s{};if(!mz_zip_reader_file_stat(&zip,i,&s))throw InputError("Invalid XLSX entry");if(s.m_is_directory)continue;
    if(s.m_uncomp_size>32*1024*1024||total+s.m_uncomp_size>64*1024*1024)throw InputError("XLSX expands beyond limit");total+=static_cast<size_t>(s.m_uncomp_size);
    size_t size=0;void* p=mz_zip_reader_extract_to_heap(&zip,i,&size,0);if(!p)throw InputError("Invalid XLSX compression");
    auto inserted=files.emplace(s.m_filename,std::string(static_cast<char*>(p),size));mz_free(p);if(!inserted.second)throw InputError("Duplicate XLSX entry");
   }
  }catch(...){mz_zip_reader_end(&zip);throw;}mz_zip_reader_end(&zip);
 }
 std::string write(){mz_zip_archive out{};if(!mz_zip_writer_init_heap(&out,0,0))throw InputError("XLSX writer init failed");
  try{for(const auto& p:files)if(!mz_zip_writer_add_mem(&out,p.first.c_str(),p.second.data(),p.second.size(),MZ_BEST_SPEED))throw InputError("XLSX write failed");
   void* p=nullptr;size_t n=0;if(!mz_zip_writer_finalize_heap_archive(&out,&p,&n))throw InputError("XLSX finalize failed");std::string result(static_cast<char*>(p),n);mz_free(p);mz_zip_writer_end(&out);return result;
  }catch(...){mz_zip_writer_end(&out);throw;}
 }
};
void parse(pugi::xml_document& xml,const std::string& text){if(!xml.load_buffer(text.data(),text.size()))throw InputError("Invalid XLSX XML");}
std::string xml_string(const pugi::xml_document& xml){std::ostringstream out;xml.save(out,"",pugi::format_raw,pugi::encoding_utf8);return out.str();}
std::string cell_text(pugi::xml_node c,const std::vector<std::string>& strings){auto type=std::string(c.attribute("t").value());if(type=="s"){auto index=c.child("v").text().as_int(-1);return index>=0&&static_cast<size_t>(index)<strings.size()?strings[index]:std::string();}if(type=="inlineStr"){std::string text;for(auto t:c.select_nodes(".//t"))text+=t.node().text().get();return text;}return c.child("v").text().get();}
struct Field {std::string file,ref,text,sheet;bool named=false;};
std::vector<Field> fields(Zip& zip){
 std::vector<std::string> strings;
 if(zip.files.count("xl/sharedStrings.xml")){pugi::xml_document xml;parse(xml,zip.files.at("xl/sharedStrings.xml"));for(auto si:xml.child("sst").children("si")){std::string text;for(auto t:si.select_nodes(".//t"))text+=t.node().text().get();strings.push_back(text);}}
 pugi::xml_document book,rels;parse(book,zip.files.at("xl/workbook.xml"));parse(rels,zip.files.at("xl/_rels/workbook.xml.rels"));
 std::map<std::string,std::string> targets;for(auto r:rels.child("Relationships").children("Relationship")){auto path=std::string(r.attribute("Target").value());if(path.rfind("/",0)==0)path.erase(0,1);else if(path.rfind("xl/",0)!=0)path="xl/"+path;targets[r.attribute("Id").value()]=path;}
 std::map<std::string,std::string> named;
 for(auto n:book.child("workbook").child("definedNames").children("definedName"))if(std::string(n.attribute("name").value())=="RepairCards_OneCCode"){
  std::string name=n.text().get();auto p=name.rfind('!');if(p!=name.npos){auto sheet=name.substr(0,p);if(sheet.size()>1&&sheet.front()=='\''&&sheet.back()=='\''){sheet=sheet.substr(1,sheet.size()-2);size_t at=0;while((at=sheet.find("''",at))!=sheet.npos)sheet.replace(at,2,"'");}auto ref=name.substr(p+1);ref.erase(std::remove(ref.begin(),ref.end(),'$'),ref.end());named[sheet]=ref;}}
 std::vector<Field> result;
 for(auto sheet:book.child("workbook").child("sheets").children("sheet")){
  auto it=targets.find(sheet.attribute("r:id").value());if(it==targets.end()||!zip.files.count(it->second))continue;
  pugi::xml_document xml;parse(xml,zip.files.at(it->second));std::string name=sheet.attribute("name").value();
  for(auto c:xml.select_nodes("/worksheet/sheetData/row/c")){auto cell=c.node();auto text=cell_text(cell,strings);auto ref=std::string(cell.attribute("r").value());if((!named.empty()?(named.count(name)&&named[name]==ref):bool(label(text))))result.push_back({it->second,ref,text,name,!named.empty()});}
 }return result;
}
}
std::string validate_one_c(std::string code){code=trim(code);if(code.size()>128||std::any_of(code.begin(),code.end(),[](unsigned char c){return c<32||c==127;}))throw InputError("Код 1С должен быть строкой до 128 байт без управляющих символов.");return code;}
std::optional<std::string> document_one_c(Json& document){
 std::optional<std::string> code;bool hasMarker=false;
 for(auto& sheet:document.at("sheets"))for(auto& row:sheet.at("rows").items())if(row.value().is_object()&&row.value().contains("cells"))for(auto& entry:row.value()["cells"].items())if(entry.value().value("repairField",std::string())=="one_c_code")hasMarker=true;
 for(auto& sheet:document.at("sheets"))for(auto& row:sheet.at("rows").items())if(row.value().is_object()&&row.value().contains("cells"))for(auto& entry:row.value()["cells"].items()){
  auto& cell=entry.value();auto text=cell.value("text",Json(""));if(!text.is_string())continue;auto value=label(text.get<std::string>(),!hasMarker);
  if(cell.value("repairField",std::string())=="one_c_code"||(!hasMarker&&value)){auto next=value?*value:validate_one_c(text.get<std::string>());if(code&&*code!=next)throw InputError("В бланке найдены разные значения поля «Код 1С».");code=next;cell["repairField"]="one_c_code";}
 }return code;
}
bool set_document_one_c(Json& document,const std::string& code){document_one_c(document);bool found=false;for(auto& sheet:document.at("sheets"))for(auto& row:sheet.at("rows").items())if(row.value().is_object()&&row.value().contains("cells"))for(auto& entry:row.value()["cells"].items()){auto& c=entry.value();if(c.value("repairField",std::string())=="one_c_code"){c["text"]="Код 1С: "+code;found=true;}}return found;}
std::optional<std::string> workbook_one_c(const std::string& workbook){Zip zip(workbook);auto found=fields(zip);std::optional<std::string> code;for(const auto& f:found){auto value=label(f.text,!f.named);auto next=value?*value:validate_one_c(f.text);if(code&&*code!=next)throw InputError("Разные значения «Код 1С» в XLSX.");code=next;}return code;}
std::string set_workbook_one_c(const std::string& workbook,const std::string& code){
 auto valid=validate_one_c(code);Zip zip(workbook);auto found=fields(zip);if(found.empty())throw InputError("В XLSX не найдено поле «Код 1С». Восстановите его в бланке перед назначением кода.");
 for(const auto& f:found){pugi::xml_document xml;parse(xml,zip.files.at(f.file));for(auto c:xml.select_nodes("/worksheet/sheetData/row/c"))if(std::string(c.node().attribute("r").value())==f.ref){auto cell=c.node();cell.remove_children();cell.remove_attribute("t");cell.append_attribute("t")="inlineStr";auto t=cell.append_child("is").append_child("t");t.append_attribute("xml:space")="preserve";t.text().set(("Код 1С: "+valid).c_str());}zip.files[f.file]=xml_string(xml);}
 pugi::xml_document book;parse(book,zip.files.at("xl/workbook.xml"));auto names=book.child("workbook").child("definedNames");if(!names){auto workbookNode=book.child("workbook");pugi::xml_node before;for(auto n:workbookNode.children()){std::string name=n.name();if(name=="calcPr"||name=="oleSize"||name=="customWorkbookViews"||name=="pivotCaches"||name=="webPublishing"||name=="extLst"){before=n;break;}}names=before?workbookNode.insert_child_before("definedNames",before):workbookNode.append_child("definedNames");}bool present=false;for(auto n:names.children("definedName"))if(std::string(n.attribute("name").value())=="RepairCards_OneCCode")present=true;
 if(!present){auto node=names.append_child("definedName");node.append_attribute("name")="RepairCards_OneCCode";auto sheet=found.front().sheet;size_t at=0;while((at=sheet.find("'",at))!=sheet.npos){sheet.insert(at,"'");at+=2;}auto ref=found.front().ref;auto number=ref.find_first_of("0123456789");if(number==ref.npos)throw InputError("Invalid code field address");auto formula="'"+sheet+"'!$"+ref.substr(0,number)+"$"+ref.substr(number);node.text().set(formula.c_str());zip.files["xl/workbook.xml"]=xml_string(book);}
 return zip.write();
}
