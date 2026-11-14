#pragma once
#include <windows.h>
#include <string>
bool open_card_viewer(HWND owner,const std::string& url,bool editable);
bool close_card_viewers(HWND owner);
