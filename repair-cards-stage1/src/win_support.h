#pragma once
#include <windows.h>
#include <shlobj.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <atomic>
#include "json.hpp"
#include "ui_theme_win.h"

namespace repair::win {
inline std::wstring wide(const std::string& s){if(s.empty())return{};int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);if(!n)throw std::runtime_error("Invalid UTF-8");std::wstring out(n,L'\0');MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),out.data(),n);return out;}
inline std::string utf8(const std::wstring& s){if(s.empty())return{};int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);if(!n)throw std::runtime_error("Invalid Unicode");std::string out(n,'\0');WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),out.data(),n,nullptr,nullptr);return out;}
inline std::wstring text(HWND h){int n=GetWindowTextLengthW(h);std::wstring out(n+1,L'\0');GetWindowTextW(h,out.data(),n+1);out.resize(n);return out;}
inline void set_text(HWND h,const std::wstring& s){SetWindowTextW(h,s.c_str());}
inline HWND control(HWND parent,const wchar_t* type,const wchar_t* label,DWORD style,int id,HFONT font){
    auto result=CreateWindowExW(0,type,label,WS_CHILD|WS_VISIBLE|style,0,0,0,0,parent,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
    if(!result)throw std::runtime_error("Cannot create control");style_control(result);SendMessageW(result,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);return result;
}
inline std::filesystem::path executable_directory(){std::wstring path(32768,L'\0');auto n=GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));if(!n||n==path.size())throw std::runtime_error("Cannot resolve executable path");path.resize(n);return std::filesystem::path(path).parent_path();}
inline std::filesystem::path profile_directory(){PWSTR path=nullptr;if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&path)))throw std::runtime_error("Cannot access LocalAppData");auto out=std::filesystem::path(path)/L"RepairCards";CoTaskMemFree(path);std::filesystem::create_directories(out);return out;}
inline nlohmann::json load_profile(const std::string& name){auto path=profile_directory()/name;std::ifstream in(path);if(!in)return nlohmann::json::object();auto data=nlohmann::json::parse(in,nullptr,false);return data.is_object()?data:nlohmann::json::object();}
inline void save_profile(const std::string& name,const nlohmann::json& data){auto path=profile_directory()/name,tmp=path;static std::atomic<unsigned long> sequence{0};tmp+=L"."+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(sequence.fetch_add(1))+L".tmp";{std::ofstream out(tmp,std::ios::binary);auto value=data.dump(2);if(!out||!out.write(value.data(),static_cast<std::streamsize>(value.size())))throw std::runtime_error("Cannot write settings");}if(!MoveFileExW(tmp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot save settings");}
}
