#include "tray.h"
#include <shellapi.h>
#include "settings.h"

NOTIFYICONDATAW g_nid = { 0 };

bool GetStartupState() {
    HKEY hKey;
    LONG result = RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey);
    if (result == ERROR_SUCCESS) {
        wchar_t path[MAX_PATH];
        DWORD size = sizeof(path);
        result = RegQueryValueExW(hKey, L"TaskbarMediaPlayer", NULL, NULL, (LPBYTE)path, &size);
        RegCloseKey(hKey);
        return (result == ERROR_SUCCESS);
    }
    return false;
}

void ToggleStartupState() {
    HKEY hKey;
    LONG result = RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey);
    if (result == ERROR_SUCCESS) {
        if (GetStartupState()) {
            RegDeleteValueW(hKey, L"TaskbarMediaPlayer");
        } else {
            wchar_t path[MAX_PATH];
            GetModuleFileNameW(NULL, path, MAX_PATH);
            RegSetValueExW(hKey, L"TaskbarMediaPlayer", 0, REG_SZ, (BYTE*)path, (DWORD)(wcslen(path) + 1) * sizeof(wchar_t));
        }
        RegCloseKey(hKey);
    }
}

void CreateTrayIcon(HWND hwnd) {
    g_nid.cbSize = sizeof(NOTIFYICONDATAW);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    
    // Tenta carregar o ícone padrão do aplicativo (IDI_APPLICATION)
    g_nid.hIcon = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(32512)); // 32512 é IDI_APPLICATION
    if (!g_nid.hIcon) {
        g_nid.hIcon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
    }
    
    wcscpy_s(g_nid.szTip, L"Taskbar Media Player");
    
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

void RemoveTrayIcon() {
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
}

void ShowTrayMenu(HWND hwnd, POINT pt) {
    HMENU hMenu = CreatePopupMenu();
    if (hMenu) {
        InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_STRING, ID_TRAY_PLAYPAUSE, L"Reproduzir / Pausar");
        InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_STRING, ID_TRAY_PREV, L"Música Anterior");
        InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_STRING, ID_TRAY_NEXT, L"Próxima Música");
        InsertMenuW(hMenu, -1, MF_SEPARATOR, 0, NULL);
        
        InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_STRING, ID_TRAY_SETTINGS, L"Configurações (config.json)");
        
        UINT startupFlags = MF_BYPOSITION | MF_STRING;
        if (GetStartupState()) {
            startupFlags |= MF_CHECKED;
        }
        InsertMenuW(hMenu, -1, startupFlags, ID_TRAY_STARTUP, L"Iniciar com o Windows");
        
        InsertMenuW(hMenu, -1, MF_SEPARATOR, 0, NULL);
        InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_STRING, ID_TRAY_EXIT, L"Sair");
        
        SetForegroundWindow(hwnd);
        TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, NULL);
        DestroyMenu(hMenu);
    }
}
