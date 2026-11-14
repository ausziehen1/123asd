#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include "win_support.h"
#include "label_dialog_win.h"
#include "labels.h"
#include <chrono>
#include <future>

namespace {
using namespace repair::win;
using repair::Json;
struct WindowSearch { DWORD pid; std::wstring executable; HWND found{}; };
HWND bartender_window(DWORD pid,const std::filesystem::path& executable){
    WindowSearch search{pid,executable.wstring(),nullptr};
    EnumWindows([](HWND window,LPARAM param)->BOOL{
        auto& state=*reinterpret_cast<WindowSearch*>(param);
        if(!IsWindowVisible(window)||GetWindowTextLengthW(window)==0||
           !(GetWindowLongPtrW(window,GWL_STYLE)&WS_CAPTION))return TRUE;
        DWORD owner=0;GetWindowThreadProcessId(window,&owner);
        bool matches=owner==state.pid;
        if(!matches){
            auto process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,owner);
            if(process){
                std::wstring path(32768,L'\0');DWORD size=static_cast<DWORD>(path.size());
                if(QueryFullProcessImageNameW(process,0,path.data(),&size)){
                    path.resize(size);matches=_wcsicmp(path.c_str(),state.executable.c_str())==0;
                }
                CloseHandle(process);
            }
        }
        if(!matches)return TRUE;
        state.found=window;return FALSE;
    },reinterpret_cast<LPARAM>(&search));
    return search.found;
}
enum Id { Public=200,Exe,Btw,ChooseExe,ChooseBtw,Prepare,CheckQr,Design,Print,Folder,Close };
struct Dialog {
    HWND window{},owner{},public_edit{},exe_edit{},btw_edit{},prepare{},check{},design{},print{},folder{},status{};
    HFONT font{};int dpi=96;bool busy=false,closing=false,done=false;
    std::string api;Json cards,info,prefs;std::filesystem::path job;
    std::future<Json> task;
    HANDLE launch_process{};DWORD launch_pid{};ULONGLONG launch_started{},launch_checked{};
    std::filesystem::path launch_executable;bool launch_printing=false;
    ~Dialog(){if(launch_process)CloseHandle(launch_process);if(font)DeleteObject(font);}
    int u(int value)const{return MulDiv(value,dpi,96);}
    void move(HWND h,int x,int y,int w,int height){MoveWindow(h,u(x),u(y),u(w),u(height),TRUE);}
    HWND item(const wchar_t* type,const wchar_t* label,DWORD style,int id,int x,int y,int w,int h){auto c=control(window,type,label,style,id,font);move(c,x,y,w,h);return c;}
    void message(const std::wstring& s){set_text(status,s);}
    void error(const std::exception& e){MessageBoxW(window,wide(e.what()).c_str(),L"QR и BarTender",MB_OK|MB_ICONWARNING);}
    std::wstring guess_exe(){
        for(const auto* var:{L"ProgramFiles(x86)",L"ProgramFiles"}){
            std::wstring root(32768,L'\0');auto n=GetEnvironmentVariableW(var,root.data(),static_cast<DWORD>(root.size()));
            if(!n||n>=root.size())continue;root.resize(n);
            auto path=std::filesystem::path(root)/L"Seagull"/L"BarTender Suite"/L"bartend.exe";
            if(std::filesystem::is_regular_file(path))return path.wstring();
        }return{};
    }
    std::wstring working_template(){
        auto destination=profile_directory()/L"labels"/L"RepairCards.btw";
        if(!std::filesystem::exists(destination)){
            for(const auto& root:{std::filesystem::current_path(),executable_directory()}){
                auto seed=root/L"labels"/L"TEST-original.btw";
                if(std::filesystem::is_regular_file(seed)){
                    std::filesystem::create_directories(destination.parent_path());
                    std::filesystem::copy_file(seed,destination,std::filesystem::copy_options::none);break;
                }
            }
        }
        return std::filesystem::is_regular_file(destination)?destination.wstring():std::wstring();
    }
    void initialize(){
        HDC dc=GetDC(window);dpi=GetDeviceCaps(dc,LOGPIXELSX);ReleaseDC(window,dc);
        font=CreateFontW(-MulDiv(10,dpi,72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        prefs=load_profile("printing.json");
        item(L"STATIC",L"QR для выбранных карточек: ",0,-1,16,16,270,24);
        item(L"STATIC",std::to_wstring(cards.size()).c_str(),0,-1,290,16,80,24);
        item(L"STATIC",L"Сетевой адрес в QR:",0,-1,16,58,190,24);
        public_edit=item(L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWN|WS_VSCROLL,Public,210,52,600,260);
        for(const auto& url:info.at("suggested_urls")){auto value=wide(url.get<std::string>());SendMessageW(public_edit,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(value.c_str()));}
        auto current=info.value("public_url","");
        if(current.empty()&&info.at("suggested_urls").size()==1)current=info.at("suggested_urls").at(0).get<std::string>();
        set_text(public_edit,wide(current));
        item(L"STATIC",L"Выберите адрес ПК с сервером, доступный телефону и другим компьютерам. Сохраните этот IP постоянным.",0,-1,16,92,795,43);
        item(L"STATIC",L"BarTender (bartend.exe):",0,-1,16,150,190,24);
        exe_edit=item(L"EDIT",L"",WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL,Exe,210,144,510,30);
        item(L"BUTTON",L"Обзор…",WS_TABSTOP,ChooseExe,730,144,80,30);
        set_text(exe_edit,prefs.contains("exe")?wide(prefs.at("exe").get<std::string>()):guess_exe());
        item(L"STATIC",L"Рабочий шаблон .btw:",0,-1,16,196,190,24);
        btw_edit=item(L"EDIT",L"",WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL,Btw,210,190,510,30);
        item(L"BUTTON",L"Обзор…",WS_TABSTOP,ChooseBtw,730,190,80,30);
        set_text(btw_edit,prefs.contains("template")?wide(prefs.at("template").get<std::string>()):working_template());
        prepare=item(L"BUTTON",L"1. Подготовить QR",WS_TABSTOP,Prepare,16,245,210,36);
        check=item(L"BUTTON",L"Проверить QR-ссылки",WS_TABSTOP,CheckQr,240,245,230,36);
        folder=item(L"BUTTON",L"Папка задания",WS_TABSTOP,Folder,484,245,170,36);
        design=item(L"BUTTON",L"2. Макет в BarTender",WS_TABSTOP,Design,16,300,260,38);
        print=item(L"BUTTON",L"3. Предпросмотр / печать…",WS_TABSTOP,Print,290,300,300,38);
        item(L"STATIC",L"Первое подключение TEST: в BarTender выберите у QR-объекта источник «Поле базы данных» → QRUrl. Отключите сериализацию значения QR. Сохраните рабочий шаблон. Макет, размеры и расположение меняются в BarTender.",0,-1,16,358,795,75);
        status=item(L"STATIC",L"",0,-1,16,445,795,70);
        item(L"BUTTON",L"Закрыть",WS_TABSTOP,Close,690,530,120,32);
        if(info.value("local_only",true))message(L"Сервер пока доступен только на своем ПК. В его конфиге установите host: 0.0.0.0 и перезапустите сервер, затем откройте это окно снова.");
        else message(L"Сначала подготовьте QR. Макет и точный предпросмотр открываются в BarTender; печать запускается вручную в его диалоге.");
        SetTimer(window,1,60,nullptr);enable();
    }
    void enable(){
        bool ready=!busy&&!launch_process;
        for(auto h:{public_edit,exe_edit,btw_edit,design})EnableWindow(h,ready);
        EnableWindow(prepare,ready&&!info.value("local_only",true));
        for(auto h:{check,print,folder})EnableWindow(h,ready&&!job.empty());
    }
    void diagnostic(const Json& event){
        try{
            auto entry=event;entry["time_ms"]=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            auto line=entry.dump()+"\n";
            for(const auto& root:{profile_directory(),job})if(!root.empty()){
                std::ofstream out(root/L"bartender-launch.log",std::ios::binary|std::ios::app);
                if(out)out.write(line.data(),static_cast<std::streamsize>(line.size()));
            }
        }catch(const std::exception&){}
    }
    void finish_launch(){CloseHandle(launch_process);launch_process=nullptr;enable();}
    std::wstring log_location(){return ((job.empty()?profile_directory():job)/L"bartender-launch.log").wstring();}
    void poll_launch(){
        if(!launch_process||GetTickCount64()-launch_checked<250)return;
        launch_checked=GetTickCount64();
        DWORD code=STILL_ACTIVE;
        bool exited=WaitForSingleObject(launch_process,0)==WAIT_OBJECT_0;
        if(exited&&GetExitCodeProcess(launch_process,&code)&&code!=0){
            diagnostic({{"event","exit_error"},{"exit_code",code}});finish_launch();
            message(L"BarTender завершился с ошибкой. Журнал: "+log_location());
            throw repair::ApiError("BarTender завершился с кодом "+std::to_string(code)+". Проверьте, открывается ли рабочий .btw вручную. Журнал: "+utf8(log_location()));
        }
        auto window= bartender_window(launch_pid,launch_executable);
        if(window){
            auto popup=GetLastActivePopup(window);if(popup&&IsWindowVisible(popup))window=popup;
            ShowWindowAsync(window,IsIconic(window)?SW_RESTORE:SW_SHOW);SetForegroundWindow(window);
            diagnostic({{"event","visible_window"},{"title",utf8(text(window))},{"launcher_exited",exited}});
            finish_launch();
            message(launch_printing?L"Найдено окно BarTender. Проверьте диалог печати и предпросмотр в нем. Печать запускается вручную.":L"Найдено окно BarTender. Проверьте открытый макет; после правок сохраните его Ctrl+S.");
        }else if(GetTickCount64()-launch_started>=15000){
            diagnostic({{"event","no_visible_window"},{"launcher_exited",exited},{"exit_code",code}});finish_launch();
            message(L"Видимое окно BarTender не найдено. Журнал: "+log_location());
            throw repair::ApiError("За 15 секунд видимое окно BarTender не найдено. Проверьте ручное открытие рабочего .btw и редакцию BarTender (для CLI нужна Automation или Enterprise Automation). Журнал: "+utf8(log_location()));
        }
    }
    void preferences(){prefs["exe"]=utf8(text(exe_edit));prefs["template"]=utf8(text(btw_edit));save_profile("printing.json",prefs);}
    void browse(HWND edit,bool executable){
        wchar_t path[32768]{};auto current=text(edit);if(current.size()<32768)std::copy(current.begin(),current.end(),path);
        OPENFILENAMEW ofn{};ofn.lStructSize=sizeof(ofn);ofn.hwndOwner=window;ofn.lpstrFile=path;ofn.nMaxFile=32768;
        ofn.lpstrFilter=executable?L"BarTender (bartend.exe)\0bartend.exe\0Programs (*.exe)\0*.exe\0\0":L"BarTender (*.btw)\0*.btw\0\0";
        ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
        if(GetOpenFileNameW(&ofn)){set_text(edit,path);preferences();}
    }
    void launch(bool printing){
        if(text(exe_edit).empty())throw repair::ApiError("Укажите bartend.exe кнопкой «Обзор».");
        if(text(btw_edit).empty())throw repair::ApiError("Укажите рабочий шаблон .btw.");
        auto exe=std::filesystem::absolute(std::filesystem::path(text(exe_edit))),btw=std::filesystem::absolute(std::filesystem::path(text(btw_edit)));
        if(!std::filesystem::is_regular_file(exe))throw repair::ApiError("Укажите существующий bartend.exe кнопкой «Обзор».");
        if(!std::filesystem::is_regular_file(btw))throw repair::ApiError("Укажите существующий шаблон .btw.");
        if(printing&&job.empty())throw repair::ApiError("Сначала нажмите «Подготовить QR».");
        preferences();std::vector<std::wstring> args;
        for(const auto& arg:repair::bartender_arguments(btw.u8string(),job.empty()?std::string():(job/"labels.csv").u8string(),printing))args.push_back(wide(arg));
        auto command=repair::bartender_command_line(exe.wstring(),args);
        diagnostic({{"event","launch"},{"command_line",utf8(command)},{"working_directory",btw.parent_path().u8string()},{"print_dialog",printing}});
        STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_SHOWNORMAL;PROCESS_INFORMATION process{};
        if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,btw.parent_path().c_str(),&startup,&process)){
            auto code=GetLastError();diagnostic({{"event","create_process_failed"},{"windows_error",code}});
            throw repair::ApiError("Не удалось запустить BarTender. Код Windows: "+std::to_string(code));
        }
        CloseHandle(process.hThread);launch_process=process.hProcess;launch_pid=process.dwProcessId;
        launch_executable=exe;launch_printing=printing;launch_started=GetTickCount64();launch_checked=0;enable();
        message(L"Команда отправлена BarTender. Проверяем появление окна…");
    }
    void prepare_job(){
        auto url=repair::normalize_public_url(utf8(text(public_edit)));auto rows=cards;
        auto expected=info.at("server_id").get<std::string>();auto endpoint=api;auto parent=profile_directory()/L"jobs";
        preferences();busy=true;enable();message(L"Проверяем сетевой адрес и готовим QR-коды…");
        try{task=std::async(std::launch::async,[url,rows,expected,endpoint,parent]{
            auto remote=repair::ApiClient(url).server_info();
            if(remote.at("server_id").get<std::string>()!=expected)throw repair::ApiError("По выбранному адресу отвечает другая база. Проверьте адрес ПК с сервером.");
            Json ids=Json::array();for(const auto& row:rows)ids.push_back(row.at("id"));
            auto current=repair::ApiClient(endpoint).resolve_cards(ids);
            auto updated=repair::ApiClient(endpoint).set_public_url(url);
            auto directory=repair::write_label_job(parent,repair::make_labels(current,url));
            return Json{{"directory",directory.u8string()},{"info",updated}};
        });}catch(...){busy=false;enable();throw;}
    }
    void poll(){
        poll_launch();
        if(!busy||!task.valid()||task.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready)return;
        try{auto result=task.get();busy=false;if(closing){done=true;return;}job=std::filesystem::u8path(result.at("directory").get<std::string>());info=result.at("info");message(L"Задание готово. Этикеток: "+std::to_wstring(cards.size())+L". Проверьте QR-ссылки или откройте макет и предпросмотр BarTender.");}
        catch(const std::exception& e){busy=false;if(closing){done=true;return;}error(e);message(L"Подготовка не завершена. Проверьте адрес, доступ по сети и сообщение об ошибке.");}
        enable();
    }
    void open_file(const std::filesystem::path& path){if(reinterpret_cast<INT_PTR>(ShellExecuteW(window,L"open",path.c_str(),nullptr,nullptr,SW_SHOWNORMAL))<=32)throw repair::ApiError("Не удалось открыть файл задания.");}
    void command(int id,int event){
        if(id==Close){if(busy){closing=true;message(L"Завершаем подготовку…");}else done=true;return;}
        if(busy||launch_process)return;
        if(id==Public&&(event==CBN_EDITCHANGE||event==CBN_SELCHANGE)){job.clear();enable();}
        else if(id==ChooseExe)browse(exe_edit,true);
        else if(id==ChooseBtw)browse(btw_edit,false);
        else if(id==Prepare)prepare_job();else if(id==Design)launch(false);else if(id==Print)launch(true);
        else if(id==CheckQr)open_file(job/L"preview.html");else if(id==Folder)open_file(job);
    }
};
LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wp,LPARAM lp){
    auto* state=reinterpret_cast<Dialog*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){state=static_cast<Dialog*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);state->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(state));}
    if(!state)return DefWindowProcW(window,message,wp,lp);
    try{switch(message){case WM_CREATE:state->initialize();return 0;case WM_COMMAND:state->command(LOWORD(wp),HIWORD(wp));return 0;case WM_TIMER:state->poll();return 0;case WM_CLOSE:state->command(Close,0);return 0;}}
    catch(const std::exception& e){if(message==WM_CREATE)return -1;state->error(e);}
    return DefWindowProcW(window,message,wp,lp);
}
}
void show_label_dialog(HWND owner,const std::string& api_base,const repair::Json& cards,const repair::Json& server_info){
    WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=procedure;cls.lpszClassName=L"RepairLabelDialog";
    cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
    RegisterClassW(&cls);Dialog state;state.owner=owner;state.api=api_base;state.cards=cards;state.info=server_info;
    HDC dc=GetDC(owner);auto dpi=GetDeviceCaps(dc,LOGPIXELSX);ReleaseDC(owner,dc);
    auto window=CreateWindowExW(WS_EX_DLGMODALFRAME,cls.lpszClassName,L"QR и печать через BarTender — 0.4.1",WS_CAPTION|WS_SYSMENU|WS_POPUP,
        CW_USEDEFAULT,CW_USEDEFAULT,MulDiv(850,dpi,96),MulDiv(620,dpi,96),owner,nullptr,cls.hInstance,&state);
    if(!window)throw repair::ApiError("Не удалось открыть окно печати.");
    EnableWindow(owner,FALSE);ShowWindow(window,SW_SHOW);MSG msg{};
    while(!state.done){int n=static_cast<int>(GetMessageW(&msg,nullptr,0,0));if(n<=0){if(n==0)PostQuitMessage(static_cast<int>(msg.wParam));break;}if(!IsDialogMessageW(window,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
    KillTimer(window,1);EnableWindow(owner,TRUE);DestroyWindow(window);SetActiveWindow(owner);
}
