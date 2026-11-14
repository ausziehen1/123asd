#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include "win_support.h"
#include "labels.h"
#include <future>
#include <chrono>

namespace {
using namespace repair::win;
enum Id { Base=100,Input,Open,Status };
struct Reader {
    HWND window{},base{},input{},open{},status{},identity{};HFONT font{};
    bool busy=false,closing=false;std::future<repair::Json> future;
    ~Reader(){if(font)DeleteObject(font);}
    void controls(bool enable){for(auto h:{base,input,open})EnableWindow(h,enable);}
    void initialize(){
        HDC dc=GetDC(window);int dpi=GetDeviceCaps(dc,LOGPIXELSX);ReleaseDC(window,dc);
        font=CreateFontW(-MulDiv(10,dpi,72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        auto item=[&](const wchar_t* kind,const wchar_t* label,DWORD style,int id,int x,int y,int w,int h){auto c=control(window,kind,label,style,id,font);MoveWindow(c,MulDiv(x,dpi,96),MulDiv(y,dpi,96),MulDiv(w,dpi,96),MulDiv(h,dpi,96),TRUE);return c;};
        item(L"STATIC",L"Сервер:",0,-1,16,22,130,24);
        base=item(L"EDIT",L"http://127.0.0.1:8080",WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL,Base,150,16,530,30);
        auto settings=load_profile("reader.json");if(settings.contains("server")&&settings["server"].is_string())set_text(base,wide(settings["server"].get<std::string>()));
        item(L"STATIC",L"QR / номер:",0,-1,16,72,130,24);
        input=item(L"EDIT",L"",WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL,Input,150,66,530,32);
        SendMessageW(input,EM_SETLIMITTEXT,1024,0);
        SendMessageW(input,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"Сканируйте QR или вставьте ссылку; Enter — открыть"));
        open=item(L"BUTTON",L"Открыть для просмотра",WS_TABSTOP|BS_DEFPUSHBUTTON,Open,150,114,280,36);
        identity=item(L"STATIC",L"",0,-1,16,172,664,48);
        status=item(L"STATIC",L"Ридер открывает только просмотр. USB-сканер работает как клавиатура. На телефоне можно сканировать QR обычной камерой.",0,Status,16,232,664,70);
        SetWindowSubclass(input,[](HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR parent)->LRESULT{
            if(m==WM_KEYDOWN&&w==VK_RETURN){PostMessageW(reinterpret_cast<HWND>(parent),WM_COMMAND,Open,0);return 0;}
            return DefSubclassProc(h,m,w,l);},1,reinterpret_cast<DWORD_PTR>(window));
        SetTimer(window,1,60,nullptr);
    }
    void start(){
        if(busy)return;auto scan=repair::parse_scan(utf8(text(input)),utf8(text(base)));
        busy=true;controls(false);set_text(status,L"Проверяем карточку…");
        try{future=std::async(std::launch::async,[scan]{repair::ApiClient api(scan.base);api.health();auto row=api.card(scan.id);
            return repair::Json{{"base",scan.base},{"url",scan.url},{"card",row}};});}
        catch(...){busy=false;controls(true);throw;}
    }
    void poll(){
        if(!busy||!future.valid()||future.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready)return;
        try{auto data=future.get();busy=false;if(closing){DestroyWindow(window);return;}
            auto url=data.at("url").get<std::string>();auto server=data.at("base").get<std::string>();auto card=data.at("card");
            set_text(base,wide(server));
            set_text(identity,wide(card.at("product_id").get<std::string>()+" · "+card.at("platform_name").get<std::string>()+" · Внутренний ID: "+std::to_string(card.at("id").get<long long>())));
            try{save_profile("reader.json",{{"server",server}});}catch(const std::exception&){}
            if(reinterpret_cast<INT_PTR>(ShellExecuteW(window,L"open",wide(url).c_str(),nullptr,nullptr,SW_SHOWNORMAL))<=32)throw repair::ApiError("Не удалось открыть браузер.");
            set_text(status,L"Карточка открыта в браузере только для просмотра. Можно сканировать следующую.");
            set_text(input,L"");controls(true);SetFocus(input);
        }catch(const std::exception& e){busy=false;if(closing){DestroyWindow(window);return;}controls(true);set_text(status,wide(e.what()));SetFocus(input);SendMessageW(input,EM_SETSEL,0,-1);}
    }
};
LRESULT CALLBACK procedure(HWND window,UINT m,WPARAM w,LPARAM l){
    auto* app=reinterpret_cast<Reader*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(m==WM_NCCREATE){app=static_cast<Reader*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);app->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));}
    if(!app)return DefWindowProcW(window,m,w,l);
    try{switch(m){case WM_CREATE:app->initialize();return 0;case WM_COMMAND:if(LOWORD(w)==Open)app->start();return 0;
        case WM_TIMER:app->poll();return 0;case WM_CLOSE:if(app->busy){app->closing=true;set_text(app->status,L"Завершаем запрос…");}else DestroyWindow(window);return 0;
        case WM_DESTROY:KillTimer(window,1);PostQuitMessage(0);return 0;}}
    catch(const std::exception& e){if(m==WM_CREATE)return -1;set_text(app->status,wide(e.what()));}
    return DefWindowProcW(window,m,w,l);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show){
    SetProcessDPIAware();INITCOMMONCONTROLSEX cc{sizeof(cc),ICC_STANDARD_CLASSES};InitCommonControlsEx(&cc);
    WNDCLASSW cls{};cls.hInstance=instance;cls.lpfnWndProc=procedure;cls.lpszClassName=L"RepairReader";cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
    if(!RegisterClassW(&cls))return 1;Reader app;HDC dc=GetDC(nullptr);int dpi=GetDeviceCaps(dc,LOGPIXELSX);ReleaseDC(nullptr,dc);
    auto window=CreateWindowExW(0,cls.lpszClassName,L"Ридер — просмотр карточек",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,
        CW_USEDEFAULT,CW_USEDEFAULT,MulDiv(720,dpi,96),MulDiv(365,dpi,96),nullptr,nullptr,instance,&app);
    if(!window){MessageBoxW(nullptr,L"Не удалось открыть Ридер.",L"Ридер",MB_OK|MB_ICONERROR);return 1;}
    ShowWindow(window,show);UpdateWindow(window);SetFocus(app.input);
    int argc=0;auto args=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(args){if(argc==2){set_text(app.input,args[1]);PostMessageW(window,WM_COMMAND,Open,0);}LocalFree(args);}
    MSG msg{};int result;while((result=static_cast<int>(GetMessageW(&msg,nullptr,0,0)))>0){if(!IsDialogMessageW(window,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
    return result<0?1:static_cast<int>(msg.wParam);
}
