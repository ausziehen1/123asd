#include <windows.h>
#include <string>
#ifndef CLIENT_ROLE
#define CLIENT_ROLE 0
#endif
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int show){
 wchar_t path[32768]{};auto n=GetModuleFileNameW(nullptr,path,32768);if(!n||n>=32768)return 1;
 std::wstring folder(path,n);folder.resize(folder.find_last_of(L"\\/")+1);
 auto lock=CreateFileW((folder+L".clients.lock").c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_HIDDEN,nullptr);
 if(lock==INVALID_HANDLE_VALUE){MessageBoxW(nullptr,L"Папка недоступна или выполняется обновление. Дождитесь завершения обновления и проверьте права на папку.",L"Repair Cards",MB_OK|MB_ICONWARNING);return 1;}
 if(GetFileAttributesW((folder+L".updates\\pending.json").c_str())!=INVALID_FILE_ATTRIBUTES){MessageBoxW(nullptr,L"Предыдущее обновление прервано. Запустите Updater.exe для восстановления файлов.",L"Repair Cards",MB_OK|MB_ICONWARNING);CloseHandle(lock);return 1;}
 auto dll=LoadLibraryExW((folder+L"RepairClient.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
 if(!dll){MessageBoxW(nullptr,L"Не удалось загрузить RepairClient.dll. Распакуйте полный комплект клиента и запустите EXE из его папки.",L"Repair Cards",MB_OK|MB_ICONERROR);CloseHandle(lock);return 1;}
 auto abi=reinterpret_cast<unsigned(*)()>(GetProcAddress(dll,"RepairClientABI"));auto run=reinterpret_cast<int(*)(unsigned,int)>(GetProcAddress(dll,"RepairClientRun"));
 if(!abi||abi()!=1||!run){MessageBoxW(nullptr,L"Несовместимая версия RepairClient.dll. Установите полный комплект одной версии.",L"Repair Cards",MB_OK|MB_ICONERROR);FreeLibrary(dll);CloseHandle(lock);return 1;}
 auto result=run(CLIENT_ROLE,show);CloseHandle(lock);return result;
}
