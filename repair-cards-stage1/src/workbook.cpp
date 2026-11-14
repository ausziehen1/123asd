#include "merge.h"
#include "document.h"
#include "miniz.h"
#include "pugixml.hpp"
#include <map>
#include <sstream>
#include <algorithm>
#include <vector>
namespace {
using Json=nlohmann::json;
std::string xml(pugi::xml_document& value){std::ostringstream out;value.save(out,"",pugi::format_raw,pugi::encoding_utf8);return out.str();}
std::string text(const Json& value){if(value.is_string())return value.get<std::string>();if(value.is_null())return "";return value.dump();}
std::string address(int row,int col){std::string letters;for(++col;col;col=(col-1)/26)letters.insert(letters.begin(),static_cast<char>('A'+(col-1)%26));return letters+std::to_string(row+1);}
void color(pugi::xml_node parent,const char* tag,const Json& value){auto c=text(value);if(c.size()==7&&c[0]=='#'){auto n=parent.append_child(tag);n.append_attribute("rgb")=("FF"+c.substr(1)).c_str();}}
void style(pugi::xml_node fonts,pugi::xml_node fills,pugi::xml_node borders,pugi::xml_node xfs,const Json& s,int index){
 auto font=fonts.append_child("font");auto f=s.value("font",Json::object());font.append_child("sz").append_attribute("val")=f.value("size",11.0);font.append_child("name").append_attribute("val")=f.value("name",std::string("Calibri")).c_str();
 if(f.value("bold",false))font.append_child("b");if(f.value("italic",false))font.append_child("i");if(s.value("underline",false))font.append_child("u");if(s.value("strike",false))font.append_child("strike");if(s.contains("color"))color(font,"color",s["color"]);
 auto fill=fills.append_child("fill").append_child("patternFill");fill.append_attribute("patternType")="solid";color(fill,"fgColor",s.value("bgcolor",Json("#ffffff")));fill.append_child("bgColor").append_attribute("indexed")=64;
 auto border=borders.append_child("border");auto edges=s.value("border",Json::object());for(auto name:{"left","right","top","bottom"}){auto node=border.append_child(name);if(edges.contains(name)&&edges[name].is_array()&&edges[name].size()==2){node.append_attribute("style")=text(edges[name][0]).c_str();color(node,"color",edges[name][1]);}}border.append_child("diagonal");
 auto xf=xfs.append_child("xf");xf.append_attribute("numFmtId")=0;xf.append_attribute("fontId")=index;xf.append_attribute("fillId")=index+2;xf.append_attribute("borderId")=index;xf.append_attribute("xfId")=0;xf.append_attribute("applyAlignment")=1;auto a=xf.append_child("alignment");a.append_attribute("horizontal")=s.value("align",std::string("left")).c_str();auto valign=s.value("valign",std::string("bottom"));a.append_attribute("vertical")=(valign=="middle"?"center":valign).c_str();a.append_attribute("wrapText")=s.value("textwrap",false)?1:0;
}
}
std::string document_workbook(const Json& document){
 validate_document(document);std::map<std::string,std::string> files;
 pugi::xml_document content,rels,workbook,bookrels,styles;
 auto types=content.append_child("Types");types.append_attribute("xmlns")="http://schemas.openxmlformats.org/package/2006/content-types";
 auto add_type=[&](const char* part,const char* type){auto n=types.append_child("Override");n.append_attribute("PartName")=part;n.append_attribute("ContentType")=type;};
 auto d=types.append_child("Default");d.append_attribute("Extension")="rels";d.append_attribute("ContentType")="application/vnd.openxmlformats-package.relationships+xml";
 d=types.append_child("Default");d.append_attribute("Extension")="xml";d.append_attribute("ContentType")="application/xml";
 add_type("/xl/workbook.xml","application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml");add_type("/xl/styles.xml","application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml");
 auto rootrels=rels.append_child("Relationships");rootrels.append_attribute("xmlns")="http://schemas.openxmlformats.org/package/2006/relationships";
 auto rr=rootrels.append_child("Relationship");rr.append_attribute("Id")="rId1";rr.append_attribute("Type")="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument";rr.append_attribute("Target")="xl/workbook.xml";
 auto book=workbook.append_child("workbook");book.append_attribute("xmlns")="http://schemas.openxmlformats.org/spreadsheetml/2006/main";book.append_attribute("xmlns:r")="http://schemas.openxmlformats.org/officeDocument/2006/relationships";auto sheets=book.append_child("sheets");
 auto br=bookrels.append_child("Relationships");br.append_attribute("xmlns")="http://schemas.openxmlformats.org/package/2006/relationships";
 auto stylesheet=styles.append_child("styleSheet");stylesheet.append_attribute("xmlns")="http://schemas.openxmlformats.org/spreadsheetml/2006/main";
 auto fonts=stylesheet.append_child("fonts"),fills=stylesheet.append_child("fills"),borders=stylesheet.append_child("borders");
 fills.append_child("fill").append_child("patternFill").append_attribute("patternType")="none";fills.append_child("fill").append_child("patternFill").append_attribute("patternType")="gray125";
 auto stylexfs=stylesheet.append_child("cellStyleXfs");auto sx=stylexfs.append_child("xf");for(auto key:{"numFmtId","fontId","fillId","borderId"})sx.append_attribute(key)=0;
 auto xfs=stylesheet.append_child("cellXfs");style(fonts,fills,borders,xfs,Json::object(),0);int style_count=1;std::vector<int> offsets;
 for(const auto& sheet:document["sheets"]){offsets.push_back(style_count);for(const auto& s:sheet.value("styles",Json::array()))style(fonts,fills,borders,xfs,s,style_count++);}
 fonts.append_attribute("count")=style_count;fills.append_attribute("count")=style_count+2;borders.append_attribute("count")=style_count;xfs.append_attribute("count")=style_count;stylexfs.append_attribute("count")=1;
 auto cellstyles=stylesheet.append_child("cellStyles");cellstyles.append_attribute("count")=1;auto normal=cellstyles.append_child("cellStyle");normal.append_attribute("name")="Normal";normal.append_attribute("xfId")=0;normal.append_attribute("builtinId")=0;
 size_t i=0;std::string named_field;
 for(const auto& sheet:document["sheets"]){
  ++i;auto file="worksheets/sheet"+std::to_string(i)+".xml";auto rid="rId"+std::to_string(i);auto item=sheets.append_child("sheet");item.append_attribute("name")=sheet["name"].get<std::string>().c_str();item.append_attribute("sheetId")=static_cast<unsigned>(i);item.append_attribute("r:id")=rid.c_str();
  auto r=br.append_child("Relationship");r.append_attribute("Id")=rid.c_str();r.append_attribute("Type")="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet";r.append_attribute("Target")=file.c_str();add_type(("/xl/"+file).c_str(),"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml");
  pugi::xml_document page;auto ws=page.append_child("worksheet");ws.append_attribute("xmlns")="http://schemas.openxmlformats.org/spreadsheetml/2006/main";
  auto format=ws.append_child("sheetFormatPr");format.append_attribute("defaultRowHeight")=15;format.append_attribute("defaultColWidth")=8.43;
  auto cols=ws.append_child("cols");std::map<int,Json> ordered_cols;for(const auto& c:sheet["cols"].items())if(c.key()!="len")ordered_cols[std::stoi(c.key())]=c.value();for(const auto& pair:ordered_cols){auto c=cols.append_child("col");c.append_attribute("min")=pair.first+1;c.append_attribute("max")=pair.first+1;c.append_attribute("width")=std::max(.1,(pair.second.value("width",64.0)-5)/7);c.append_attribute("customWidth")=1;if(pair.second.value("hide",false))c.append_attribute("hidden")=1;}
  auto data=ws.append_child("sheetData");std::map<int,Json> rows;for(const auto& row:sheet["rows"].items())if(row.key()!="len")rows[std::stoi(row.key())]=row.value();
  for(const auto& row:rows){auto rn=data.append_child("row");rn.append_attribute("r")=row.first+1;if(row.second.contains("height")){rn.append_attribute("ht")=row.second["height"].get<double>()*.75;rn.append_attribute("customHeight")=1;}if(row.second.value("hide",false))rn.append_attribute("hidden")=1;
   std::map<int,Json> cells;const auto cell_data=row.second.value("cells",Json::object());for(const auto& c:cell_data.items())cells[std::stoi(c.key())]=c.value();for(const auto& pair:cells){const auto& cell=pair.second;auto c=rn.append_child("c");auto ref=address(row.first,pair.first);c.append_attribute("r")=ref.c_str();if(cell.contains("style"))c.append_attribute("s")=offsets[i-1]+cell["style"].get<int>();auto value=text(cell.value("text",Json("")));if(!value.empty()&&value.front()=='=')c.append_child("f").text().set(value.substr(1).c_str());else{c.append_attribute("t")="inlineStr";auto t=c.append_child("is").append_child("t");t.append_attribute("xml:space")="preserve";t.text().set(value.c_str());}
    if(cell.value("repairField",std::string())=="one_c_code"){auto name=sheet["name"].get<std::string>();size_t pos=0;while((pos=name.find('\'',pos))!=name.npos){name.insert(pos,"'");pos+=2;}auto digit=ref.find_first_of("0123456789");named_field="'"+name+"'!$"+ref.substr(0,digit)+"$"+ref.substr(digit);}
   }
  }
  if(sheet.contains("merges")&&!sheet["merges"].empty()){auto merges=ws.append_child("mergeCells");merges.append_attribute("count")=static_cast<unsigned>(sheet["merges"].size());for(const auto& range:sheet["merges"])merges.append_child("mergeCell").append_attribute("ref")=range.get<std::string>().c_str();}
  files["xl/"+file]=xml(page);
 }
 auto sr=br.append_child("Relationship");sr.append_attribute("Id")="styles";sr.append_attribute("Type")="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles";sr.append_attribute("Target")="styles.xml";
 if(!named_field.empty()){auto n=book.append_child("definedNames").append_child("definedName");n.append_attribute("name")="RepairCards_OneCCode";n.text().set(named_field.c_str());}
 book.append_child("calcPr").append_attribute("fullCalcOnLoad")=1;
 files["[Content_Types].xml"]=xml(content);files["_rels/.rels"]=xml(rels);files["xl/workbook.xml"]=xml(workbook);files["xl/_rels/workbook.xml.rels"]=xml(bookrels);files["xl/styles.xml"]=xml(styles);
 mz_zip_archive zip{};if(!mz_zip_writer_init_heap(&zip,0,0))throw std::runtime_error("XLSX init failed");
 try{for(const auto& file:files)if(!mz_zip_writer_add_mem(&zip,file.first.c_str(),file.second.data(),file.second.size(),MZ_BEST_SPEED))throw std::runtime_error("XLSX write failed");void* ptr=nullptr;size_t n=0;if(!mz_zip_writer_finalize_heap_archive(&zip,&ptr,&n))throw std::runtime_error("XLSX finish failed");std::string result(static_cast<char*>(ptr),n);mz_free(ptr);mz_zip_writer_end(&zip);return result;}catch(...){mz_zip_writer_end(&zip);throw;}
}
