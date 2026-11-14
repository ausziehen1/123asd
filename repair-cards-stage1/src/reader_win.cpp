#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include "win_support.h"
#include "labels.h"
#include "viewer_win.h"
#include <future>
#include <chrono>
namespace {
using namespace repair::win;
enum Id{Base=100,Input,Open,Toggle};
struct Scanner{
 HWND window{},server_label{},base{},input{},open{},status{},identity{};HFONT font{};int dpi=96;
 bool repair=false,server_visible=true,busy=false,closing=false,connecting=false;std::future<repair::Json> future;
 ~Scanner(){if(font)DeleteObject(font);}
 std::string profile()const{return repair?"repair.json":"reader.json";}
 void controls(bool enabled){EnableWindow(base,enabled&&server_visible);for(auto h:{input,open,server_label})EnableWindow(h,enabled);}
 void layout(){RECT r{};GetClientRect(window,&r);auto u=[&](int n){return MulDiv(n,dpi,96);};int width=MulDiv(r.right,96,dpi);
  auto move=[&](HWND h,int x,int y,int w,int height){MoveWindow(h,u(x),u(y),u(w),u(height),TRUE);};
  move(server_label,18,22,112,24);move(base,140,16,width-158,32);move(input,140,68,width-276,36);move(open,width-120,68,102,36);move(identity,18,130,width-36,54);move(status,18,198,width-36,66);
 }
 void show_server(bool show){server_visible=show;ShowWindow(base,show?SW_SHOW:SW_HIDE);EnableWindow(base,show&&!busy);set_text(server_label,show?L"Сервер ▾":L"Сервер ▸");if(show){SetFocus(base);SendMessageW(base,EM_SETSEL,0,-1);}}
 void initialize(){HDC dc=GetDC(window);dpi=GetDeviceCaps(dc,LOGPIXELSX);ReleaseDC(window,dc);font=theme_font(dpi);
  server_label=control(window,L"STATIC",L"Сервер ▾",SS_NOTIFY,Toggle,font);
  base=control(window,L"EDIT",L"http://127.0.0.1:8080",WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL,Base,font);SendMessageW(base,EM_SETLIMITTEXT,512,0);
  auto settings=load_profile(profile());if(settings.contains("server")&&settings["server"].is_string())set_text(base,wide(settings["server"].get<std::string>()));
  control(window,L"STATIC",L"",0,-1,font);
  auto qr=control(window,L"STATIC",L"QR / номер",0,-1,font);MoveWindow(qr,MulDiv(18,dpi,96),MulDiv(76,dpi,96),MulDiv(112,dpi,96),MulDiv(24,dpi,96),TRUE);
  input=control(window,L"EDIT",L"",WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL,Input,font);SendMessageW(input,EM_SETLIMITTEXT,1024,0);
  SendMessageW(input,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"Сканируйте QR · Enter — открыть"));
  open=control(window,L"BUTTON",L"Открыть",WS_TABSTOP,Open,font);identity=control(window,L"STATIC",L"",0,-1,font);
  status=control(window,L"STATIC",repair?L"Сканируйте QR: бланк откроется в отдельном окне Repair для редактирования и сохранения.":L"Сканируйте QR: бланк откроется в отдельном окне Reader для просмотра.",0,-1,font);
  layout();show_server(!settings.value("server_confirmed",false));SetTimer(window,1,60,nullptr);apply_app_icon(window);install_update_menu(window);
 }
 void start(bool connection=false){if(busy)return;connecting=connection;
  auto scan=connection?repair::Scan{repair::normalize_server_url(utf8(text(base))),0,{}}:repair::parse_scan(utf8(text(input)),utf8(text(base)));
  busy=true;controls(false);set_text(status,connection?L"Проверяем адрес сервера…":L"Проверяем карточку…");
  try{future=std::async(std::launch::async,[scan,connection,editing=repair]{repair::ApiClient api(scan.base);api.health();if(editing&&!api.server_info().contains("capabilities"))throw repair::ApiError("Для Repair обновите сервер до 0.5.0.");return repair::Json{{"base",scan.base},{"url",scan.url},{"card",connection?repair::Json():api.card(scan.id)}};});}catch(...){busy=false;controls(true);throw;}
 }
 void poll(){if(!busy||!future.valid()||future.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready)return;
  try{auto data=future.get();busy=false;if(closing){DestroyWindow(window);return;}auto server=data.at("base").get<std::string>();set_text(base,wide(server));
   save_profile(profile(),{{"server",server},{"server_confirmed",true}});controls(true);show_server(false);
   if(connecting){set_text(status,L"Сервер подключен. Для изменения адреса нажмите «Сервер».");SetFocus(input);return;}
   auto card=data.at("card");set_text(identity,wide(card.at("product_id").get<std::string>()+" · "+card.at("platform_name").get<std::string>()+" · Unique ID: "+std::to_string(card.at("id").get<long long>())+" · 1C Code: "+card.value("one_c_code",std::string())));
   auto url=data.at("url").get<std::string>();if(repair)url=server+"/edit/"+std::to_string(card.at("id").get<long long>());
   if(!open_card_viewer(window,url,repair)){set_text(status,L"Переход к другому бланку отменен. Текущие правки сохранены в окне редактора.");return;}set_text(status,repair?L"Бланк открыт в Repair. Сохранение — кнопкой в бланке или Ctrl+S.":L"Бланк открыт для просмотра. Следующий номер сканируйте в поле QR.");set_text(input,L"");
  }catch(const std::exception& e){busy=false;if(closing){DestroyWindow(window);return;}controls(true);set_text(status,wide(e.what()));if(connecting)show_server(true);else{SetFocus(input);SendMessageW(input,EM_SETSEL,0,-1);}}
 }
};
LRESULT CALLBACK procedure(HWND window,UINT m,WPARAM w,LPARAM l){
 auto* app=reinterpret_cast<Scanner*>(GetWindowLongPtrW(window,GWLP_USERDATA));if(m==WM_NCCREATE){app=static_cast<Scanner*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);app->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));}if(!app)return DefWindowProcW(window,m,w,l);
 LRESULT themed;if(theme_message(window,m,w,l,themed))return themed;
 try{switch(m){case WM_CREATE:app->initialize();return 0;case WM_SIZE:app->layout();return 0;case WM_GETMINMAXINFO:{auto* size=reinterpret_cast<MINMAXINFO*>(l);size->ptMinTrackSize={MulDiv(680,app->dpi,96),MulDiv(325,app->dpi,96)};return 0;}
  case WM_COMMAND:if(LOWORD(w)==Open)app->start();else if(LOWORD(w)==Toggle&&HIWORD(w)==STN_CLICKED&&!app->busy)app->show_server(!app->server_visible);return 0;
  case WM_TIMER:app->poll();return 0;case WM_CLOSE:if(!close_card_viewers(window))return 0;if(app->busy){app->closing=true;set_text(app->status,L"Завершаем запрос…");}else DestroyWindow(window);return 0;
  case WM_DESTROY:KillTimer(window,1);PostQuitMessage(0);return 0;}}
 catch(const std::exception& e){if(m==WM_CREATE)return -1;set_text(app->status,wide(e.what()));}return DefWindowProcW(window,m,w,l);
}
}
int run_scanner(HINSTANCE instance,int show,bool repair){
 SetProcessDPIAware();INITCOMMONCONTROLSEX cc{sizeof(cc),ICC_STANDARD_CLASSES};InitCommonControlsEx(&cc);
 WNDCLASSW cls{};cls.hInstance=instance;cls.lpfnWndProc=procedure;cls.lpszClassName=repair?L"RepairEditorClient":L"RepairReader";cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=theme_background();cls.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(101));if(!RegisterClassW(&cls))return 1;
 Scanner app;app.repair=repair;HDC dc=GetDC(nullptr);int dpi=GetDeviceCaps(dc,LOGPIXELSX);ReleaseDC(nullptr,dc);
 auto window=CreateWindowExW(0,cls.lpszClassName,repair?L"Repair":L"Reader",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,MulDiv(740,dpi,96),MulDiv(340,dpi,96),nullptr,nullptr,instance,&app);
 if(!window){MessageBoxW(nullptr,L"Не удалось открыть приложение.",repair?L"Repair":L"Reader",MB_OK|MB_ICONERROR);return 1;}ShowWindow(window,show);UpdateWindow(window);SetFocus(app.input);
 int argc=0;auto args=CommandLineToArgvW(GetCommandLineW(),&argc);if(args){if(argc==2){set_text(app.input,args[1]);PostMessageW(window,WM_COMMAND,Open,0);}LocalFree(args);}
 MSG msg{};int result;while((result=static_cast<int>(GetMessageW(&msg,nullptr,0,0)))>0){
  if(msg.message==WM_KEYDOWN&&msg.wParam==VK_RETURN&&(msg.hwnd==app.input||msg.hwnd==app.base)){try{app.start(msg.hwnd==app.base);}catch(const std::exception& e){set_text(app.status,wide(e.what()));}continue;}
  if(!IsDialogMessageW(window,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}
 }return result<0?1:static_cast<int>(msg.wParam);
}
