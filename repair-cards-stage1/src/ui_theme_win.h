#pragma once
#include <windows.h>
void style_control(HWND control);
bool theme_message(HWND window,UINT message,WPARAM w,LPARAM l,LRESULT& result);
HBRUSH theme_background();
HFONT theme_font(int dpi);
void apply_app_icon(HWND window);

void install_update_menu(HWND window);
