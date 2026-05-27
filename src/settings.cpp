#define _SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS
#include "settings.h"
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <shlobj.h>
#include <fstream>
#include <sstream>

using namespace winrt::Windows::Data::Json;
using namespace std;

AppSettings g_Settings;

std::wstring GetSettingsFilePath() {
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, path))) {
        wstring fullPath = wstring(path) + L"\\TaskbarMediaPlayer";
        CreateDirectoryW(fullPath.c_str(), NULL);
        return fullPath + L"\\config.json";
    }
    return L"config.json";
}

void LoadSettings() {
    wstring filePath = GetSettingsFilePath();
    ifstream file(filePath, ios::in | ios::binary);
    if (!file.is_open()) {
        // Usa os valores padrões definidos na declaração de AppSettings
        return;
    }

    stringstream buffer;
    buffer << file.rdbuf();
    string content = buffer.str();
    file.close();

    if (content.empty()) {
        return;
    }

    try {
        int wideLen = MultiByteToWideChar(CP_UTF8, 0, content.c_str(), (int)content.length(), NULL, 0);
        if (wideLen <= 0) return;
        wstring wideContent(wideLen, 0);
        MultiByteToWideChar(CP_UTF8, 0, content.c_str(), (int)content.length(), &wideContent[0], wideLen);
        
        JsonObject obj;
        if (JsonObject::TryParse(wideContent.c_str(), obj)) {
            if (obj.HasKey(L"width")) g_Settings.width = (int)obj.GetNamedNumber(L"width");
            if (obj.HasKey(L"height")) g_Settings.height = (int)obj.GetNamedNumber(L"height");
            if (obj.HasKey(L"fontSize")) g_Settings.fontSize = (int)obj.GetNamedNumber(L"fontSize");
            if (obj.HasKey(L"buttonScale")) g_Settings.buttonScale = obj.GetNamedNumber(L"buttonScale");
            if (obj.HasKey(L"hideFullscreen")) g_Settings.hideFullscreen = obj.GetNamedBoolean(L"hideFullscreen");
            if (obj.HasKey(L"idleTimeout")) g_Settings.idleTimeout = (int)obj.GetNamedNumber(L"idleTimeout");
            if (obj.HasKey(L"offsetX")) g_Settings.offsetX = (int)obj.GetNamedNumber(L"offsetX");
            if (obj.HasKey(L"offsetY")) g_Settings.offsetY = (int)obj.GetNamedNumber(L"offsetY");
            if (obj.HasKey(L"theme")) g_Settings.theme = (int)obj.GetNamedNumber(L"theme");
            if (obj.HasKey(L"customAccent")) g_Settings.customAccent = obj.GetNamedBoolean(L"customAccent");
            if (obj.HasKey(L"accentColor")) g_Settings.accentColor = (unsigned int)obj.GetNamedNumber(L"accentColor");
            if (obj.HasKey(L"uiLanguage")) g_Settings.uiLanguage = (int)obj.GetNamedNumber(L"uiLanguage");
            if (obj.HasKey(L"translationLanguage")) g_Settings.translationLanguage = (int)obj.GetNamedNumber(L"translationLanguage");
        }
    } catch (...) {
        // Ignora erros de parsing e mantém os padrões
    }
}

void SaveSettings() {
    wstring filePath = GetSettingsFilePath();
    
    JsonObject obj;
    obj.Insert(L"width", JsonValue::CreateNumberValue(g_Settings.width));
    obj.Insert(L"height", JsonValue::CreateNumberValue(g_Settings.height));
    obj.Insert(L"fontSize", JsonValue::CreateNumberValue(g_Settings.fontSize));
    obj.Insert(L"buttonScale", JsonValue::CreateNumberValue(g_Settings.buttonScale));
    obj.Insert(L"hideFullscreen", JsonValue::CreateBooleanValue(g_Settings.hideFullscreen));
    obj.Insert(L"idleTimeout", JsonValue::CreateNumberValue(g_Settings.idleTimeout));
    obj.Insert(L"offsetX", JsonValue::CreateNumberValue(g_Settings.offsetX));
    obj.Insert(L"offsetY", JsonValue::CreateNumberValue(g_Settings.offsetY));
    obj.Insert(L"theme", JsonValue::CreateNumberValue(g_Settings.theme));
    obj.Insert(L"customAccent", JsonValue::CreateBooleanValue(g_Settings.customAccent));
    obj.Insert(L"accentColor", JsonValue::CreateNumberValue(g_Settings.accentColor));
    obj.Insert(L"uiLanguage", JsonValue::CreateNumberValue(g_Settings.uiLanguage));
    obj.Insert(L"translationLanguage", JsonValue::CreateNumberValue(g_Settings.translationLanguage));
    
    wstring jsonStr = obj.Stringify().c_str();
    
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, jsonStr.c_str(), (int)jsonStr.length(), NULL, 0, NULL, NULL);
    if (utf8Len <= 0) return;
    string utf8Content(utf8Len, 0);
    WideCharToMultiByte(CP_UTF8, 0, jsonStr.c_str(), (int)jsonStr.length(), &utf8Content[0], utf8Len, NULL, NULL);
    
    ofstream file(filePath, ios::out | ios::binary | ios::trunc);
    if (file.is_open()) {
        file << utf8Content;
        file.close();
    }
}
