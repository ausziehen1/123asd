#include <windows.h>
#include "WebView2.h"
#include "viewer_win.h"
#include "win_support.h"
#include <functional>
#include <memory>
#include <atomic>
#include <algorithm>
namespace {
using namespace repair::win;
bool same_url(std::wstring a,std::wstring b){for(auto* s:{&a,&b}){auto p=s->find(L":80/");if(p!=s->npos)s->erase(p,3);}return _wcsicmp(a.c_str(),b.c_str())==0;}
template<class I,class... Args>class Callback final:public I {
 std::atomic<ULONG> refs{1};GUID iid{};std::function<HRESULT(Args...)> fn;
public:
 Callback(const wchar_t* identity,std::function<HRESULT(Args...)> f):fn(std::move(f)){CLSIDFromString(identity,&iid);}
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(IsEqualIID(id,IID_IUnknown)||IsEqualIID(id,iid)){*out=static_cast<I*>(this);AddRef();return S_OK;}return E_NOINTERFACE;}
 ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
 ULONG STDMETHODCALLTYPE Release()override{auto n=--refs;if(!n)delete this;return n;}
 HRESULT STDMETHODCALLTYPE Invoke(Args... args)override{try{return fn(args...);}catch(...){return E_FAIL;}}
};
template<class I,class... Args,class F>I* callback(const wchar_t* id,F f){return new Callback<I,Args...>(id,std::move(f));}
struct Viewer:std::enable_shared_from_this<Viewer>{
 HWND window{},owner{},status{};std::wstring url,pending_url;bool editable=false,dirty=false,waiting_save=false,checking_close=false,close_owner_when_done=false;
 ICoreWebView2Controller* controller{};ICoreWebView2* web{};
 ~Viewer(){if(controller)controller->Close();if(web)web->Release();if(controller)controller->Release();}
 void diagnostic(nlohmann::json data){wchar_t flag[8]{};if(GetEnvironmentVariableW(L"REPAIRCARDS_DIAGNOSTICS",flag,8)&&std::wstring(flag)==L"1")try{data["editable"]=editable;data["url"]=utf8(url);data["pid"]=GetCurrentProcessId();save_profile("viewer-"+std::to_string(GetCurrentProcessId())+".json",data);save_profile("viewer-diagnostic.json",data);}catch(...){} }
 void error(const std::wstring& text){SetWindowTextW(status,text.c_str());ShowWindow(status,SW_SHOW);diagnostic({{"event","error"},{"message",utf8(text)}});}
 void fit(int width,int height){MONITORINFO monitor{};monitor.cbSize=sizeof(monitor);GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor);auto area=monitor.rcWork;HDC dc=GetDC(window);int dpi=GetDeviceCaps(dc,LOGPIXELSX);ReleaseDC(window,dc);RECT rect{0,0,MulDiv(std::clamp(width,600,1800),dpi,96),MulDiv(std::clamp(height,400,1400),dpi,96)};AdjustWindowRectEx(&rect,static_cast<DWORD>(GetWindowLongPtrW(window,GWL_STYLE)),FALSE,0);int w=std::min(static_cast<int>(area.right-area.left),static_cast<int>(rect.right-rect.left));int h=std::min(static_cast<int>(area.bottom-area.top),static_cast<int>(rect.bottom-rect.top));SetWindowPos(window,nullptr,area.left+(area.right-area.left-w)/2,area.top+(area.bottom-area.top-h)/2,w,h,SWP_NOZORDER|SWP_NOACTIVATE);}

 void resize(){RECT rect{};GetClientRect(window,&rect);MoveWindow(status,18,24,std::max(50L,rect.right-36),160,TRUE);if(controller)controller->put_Bounds(rect);}
 void start(){
  static auto loader=LoadLibraryExW((executable_directory()/L"WebView2Loader.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
  if(!loader){error(L"Рядом с приложением не найден WebView2Loader.dll. Восстановите файлы из полного архива.");return;}
  using Create=HRESULT(STDAPICALLTYPE*)(PCWSTR,PCWSTR,ICoreWebView2EnvironmentOptions*,ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);
  auto create=reinterpret_cast<Create>(GetProcAddress(loader,"CreateCoreWebView2EnvironmentWithOptions"));
  if(!create){error(L"Поврежден WebView2Loader.dll. Восстановите его из архива.");return;}
  std::weak_ptr<Viewer> weak=shared_from_this();
  auto env=callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler,HRESULT,ICoreWebView2Environment*>(L"{4e8a3389-c9d8-4bd2-b6b5-124fee6cc14d}",[weak](HRESULT result,ICoreWebView2Environment* environment){
   auto self=weak.lock();if(!self||!IsWindow(self->window))return S_OK;
   if(FAILED(result)||!environment){self->error(L"Для встроенного бланка установите Microsoft Edge WebView2 Runtime (Evergreen x64). Инструкция находится в docs/CLIENTS.md. Код: "+std::to_wstring(static_cast<unsigned long>(result)));return S_OK;}
   auto completed=callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler,HRESULT,ICoreWebView2Controller*>(L"{6c4819f3-c9b7-4260-8127-c9f5bde7f68c}",[weak](HRESULT hr,ICoreWebView2Controller* controller){
    auto s=weak.lock();if(!s||!IsWindow(s->window))return S_OK;if(FAILED(hr)||!controller){s->error(L"Не удалось создать окно бланка WebView2.");return S_OK;}
    controller->AddRef();s->controller=controller;controller->get_CoreWebView2(&s->web);if(!s->web){s->error(L"Не удалось открыть WebView2.");return S_OK;}
    ICoreWebView2Settings* settings=nullptr;s->web->get_Settings(&settings);if(settings){settings->put_AreDevToolsEnabled(FALSE);settings->put_AreDefaultContextMenusEnabled(FALSE);settings->put_IsStatusBarEnabled(FALSE);settings->Release();}
    EventRegistrationToken token{};
    auto navigation=callback<ICoreWebView2NavigationStartingEventHandler,ICoreWebView2*,ICoreWebView2NavigationStartingEventArgs*>(L"{9adbe429-f36d-432b-9ddc-f8881fbd76e3}",[weak](ICoreWebView2*,ICoreWebView2NavigationStartingEventArgs* args){auto v=weak.lock();LPWSTR uri=nullptr;args->get_Uri(&uri);std::wstring target=uri?uri:L"";CoTaskMemFree(uri);if(!v||!same_url(target,v->url))args->put_Cancel(TRUE);return S_OK;});
    s->web->add_NavigationStarting(navigation,&token);navigation->Release();
    auto new_window=callback<ICoreWebView2NewWindowRequestedEventHandler,ICoreWebView2*,ICoreWebView2NewWindowRequestedEventArgs*>(L"{d4c185fe-c81c-4989-97af-2d3fa7ab5651}",[](ICoreWebView2*,ICoreWebView2NewWindowRequestedEventArgs* args){args->put_Handled(TRUE);return S_OK;});
    s->web->add_NewWindowRequested(new_window,&token);new_window->Release();
    auto message=callback<ICoreWebView2WebMessageReceivedEventHandler,ICoreWebView2*,ICoreWebView2WebMessageReceivedEventArgs*>(L"{57213f19-00e6-49fa-8e07-898ea01ecbd2}",[weak](ICoreWebView2*,ICoreWebView2WebMessageReceivedEventArgs* args){
     auto v=weak.lock();if(!v)return S_OK;LPWSTR source=nullptr;args->get_Source(&source);bool allowed=source&&same_url(v->url,source);CoTaskMemFree(source);if(!allowed)return S_OK;
     LPWSTR value=nullptr;args->get_WebMessageAsJson(&value);auto text=value?utf8(value):"";CoTaskMemFree(value);auto data=nlohmann::json::parse(text,nullptr,false);if(!data.is_object())return S_OK;
     if(data.value("type","")=="ready"){auto title=repair::win::text(v->owner)+L" — "+wide(data.value("title",std::string("Бланк")));SetWindowTextW(v->window,title.c_str());v->fit(data.value("width",1100),data.value("height",800));v->diagnostic({{"event","ready"},{"title",data.value("title",std::string())},{"mode",data.value("mode",std::string())}});}
     if(data.value("type","")=="state")v->dirty=data.value("dirty",false)||data.value("saving",false);
     if(data.value("type","")=="identity"&&repair::win::text(v->owner)!=L"Creator"){
      auto identity=GetDlgItem(v->owner,104);if(identity)set_text(identity,wide(data.value("product_id",std::string())+" · "+data.value("platform_name",std::string())+" · Unique ID: "+std::to_string(data.value("id",0LL))+" · 1C Code: "+data.value("one_c_code",std::string())));
     }
     if(data.value("type","")=="save-close"){v->waiting_save=false;if(!data.value("ok",false)){v->close_owner_when_done=false;v->pending_url.clear();}if(data.value("ok",false)){v->dirty=false;if(v->pending_url.empty()||v->close_owner_when_done)DestroyWindow(v->window);else{v->url=v->pending_url;v->pending_url.clear();v->web->Navigate(v->url.c_str());}}}
     return S_OK;});
    s->web->add_WebMessageReceived(message,&token);message->Release();
    s->resize();controller->put_IsVisible(TRUE);ShowWindow(s->status,SW_HIDE);s->web->Navigate(s->url.c_str());return S_OK;
   });
   auto hr=environment->CreateCoreWebView2Controller(self->window,completed);completed->Release();if(FAILED(hr))self->error(L"Ошибка создания WebView2.");return S_OK;
  });
  auto role=repair::win::text(owner);if(role!=L"Creator"&&role!=L"Reader"&&role!=L"Repair")role=L"Viewer";auto folder=profile_directory()/L"WebView2"/role/(L"process-"+std::to_wstring(GetCurrentProcessId()));
  auto hr=create(nullptr,folder.c_str(),nullptr,env);env->Release();if(FAILED(hr))error(L"WebView2 Runtime не найден или недоступен. Установите Evergreen Runtime x64. Код: "+std::to_wstring(static_cast<unsigned long>(hr)));
 }
 bool close_confirmed(){if(waiting_save)return false;if(editable&&dirty){auto choice=MessageBoxW(window,L"Сохранить изменения перед закрытием бланка?",L"Repair",MB_YESNOCANCEL|MB_ICONQUESTION);if(choice==IDCANCEL){close_owner_when_done=false;return false;}if(choice==IDYES){if(web){waiting_save=true;web->ExecuteScript(L"window.repairSaveAndClose && window.repairSaveAndClose()",nullptr);}return false;}}DestroyWindow(window);return true;}
 bool close(){
  if(waiting_save||checking_close)return false;
  if(!editable||!web)return close_confirmed();
  checking_close=true;std::weak_ptr<Viewer> weak=shared_from_this();
  auto completed=callback<ICoreWebView2ExecuteScriptCompletedHandler,HRESULT,LPCWSTR>(L"{49511172-cc67-4bca-9923-137112f4c4cc}",[weak](HRESULT hr,LPCWSTR value){auto v=weak.lock();if(!v||!IsWindow(v->window))return S_OK;v->checking_close=false;if(SUCCEEDED(hr)&&value&&std::wstring(value)!=L"null")v->dirty=std::wstring(value)==L"true";v->close_confirmed();return S_OK;});
  auto hr=web->ExecuteScript(L"typeof window.repairIsDirty==='function'?window.repairIsDirty():null",completed);completed->Release();if(FAILED(hr)){checking_close=false;return close_confirmed();}return false;
 }
};
LRESULT CALLBACK procedure(HWND window,UINT msg,WPARAM w,LPARAM l){
 auto* holder=reinterpret_cast<std::shared_ptr<Viewer>*>(GetWindowLongPtrW(window,GWLP_USERDATA));
 if(msg==WM_NCCREATE){auto value=static_cast<std::shared_ptr<Viewer>*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);holder=new std::shared_ptr<Viewer>(*value);(*holder)->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(holder));}
 if(!holder)return DefWindowProcW(window,msg,w,l);auto view=*holder;
 try{switch(msg){case WM_CREATE:view->status=CreateWindowW(L"STATIC",L"Открываем бланк…",WS_CHILD|WS_VISIBLE,18,24,700,160,window,nullptr,GetModuleHandleW(nullptr),nullptr);PostMessageW(window,WM_APP,0,0);return 0;case WM_APP:view->start();return 0;case WM_SIZE:view->resize();return 0;case WM_CLOSE:view->close();return 0;case WM_NCDESTROY:SetWindowLongPtrW(window,GWLP_USERDATA,0);delete holder;if(view->close_owner_when_done&&IsWindow(view->owner))PostMessageW(view->owner,WM_CLOSE,0,0);return DefWindowProcW(window,msg,w,l);}}
 catch(const std::exception& e){view->error(wide(e.what()));return 0;}
 return DefWindowProcW(window,msg,w,l);
}
}
bool open_card_viewer(HWND owner,const std::string& url,bool editable){
 HWND previous=nullptr;struct Search{HWND owner;HWND* found;};Search search{owner,&previous};
 EnumWindows([](HWND h,LPARAM p)->BOOL{auto& s=*reinterpret_cast<Search*>(p);wchar_t name[64]{};GetClassNameW(h,name,64);if(GetWindow(h,GW_OWNER)==s.owner&&std::wstring(name)==L"RepairCardViewer"){*s.found=h;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&search));
 if(previous){auto holder=reinterpret_cast<std::shared_ptr<Viewer>*>(GetWindowLongPtrW(previous,GWLP_USERDATA));if(holder){auto v=*holder;auto next=wide(url+"?embedded=1");if(v->waiting_save||v->checking_close)throw std::runtime_error("Дождитесь сохранения текущего бланка.");if(next!=v->url){if(v->editable&&v->dirty){auto choice=MessageBoxW(previous,L"Сохранить изменения перед открытием другого бланка?",L"Repair",MB_YESNOCANCEL|MB_ICONQUESTION);if(choice==IDCANCEL)return false;if(choice==IDYES&&v->web){v->pending_url=next;v->waiting_save=true;v->web->ExecuteScript(L"window.repairSaveAndClose && window.repairSaveAndClose()",nullptr);return true;}}v->dirty=false;v->url=next;if(v->web)v->web->Navigate(next.c_str());}ShowWindow(previous,SW_RESTORE);SetForegroundWindow(previous);return true;}}

 WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=procedure;cls.lpszClassName=L"RepairCardViewer";cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);cls.hIcon=LoadIconW(cls.hInstance,MAKEINTRESOURCEW(101));RegisterClassW(&cls);
 RECT area{};SystemParametersInfoW(SPI_GETWORKAREA,0,&area,0);HDC dc=GetDC(owner);int dpi=GetDeviceCaps(dc,LOGPIXELSX);ReleaseDC(owner,dc);
 int width=std::min(static_cast<int>(area.right-area.left),MulDiv(1160,dpi,96)),height=std::min(static_cast<int>(area.bottom-area.top),MulDiv(860,dpi,96));
 auto view=std::make_shared<Viewer>();view->owner=owner;view->editable=editable;view->url=wide(url+"?embedded=1");
 auto window=CreateWindowExW(0,cls.lpszClassName,editable?L"Repair — Бланк":L"Reader — Бланк",WS_OVERLAPPEDWINDOW,area.left+(area.right-area.left-width)/2,area.top+(area.bottom-area.top-height)/2,width,height,owner,nullptr,cls.hInstance,&view);
 if(!window)throw std::runtime_error("Не удалось открыть окно бланка.");ShowWindow(window,SW_SHOW);SetForegroundWindow(window);return true;
}
bool close_card_viewers(HWND owner){
 struct State{HWND owner;bool closed=true;};State state{owner};
 EnumWindows([](HWND window,LPARAM p)->BOOL{auto* state=reinterpret_cast<State*>(p);wchar_t name[64]{};GetClassNameW(window,name,64);if(GetWindow(window,GW_OWNER)==state->owner&&std::wstring(name)==L"RepairCardViewer"){auto holder=reinterpret_cast<std::shared_ptr<Viewer>*>(GetWindowLongPtrW(window,GWLP_USERDATA));if(holder)(*holder)->close_owner_when_done=true;SendMessageW(window,WM_CLOSE,0,0);if(IsWindow(window))state->closed=false;}return TRUE;},reinterpret_cast<LPARAM>(&state));return state.closed;
}
