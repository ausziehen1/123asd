#include <windows.h>
#include <commdlg.h>
#include <filesystem>
#include <future>
#include <fstream>
#include "json.hpp"
#include "update.h"
namespace {
std::wstring wide(const std::string& s){if(s.empty())return{};int n=MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0);std::wstring out(n,L'\0');MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),out.data(),n);return out;}
std::filesystem::path folder;
HWND window{},path_edit{},status{},choose{},apply_button{},rollback_button{};std::filesystem::path archive;std::future<std::wstring> task;bool busy=false;
void set_busy(bool active){busy=active;for(auto h:{choose,apply_button,rollback_button})EnableWindow(h,!active);}
void begin(bool rollback){if(busy)return;if(!rollback&&archive.empty())throw std::runtime_error("Выберите ZIP обновления.");
 auto lock=CreateFileW((folder/L".clients.lock").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_HIDDEN,nullptr);
 if(lock==INVALID_HANDLE_VALUE)throw std::runtime_error("Закройте Creator, Reader и Repair, включая окна бланков, затем повторите обновление.");
 set_busy(true);SetWindowTextW(status,L"Проверяем и обновляем файлы…");auto package=archive;
 try{task=std::async(std::launch::async,[lock,package,rollback]{try{if(rollback)repair::update::rollback(folder);else repair::update::apply(package,folder);CloseHandle(lock);return std::wstring(rollback?L"Предыдущая версия восстановлена. Можно запускать клиенты.":L"Обновление установлено. Можно запускать клиенты.");}catch(const std::exception& e){CloseHandle(lock);return L"Ошибка: "+wide(e.what());}});}catch(...){CloseHandle(lock);set_busy(false);throw;}
}
LRESULT CALLBACK procedure(HWND h,UINT m,WPARAM w,LPARAM l){
 try{switch(m){case WM_CREATE:{auto font=GetStockObject(DEFAULT_GUI_FONT);auto item=[&](const wchar_t* cls,const wchar_t* text,DWORD style,int id,int x,int y,int width,int height){auto c=CreateWindowW(cls,text,WS_CHILD|WS_VISIBLE|style,x,y,width,height,h,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);return c;};
 item(L"STATIC",L"Локальное обновление клиентов Repair Cards",0,-1,18,18,610,24);
 item(L"STATIC",L"Закройте Creator, Reader и Repair перед установкой. База и рабочий макет сохраняются отдельно.",0,-1,18,50,610,40);
 path_edit=item(L"EDIT",L"",WS_BORDER|ES_READONLY|ES_AUTOHSCROLL,-1,18,100,470,30);choose=item(L"BUTTON",L"Выбрать ZIP…",WS_TABSTOP,1,500,100,125,30);
 apply_button=item(L"BUTTON",L"Установить",WS_TABSTOP,2,18,150,170,34);rollback_button=item(L"BUTTON",L"Откатить обновление",WS_TABSTOP,3,202,150,200,34);
 status=item(L"STATIC",L"Выберите локальный ZIP-патч для установленной версии.",0,-1,18,205,610,90);
 SetTimer(h,1,100,nullptr);
 auto lock=CreateFileW((folder/L".clients.lock").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_HIDDEN,nullptr);
 if(lock!=INVALID_HANDLE_VALUE){try{if(repair::update::recover(folder))SetWindowTextW(status,L"Прерванное обновление восстановлено. Предыдущая версия готова к запуску.");}catch(const std::exception& e){SetWindowTextW(status,wide(e.what()).c_str());}CloseHandle(lock);}return 0;}
 case WM_COMMAND:if(LOWORD(w)==1){wchar_t path[32768]{};OPENFILENAMEW f{};f.lStructSize=sizeof(f);f.hwndOwner=h;f.lpstrFile=path;f.nMaxFile=32768;f.lpstrFilter=L"Repair Cards patch (*.zip)\0*.zip\0\0";f.Flags=OFN_FILEMUSTEXIST|OFN_NOCHANGEDIR;if(GetOpenFileNameW(&f)){auto manifest=repair::update::inspect(path,folder);archive=path;SetWindowTextW(path_edit,path);SetWindowTextW(status,(L"Патч "+wide(manifest.at("from_version").get<std::string>())+L" → "+wide(manifest.at("version").get<std::string>())+L". Файлов: "+std::to_wstring(manifest.at("files").size())).c_str());}}
 else if(LOWORD(w)==2)begin(false);else if(LOWORD(w)==3)begin(true);return 0;
 case WM_TIMER:if(busy&&task.valid()&&task.wait_for(std::chrono::milliseconds(0))==std::future_status::ready){auto message=task.get();set_busy(false);SetWindowTextW(status,message.c_str());}return 0;
 case WM_CLOSE:if(busy){MessageBoxW(h,L"Дождитесь завершения обновления.",L"Обновление",MB_OK);return 0;}DestroyWindow(h);return 0;case WM_DESTROY:KillTimer(h,1);PostQuitMessage(0);return 0;}}
 catch(const std::exception& e){MessageBoxW(h,wide(e.what()).c_str(),L"Обновление",MB_OK|MB_ICONWARNING);}return DefWindowProcW(h,m,w,l);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show){wchar_t path[32768]{};auto n=GetModuleFileNameW(nullptr,path,32768);if(!n||n>=32768)return 1;folder=std::filesystem::path(path).parent_path();SetProcessDPIAware();WNDCLASSW cls{};cls.hInstance=instance;cls.lpfnWndProc=procedure;cls.lpszClassName=L"RepairCardsUpdater";cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);cls.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(101));RegisterClassW(&cls);
 window=CreateWindowW(cls.lpszClassName,L"Repair Cards — Обновление",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,660,360,nullptr,nullptr,instance,nullptr);if(!window)return 1;ShowWindow(window,show);MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){if(!IsDialogMessageW(window,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}return static_cast<int>(msg.wParam);}
