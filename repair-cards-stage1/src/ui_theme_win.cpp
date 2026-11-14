#include "win_support.h"
#include "ui_theme_win.h"
#include <commctrl.h>
#include <uxtheme.h>
#include <shellapi.h>
#include <algorithm>
namespace {
struct Theme{
 COLORREF background=RGB(244,246,249),paper=RGB(255,255,255),text=RGB(36,50,70),accent=RGB(56,94,132),line=RGB(205,214,225),muted=RGB(105,118,135);
 HBRUSH bg{},field{};std::wstring font=L"Segoe UI";int size=10;
 Theme(){try{auto p=repair::win::executable_directory()/L"theme.json";std::ifstream in(p);auto value=nlohmann::json::parse(in,nullptr,false);if(value.is_object()){
  auto color=[&](const char* key,COLORREF& out){auto s=value.value(key,"");if(s.size()==7&&s[0]=='#'&&s.find_first_not_of("0123456789abcdefABCDEF",1)==s.npos){auto n=std::stoul(s.substr(1),nullptr,16);out=RGB((n>>16)&255,(n>>8)&255,n&255);}};
  color("background",background);color("paper",paper);color("text",text);color("accent",accent);color("line",line);if(value.contains("font"))font=repair::win::wide(value.at("font").get<std::string>());size=std::clamp(value.value("font_size",10),9,14);
 }}catch(const std::exception&){}bg=CreateSolidBrush(background);field=CreateSolidBrush(paper);}
 ~Theme(){DeleteObject(bg);DeleteObject(field);}
};Theme& theme(){static Theme t;return t;}
}
HBRUSH theme_background(){return theme().bg;}
HFONT theme_font(int dpi){return CreateFontW(-MulDiv(theme().size,dpi,72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,theme().font.c_str());}
void apply_app_icon(HWND window){auto instance=GetModuleHandleW(nullptr);for(auto pair:{std::pair<int,int>{ICON_SMALL,GetSystemMetrics(SM_CXSMICON)},{ICON_BIG,GetSystemMetrics(SM_CXICON)}}){auto icon=LoadImageW(instance,MAKEINTRESOURCEW(101),IMAGE_ICON,pair.second,pair.second,LR_SHARED);if(icon)SendMessageW(window,WM_SETICON,pair.first,reinterpret_cast<LPARAM>(icon));}}
void style_control(HWND control){
 wchar_t kind[32]{};GetClassNameW(control,kind,32);
 if(_wcsicmp(kind,L"Button")==0){auto style=GetWindowLongPtrW(control,GWL_STYLE);if((style&BS_TYPEMASK)==BS_PUSHBUTTON||(style&BS_TYPEMASK)==BS_DEFPUSHBUTTON)SetWindowLongPtrW(control,GWL_STYLE,(style&~BS_TYPEMASK)|BS_OWNERDRAW);}
 SetWindowTheme(control,L"Explorer",nullptr);
}
bool theme_message(HWND window,UINT message,WPARAM w,LPARAM l,LRESULT& result){
 if(message==WM_SYSCOMMAND&&((w&0xfff0)==0xa110||(w&0xfff0)==0xa120)){
  if((w&0xfff0)==0xa110){auto file=repair::win::executable_directory()/L"Updater.exe";if(reinterpret_cast<INT_PTR>(ShellExecuteW(window,L"open",file.c_str(),nullptr,nullptr,SW_SHOWNORMAL))<=32)MessageBoxW(window,L"Рядом с приложением не найден Updater.exe. Восстановите его из полного архива.",L"Обновление",MB_OK|MB_ICONWARNING);}
  else {std::wstring value=L"Repair Cards 0.5.0";try{std::ifstream in(repair::win::executable_directory()/L"installation.json");auto data=nlohmann::json::parse(in);value=L"Repair Cards "+repair::win::wide(data.at("version").get<std::string>());}catch(...){}MessageBoxW(window,value.c_str(),L"О программе",MB_OK);}
  result=0;return true;
 }
 auto& t=theme();
 if(message==WM_CTLCOLORSTATIC||message==WM_CTLCOLOREDIT||message==WM_CTLCOLORLISTBOX){auto dc=reinterpret_cast<HDC>(w);SetTextColor(dc,t.text);auto field=message!=WM_CTLCOLORSTATIC;SetBkColor(dc,field?t.paper:t.background);result=reinterpret_cast<LRESULT>(field?t.field:t.bg);return true;}
 if(message!=WM_DRAWITEM)return false;auto* item=reinterpret_cast<DRAWITEMSTRUCT*>(l);if(!item||item->CtlType!=ODT_BUTTON)return false;
 auto text=repair::win::text(item->hwndItem);bool primary=text==L"Сформировать диапазон"||text==L"Сформировать QR"||text==L"Открыть";
 auto selectedFont=SendMessageW(item->hwndItem,WM_GETFONT,0,0);auto previousFont=SelectObject(item->hDC,reinterpret_cast<HGDIOBJ>(selectedFont));
 auto rect=item->rcItem;FillRect(item->hDC,&rect,t.bg);InflateRect(&rect,-1,-1);bool enabled=!(item->itemState&ODS_DISABLED);
 auto fill=CreateSolidBrush(primary&&enabled?t.accent:t.paper);auto pen=CreatePen(PS_SOLID,1,primary&&enabled?t.accent:t.line);auto oldBrush=SelectObject(item->hDC,fill),oldPen=SelectObject(item->hDC,pen);
 RoundRect(item->hDC,rect.left,rect.top,rect.right,rect.bottom,10,10);SelectObject(item->hDC,oldBrush);SelectObject(item->hDC,oldPen);DeleteObject(fill);DeleteObject(pen);
 SetBkMode(item->hDC,TRANSPARENT);SetTextColor(item->hDC,!enabled?t.muted:(primary?t.paper:t.text));if(item->itemState&ODS_SELECTED)OffsetRect(&rect,1,1);
 DrawTextW(item->hDC,text.c_str(),-1,&rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
 if(item->itemState&ODS_FOCUS){InflateRect(&rect,-4,-4);DrawFocusRect(item->hDC,&rect);}SelectObject(item->hDC,previousFont);result=TRUE;return true;
}

void install_update_menu(HWND window){auto menu=GetSystemMenu(window,FALSE);AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,0xa110,L"Локальное обновление…");AppendMenuW(menu,MF_STRING,0xa120,L"О программе…");}
