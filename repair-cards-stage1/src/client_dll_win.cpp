#include <windows.h>
#include <objbase.h>
int run_creator(HINSTANCE,int);
int run_scanner(HINSTANCE,int,bool);
extern "C" __declspec(dllexport) unsigned RepairClientABI(){return 1;}
extern "C" __declspec(dllexport) int RepairClientRun(unsigned role,int show){
 auto result=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(result)){MessageBoxW(nullptr,L"Не удалось инициализировать COM.",L"Repair Cards",MB_OK|MB_ICONERROR);return 1;}
 int status=role==0?run_creator(GetModuleHandleW(nullptr),show):run_scanner(GetModuleHandleW(nullptr),show,role==2);CoUninitialize();return status;
}
