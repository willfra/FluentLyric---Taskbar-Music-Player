#pragma once
#include <windows.h>

#define WM_TRAYICON (WM_APP + 20)
#define ID_TRAY_EXIT 30001
#define ID_TRAY_SETTINGS 30002
#define ID_TRAY_PLAYPAUSE 30003
#define ID_TRAY_NEXT 30004
#define ID_TRAY_PREV 30005
#define ID_TRAY_STARTUP 30006

void CreateTrayIcon(HWND hwnd);
void RemoveTrayIcon();
void ShowTrayMenu(HWND hwnd, POINT pt);
bool GetStartupState();
void ToggleStartupState();
