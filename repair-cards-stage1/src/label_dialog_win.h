#pragma once
#include <windows.h>
#include "api_client.h"
void show_label_dialog(HWND owner,const std::string& api_base,const repair::Json& cards,const repair::Json& server_info);
