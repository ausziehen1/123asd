#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include "api_client.h"
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <vector>

using repair::Json;
namespace {
std::wstring wide(const std::string& value) {
    if (value.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (!n) throw std::runtime_error("Invalid UTF-8");
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), out.data(), n);
    return out;
}
std::string utf8(const std::wstring& value) {
    if (value.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (!n) throw std::runtime_error("Invalid Unicode");
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), out.data(), n, nullptr, nullptr);
    return out;
}
std::wstring text(HWND control) {
    int n = GetWindowTextLengthW(control);
    std::wstring result(static_cast<size_t>(n) + 1, L'\0');
    GetWindowTextW(control, result.data(), n + 1);
    result.resize(n);
    return result;
}
void set_text(HWND control, const std::wstring& value) { SetWindowTextW(control, value.c_str()); }
HWND control(HWND parent, const wchar_t* kind, const wchar_t* label, DWORD style, int id, HFONT font) {
    HWND result = CreateWindowExW(0, kind, label, WS_CHILD | WS_VISIBLE | style,
        0, 0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    if (!result) throw std::runtime_error("Cannot create a Windows control");
    SendMessageW(result, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return result;
}
struct NamePrompt {
    HWND edit = nullptr;
    HFONT font = nullptr;
    bool done = false, accepted = false;
    int scale = 96;
    std::wstring value;
};
LRESULT CALLBACK prompt_proc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    auto* p = reinterpret_cast<NamePrompt*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        p = static_cast<NamePrompt*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(p));
    }
    if (!p) return DefWindowProcW(window, message, wp, lp);
    auto unit = [p](int value) { return MulDiv(value, p->scale, 96); };
    if (message == WM_CREATE) {
        auto label = control(window, L"STATIC", L"Название типа платформы:", 0, -1, p->font);
        MoveWindow(label, unit(16), unit(16), unit(380), unit(24), TRUE);
        p->edit = control(window, L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 10, p->font);
        MoveWindow(p->edit, unit(16), unit(44), unit(380), unit(28), TRUE);
        SendMessageW(p->edit, EM_SETLIMITTEXT, 80, 0);
        auto add = control(window, L"BUTTON", L"Добавить", WS_TABSTOP | BS_DEFPUSHBUTTON, IDOK, p->font);
        auto cancel = control(window, L"BUTTON", L"Отмена", WS_TABSTOP, IDCANCEL, p->font);
        MoveWindow(add, unit(166), unit(92), unit(110), unit(30), TRUE);
        MoveWindow(cancel, unit(286), unit(92), unit(110), unit(30), TRUE);
        return 0;
    }
    if (message == WM_COMMAND && LOWORD(wp) == IDOK) {
        auto value = text(p->edit);
        auto first = value.find_first_not_of(L" \t\r\n");
        if (first == std::wstring::npos) {
            MessageBoxW(window, L"Введите название платформы.", L"Тип платформы", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        p->value = value.substr(first, value.find_last_not_of(L" \t\r\n") - first + 1);
        p->accepted = true; p->done = true;
        return 0;
    }
    if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(wp) == IDCANCEL)) {
        p->done = true; return 0;
    }
    return DefWindowProcW(window, message, wp, lp);
}
std::wstring ask_platform(HWND owner, HFONT font, int dpi) {
    NamePrompt state; state.font = font; state.scale = dpi;
    RECT parent{}; GetWindowRect(owner, &parent);
    int width = MulDiv(430, dpi, 96), height = MulDiv(175, dpi, 96);
    HWND window = CreateWindowExW(WS_EX_DLGMODALFRAME, L"RepairPlatformPrompt", L"Добавить тип платформы",
        WS_CAPTION | WS_SYSMENU | WS_POPUP, parent.left + (parent.right - parent.left - width) / 2,
        parent.top + (parent.bottom - parent.top - height) / 2, width, height, owner, nullptr,
        GetModuleHandleW(nullptr), &state);
    if (!window) throw std::runtime_error("Cannot open platform dialog");
    EnableWindow(owner, FALSE);
    ShowWindow(window, SW_SHOW); SetFocus(state.edit);
    MSG message{};
    while (!state.done) {
        int result = static_cast<int>(GetMessageW(&message, nullptr, 0, 0));
        if (result <= 0) {
            if (result == 0) PostQuitMessage(static_cast<int>(message.wParam));
            break;
        }
        if (message.message == WM_KEYDOWN && message.wParam == VK_RETURN) {
            SendMessageW(window, WM_COMMAND, IDOK, 0); continue;
        }
        if (message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE) {
            SendMessageW(window, WM_COMMAND, IDCANCEL, 0); continue;
        }
        if (!IsDialogMessageW(window, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    EnableWindow(owner, TRUE); DestroyWindow(window); SetActiveWindow(owner);
    return state.accepted ? state.value : std::wstring();
}

enum Id { Server = 100, Connect, Platform, Add, First, Count, Preview, Register, List,
          Open, Refresh, Previous, Next };
enum class Job { Connect, Add, Cards, Preview, ConfirmRegistration, Register };
struct Batch {
    std::string base, first, platform_name;
    long long platform = 0;
    int count = 0;
};
class App {
public:
    HWND window = nullptr;
    HFONT font = nullptr;
    int dpi = 96;
    bool busy = false, closing = false;
    HWND server{}, connect{}, platform{}, add{}, first{}, count{}, preview{}, create{}, list{},
         open{}, refresh{}, previous{}, next{}, status{}, range{}, note{};
    HWND server_label{}, platform_label{}, first_label{}, count_label{}, list_label{};
    std::string connected_base;
    Json platforms = Json::array(), cards = Json::array();
    long long current_after = 0;
    std::vector<long long> history;
    std::future<Json> future;
    Job job = Job::Connect;
    Batch pending;

    ~App() { if (font) DeleteObject(font); }
    int unit(int value) const { return MulDiv(value, dpi, 96); }
    void move(HWND hwnd, int x, int y, int w, int h) {
        MoveWindow(hwnd, unit(x), unit(y), unit(w), unit(h), TRUE);
    }
    void initialize() {
        HDC screen = GetDC(window); dpi = GetDeviceCaps(screen, LOGPIXELSX); ReleaseDC(window, screen);
        font = CreateFontW(-MulDiv(10, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        server_label = control(window, L"STATIC", L"Сервер:", 0, -1, font);
        server = control(window, L"EDIT", L"http://127.0.0.1:8080", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, Server, font);
        connect = control(window, L"BUTTON", L"Подключить", WS_TABSTOP, Connect, font);
        platform_label = control(window, L"STATIC", L"Тип платформы:", 0, -1, font);
        platform = control(window, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, Platform, font);
        add = control(window, L"BUTTON", L"+", WS_TABSTOP, Add, font);
        first_label = control(window, L"STATIC", L"Начальный ID:", 0, -1, font);
        first = control(window, L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, First, font);
        SendMessageW(first, EM_SETLIMITTEXT, 128, 0);
        count_label = control(window, L"STATIC", L"Количество:", 0, -1, font);
        count = control(window, L"EDIT", L"1", WS_BORDER | WS_TABSTOP | ES_NUMBER, Count, font);
        SendMessageW(count, EM_SETLIMITTEXT, 4, 0);
        preview = control(window, L"BUTTON", L"Проверить диапазон", WS_TABSTOP, Preview, font);
        create = control(window, L"BUTTON", L"Создать партию", WS_TABSTOP | BS_DEFPUSHBUTTON, Register, font);
        range = control(window, L"STATIC", L"", 0, -1, font);
        list_label = control(window, L"STATIC", L"Зарегистрированные карточки", 0, -1, font);
        list = control(window, WC_LISTVIEWW, L"", WS_BORDER | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, List, font);
        ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
        const wchar_t* names[] = {L"Внутренний ID", L"ID продукции", L"Тип платформы"};
        for (int i = 0; i < 3; ++i) {
            LVCOLUMNW column{}; column.mask = LVCF_TEXT | LVCF_WIDTH;
            column.pszText = const_cast<wchar_t*>(names[i]); column.cx = unit(i == 0 ? 140 : 220);
            ListView_InsertColumn(list, i, &column);
        }
        open = control(window, L"BUTTON", L"Открыть бланк", WS_TABSTOP, Open, font);
        refresh = control(window, L"BUTTON", L"Обновить", WS_TABSTOP, Refresh, font);
        previous = control(window, L"BUTTON", L"Назад", WS_TABSTOP, Previous, font);
        next = control(window, L"BUTTON", L"Следующие 100", WS_TABSTOP, Next, font);
        status = control(window, L"STATIC", L"Запустите сервер и нажмите «Подключить».", 0, -1, font);
        note = control(window, L"STATIC", L"Этап 2.1: регистрация партий. Редактор бланка и печать QR — следующие шаги.", 0, -1, font);
        SetTimer(window, 1, 60, nullptr);
        fill_platforms(0); layout(); enable_controls();
    }
    void layout() {
        RECT rect{}; GetClientRect(window, &rect);
        int w = MulDiv(rect.right, 96, dpi), h = MulDiv(rect.bottom, 96, dpi);
        move(server_label, 16, 23, 90, 24);
        move(server, 160, 18, w - 330, 30); move(connect, w - 154, 18, 138, 30);
        move(platform_label, 16, 73, 140, 24);
        move(platform, 160, 66, w - 224, 300); move(add, w - 54, 66, 38, 30);
        move(first_label, 16, 121, 140, 24); move(first, 160, 114, 230, 30);
        move(count_label, 406, 121, 110, 24); move(count, 518, 114, 70, 30);
        move(preview, w - 204, 114, 188, 30);
        move(create, 160, 162, 230, 34); move(range, 406, 165, w - 422, 48);
        move(list_label, 16, 220, w - 32, 24);
        move(list, 16, 250, w - 32, h - 382);
        ListView_SetColumnWidth(list, 2, unit(w - 414));
        move(open, 16, h - 116, 170, 32); move(refresh, 198, h - 116, 112, 32);
        move(previous, w - 272, h - 116, 100, 32); move(next, w - 160, h - 116, 144, 32);
        move(status, 16, h - 70, w - 32, 28); move(note, 16, h - 34, w - 32, 24);
    }
    long long selected_platform() const {
        auto index = SendMessageW(platform, CB_GETCURSEL, 0, 0);
        return index > 0 && static_cast<size_t>(index) <= platforms.size()
            ? platforms.at(static_cast<size_t>(index) - 1).at("id").get<long long>() : 0;
    }
    void fill_platforms(long long selected) {
        SendMessageW(platform, CB_RESETCONTENT, 0, 0);
        SendMessageW(platform, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Все типы (просмотр)"));
        int selected_index = 0;
        for (size_t i = 0; i < platforms.size(); ++i) {
            auto name = wide(platforms.at(i).at("name").get<std::string>());
            SendMessageW(platform, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
            if (platforms.at(i).at("id").get<long long>() == selected) selected_index = static_cast<int>(i) + 1;
        }
        SendMessageW(platform, CB_SETCURSEL, selected_index, 0);
        suggest_id();
    }
    void suggest_id() {
        auto selected = selected_platform();
        std::string suggestion;
        for (const auto& p : platforms)
            if (p.at("id").get<long long>() == selected) suggestion = p.value("next_product_id", "");
        set_text(first, wide(suggestion)); set_text(range, L"");
    }
    void render_cards(const Json& rows) {
        cards = rows;
        ListView_DeleteAllItems(list);
        for (size_t i = 0; i < cards.size(); ++i) {
            const auto& card = cards.at(i);
            auto id = std::to_wstring(card.at("id").get<long long>());
            LVITEMW item{}; item.mask = LVIF_TEXT; item.iItem = static_cast<int>(i); item.pszText = id.data();
            ListView_InsertItem(list, &item);
            auto product = wide(card.at("product_id").get<std::string>());
            auto name = wide(card.at("platform_name").get<std::string>());
            ListView_SetItemText(list, static_cast<int>(i), 1, product.data());
            ListView_SetItemText(list, static_cast<int>(i), 2, name.data());
        }
        set_text(status, L"Подключено. Показано карточек: " + std::to_wstring(cards.size()) + L".");
        enable_controls();
    }
    void enable_controls() {
        bool connected = !connected_base.empty();
        for (auto hwnd : {server, connect}) EnableWindow(hwnd, !busy);
        for (auto hwnd : {platform, add, refresh}) EnableWindow(hwnd, !busy && connected);
        for (auto hwnd : {first, count, preview, create}) EnableWindow(hwnd, !busy && connected && selected_platform() > 0);
        EnableWindow(open, !busy && ListView_GetNextItem(list, -1, LVNI_SELECTED) >= 0);
        EnableWindow(previous, !busy && !history.empty());
        EnableWindow(next, !busy && cards.size() == 100);
    }
    std::string base() const {
        auto result = repair::normalize_server_url(utf8(text(server)));
        if (result != connected_base) throw repair::ApiError("Нажмите «Подключить» для выбранного адреса сервера.");
        return result;
    }
    void start(Job kind, std::function<Json()> action) {
        busy = true; job = kind; enable_controls(); set_text(status, L"Выполняется запрос к серверу…");
        try { future = std::async(std::launch::async, std::move(action)); }
        catch (...) { busy = false; enable_controls(); throw; }
    }
    void load_cards(long long after) {
        auto url = base(); auto id = selected_platform();
        start(Job::Cards, [url, id, after] {
            repair::ApiClient api(url);
            auto types = api.platforms();
            return Json{{"cards", api.cards(id, after)}, {"platforms", types}, {"after", after}};
        });
    }
    Batch read_batch() const {
        Batch b; b.base = base(); b.platform = selected_platform(); b.first = utf8(text(first));
        if (b.platform <= 0) throw repair::ApiError("Выберите тип платформы.");
        for (const auto& p : platforms) if (p.at("id").get<long long>() == b.platform) b.platform_name = p.at("name").get<std::string>();
        auto number = text(count);
        if (number.empty() || number.size() > 4 || number.find_first_not_of(L"0123456789") != std::wstring::npos)
            throw repair::ApiError("Количество должно быть целым числом от 1 до 1000.");
        b.count = std::stoi(number);
        if (b.count < 1 || b.count > 1000) throw repair::ApiError("Количество должно быть от 1 до 1000.");
        return b;
    }
    void open_card() {
        auto url = base(); int index = ListView_GetNextItem(list, -1, LVNI_SELECTED);
        if (index < 0) return;
        auto link = wide(url + "/cards/" + std::to_string(cards.at(index).at("id").get<long long>()));
        auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(window, L"open", link.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
        if (result <= 32) throw repair::ApiError("Не удалось открыть браузер.");
    }
    void command(int id, int event) {
        if (busy) return;
        if (id == Connect) {
            auto url = repair::normalize_server_url(utf8(text(server)));
            start(Job::Connect, [url] {
                repair::ApiClient api(url); api.health();
                return Json{{"base", url}, {"platforms", api.platforms()}, {"cards", api.cards(0)}};
            });
        } else if (id == Add) {
            auto url = base(); auto name = ask_platform(window, font, dpi);
            if (name.empty()) return;
            start(Job::Add, [url, name] { return repair::ApiClient(url).add_platform(utf8(name)); });
        } else if (id == Platform && event == CBN_SELCHANGE) {
            suggest_id(); current_after = 0; history.clear(); render_cards(Json::array()); load_cards(0);
        } else if (id == Preview || id == Register) {
            pending = read_batch(); auto snapshot = pending;
            start(id == Preview ? Job::Preview : Job::ConfirmRegistration, [snapshot] {
                return repair::ApiClient(snapshot.base).preview(snapshot.first, snapshot.count);
            });
        } else if (id == Refresh) load_cards(0);
        else if (id == Next && !cards.empty()) load_cards(cards.back().at("id").get<long long>());
        else if (id == Previous && !history.empty()) load_cards(history.back());
        else if (id == Open) open_card();
    }
    void finish(Job kind, const Json& result) {
        if (kind == Job::Connect) {
            connected_base = result.at("base").get<std::string>();
            platforms = result.at("platforms"); fill_platforms(0); current_after = 0; history.clear();
            render_cards(result.at("cards"));
        } else if (kind == Job::Add) {
            auto id = result.at("id").get<long long>(); bool found = false;
            for (const auto& p : platforms) if (p.at("id").get<long long>() == id) found = true;
            if (!found) platforms.push_back(result);
            fill_platforms(id); current_after = 0; history.clear(); load_cards(0);
        } else if (kind == Job::Cards) {
            auto selected = selected_platform();
            auto entered = text(first);
            std::wstring old_suggestion;
            for (const auto& p : platforms)
                if (p.at("id").get<long long>() == selected) old_suggestion = wide(p.value("next_product_id", ""));
            bool preserve_manual = !entered.empty() && entered != old_suggestion;
            platforms = result.at("platforms"); fill_platforms(selected);
            if (preserve_manual) set_text(first, entered);
            auto after = result.at("after").get<long long>();
            if (after == 0) history.clear();
            else if (after > current_after) history.push_back(current_after);
            else if (!history.empty()) history.pop_back();
            current_after = after; render_cards(result.at("cards"));
        } else if (kind == Job::Preview || kind == Job::ConfirmRegistration) {
            auto description = L"Карточек: " + std::to_wstring(pending.count) + L". " +
                wide(result.at("first_product_id").get<std::string>()) + L" — " + wide(result.at("last_product_id").get<std::string>());
            set_text(range, description); set_text(status, L"Диапазон проверен. Записи еще не созданы.");
            if (kind == Job::ConfirmRegistration) {
                auto message = L"Тип платформы: " + wide(pending.platform_name) + L"\n" + description + L"\n\nСоздать эту партию?";
                if (MessageBoxW(window, message.c_str(), L"Регистрация партии", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES) {
                    auto snapshot = pending;
                    start(Job::Register, [snapshot] {
                        return repair::ApiClient(snapshot.base).register_batch(snapshot.platform, snapshot.first, snapshot.count);
                    });
                }
            }
        } else if (kind == Job::Register) {
            auto next_id = result.at("next_product_id").get<std::string>();
            for (auto& p : platforms) if (p.at("id").get<long long>() == pending.platform) p["next_product_id"] = next_id;
            set_text(first, wide(next_id));
            auto message = L"Создано карточек: " + std::to_wstring(result.at("count").get<int>()) + L".\n" +
                wide(result.at("first_product_id").get<std::string>()) + L" — " + wide(result.at("last_product_id").get<std::string>()) +
                L"\nСледующий ID: " + wide(next_id);
            set_text(range, L"Партия зарегистрирована.");
            MessageBoxW(window, message.c_str(), L"Партия сохранена", MB_OK | MB_ICONINFORMATION);
            load_cards(0);
        }
    }
    void poll() {
        if (!busy || !future.valid() || future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) return;
        auto kind = job;
        try {
            auto result = future.get(); busy = false;
            if (closing) { DestroyWindow(window); return; }
            enable_controls(); finish(kind, result); enable_controls();
        } catch (const std::exception& e) {
            busy = false;
            if (closing) { DestroyWindow(window); return; }
            show_error(e.what()); enable_controls();
        }
    }
    void show_error(const std::string& message) {
        auto value = wide(message);
        set_text(status, L"Запрос не выполнен. См. сообщение об ошибке.");
        MessageBoxW(window, value.c_str(), L"Креатор", MB_OK | MB_ICONWARNING);
    }
};
LRESULT CALLBACK app_proc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        app = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        app->window = window; SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    if (!app) return DefWindowProcW(window, message, wp, lp);
    try {
        switch (message) {
        case WM_CREATE: app->initialize(); return 0;
        case WM_SIZE: if (app->font) app->layout(); return 0;
        case WM_GETMINMAXINFO: {
            auto info = reinterpret_cast<MINMAXINFO*>(lp);
            info->ptMinTrackSize = {app->unit(900), app->unit(620)}; return 0;
        }
        case WM_COMMAND: app->command(LOWORD(wp), HIWORD(wp)); return 0;
        case WM_TIMER: app->poll(); return 0;
        case WM_NOTIFY: {
            auto notification = reinterpret_cast<NMHDR*>(lp);
            if (notification->idFrom == List) {
                if (notification->code == NM_DBLCLK && !app->busy) app->open_card();
                if (notification->code == LVN_ITEMCHANGED) app->enable_controls();
            }
            return 0;
        }
        case WM_CLOSE:
            if (app->busy) {
                app->closing = true;
                set_text(app->status, L"Завершаем текущий запрос и закрываем окно…");
            } else DestroyWindow(window);
            return 0;
        case WM_DESTROY: KillTimer(window, 1); PostQuitMessage(0); return 0;
        }
    } catch (const std::exception& e) {
        if (message == WM_CREATE) return -1;
        app->show_error(e.what());
        return 0;
    }
    return DefWindowProcW(window, message, wp, lp);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    SetProcessDPIAware();
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&controls);
    WNDCLASSW main_class{}; main_class.lpfnWndProc = app_proc; main_class.hInstance = instance;
    main_class.lpszClassName = L"RepairCreator"; main_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    main_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (!RegisterClassW(&main_class)) return 1;
    auto prompt_class = main_class; prompt_class.lpfnWndProc = prompt_proc; prompt_class.lpszClassName = L"RepairPlatformPrompt";
    if (!RegisterClassW(&prompt_class)) return 1;
    App app;
    HDC dc = GetDC(nullptr); int dpi = GetDeviceCaps(dc, LOGPIXELSX); ReleaseDC(nullptr, dc);
    HWND window = CreateWindowExW(0, L"RepairCreator", L"Креатор — Карточки ремонта (этап 2.1)",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, MulDiv(1050, dpi, 96), MulDiv(730, dpi, 96),
        nullptr, nullptr, instance, &app);
    if (!window) { MessageBoxW(nullptr, L"Не удалось открыть окно Креатора.", L"Креатор", MB_OK | MB_ICONERROR); return 1; }
    ShowWindow(window, show); UpdateWindow(window);
    MSG message{};
    int result;
    while ((result = static_cast<int>(GetMessageW(&message, nullptr, 0, 0))) > 0) {
        if (!IsDialogMessageW(window, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    return result < 0 ? 1 : static_cast<int>(message.wParam);
}
