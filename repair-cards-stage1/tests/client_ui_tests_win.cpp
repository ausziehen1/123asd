#include <windows.h>
#include <commctrl.h>
#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include "api_client.h"
namespace {
std::atomic<int> result{1};
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void wait(const std::function<bool()>& condition){auto start=GetTickCount64();while(!condition()){if(GetTickCount64()-start>30000)throw std::runtime_error("Native UI wait timed out");std::this_thread::sleep_for(std::chrono::milliseconds(50));}}
std::wstring text(HWND h){int n=GetWindowTextLengthW(h);std::wstring value(n+1,L'\0');GetWindowTextW(h,value.data(),n+1);value.resize(n);return value;}
void click(HWND h){require(h!=nullptr,"Missing button");SendMessageW(h,BM_CLICK,0,0);}
std::wstring cell(HWND list,int row,int col){wchar_t value[256]{};LVITEMW item{};item.iSubItem=col;item.pszText=value;item.cchTextMax=256;SendMessageW(list,LVM_GETITEMTEXTW,row,reinterpret_cast<LPARAM>(&item));return value;}
}
int main(int argc,char** argv){if(argc!=2)return 2;std::wstring base;for(const unsigned char c:std::string(argv[1]))base+=c;
 wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);std::wstring folder=path;folder.resize(folder.find_last_of(L"\\/")+1);
 auto dll=LoadLibraryExW((folder+L"RepairClient.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);if(!dll)return 3;auto run=reinterpret_cast<int(*)(unsigned,int)>(GetProcAddress(dll,"RepairClientRun"));if(!run)return 4;
 std::thread automation([base]{try{
  HWND main=nullptr;wait([&]{main=FindWindowW(L"RepairCreator",L"Creator");return main!=nullptr;});
  SendMessageW(GetDlgItem(main,100),WM_SETTEXT,0,reinterpret_cast<LPARAM>(base.c_str()));click(GetDlgItem(main,101));auto list=GetDlgItem(main,108);
  wait([&]{return SendMessageW(list,LVM_GETITEMCOUNT,0,0)==10;});
  LVITEMW selected{};selected.state=LVIS_SELECTED;selected.stateMask=LVIS_SELECTED;SendMessageW(list,LVM_SETITEMSTATE,static_cast<WPARAM>(-1),reinterpret_cast<LPARAM>(&selected));
  wait([&]{return IsWindowEnabled(GetDlgItem(main,113));});click(GetDlgItem(main,113));
  HWND dialog=nullptr;wait([&]{dialog=FindWindowW(L"RepairLabelDialog",nullptr);return dialog!=nullptr&&IsWindowVisible(dialog);});
  HWND panel=nullptr;for(auto h=GetWindow(dialog,GW_CHILD);h;h=GetWindow(h,GW_HWNDNEXT)){wchar_t cls[32]{};GetClassNameW(h,cls,32);if(_wcsicmp(cls,L"Static")==0&&GetDlgItem(h,500))panel=h;}
  require(panel!=nullptr,"Allocation panel missing");
  auto set=[&](int id,const wchar_t* value){auto h=GetDlgItem(panel,id);require(h!=nullptr,"Allocation field missing");SendMessageW(h,WM_SETTEXT,0,reinterpret_cast<LPARAM>(value));};
  set(500,L"2");set(501,L"123");click(GetDlgItem(panel,502));set(503,L"7");set(504,L"1234");click(GetDlgItem(panel,505));set(506,L"1");set(507,L"12345");
  auto mapping=FindWindowExW(dialog,nullptr,WC_LISTVIEWW,nullptr);require(mapping!=nullptr,"Mapping preview missing");
  for(int n=0;n<10;n++)require(cell(mapping,n,2)==(n<2?L"123":n<9?L"1234":L"12345"),"Native 2/7/1 mapping differs from fields");
  require(text(GetDlgItem(panel,502))==L"−"&&text(GetDlgItem(panel,508))==L"+","Plus/minus positions wrong");
  auto prepare=GetDlgItem(dialog,205);require(IsWindowEnabled(prepare),"Valid allocation is disabled");set(506,L"2");require(!IsWindowEnabled(prepare),"Count mismatch must disable preparation");set(506,L"1");require(IsWindowEnabled(prepare),"Fixed count must enable preparation");
  set(507,L"");require(cell(mapping,9,2).empty(),"Blank code must stay blank");
  RECT groupRect{},fieldRect{};GetClientRect(panel,&groupRect);GetWindowRect(GetDlgItem(panel,508),&fieldRect);MapWindowPoints(nullptr,panel,reinterpret_cast<POINT*>(&fieldRect),2);require(fieldRect.top>=0&&fieldRect.bottom<=groupRect.bottom,"Third row is clipped");
  click(GetDlgItem(panel,502));require(text(GetDlgItem(panel,500))==L"7"&&!GetDlgItem(panel,506),"Minus must remove the selected group");require(!IsWindowEnabled(prepare),"Removed group leaves unallocated numbers");
  click(GetDlgItem(panel,505));set(506,L"1");click(GetDlgItem(panel,508));
  require(IsWindowEnabled(prepare),"Four-group allocation must be valid");
  GetClientRect(panel,&groupRect);GetWindowRect(GetDlgItem(panel,511),&fieldRect);MapWindowPoints(nullptr,panel,reinterpret_cast<POINT*>(&fieldRect),2);require(fieldRect.left>=0&&fieldRect.right<=groupRect.right&&fieldRect.top>=0&&fieldRect.bottom<=groupRect.bottom,"Plus button must remain visible after scrollbar appears");
  PostMessageW(dialog,WM_CLOSE,0,0);wait([&]{return !IsWindow(dialog);});
  SendMessageW(GetDlgItem(main,102),CB_SETCURSEL,1,0);SendMessageW(main,WM_COMMAND,MAKEWPARAM(102,CBN_SELCHANGE),reinterpret_cast<LPARAM>(GetDlgItem(main,102)));
  auto first=GetDlgItem(main,104);wait([&]{return IsWindowEnabled(first)&&text(first)==L"V00011";});
  selected.state=LVIS_SELECTED;SendMessageW(list,LVM_SETITEMSTATE,0,reinterpret_cast<LPARAM>(&selected));
  auto ui_thread=GetWindowThreadProcessId(main,nullptr);require(AttachThreadInput(GetCurrentThreadId(),ui_thread,TRUE),"Cannot attach input for native focus test");SetForegroundWindow(main);SetFocus(first);AttachThreadInput(GetCurrentThreadId(),ui_thread,FALSE);
  GUITHREADINFO focus{};focus.cbSize=sizeof(focus);require(GetGUIThreadInfo(ui_thread,&focus)&&focus.hwndFocus==first,"Table ID input focus missing");
  SendMessageW(first,WM_SETTEXT,0,reinterpret_cast<LPARAM>(L""));
  std::string endpoint;for(auto c:base)endpoint+=static_cast<char>(c);repair::ApiClient api(endpoint);auto platform=api.platforms().at(0).at("id").get<long long>();api.register_batch(platform,"Q00001",1);
  wait([&]{return SendMessageW(list,LVM_GETITEMCOUNT,0,0)==11;});require(text(first).empty(),"Live refresh interrupted empty Table ID input");require(SendMessageW(list,LVM_GETSELECTEDCOUNT,0,0)==1,"Live refresh lost card selection");
  PostMessageW(main,WM_CLOSE,0,0);result=0;
  std::cout<<"Native Creator UI passed: connect, select 10 cards, add/remove groups, 2/7/1 mapping, blank code, sum validation, row layout and live refresh preserving active Table ID input/selection\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';ExitProcess(1);}});
 auto code=run(0,SW_SHOW);automation.join();return code?code:result.load();
}
