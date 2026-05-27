#pragma once
#include <string>

struct AppSettings {
  int width = 300;
  int height = 46;
  int fontSize = 11;
  double buttonScale = 1.0;
  bool hideFullscreen = false;
  int idleTimeout = 0;
  int offsetX = 12;
  int offsetY = 0;
  int theme = 0; // 0 = System, 1 = Light, 2 = Dark
  bool customAccent = false;
  unsigned int accentColor = 0xFF0078D7;
  int uiLanguage = 0;          // 0 = Auto, 1 = PT, 2 = EN, 3 = ES, 4 = FR, 5 = DE, 6 = IT
  int translationLanguage = 0; // 0 = Auto, 1 = PT, 2 = EN, 3 = ES, 4 = FR, 5 = DE, 6 = IT
};

extern AppSettings g_Settings;

void LoadSettings();
void SaveSettings();
std::wstring GetSettingsFilePath();
