// ==WindhawkMod==
// @id              taskbar-music-player-native
// @name            Taskbar Music Player Native
// @description     Native transparent music ticker with media controls.
// @version         5.0
// @author          willfra
// @github          https://github.com/willf/taskbar-music-player-native
// @include         explorer.exe
// @compilerOptions -lole32 -ldwmapi -lgdi32 -luser32 -lwindowsapp -lshcore
// -lgdiplus -lshell32 -lwinhttp
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Taskbar Music Player Native

A media controller that uses Windows 11 native DWM styling for a seamless,
transparent look.

## ✨ Features
* **Universal Support:** Smart scanning detects active playback from any app,
not just the "focused" one.
* **Album Art:** Displays current track cover art.
* **Fullscreen Mode:** Hides automatically when running full-screen
applications.
* **Fully Transparent:** Zero-background footprint that blends natively with
your taskbar.
* **Outline Icon Controls:** Elegant line-style media controls.
* **Theme Sync:** Automatically shifts colors between light and dark Windows
themes.
* **Controls:** Play/Pause, Next, Previous.
* **Volume:** Scroll over widget to adjust volume.
*/
// ==/WindhawkModReadme==

#include <audiopolicy.h>
#include <cstdio>
#include <dwmapi.h>
#include <endpointvolume.h>
#include <gdiplus.h>
#include <iomanip>
#include <mmdeviceapi.h>
#include <mutex>
#include <shcore.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <sstream>
#include <string>
#include <thread>
#include <windows.h>
#include <winhttp.h>

// WinRT
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>

using namespace Gdiplus;
using namespace std;
using namespace winrt;
using namespace Windows::Media::Control;
using namespace Windows::Storage::Streams;

struct winrt_apartment_scope {
  winrt_apartment_scope() { winrt::init_apartment(); }
  ~winrt_apartment_scope() { winrt::uninit_apartment(); }
};

// --- Constants ---
const WCHAR *FONT_NAME = L"Segoe UI Variable Text";

#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWA_COLOR_DEFAULT
#define DWMWA_COLOR_DEFAULT 0xFFFFFFFF
#endif
#ifndef DWMWA_COLOR_NONE
#define DWMWA_COLOR_NONE 0xFFFFFFFE
#endif
#ifndef DWMWA_NCRENDERING_POLICY
#define DWMWA_NCRENDERING_POLICY 2
#endif
#ifndef DWMNCRP_DISABLED
#define DWMNCRP_DISABLED 1
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_DONOTROUND
#define DWMWCP_DONOTROUND 1
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif
#ifndef DWMNCRP_ENABLED
#define DWMNCRP_ENABLED 2
#endif

#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

// --- DWM API ---
typedef enum _WINDOWCOMPOSITIONATTRIB {
  WCA_ACCENT_POLICY = 19
} WINDOWCOMPOSITIONATTRIB;
typedef enum _ACCENT_STATE {
  ACCENT_DISABLED = 0,
  ACCENT_ENABLE_BLURBEHIND = 3,
  ACCENT_ENABLE_ACRYLICBLURBEHIND = 4,
  ACCENT_INVALID_STATE = 5
} ACCENT_STATE;
typedef struct _ACCENT_POLICY {
  ACCENT_STATE AccentState;
  DWORD AccentFlags;
  DWORD GradientColor;
  DWORD AnimationId;
} ACCENT_POLICY;
typedef struct _WINDOWCOMPOSITIONATTRIBDATA {
  WINDOWCOMPOSITIONATTRIB Attribute;
  PVOID Data;
  SIZE_T SizeOfData;
} WINDOWCOMPOSITIONATTRIBDATA;
typedef BOOL(WINAPI *pSetWindowCompositionAttribute)(
    HWND, WINDOWCOMPOSITIONATTRIBDATA *);

// --- Z-Band API ---
enum ZBID {
  ZBID_DEFAULT = 0,
  ZBID_DESKTOP = 1,
  ZBID_UIACCESS = 2,
  ZBID_IMMERSIVE_IHM = 3,
  ZBID_IMMERSIVE_NOTIFICATION = 4,
  ZBID_IMMERSIVE_APPCHROME = 5,
  ZBID_IMMERSIVE_MOGO = 6,
  ZBID_IMMERSIVE_EDGY = 7,
  ZBID_IMMERSIVE_INACTIVEMOBODY = 8,
  ZBID_IMMERSIVE_INACTIVEDOCK = 9,
  ZBID_IMMERSIVE_ACTIVEMOBODY = 10,
  ZBID_IMMERSIVE_ACTIVEDOCK = 11,
  ZBID_IMMERSIVE_BACKGROUND = 12,
  ZBID_IMMERSIVE_SEARCH = 13,
  ZBID_GENUINE_WINDOWS = 14,
  ZBID_IMMERSIVE_RESTRICTED = 15,
  ZBID_SYSTEM_TOOLS = 16,
  ZBID_LOCK = 17,
  ZBID_ABOVELOCK_UX = 18,
};

typedef HWND(WINAPI *pCreateWindowInBand)(DWORD dwExStyle, LPCWSTR lpClassName,
                                          LPCWSTR lpWindowName, DWORD dwStyle,
                                          int x, int y, int nWidth, int nHeight,
                                          HWND hWndParent, HMENU hMenu,
                                          HINSTANCE hInstance, LPVOID lpParam,
                                          DWORD dwBand);

// --- Configurable State ---
struct ModSettings {
  int width = 300;
  int height = 46;
  int fontSize = 11;
  double buttonScale = 1.0;
  bool hideFullscreen = false;
  int idleTimeout = 0;
  int offsetX = 12;
  int offsetY = 0;
} g_Settings;

// --- Global State ---
HWND g_hMediaWindow = NULL;
HWND g_hFlyoutWindow = NULL;
HWND g_hFlyoutChildWindow = NULL;
bool g_Running = true;
int g_HoverState = 0;
HWINEVENTHOOK g_TaskbarHook = nullptr;
UINT g_TaskbarCreatedMsg = RegisterWindowMessage(L"TaskbarCreated");

// Flyout Animation & State
int g_FlyoutState = 0; // 0 = Closed, 1 = Opening, 2 = Open, 3 = Closing
int g_FlyoutAlpha = 0;
int g_FlyoutYOffset = 0;
int g_FlyoutTargetX = 0;
int g_FlyoutTargetY = 0;
int g_FlyoutMouseOutSeconds = 0;

// Lyrics State
struct LyricLine {
  int timeSeconds;
  wstring text;
  wstring translatedText;
};
bool g_ShowLyrics = true; // Letras sempre ativadas por padrão agora
bool g_FlyoutPinned = false;
bool g_HasLyrics = false;
bool g_IsFetchingLyrics = false;
bool g_TranslateLyrics = false;
bool g_IsTranslating = false;
float g_LyricsScrollY = 0.0f;
wstring g_CurrentLyricsTitle = L"";
wstring g_CurrentLyricsArtist = L"";
wstring g_PlainLyrics = L"";
wstring g_LyricsSource = L"lrclib.net";
vector<LyricLine> g_SyncedLyrics;
mutex g_LyricsMutex;
float g_FlyoutBaseHeight = 150.0f;
float g_FlyoutTargetHeight = 400.0f;
float g_FlyoutExpandedHeight = 400.0f;
float g_FlyoutCurrentHeight = 400.0f;

// Variáveis para animação unificada baseada em progresso (Lerp Puro)
float g_FlyoutOpenProgress = 0.0f;   // 0.0f = fechado, 1.0f = aberto
float g_LyricsExpandProgress = 1.0f; // Sempre 1.0f (letras sempre expandidas)
int g_FlyoutStartOffset = 15;        // Offset de início do slide-up (-15 ou 15)

// Coordinate cache to avoid redundant SetWindowPos calls
int g_LastFlyoutX = -9999;
int g_LastFlyoutY = -9999;
int g_LastFlyoutW = -9999;
int g_LastFlyoutH = -9999;

void ResetFlyoutPositionCache() {
  g_LastFlyoutX = -9999;
  g_LastFlyoutY = -9999;
  g_LastFlyoutW = -9999;
  g_LastFlyoutH = -9999;
}

// Progress Bar Slider State
bool g_ProgressHover = false;     // mouse está sobre a área da barra
bool g_ProgressDragging = false;  // arrastando o thumb
float g_ThumbAnimRadius = 0.0f;   // raio animado atual do thumb
float g_ThumbTargetRadius = 0.0f; // raio alvo do thumb
float g_DragRatio = 0.0f;         // posição durante o arrasto (0..1)

// Coordenadas globais da barra de progresso para hit-testing
float g_ProgressBarX = 0.0f;
float g_ProgressBarY = 0.0f;
float g_ProgressBarW = 0.0f;
float g_ProgressBarH = 0.0f;

// Volume Slider State
bool g_VolumeHover = false;
bool g_VolumeDragging = false;
float g_VolThumbAnimRadius = 0.0f;
float g_VolThumbTargetRadius = 0.0f;
float g_VolumeDragRatio = 0.0f;
float g_VolumeExpandProgress = 0.0f;

float g_VolumeX = 0.0f;
float g_VolumeY = 0.0f;
float g_VolumeW = 0.0f;
float g_VolumeH = 0.0f;

float g_VolumeAreaX = 0.0f;
float g_VolumeAreaW = 0.0f;
float g_VolumeIconX = 0.0f;
float g_VolumeIconW = 0.0f;

// WASAPI active app volume cache
ISimpleAudioVolume *g_pActiveAppVolume = nullptr;
wstring g_ActiveAppVolumeId = L"";
mutex g_AudioVolumeMutex;

// Idle Tracking
int g_IdleSecondsCounter = 0;
bool g_IsHiddenByIdle = false;

// Data Model
struct MediaState {
  wstring title = L"Waiting for media...";
  wstring artist = L"";
  wstring albumTitle = L"";
  wstring albumArtist = L"";
  int durationTotalSeconds = 0;
  int durationPositionSeconds = 0;
  bool isPlaying = false;
  bool hasMedia = false;
  Bitmap *albumArt = nullptr;
  wstring sourceAppName = L"";
  wstring sourceAppIcon = L"";
  wstring appId = L"";
  bool isBrowser = false;
  wstring webSiteName = L"";
  wstring webSiteIcon = L"";
  mutex lock;
} g_MediaState;

// Animation
int g_ScrollOffset = 0;
int g_TextWidth = 0;
bool g_IsScrolling = false;
int g_ScrollWait = 60;

// --- Settings ---
void LoadSettings() {
  g_Settings.width = Wh_GetIntSetting(L"PanelWidth");
  g_Settings.height = Wh_GetIntSetting(L"PanelHeight");
  g_Settings.fontSize = Wh_GetIntSetting(L"FontSize");
  g_Settings.offsetX = Wh_GetIntSetting(L"OffsetX");
  g_Settings.offsetY = Wh_GetIntSetting(L"OffsetY");

  PCWSTR scaleStr = Wh_GetStringSetting(L"ButtonScale");
  if (scaleStr) {
    g_Settings.buttonScale = _wtof(scaleStr);
    Wh_FreeStringSetting(scaleStr);
  } else {
    g_Settings.buttonScale = 1.0;
  }
  if (g_Settings.buttonScale < 0.5)
    g_Settings.buttonScale = 0.5;
  if (g_Settings.buttonScale > 4.0)
    g_Settings.buttonScale = 4.0;

  g_Settings.hideFullscreen = Wh_GetIntSetting(L"HideFullscreen") != 0;
  g_Settings.idleTimeout = Wh_GetIntSetting(L"IdleTimeout");

  if (g_Settings.width < 100)
    g_Settings.width = 300;
  if (g_Settings.height < 24)
    g_Settings.height = 46;
}

// --- Volume Helper Functions ---
float GetSystemVolume() {
  float volume = 0.0f;
  IMMDeviceEnumerator *deviceEnumerator = nullptr;
  HRESULT hr = CoCreateInstance(
      __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
      __uuidof(IMMDeviceEnumerator), (void **)&deviceEnumerator);
  if (SUCCEEDED(hr) && deviceEnumerator) {
    IMMDevice *defaultDevice = nullptr;
    hr = deviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole,
                                                   &defaultDevice);
    if (SUCCEEDED(hr) && defaultDevice) {
      IAudioEndpointVolume *endpointVolume = nullptr;
      hr = defaultDevice->Activate(__uuidof(IAudioEndpointVolume),
                                   CLSCTX_INPROC_SERVER, nullptr,
                                   (void **)&endpointVolume);
      if (SUCCEEDED(hr) && endpointVolume) {
        endpointVolume->GetMasterVolumeLevelScalar(&volume);
        endpointVolume->Release();
      }
      defaultDevice->Release();
    }
    deviceEnumerator->Release();
  }
  return volume;
}

void SetSystemVolume(float vol) {
  if (vol < 0.0f)
    vol = 0.0f;
  if (vol > 1.0f)
    vol = 1.0f;
  IMMDeviceEnumerator *deviceEnumerator = nullptr;
  HRESULT hr = CoCreateInstance(
      __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
      __uuidof(IMMDeviceEnumerator), (void **)&deviceEnumerator);
  if (SUCCEEDED(hr) && deviceEnumerator) {
    IMMDevice *defaultDevice = nullptr;
    hr = deviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole,
                                                   &defaultDevice);
    if (SUCCEEDED(hr) && defaultDevice) {
      IAudioEndpointVolume *endpointVolume = nullptr;
      hr = defaultDevice->Activate(__uuidof(IAudioEndpointVolume),
                                   CLSCTX_INPROC_SERVER, nullptr,
                                   (void **)&endpointVolume);
      if (SUCCEEDED(hr) && endpointVolume) {
        endpointVolume->SetMasterVolumeLevelScalar(vol, nullptr);
        endpointVolume->Release();
      }
      defaultDevice->Release();
    }
    deviceEnumerator->Release();
  }
}

wstring GetProcessName(DWORD pid) {
  wstring processName = L"";
  HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (hProcess) {
    wchar_t buf[MAX_PATH];
    DWORD size = MAX_PATH;
    if (QueryFullProcessImageNameW(hProcess, 0, buf, &size)) {
      wstring path(buf);
      size_t lastSlash = path.find_last_of(L"\\");
      if (lastSlash != wstring::npos) {
        processName = path.substr(lastSlash + 1);
      } else {
        processName = path;
      }
    }
    CloseHandle(hProcess);
  }
  return processName;
}

ISimpleAudioVolume *GetActiveAppAudioVolume(const wstring &targetAppId) {
  if (targetAppId.empty())
    return nullptr;

  wstring targetLower = targetAppId;
  for (auto &c : targetLower)
    c = towlower(c);

  IMMDeviceEnumerator *deviceEnumerator = nullptr;
  HRESULT hr = CoCreateInstance(
      __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
      __uuidof(IMMDeviceEnumerator), (void **)&deviceEnumerator);
  if (FAILED(hr) || !deviceEnumerator)
    return nullptr;

  IMMDevice *defaultDevice = nullptr;
  hr = deviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole,
                                                 &defaultDevice);
  if (FAILED(hr) || !defaultDevice) {
    deviceEnumerator->Release();
    return nullptr;
  }

  IAudioSessionManager2 *sessionManager = nullptr;
  hr = defaultDevice->Activate(__uuidof(IAudioSessionManager2),
                               CLSCTX_INPROC_SERVER, nullptr,
                               (void **)&sessionManager);
  if (FAILED(hr) || !sessionManager) {
    defaultDevice->Release();
    deviceEnumerator->Release();
    return nullptr;
  }

  IAudioSessionEnumerator *sessionEnumerator = nullptr;
  hr = sessionManager->GetSessionEnumerator(&sessionEnumerator);
  if (FAILED(hr) || !sessionEnumerator) {
    sessionManager->Release();
    defaultDevice->Release();
    deviceEnumerator->Release();
    return nullptr;
  }

  int sessionCount = 0;
  sessionEnumerator->GetCount(&sessionCount);

  ISimpleAudioVolume *matchedVolume = nullptr;

  for (int i = 0; i < sessionCount; i++) {
    IAudioSessionControl *sessionControl = nullptr;
    hr = sessionEnumerator->GetSession(i, &sessionControl);
    if (FAILED(hr) || !sessionControl)
      continue;

    IAudioSessionControl2 *sessionControl2 = nullptr;
    hr = sessionControl->QueryInterface(__uuidof(IAudioSessionControl2),
                                        (void **)&sessionControl2);
    if (SUCCEEDED(hr) && sessionControl2) {
      AudioSessionState state;
      sessionControl2->GetState(&state);
      if (state == AudioSessionStateExpired) {
        sessionControl2->Release();
        sessionControl->Release();
        continue;
      }

      LPWSTR sessionIdentifier = nullptr;
      hr = sessionControl2->GetSessionIdentifier(&sessionIdentifier);
      wstring idStr = L"";
      if (SUCCEEDED(hr) && sessionIdentifier) {
        idStr = sessionIdentifier;
        CoTaskMemFree(sessionIdentifier);
      }

      DWORD pid = 0;
      sessionControl2->GetProcessId(&pid);
      wstring procName = GetProcessName(pid);

      for (auto &c : idStr)
        c = towlower(c);
      for (auto &c : procName)
        c = towlower(c);

      bool isMatch = false;
      wstring procBase = procName;
      size_t dotPos = procBase.find(L".exe");
      if (dotPos != wstring::npos) {
        procBase = procBase.substr(0, dotPos);
      }

      if (!procBase.empty() && targetLower.find(procBase) != wstring::npos) {
        isMatch = true;
      } else if (!idStr.empty() && idStr.find(targetLower) != wstring::npos) {
        isMatch = true;
      } else if (targetLower.find(L"chrome") != wstring::npos &&
                 procBase == L"chrome") {
        isMatch = true;
      } else if (targetLower.find(L"edge") != wstring::npos &&
                 (procBase == L"msedge" || procBase == L"edge")) {
        isMatch = true;
      } else if (targetLower.find(L"firefox") != wstring::npos &&
                 procBase == L"firefox") {
        isMatch = true;
      }

      if (isMatch) {
        hr = sessionControl2->QueryInterface(__uuidof(ISimpleAudioVolume),
                                             (void **)&matchedVolume);
        if (SUCCEEDED(hr) && matchedVolume) {
          sessionControl2->Release();
          sessionControl->Release();
          break;
        }
      }

      sessionControl2->Release();
    }
    sessionControl->Release();
  }

  sessionEnumerator->Release();
  sessionManager->Release();
  defaultDevice->Release();
  deviceEnumerator->Release();

  return matchedVolume;
}

void UpdateActiveAppVolumeCache(const wstring &targetAppId) {
  if (targetAppId == g_ActiveAppVolumeId && g_pActiveAppVolume != nullptr) {
    return;
  }

  lock_guard<mutex> guard(g_AudioVolumeMutex);

  if (g_pActiveAppVolume) {
    g_pActiveAppVolume->Release();
    g_pActiveAppVolume = nullptr;
  }

  g_ActiveAppVolumeId = targetAppId;
  if (targetAppId.empty()) {
    return;
  }

  g_pActiveAppVolume = GetActiveAppAudioVolume(targetAppId);
}

float GetVolumeLevel(const wstring &targetAppId) {
  lock_guard<mutex> guard(g_AudioVolumeMutex);
  if (g_pActiveAppVolume && targetAppId == g_ActiveAppVolumeId) {
    float volume = 0.0f;
    HRESULT hr = g_pActiveAppVolume->GetMasterVolume(&volume);
    if (SUCCEEDED(hr)) {
      return volume;
    } else {
      g_pActiveAppVolume->Release();
      g_pActiveAppVolume = nullptr;
      g_ActiveAppVolumeId = L"";
    }
  }
  return GetSystemVolume();
}

void SetVolumeLevel(const wstring &targetAppId, float vol) {
  if (vol < 0.0f)
    vol = 0.0f;
  if (vol > 1.0f)
    vol = 1.0f;

  lock_guard<mutex> guard(g_AudioVolumeMutex);
  if (g_pActiveAppVolume && targetAppId == g_ActiveAppVolumeId) {
    HRESULT hr = g_pActiveAppVolume->SetMasterVolume(vol, nullptr);
    if (SUCCEEDED(hr)) {
      return;
    } else {
      g_pActiveAppVolume->Release();
      g_pActiveAppVolume = nullptr;
      g_ActiveAppVolumeId = L"";
    }
  }
  SetSystemVolume(vol);
}

bool GetSystemMute() {
  BOOL mute = FALSE;
  IMMDeviceEnumerator *deviceEnumerator = nullptr;
  HRESULT hr = CoCreateInstance(
      __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
      __uuidof(IMMDeviceEnumerator), (void **)&deviceEnumerator);
  if (SUCCEEDED(hr) && deviceEnumerator) {
    IMMDevice *defaultDevice = nullptr;
    hr = deviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole,
                                                   &defaultDevice);
    if (SUCCEEDED(hr) && defaultDevice) {
      IAudioEndpointVolume *endpointVolume = nullptr;
      hr = defaultDevice->Activate(__uuidof(IAudioEndpointVolume),
                                   CLSCTX_INPROC_SERVER, nullptr,
                                   (void **)&endpointVolume);
      if (SUCCEEDED(hr) && endpointVolume) {
        endpointVolume->GetMute(&mute);
        endpointVolume->Release();
      }
      defaultDevice->Release();
    }
    deviceEnumerator->Release();
  }
  return mute != FALSE;
}

void SetSystemMute(bool mute) {
  IMMDeviceEnumerator *deviceEnumerator = nullptr;
  HRESULT hr = CoCreateInstance(
      __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
      __uuidof(IMMDeviceEnumerator), (void **)&deviceEnumerator);
  if (SUCCEEDED(hr) && deviceEnumerator) {
    IMMDevice *defaultDevice = nullptr;
    hr = deviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole,
                                                   &defaultDevice);
    if (SUCCEEDED(hr) && defaultDevice) {
      IAudioEndpointVolume *endpointVolume = nullptr;
      hr = defaultDevice->Activate(__uuidof(IAudioEndpointVolume),
                                   CLSCTX_INPROC_SERVER, nullptr,
                                   (void **)&endpointVolume);
      if (SUCCEEDED(hr) && endpointVolume) {
        endpointVolume->SetMute(mute, nullptr);
        endpointVolume->Release();
      }
      defaultDevice->Release();
    }
    deviceEnumerator->Release();
  }
}

bool GetAppMute(const wstring &targetAppId) {
  lock_guard<mutex> guard(g_AudioVolumeMutex);
  if (g_pActiveAppVolume && targetAppId == g_ActiveAppVolumeId) {
    BOOL mute = FALSE;
    HRESULT hr = g_pActiveAppVolume->GetMute(&mute);
    if (SUCCEEDED(hr)) {
      return mute != FALSE;
    } else {
      g_pActiveAppVolume->Release();
      g_pActiveAppVolume = nullptr;
      g_ActiveAppVolumeId = L"";
    }
  }
  return GetSystemMute();
}

void SetAppMute(const wstring &targetAppId, bool mute) {
  lock_guard<mutex> guard(g_AudioVolumeMutex);
  if (g_pActiveAppVolume && targetAppId == g_ActiveAppVolumeId) {
    HRESULT hr = g_pActiveAppVolume->SetMute(mute, nullptr);
    if (SUCCEEDED(hr)) {
      return;
    } else {
      g_pActiveAppVolume->Release();
      g_pActiveAppVolume = nullptr;
      g_ActiveAppVolumeId = L"";
    }
  }
  SetSystemMute(mute);
}

// --- Internationalization ---
enum class UIStringId {
  LyricsHeader,
  TranslateButton,
  FetchingLyrics,
  LyricsNotFound
};
wstring GetLocalizedText(UIStringId id);
wstring GetUserLanguageTag();

// --- Lyrics API ---
wstring UrlEncode(const wstring &value) {
  wstringstream escaped;
  escaped.fill(L'0');
  escaped << hex;
  for (wchar_t c : value) {
    if (iswalnum(c) || c == L'-' || c == L'_' || c == L'.' || c == L'~') {
      escaped << c;
    } else if (c == L' ') {
      escaped << L'+';
    } else {
      char utf8[4] = {0};
      int len = WideCharToMultiByte(CP_UTF8, 0, &c, 1, utf8, 4, NULL, NULL);
      for (int i = 0; i < len; ++i) {
        escaped << L'%' << setw(2) << uppercase
                << ((int)(unsigned char)utf8[i]);
      }
    }
  }
  return escaped.str();
}

bool HttpGet(const wstring &host, const wstring &path, string &responseStr) {
  HINTERNET hSession =
      WinHttpOpen(L"TaskbarMediaPlayer/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!hSession)
    return false;

  WinHttpSetTimeouts(hSession, 3000, 3000, 3000, 3000);

  HINTERNET hConnect =
      WinHttpConnect(hSession, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
  if (!hConnect) {
    WinHttpCloseHandle(hSession);
    return false;
  }

  HINTERNET hRequest = WinHttpOpenRequest(
      hConnect, L"GET", path.c_str(), NULL, WINHTTP_NO_REFERER,
      WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
  if (!hRequest) {
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return false;
  }

  bool success = false;
  if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                         WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
    if (WinHttpReceiveResponse(hRequest, NULL)) {
      DWORD statusCode = 0;
      DWORD dwSize = sizeof(statusCode);
      WinHttpQueryHeaders(hRequest,
                          WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                          WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &dwSize,
                          WINHTTP_NO_HEADER_INDEX);

      if (statusCode == 200) {
        DWORD bytesAvailable = 0;
        while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) &&
               bytesAvailable > 0) {
          char *buf = new char[bytesAvailable + 1];
          DWORD bytesRead = 0;
          if (WinHttpReadData(hRequest, buf, bytesAvailable, &bytesRead)) {
            buf[bytesRead] = 0;
            responseStr += buf;
          }
          delete[] buf;
        }
        success = true;
      }
    }
  }

  WinHttpCloseHandle(hRequest);
  WinHttpCloseHandle(hConnect);
  WinHttpCloseHandle(hSession);
  return success;
}

bool ParseLrcString(const wstring &lrcStr, vector<LyricLine> &lines) {
  if (lrcStr.empty())
    return false;
  wstringstream ss(lrcStr);
  wstring line;
  while (getline(ss, line, L'\n')) {
    if (!line.empty() && line.back() == L'\r')
      line.pop_back();
    size_t bracketEnd = line.find(L']');
    if (bracketEnd != wstring::npos && bracketEnd >= 6 && line[0] == L'[') {
      wstring timeStr = line.substr(1, bracketEnd - 1);
      size_t colon = timeStr.find(L':');
      if (colon != wstring::npos) {
        try {
          int m = stoi(timeStr.substr(0, colon));
          int s = stoi(timeStr.substr(colon + 1, 2));
          int totalSecs = m * 60 + s;
          wstring text = line.substr(bracketEnd + 1);
          if (!text.empty() && text[0] == L' ')
            text = text.substr(1);
          lines.push_back({totalSecs, text});
        } catch (...) {
        }
      }
    }
  }
  return !lines.empty();
}

void FetchLyricsAsync(wstring track, wstring artist) {
  if (track.empty() || artist.empty())
    return;

  {
    lock_guard<mutex> guard(g_LyricsMutex);
    if (track == g_CurrentLyricsTitle && artist == g_CurrentLyricsArtist)
      return;
    g_CurrentLyricsTitle = track;
    g_CurrentLyricsArtist = artist;
    g_IsFetchingLyrics = true;
    g_HasLyrics = false;
    g_PlainLyrics.clear();
    g_SyncedLyrics.clear();
  }
  if (g_hFlyoutWindow) {
    InvalidateRect(g_hFlyoutWindow, NULL, TRUE);
  }

  std::thread([track, artist]() {
    winrt_apartment_scope scope;

    wstring lrclibPlain = L"";
    wstring neteasePlain = L"";
    vector<LyricLine> syncedLines;
    bool foundSynced = false;

    // 1. lrclib.net (Synced)
    {
      wstring urlPath = L"/api/get?track_name=" + UrlEncode(track) +
                        L"&artist_name=" + UrlEncode(artist);
      string jsonStr;
      if (HttpGet(L"lrclib.net", urlPath, jsonStr) && !jsonStr.empty()) {
        try {
          int wideLen =
              MultiByteToWideChar(CP_UTF8, 0, jsonStr.c_str(), -1, NULL, 0);
          if (wideLen > 0) {
            wchar_t *wideBuf = new wchar_t[wideLen + 1];
            MultiByteToWideChar(CP_UTF8, 0, jsonStr.c_str(), -1, wideBuf,
                                wideLen);
            winrt::hstring jsonHString(wideBuf);
            delete[] wideBuf;

            winrt::Windows::Data::Json::JsonObject jsonObj = nullptr;
            if (winrt::Windows::Data::Json::JsonObject::TryParse(jsonHString,
                                                                 jsonObj)) {
              if (jsonObj.HasKey(L"syncedLyrics") &&
                  jsonObj.GetNamedValue(L"syncedLyrics").ValueType() ==
                      winrt::Windows::Data::Json::JsonValueType::String) {
                wstring syncedStr =
                    jsonObj.GetNamedValue(L"syncedLyrics").GetString().c_str();
                if (!syncedStr.empty() &&
                    ParseLrcString(syncedStr, syncedLines)) {
                  foundSynced = true;
                  lock_guard<mutex> guard(g_LyricsMutex);
                  if (track == g_CurrentLyricsTitle &&
                      artist == g_CurrentLyricsArtist) {
                    g_SyncedLyrics = syncedLines;
                    g_PlainLyrics.clear();
                    g_LyricsSource = L"lrclib.net";
                    g_HasLyrics = true;
                    g_IsFetchingLyrics = false;
                  }
                  if (g_hFlyoutWindow)
                    InvalidateRect(g_hFlyoutWindow, NULL, TRUE);

                  if (g_TranslateLyrics) {
                    void FetchTranslationThread();
                    FetchTranslationThread();
                  }
                  return;
                }
              }
              // Save plain lyrics if present
              if (jsonObj.HasKey(L"plainLyrics") &&
                  jsonObj.GetNamedValue(L"plainLyrics").ValueType() ==
                      winrt::Windows::Data::Json::JsonValueType::String) {
                lrclibPlain = jsonObj.GetNamedString(L"plainLyrics").c_str();
              }
            }
          }
        } catch (...) {
        }
      }
    }

    // 2. NetEase Cloud Music (Synced)
    if (!foundSynced) {
      wstring searchPath = L"/api/search/get?s=" +
                           UrlEncode(track + L" " + artist) +
                           L"&type=1&limit=1";
      string searchResponse;
      long long songId = 0;
      if (HttpGet(L"music.163.com", searchPath, searchResponse) &&
          !searchResponse.empty()) {
        try {
          int wideLen = MultiByteToWideChar(CP_UTF8, 0, searchResponse.c_str(),
                                            -1, NULL, 0);
          if (wideLen > 0) {
            wchar_t *wideBuf = new wchar_t[wideLen + 1];
            MultiByteToWideChar(CP_UTF8, 0, searchResponse.c_str(), -1, wideBuf,
                                wideLen);
            winrt::hstring jsonHString(wideBuf);
            delete[] wideBuf;

            winrt::Windows::Data::Json::JsonObject jsonObj = nullptr;
            if (winrt::Windows::Data::Json::JsonObject::TryParse(jsonHString,
                                                                 jsonObj)) {
              if (jsonObj.HasKey(L"result") &&
                  jsonObj.GetNamedValue(L"result").ValueType() ==
                      winrt::Windows::Data::Json::JsonValueType::Object) {
                auto resultObj = jsonObj.GetNamedObject(L"result");
                if (resultObj.HasKey(L"songs") &&
                    resultObj.GetNamedValue(L"songs").ValueType() ==
                        winrt::Windows::Data::Json::JsonValueType::Array) {
                  auto songsArr = resultObj.GetNamedArray(L"songs");
                  if (songsArr.Size() > 0) {
                    auto firstSong = songsArr.GetAt(0).GetObject();
                    if (firstSong.HasKey(L"id")) {
                      songId = (long long)firstSong.GetNamedNumber(L"id");
                    }
                  }
                }
              }
            }
          }
        } catch (...) {
        }
      }

      if (songId > 0) {
        wstring lyricPath =
            L"/api/song/lyric?id=" + to_wstring(songId) + L"&lv=-1&kv=-1&tv=-1";
        string lyricResponse;
        if (HttpGet(L"music.163.com", lyricPath, lyricResponse) &&
            !lyricResponse.empty()) {
          try {
            int wideLen = MultiByteToWideChar(CP_UTF8, 0, lyricResponse.c_str(),
                                              -1, NULL, 0);
            if (wideLen > 0) {
              wchar_t *wideBuf = new wchar_t[wideLen + 1];
              MultiByteToWideChar(CP_UTF8, 0, lyricResponse.c_str(), -1,
                                  wideBuf, wideLen);
              winrt::hstring jsonHString(wideBuf);
              delete[] wideBuf;

              winrt::Windows::Data::Json::JsonObject jsonObj = nullptr;
              if (winrt::Windows::Data::Json::JsonObject::TryParse(jsonHString,
                                                                   jsonObj)) {
                if (jsonObj.HasKey(L"lrc") &&
                    jsonObj.GetNamedValue(L"lrc").ValueType() ==
                        winrt::Windows::Data::Json::JsonValueType::Object) {
                  auto lrcObj = jsonObj.GetNamedObject(L"lrc");
                  if (lrcObj.HasKey(L"lyric") &&
                      lrcObj.GetNamedValue(L"lyric").ValueType() ==
                          winrt::Windows::Data::Json::JsonValueType::String) {
                    wstring neteaseLyricStr =
                        lrcObj.GetNamedString(L"lyric").c_str();
                    if (!neteaseLyricStr.empty()) {
                      if (ParseLrcString(neteaseLyricStr, syncedLines)) {
                        foundSynced = true;
                        lock_guard<mutex> guard(g_LyricsMutex);
                        if (track == g_CurrentLyricsTitle &&
                            artist == g_CurrentLyricsArtist) {
                          g_SyncedLyrics = syncedLines;
                          g_PlainLyrics.clear();
                          g_LyricsSource = L"NetEase";
                          g_HasLyrics = true;
                          g_IsFetchingLyrics = false;
                        }
                        if (g_hFlyoutWindow)
                          InvalidateRect(g_hFlyoutWindow, NULL, TRUE);

                        if (g_TranslateLyrics) {
                          void FetchTranslationThread();
                          FetchTranslationThread();
                        }
                        return;
                      } else {
                        neteasePlain = neteaseLyricStr;
                      }
                    }
                  }
                }
              }
            }
          } catch (...) {
          }
        }
      }
    }

    // 3. Fallback to Plain Lyrics (unsynced)

    // 3a. lrclib.net (plain)
    if (!lrclibPlain.empty()) {
      lock_guard<mutex> guard(g_LyricsMutex);
      if (track == g_CurrentLyricsTitle && artist == g_CurrentLyricsArtist) {
        g_PlainLyrics = lrclibPlain;
        g_SyncedLyrics.clear();
        g_LyricsSource = L"lrclib.net";
        g_HasLyrics = true;
        g_IsFetchingLyrics = false;
      }
      if (g_hFlyoutWindow)
        InvalidateRect(g_hFlyoutWindow, NULL, TRUE);
      if (g_TranslateLyrics) {
        void FetchTranslationThread();
        FetchTranslationThread();
      }
      return;
    }

    // 3b. Vagalume API
    {
      wstring vagalumePath =
          L"/search.php?art=" + UrlEncode(artist) + L"&mus=" + UrlEncode(track);
      string vagalumeResponse;
      if (HttpGet(L"api.vagalume.com.br", vagalumePath, vagalumeResponse) &&
          !vagalumeResponse.empty()) {
        try {
          int wideLen = MultiByteToWideChar(
              CP_UTF8, 0, vagalumeResponse.c_str(), -1, NULL, 0);
          if (wideLen > 0) {
            wchar_t *wideBuf = new wchar_t[wideLen + 1];
            MultiByteToWideChar(CP_UTF8, 0, vagalumeResponse.c_str(), -1,
                                wideBuf, wideLen);
            winrt::hstring jsonHString(wideBuf);
            delete[] wideBuf;

            winrt::Windows::Data::Json::JsonObject jsonObj = nullptr;
            if (winrt::Windows::Data::Json::JsonObject::TryParse(jsonHString,
                                                                 jsonObj)) {
              if (jsonObj.HasKey(L"mus") &&
                  jsonObj.GetNamedValue(L"mus").ValueType() ==
                      winrt::Windows::Data::Json::JsonValueType::Array) {
                auto musArr = jsonObj.GetNamedArray(L"mus");
                if (musArr.Size() > 0) {
                  auto firstMus = musArr.GetAt(0).GetObject();
                  if (firstMus.HasKey(L"text") &&
                      firstMus.GetNamedValue(L"text").ValueType() ==
                          winrt::Windows::Data::Json::JsonValueType::String) {
                    wstring vagalumeText =
                        firstMus.GetNamedString(L"text").c_str();
                    if (!vagalumeText.empty()) {
                      lock_guard<mutex> guard(g_LyricsMutex);
                      if (track == g_CurrentLyricsTitle &&
                          artist == g_CurrentLyricsArtist) {
                        g_PlainLyrics = vagalumeText;
                        g_SyncedLyrics.clear();
                        g_LyricsSource = L"Vagalume";
                        g_HasLyrics = true;
                        g_IsFetchingLyrics = false;
                      }
                      if (g_hFlyoutWindow)
                        InvalidateRect(g_hFlyoutWindow, NULL, TRUE);
                      if (g_TranslateLyrics) {
                        void FetchTranslationThread();
                        FetchTranslationThread();
                      }
                      return;
                    }
                  }
                }
              }
            }
          }
        } catch (...) {
        }
      }
    }

    // 3c. lyrics.ovh
    {
      wstring ovhPath = L"/v1/" + UrlEncode(artist) + L"/" + UrlEncode(track);
      string ovhResponse;
      if (HttpGet(L"api.lyrics.ovh", ovhPath, ovhResponse) &&
          !ovhResponse.empty()) {
        try {
          int wideLen =
              MultiByteToWideChar(CP_UTF8, 0, ovhResponse.c_str(), -1, NULL, 0);
          if (wideLen > 0) {
            wchar_t *wideBuf = new wchar_t[wideLen + 1];
            MultiByteToWideChar(CP_UTF8, 0, ovhResponse.c_str(), -1, wideBuf,
                                wideLen);
            winrt::hstring jsonHString(wideBuf);
            delete[] wideBuf;

            winrt::Windows::Data::Json::JsonObject jsonObj = nullptr;
            if (winrt::Windows::Data::Json::JsonObject::TryParse(jsonHString,
                                                                 jsonObj)) {
              if (jsonObj.HasKey(L"lyrics") &&
                  jsonObj.GetNamedValue(L"lyrics").ValueType() ==
                      winrt::Windows::Data::Json::JsonValueType::String) {
                wstring ovhText = jsonObj.GetNamedString(L"lyrics").c_str();
                if (!ovhText.empty()) {
                  lock_guard<mutex> guard(g_LyricsMutex);
                  if (track == g_CurrentLyricsTitle &&
                      artist == g_CurrentLyricsArtist) {
                    g_PlainLyrics = ovhText;
                    g_SyncedLyrics.clear();
                    g_LyricsSource = L"lyrics.ovh";
                    g_HasLyrics = true;
                    g_IsFetchingLyrics = false;
                  }
                  if (g_hFlyoutWindow)
                    InvalidateRect(g_hFlyoutWindow, NULL, TRUE);
                  if (g_TranslateLyrics) {
                    void FetchTranslationThread();
                    FetchTranslationThread();
                  }
                  return;
                }
              }
            }
          }
        } catch (...) {
        }
      }
    }

    // 3d. NetEase (plain)
    if (!neteasePlain.empty()) {
      lock_guard<mutex> guard(g_LyricsMutex);
      if (track == g_CurrentLyricsTitle && artist == g_CurrentLyricsArtist) {
        g_PlainLyrics = neteasePlain;
        g_SyncedLyrics.clear();
        g_LyricsSource = L"NetEase";
        g_HasLyrics = true;
        g_IsFetchingLyrics = false;
      }
      if (g_hFlyoutWindow)
        InvalidateRect(g_hFlyoutWindow, NULL, TRUE);
      if (g_TranslateLyrics) {
        void FetchTranslationThread();
        FetchTranslationThread();
      }
      return;
    }

    // 4. No lyrics found anywhere
    {
      lock_guard<mutex> guard(g_LyricsMutex);
      if (track == g_CurrentLyricsTitle && artist == g_CurrentLyricsArtist) {
        g_IsFetchingLyrics = false;
        g_HasLyrics = false;
      }
    }
    if (g_hFlyoutWindow)
      InvalidateRect(g_hFlyoutWindow, NULL, TRUE);
  }).detach();
}

string UrlEncode(const string &s) {
  ostringstream escaped;
  escaped.fill('0');
  escaped << hex;
  for (char c : s) {
    if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' ||
        c == '~') {
      escaped << c;
    } else {
      escaped << uppercase;
      escaped << '%' << setw(2) << int((unsigned char)c);
      escaped << nouppercase;
    }
  }
  return escaped.str();
}

wstring CleanStringForMatch(const wstring &s) {
  wstring clean = L"";
  for (wchar_t c : s) {
    if (iswalnum(c)) {
      clean += towlower(c);
    }
  }
  return clean;
}

void FetchTranslationThread() {
  thread([]() {
    wstring currentTitle;
    wstring currentArtist;
    vector<LyricLine> localLines;
    wstring localPlain;
    {
      lock_guard<mutex> guard(g_LyricsMutex);
      if (!g_HasLyrics || g_IsTranslating)
        return;
      currentTitle = g_CurrentLyricsTitle;
      currentArtist = g_CurrentLyricsArtist;
      localLines = g_SyncedLyrics;
      localPlain = g_PlainLyrics;
      g_IsTranslating = true;
    }

    if (!localLines.empty() && !localLines[0].translatedText.empty()) {
      lock_guard<mutex> guard(g_LyricsMutex);
      g_IsTranslating = false;
      return;
    }

    winrt_apartment_scope scope;

    string query = "";
    if (!localLines.empty()) {
      for (const auto &line : localLines) {
        int wideLen = WideCharToMultiByte(CP_UTF8, 0, line.text.c_str(), -1,
                                          NULL, 0, NULL, NULL);
        if (wideLen > 0) {
          char *buf = new char[wideLen + 1];
          WideCharToMultiByte(CP_UTF8, 0, line.text.c_str(), -1, buf, wideLen,
                              NULL, NULL);
          query += buf;
          query += "\n";
          delete[] buf;
        }
      }
    } else {
      int wideLen = WideCharToMultiByte(CP_UTF8, 0, localPlain.c_str(), -1,
                                        NULL, 0, NULL, NULL);
      if (wideLen > 0) {
        char *buf = new char[wideLen + 1];
        WideCharToMultiByte(CP_UTF8, 0, localPlain.c_str(), -1, buf, wideLen,
                            NULL, NULL);
        query += buf;
        delete[] buf;
      }
    }

    if (query.empty()) {
      lock_guard<mutex> guard(g_LyricsMutex);
      g_IsTranslating = false;
      return;
    }

    wstring userLangW = GetUserLanguageTag();
    string targetLang(userLangW.begin(), userLangW.end());
    int retry = 0;

    while (retry < 2) {
      string urlPath =
          "/translate_a/single?client=gtx&sl=auto&tl=" + targetLang +
          "&dt=t&q=" + UrlEncode(query);
      wstring widePath(urlPath.begin(), urlPath.end());

      HINTERNET hSession =
          WinHttpOpen(L"Windhawk/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
      if (hSession) {
        HINTERNET hConnect =
            WinHttpConnect(hSession, L"translate.googleapis.com",
                           INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (hConnect) {
          HINTERNET hRequest = WinHttpOpenRequest(
              hConnect, L"GET", widePath.c_str(), NULL, WINHTTP_NO_REFERER,
              WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
          if (hRequest) {
            if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                   WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
              if (WinHttpReceiveResponse(hRequest, NULL)) {
                DWORD statusCode = 0;
                DWORD dwSize = sizeof(statusCode);
                WinHttpQueryHeaders(hRequest,
                                    WINHTTP_QUERY_STATUS_CODE |
                                        WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &statusCode,
                                    &dwSize, WINHTTP_NO_HEADER_INDEX);

                if (statusCode == 200) {
                  string jsonStr = "";
                  DWORD bytesAvailable = 0;
                  while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) &&
                         bytesAvailable > 0) {
                    char *buf = new char[bytesAvailable + 1];
                    DWORD bytesRead = 0;
                    if (WinHttpReadData(hRequest, buf, bytesAvailable,
                                        &bytesRead)) {
                      buf[bytesRead] = 0;
                      jsonStr += buf;
                    }
                    delete[] buf;
                  }

                  bool needsRetry = false;
                  try {
                    int wideLen = MultiByteToWideChar(
                        CP_UTF8, 0, jsonStr.c_str(), -1, NULL, 0);
                    if (wideLen > 0) {
                      wchar_t *wideBuf = new wchar_t[wideLen + 1];
                      MultiByteToWideChar(CP_UTF8, 0, jsonStr.c_str(), -1,
                                          wideBuf, wideLen);
                      winrt::hstring jsonHString(wideBuf);
                      delete[] wideBuf;

                      winrt::Windows::Data::Json::JsonArray rootArr = nullptr;
                      if (winrt::Windows::Data::Json::JsonArray::TryParse(
                              jsonHString, rootArr)) {
                        if (rootArr.Size() > 0 &&
                            rootArr.GetAt(0).ValueType() ==
                                winrt::Windows::Data::Json::JsonValueType::
                                    Array) {

                          // Check detected language
                          if (retry == 0 && rootArr.Size() >= 3) {
                            if (rootArr.GetAt(2).ValueType() ==
                                winrt::Windows::Data::Json::JsonValueType::
                                    String) {
                              wstring detected =
                                  rootArr.GetAt(2).GetString().c_str();
                              wstring targetLangW(targetLang.begin(),
                                                  targetLang.end());
                              if (detected == targetLangW) {
                                needsRetry = true;
                              }
                            }
                          }

                          if (!needsRetry) {
                            winrt::Windows::Data::Json::JsonArray linesArr =
                                rootArr.GetAt(0).GetArray();

                            struct ParsedTranslation {
                              wstring orig;
                              wstring trans;
                            };
                            vector<ParsedTranslation> parsedSegments;

                            for (uint32_t i = 0; i < linesArr.Size(); i++) {
                              if (linesArr.GetAt(i).ValueType() ==
                                  winrt::Windows::Data::Json::JsonValueType::
                                      Array) {
                                auto item = linesArr.GetAt(i).GetArray();
                                if (item.Size() > 1 &&
                                    item.GetAt(0).ValueType() ==
                                        winrt::Windows::Data::Json::
                                            JsonValueType::String &&
                                    item.GetAt(1).ValueType() ==
                                        winrt::Windows::Data::Json::
                                            JsonValueType::String) {
                                  wstring t = item.GetAt(0).GetString().c_str();
                                  wstring o = item.GetAt(1).GetString().c_str();
                                  if (!t.empty() && t.back() == L'\n')
                                    t.pop_back();
                                  if (!o.empty() && o.back() == L'\n')
                                    o.pop_back();
                                  parsedSegments.push_back({o, t});
                                }
                              }
                            }

                            lock_guard<mutex> guard(g_LyricsMutex);
                            if (currentTitle == g_CurrentLyricsTitle &&
                                currentArtist == g_CurrentLyricsArtist) {
                              if (!g_SyncedLyrics.empty()) {
                                // Pre-clean all lyric lines for matching
                                vector<wstring> cleanLyricsText;
                                for (const auto &line : g_SyncedLyrics) {
                                  cleanLyricsText.push_back(
                                      CleanStringForMatch(line.text));
                                }

                                // Clear old translations
                                for (auto &line : g_SyncedLyrics) {
                                  line.translatedText.clear();
                                }

                                int lastMatchedIndex = -1;

                                // Match each segment to a synced lyric line
                                for (const auto &seg : parsedSegments) {
                                  wstring cleanO =
                                      CleanStringForMatch(seg.orig);
                                  if (cleanO.empty())
                                    continue;

                                  int bestMatchIndex = -1;
                                  size_t longestMatchLen = 0;
                                  int startIndex = lastMatchedIndex + 1;

                                  for (size_t k = 0; k < g_SyncedLyrics.size();
                                       ++k) {
                                    int j = (startIndex + k) %
                                            g_SyncedLyrics.size();
                                    const wstring &cleanLine =
                                        cleanLyricsText[j];
                                    if (cleanLine.empty())
                                      continue;

                                    if (cleanO == cleanLine) {
                                      bestMatchIndex = j;
                                      break; // perfect match
                                    }

                                    if (cleanO.find(cleanLine) !=
                                            wstring::npos ||
                                        cleanLine.find(cleanO) !=
                                            wstring::npos) {
                                      size_t matchLen = min(cleanO.length(),
                                                            cleanLine.length());
                                      if (matchLen > longestMatchLen) {
                                        longestMatchLen = matchLen;
                                        bestMatchIndex = j;
                                      }
                                    }
                                  }

                                  if (bestMatchIndex != -1) {
                                    if (bestMatchIndex == lastMatchedIndex) {
                                      g_SyncedLyrics[bestMatchIndex]
                                          .translatedText += L" " + seg.trans;
                                    } else {
                                      g_SyncedLyrics[bestMatchIndex]
                                          .translatedText = seg.trans;
                                    }
                                    lastMatchedIndex = bestMatchIndex;
                                  }
                                }
                              } else {
                                g_PlainLyrics = L"";
                                for (const auto &seg : parsedSegments) {
                                  g_PlainLyrics += seg.trans + L"\n";
                                }
                              }
                              if (g_hFlyoutWindow)
                                InvalidateRect(g_hFlyoutWindow, NULL, TRUE);
                            }

                            // Done
                            WinHttpCloseHandle(hRequest);
                            WinHttpCloseHandle(hConnect);
                            WinHttpCloseHandle(hSession);

                            g_IsTranslating = false;
                            return;
                          }
                        }
                      }
                    }
                  } catch (...) {
                  }

                  if (needsRetry) {
                    targetLang = (targetLang == "en") ? "es" : "en";
                    retry++;
                    WinHttpCloseHandle(hRequest);
                    WinHttpCloseHandle(hConnect);
                    WinHttpCloseHandle(hSession);
                    continue;
                  }
                }
              }
            }
            WinHttpCloseHandle(hRequest);
          }
          WinHttpCloseHandle(hConnect);
        }
        WinHttpCloseHandle(hSession);
      }
      break; // Break if WinHttp failed to open session or something
    }

    lock_guard<mutex> guard(g_LyricsMutex);
    g_IsTranslating = false;
  }).detach();
}

// --- WinRT / GSMTC ---
GlobalSystemMediaTransportControlsSessionManager g_SessionManager = nullptr;

Bitmap *StreamToBitmap(IRandomAccessStreamWithContentType const &stream) {
  if (!stream)
    return nullptr;
  IStream *nativeStream = nullptr;
  if (SUCCEEDED(CreateStreamOverRandomAccessStream(
          reinterpret_cast<IUnknown *>(winrt::get_abi(stream)),
          IID_PPV_ARGS(&nativeStream)))) {
    Bitmap *bmp = Bitmap::FromStream(nativeStream);
    nativeStream->Release();
    if (bmp && bmp->GetLastStatus() == Ok)
      return bmp;
    delete bmp;
  }
  return nullptr;
}

void GetFriendlySourceApp(const wstring &appId, const wstring &albumTitle,
                          const wstring &title, const wstring &artist,
                          wstring &friendlyName, wstring &iconChar,
                          bool &isBrowser, wstring &webSiteName,
                          wstring &webSiteIcon) {
  wstring lowerId = appId;
  for (auto &c : lowerId)
    c = towlower(c);

  // Log para depuração de metadados no Windhawk
  Wh_Log(L"GetFriendlySourceApp: appId='%s', albumTitle='%s', title='%s', "
         L"artist='%s'",
         appId.c_str(), albumTitle.c_str(), title.c_str(), artist.c_str());

  isBrowser = (lowerId.find(L"chrome") != wstring::npos ||
               lowerId.find(L"edge") != wstring::npos ||
               lowerId.find(L"firefox") != wstring::npos ||
               lowerId.find(L"brave") != wstring::npos ||
               lowerId.find(L"browser") != wstring::npos ||
               lowerId.find(L"explorer") != wstring::npos);

  if (isBrowser) {
    // Resolve o nome do navegador
    if (lowerId.find(L"chrome") != wstring::npos)
      friendlyName = L"Chrome";
    else if (lowerId.find(L"edge") != wstring::npos)
      friendlyName = L"Edge";
    else if (lowerId.find(L"firefox") != wstring::npos)
      friendlyName = L"Firefox";
    else if (lowerId.find(L"brave") != wstring::npos)
      friendlyName = L"Brave";
    else
      friendlyName = L"Navegador";

    iconChar = L"\xE774"; // Ícone de globo para o navegador
    webSiteName = L"";
    webSiteIcon = L"";
  } else {
    // App nativo
    if (lowerId.find(L"spotify") != wstring::npos) {
      friendlyName = L"Spotify";
      iconChar = L"spotify";
    } else if (lowerId.find(L"zunemusic") != wstring::npos ||
               lowerId.find(L"zune") != wstring::npos) {
      friendlyName = L"Media Player";
      iconChar = L"generic_app";
    } else {
      size_t dot = lowerId.find(L'.');
      if (dot != wstring::npos && dot > 0) {
        wstring part = appId.substr(0, dot);
        if (!part.empty())
          part[0] = towupper(part[0]);
        friendlyName = part;
      } else {
        friendlyName = L"Mídia";
      }
      iconChar = L"generic_app";
    }
    webSiteName = L"";
    webSiteIcon = L"";
  }
}

void UpdateMediaInfo() {
  wstring targetAppIdForCache = L"";
  try {
    if (!g_SessionManager) {
      g_SessionManager =
          GlobalSystemMediaTransportControlsSessionManager::RequestAsync()
              .get();
    }
    if (!g_SessionManager)
      return;

    // Iterate ALL sessions to find one that is actively PLAYING.
    GlobalSystemMediaTransportControlsSession session = nullptr;
    bool foundActive = false;

    auto sessionsList = g_SessionManager.GetSessions();
    for (auto const &s : sessionsList) {
      auto pb = s.GetPlaybackInfo();
      if (pb && pb.PlaybackStatus() ==
                    GlobalSystemMediaTransportControlsSessionPlaybackStatus::
                        Playing) {
        session = s;
        foundActive = true;
        break;
      }
    }

    if (!foundActive) {
      session = g_SessionManager.GetCurrentSession();
    }

    if (session) {
      auto props = session.TryGetMediaPropertiesAsync().get();
      auto info = session.GetPlaybackInfo();

      wstring newTitle = props.Title().c_str();
      wstring newArtist = props.Artist().c_str();
      wstring newAlbumTitle = props.AlbumTitle().c_str();
      wstring newAlbumArtist = props.AlbumArtist().c_str();

      int totalSecs = 0;
      int posSecs = 0;
      try {
        auto timeline = session.GetTimelineProperties();
        if (timeline) {
          totalSecs = (int)std::chrono::duration_cast<std::chrono::seconds>(
                          timeline.EndTime())
                          .count();
          auto snapshotPos = timeline.Position();
          auto lastUpdated = timeline.LastUpdatedTime();
          auto now = winrt::clock::now();

          int64_t elapsedSecs = 0;
          if (info &&
              info.PlaybackStatus() ==
                  GlobalSystemMediaTransportControlsSessionPlaybackStatus::
                      Playing) {
            elapsedSecs = std::chrono::duration_cast<std::chrono::seconds>(
                              now - lastUpdated)
                              .count();
          }

          int64_t calculatedPos =
              std::chrono::duration_cast<std::chrono::seconds>(snapshotPos)
                  .count() +
              elapsedSecs;
          if (calculatedPos < 0)
            calculatedPos = 0;
          if (calculatedPos > totalSecs)
            calculatedPos = totalSecs;

          posSecs = (int)calculatedPos;
        }
      } catch (...) {
      }

      lock_guard<mutex> guard(g_MediaState.lock);

      if (newTitle != g_MediaState.title || newArtist != g_MediaState.artist) {
        FetchLyricsAsync(newTitle, newArtist);
      }

      if (newTitle != g_MediaState.title || g_MediaState.albumArt == nullptr) {
        if (g_MediaState.albumArt) {
          delete g_MediaState.albumArt;
          g_MediaState.albumArt = nullptr;
        }
        auto thumbRef = props.Thumbnail();
        if (thumbRef) {
          try {
            auto stream = thumbRef.OpenReadAsync().get();
            g_MediaState.albumArt = StreamToBitmap(stream);
          } catch (...) {
          }
        }
      }
      wstring appId = session.SourceAppUserModelId().c_str();
      targetAppIdForCache = appId;
      wstring friendlyAppName;
      wstring friendlyAppIcon;
      bool isBrowser = false;
      wstring webSiteName;
      wstring webSiteIcon;
      GetFriendlySourceApp(appId, newAlbumTitle, newTitle, newArtist,
                           friendlyAppName, friendlyAppIcon, isBrowser,
                           webSiteName, webSiteIcon);

      g_MediaState.title = newTitle;
      g_MediaState.artist = newArtist;
      g_MediaState.albumTitle = newAlbumTitle;
      g_MediaState.albumArtist = newAlbumArtist;
      g_MediaState.durationTotalSeconds = totalSecs;
      g_MediaState.durationPositionSeconds = posSecs;
      g_MediaState.isPlaying =
          (info.PlaybackStatus() ==
           GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing);
      g_MediaState.hasMedia = true;
      g_MediaState.sourceAppName = friendlyAppName;
      g_MediaState.sourceAppIcon = friendlyAppIcon;
      g_MediaState.appId = appId;
      g_MediaState.isBrowser = isBrowser;
      g_MediaState.webSiteName = webSiteName;
      g_MediaState.webSiteIcon = webSiteIcon;
    } else {
      lock_guard<mutex> guard(g_MediaState.lock);
      g_MediaState.hasMedia = false;
      g_MediaState.title = L"No Media";
      g_MediaState.artist = L"";
      g_MediaState.albumTitle = L"";
      g_MediaState.albumArtist = L"";
      g_MediaState.durationTotalSeconds = 0;
      g_MediaState.durationPositionSeconds = 0;
      g_MediaState.sourceAppName = L"";
      g_MediaState.sourceAppIcon = L"";
      g_MediaState.appId = L"";
      g_MediaState.isBrowser = false;
      g_MediaState.webSiteName = L"";
      g_MediaState.webSiteIcon = L"";
      if (g_MediaState.albumArt) {
        delete g_MediaState.albumArt;
        g_MediaState.albumArt = nullptr;
      }
    }
  } catch (...) {
    lock_guard<mutex> guard(g_MediaState.lock);
    g_MediaState.hasMedia = false;
    g_MediaState.sourceAppName = L"";
    g_MediaState.sourceAppIcon = L"";
    g_MediaState.appId = L"";
    g_MediaState.isBrowser = false;
    g_MediaState.webSiteName = L"";
    g_MediaState.webSiteIcon = L"";
  }

  UpdateActiveAppVolumeCache(targetAppIdForCache);
}

void SendMediaCommand(int cmd) {
  try {
    if (!g_SessionManager)
      return;
    auto session = g_SessionManager.GetCurrentSession();
    if (session) {
      if (cmd == 1)
        session.TrySkipPreviousAsync();
      else if (cmd == 2)
        session.TryTogglePlayPauseAsync();
      else if (cmd == 3)
        session.TrySkipNextAsync();
    }
  } catch (...) {
  }
}

void SeekToPosition(double ratio) {
  try {
    if (!g_SessionManager)
      return;
    auto session = g_SessionManager.GetCurrentSession();
    if (session) {
      auto timeline = session.GetTimelineProperties();
      auto totalTicks = timeline.EndTime().count(); // 100-nanosecond ticks
      long long seekTicks = (long long)(ratio * totalTicks);
      if (seekTicks < 0)
        seekTicks = 0;
      if (seekTicks > totalTicks)
        seekTicks = totalTicks;
      session.TryChangePlaybackPositionAsync(seekTicks);
    }
  } catch (...) {
  }
}

// --- Visuals ---
bool IsTransparencyEnabled() {
  DWORD value = 1;
  DWORD size = sizeof(value);
  if (RegGetValueW(
          HKEY_CURRENT_USER,
          L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
          L"EnableTransparency", RRF_RT_DWORD, nullptr, &value,
          &size) == ERROR_SUCCESS) {
    return value != 0;
  }
  return true;
}

bool IsAnimationEnabled() {
  BOOL animate = TRUE;
  SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animate, 0);
  return animate != FALSE;
}

wstring GetUserLanguageTag() {
  wchar_t localeName[LOCALE_NAME_MAX_LENGTH];
  if (GetUserDefaultLocaleName(localeName, LOCALE_NAME_MAX_LENGTH) > 0) {
    wstring loc(localeName);
    if (loc.length() >= 2) {
      return loc.substr(0, 2);
    }
  }
  return L"en";
}

wstring GetLocalizedText(UIStringId id) {
  wstring lang = GetUserLanguageTag();
  for (auto &c : lang)
    c = towlower(c);

  if (lang == L"pt") {
    switch (id) {
    case UIStringId::LyricsHeader:
      return L"Letras";
    case UIStringId::TranslateButton:
      return L"Traduzir";
    case UIStringId::FetchingLyrics:
      return L"Buscando letras...";
    case UIStringId::LyricsNotFound:
      return L"Letra não encontrada para esta música.";
    }
  } else if (lang == L"es") {
    switch (id) {
    case UIStringId::LyricsHeader:
      return L"Letras";
    case UIStringId::TranslateButton:
      return L"Traducir";
    case UIStringId::FetchingLyrics:
      return L"Buscando letras...";
    case UIStringId::LyricsNotFound:
      return L"Letra no encontrada para esta canción.";
    }
  }

  // Fallback padrão (Inglês)
  switch (id) {
  case UIStringId::LyricsHeader:
    return L"Lyrics";
  case UIStringId::TranslateButton:
    return L"Translate";
  case UIStringId::FetchingLyrics:
    return L"Fetching lyrics...";
  case UIStringId::LyricsNotFound:
    return L"Lyrics not found for this track.";
  }
  return L"";
}

bool IsSystemLightMode() {
  DWORD value = 0;
  DWORD size = sizeof(value);
  if (RegGetValueW(
          HKEY_CURRENT_USER,
          L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
          L"SystemUsesLightTheme", RRF_RT_DWORD, nullptr, &value,
          &size) == ERROR_SUCCESS) {
    return value != 0;
  }
  return false;
}

COLORREF GetThemeColorKey() {
  return IsSystemLightMode() ? RGB(243, 243, 243) : RGB(0, 0, 0);
}

Color GetThemeGdiplusColorKey() {
  return IsSystemLightMode() ? Color(255, 243, 243, 243) : Color(255, 0, 0, 0);
}

Color GetWindowsAccentColor() {
  HKEY hKey;
  if (RegOpenKeyExW(
          HKEY_CURRENT_USER,
          L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent", 0,
          KEY_READ, &hKey) == ERROR_SUCCESS) {
    BYTE palette[32];
    DWORD size = sizeof(palette);
    if (RegQueryValueExW(hKey, L"AccentPalette", nullptr, nullptr, palette,
                         &size) == ERROR_SUCCESS &&
        size == 32) {
      RegCloseKey(hKey);

      // AccentPalette (8 cores ARGB de 4 bytes)
      // Index 3 (offset 12) = Base
      // Index 1 (offset 4)  = Lighter shade (usado no painel nativo em Dark
      // Mode) Index 4 (offset 16) = Darker shade (usado no painel nativo em
      // Light Mode)
      int colorIndex = IsSystemLightMode() ? 4 : 1;

      int offset = colorIndex * 4;
      BYTE r = palette[offset];
      BYTE g = palette[offset + 1];
      BYTE b = palette[offset + 2];

      return Color(255, r, g, b);
    }
    RegCloseKey(hKey);
  }

  // Fallback
  DWORD color = 0;
  BOOL opaque = FALSE;
  if (SUCCEEDED(DwmGetColorizationColor(&color, &opaque))) {
    BYTE r = (color >> 16) & 0xFF;
    BYTE g = (color >> 8) & 0xFF;
    BYTE b = color & 0xFF;
    return Color(255, r, g, b);
  }
  return Color(255, 0, 120, 215);
}

Color GetAccentForegroundColor(Color bg) {
  double lum =
      0.299 * bg.GetRed() + 0.587 * bg.GetGreen() + 0.114 * bg.GetBlue();
  return lum > 128.0 ? Color(255, 0, 0, 0) : Color(255, 255, 255, 255);
}

DWORD GetCurrentTextColor() {
  return IsSystemLightMode() ? 0xFF2D2D2D : 0xFFFFFFFF;
}

void UpdateAppearance(HWND hwnd) {
  // Remove cantos arredondados (quadrado) para integrá-lo de forma plana
  DWM_WINDOW_CORNER_PREFERENCE preference =
      (DWM_WINDOW_CORNER_PREFERENCE)DWMWCP_DONOTROUND;
  DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &preference,
                        sizeof(preference));

  // Desativa a sombra nativa do DWM para não parecer flutuante
  int ncrenderPolicy = DWMNCRP_DISABLED;
  DwmSetWindowAttribute(hwnd, DWMWA_NCRENDERING_POLICY, &ncrenderPolicy,
                        sizeof(ncrenderPolicy));

  // Remove borda nativa
  DWORD borderColor = DWMWA_COLOR_NONE;
  DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &borderColor,
                        sizeof(borderColor));

  HMODULE hUser = GetModuleHandle(L"user32.dll");
  if (hUser) {
    auto SetComp = (pSetWindowCompositionAttribute)GetProcAddress(
        hUser, "SetWindowCompositionAttribute");
    if (SetComp) {
      // Estado desativado garante que o fundo seja 100% transparente
      ACCENT_POLICY policy = {ACCENT_DISABLED, 0, 0, 0};
      WINDOWCOMPOSITIONATTRIBDATA data = {WCA_ACCENT_POLICY, &policy,
                                          sizeof(ACCENT_POLICY)};
      SetComp(hwnd, &data);
    }
  }
}

void AddRoundedRect(GraphicsPath &path, int x, int y, int w, int h, int r) {
  int d = r * 2;
  path.AddArc(x, y, d, d, 180, 90);
  path.AddArc(x + w - d, y, d, d, 270, 90);
  path.AddArc(x + w - d, y + h - d, d, d, 0, 90);
  path.AddArc(x, y + h - d, d, d, 90, 90);
  path.CloseFigure();
}

void AddRoundedRectF(GraphicsPath &path, REAL x, REAL y, REAL w, REAL h,
                     REAL r) {
  REAL d = r * 2.0f;
  path.AddArc(x, y, d, d, 180.0f, 90.0f);
  path.AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
  path.AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f);
  path.AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
  path.CloseFigure();
}

void DrawCustomIcon(Graphics &graphics, const wstring &iconType, float x,
                    float y, float w, float h) {
  graphics.SetSmoothingMode(SmoothingModeAntiAlias);

  if (iconType == L"ytmusic") {
    // Red circle
    SolidBrush redBrush(Color(255, 255, 0, 0));
    graphics.FillEllipse(&redBrush, x, y, w, h);

    // White ring
    Pen whitePen(Color(255, 255, 255, 255), w * 0.08f);
    float ringPad = w * 0.2f;
    graphics.DrawEllipse(&whitePen, x + ringPad, y + ringPad, w - 2 * ringPad,
                         h - 2 * ringPad);

    // White triangle
    float triPadX = w * 0.38f;
    float triPadY = h * 0.32f;
    PointF pts[3] = {PointF(x + triPadX, y + triPadY),
                     PointF(x + triPadX, y + h - triPadY),
                     PointF(x + w - triPadX + w * 0.05f, y + h / 2.0f)};
    SolidBrush whiteBrush(Color(255, 255, 255, 255));
    graphics.FillPolygon(&whiteBrush, pts, 3);
  } else if (iconType == L"youtube") {
    // Red rounded rectangle
    SolidBrush redBrush(Color(255, 255, 0, 0));
    GraphicsPath path;
    AddRoundedRectF(path, x, y + h * 0.1f, w, h * 0.8f, w * 0.15f);
    graphics.FillPath(&redBrush, &path);

    // White triangle
    float triPadX = w * 0.38f;
    float triPadY = h * 0.32f;
    PointF pts[3] = {PointF(x + triPadX, y + triPadY),
                     PointF(x + triPadX, y + h - triPadY),
                     PointF(x + w - triPadX + w * 0.05f, y + h / 2.0f)};
    SolidBrush whiteBrush(Color(255, 255, 255, 255));
    graphics.FillPolygon(&whiteBrush, pts, 3);
  } else if (iconType == L"spotify") {
    // Green circle
    SolidBrush greenBrush(Color(255, 30, 215, 96));
    graphics.FillEllipse(&greenBrush, x, y, w, h);

    // Black curved lines (waves)
    Pen blackPen(Color(255, 0, 0, 0), w * 0.08f);
    blackPen.SetStartCap(LineCapRound);
    blackPen.SetEndCap(LineCapRound);

    graphics.DrawArc(&blackPen, (REAL)(x + w * 0.2f), (REAL)(y + h * 0.25f),
                     (REAL)(w * 0.6f), (REAL)(h * 0.6f), -140.0f, 100.0f);
    graphics.DrawArc(&blackPen, (REAL)(x + w * 0.28f), (REAL)(y + h * 0.37f),
                     (REAL)(w * 0.44f), (REAL)(h * 0.44f), -140.0f, 100.0f);
    graphics.DrawArc(&blackPen, (REAL)(x + w * 0.36f), (REAL)(y + h * 0.5f),
                     (REAL)(w * 0.28f), (REAL)(h * 0.28f), -140.0f, 100.0f);
  } else if (iconType == L"soundcloud") {
    // Orange cloud shape
    SolidBrush orangeBrush(Color(255, 255, 85, 0));
    graphics.FillEllipse(&orangeBrush, x + w * 0.1f, y + h * 0.35f, w * 0.45f,
                         h * 0.5f);
    graphics.FillEllipse(&orangeBrush, x + w * 0.35f, y + h * 0.2f, w * 0.55f,
                         h * 0.65f);
    graphics.FillRectangle(&orangeBrush, x + w * 0.3f, y + h * 0.45f, w * 0.45f,
                           h * 0.4f);
  } else if (iconType == L"netflix") {
    Pen redPen(Color(255, 229, 9, 20), w * 0.15f);
    redPen.SetStartCap(LineCapFlat);
    redPen.SetEndCap(LineCapFlat);

    graphics.DrawLine(&redPen, x + w * 0.2f, y + h * 0.1f, x + w * 0.2f,
                      y + h * 0.9f);
    graphics.DrawLine(&redPen, x + w * 0.8f, y + h * 0.1f, x + w * 0.8f,
                      y + h * 0.9f);

    Pen darkRedPen(Color(255, 185, 9, 15), w * 0.16f);
    graphics.DrawLine(&darkRedPen, x + w * 0.2f, y + h * 0.1f, x + w * 0.8f,
                      y + h * 0.9f);
  }
}

void DrawMediaPanel(Graphics &graphics, int width, int height) {
  graphics.SetSmoothingMode(SmoothingModeAntiAlias);
  graphics.SetTextRenderingHint(TextRenderingHintAntiAlias);
  graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
  graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);

  // Limpa o fundo dependendo da transparência do sistema
  if (IsTransparencyEnabled()) {
    graphics.Clear(Color(0, 0, 0, 0));
  } else {
    if (IsSystemLightMode()) {
      graphics.Clear(Color(255, 243, 243, 243));
    } else {
      graphics.Clear(Color(255, 28, 28, 28)); // #1c1c1c
    }
  }

  Color mainColor{GetCurrentTextColor()};

  MediaState state;
  {
    lock_guard<mutex> guard(g_MediaState.lock);
    state.title = g_MediaState.title;
    state.artist = g_MediaState.artist;
    state.albumArt =
        g_MediaState.albumArt ? g_MediaState.albumArt->Clone() : nullptr;
    state.hasMedia = g_MediaState.hasMedia;
    state.isPlaying = g_MediaState.isPlaying;
  }

  // 1. Album Art (Rounded)
  int artSize = height - 12;
  int artX = 6;
  int artY = 6;

  GraphicsPath path;
  AddRoundedRect(path, artX, artY, artSize, artSize, 8);

  if (state.albumArt) {
    TextureBrush brush(state.albumArt);
    brush.SetWrapMode(WrapModeClamp); // Evita artefatos de borda branca/preta
    brush.ScaleTransform((REAL)artSize / state.albumArt->GetWidth(),
                         (REAL)artSize / state.albumArt->GetHeight());
    brush.TranslateTransform((REAL)artX, (REAL)artY, MatrixOrderAppend);

    graphics.FillPath(&brush, &path);
    delete state.albumArt;
  } else {
    SolidBrush placeBrush{Color(40, 128, 128, 128)};
    graphics.FillPath(&placeBrush, &path);
  }

  // 2. Controls (Scaled - Solid style)
  double scale = g_Settings.buttonScale;
  int startControlX = artX + artSize + (int)(20 * scale);
  int controlY = height / 2;

  SolidBrush activeBg{Color(IsSystemLightMode() ? 15 : 40, mainColor.GetRed(),
                            mainColor.GetGreen(), mainColor.GetBlue())};

  float iconW = 8.0f * (float)scale;
  float iconH = 12.0f * (float)scale;
  float gap = 32.0f * (float)scale;

  float hoverW = 28.0f * (float)scale;
  float hoverH = 38.0f * (float)scale;
  float hoverR = 4.0f * (float)scale;

  SolidBrush iconBrush(mainColor);

  // Prev
  float pX = (float)startControlX;
  if (g_HoverState == 1) {
    GraphicsPath hoverPath;
    AddRoundedRect(hoverPath, (int)(pX - hoverW / 2),
                   (int)(controlY - hoverH / 2), (int)hoverW, (int)hoverH,
                   (int)hoverR);
    graphics.FillPath(&activeBg, &hoverPath);
  }

  {
    float barW = 2.0f * (float)scale;
    float spacing = 1.5f * (float)scale;
    float leftX = pX - iconW / 2.0f;
    float rightX = pX + iconW / 2.0f;

    // Barra à esquerda
    graphics.FillRectangle(&iconBrush, leftX, (float)controlY - (iconH / 2.0f),
                           barW, iconH);

    // Triângulo à direita (apontando para a esquerda)
    float triLeftX = leftX + barW + spacing;
    PointF pts[3] = {PointF(triLeftX, (float)controlY),
                     PointF(rightX, (float)controlY - (iconH / 2.0f)),
                     PointF(rightX, (float)controlY + (iconH / 2.0f))};
    graphics.FillPolygon(&iconBrush, pts, 3);
  }

  // Play/Pause
  float plX = pX + gap;
  if (g_HoverState == 2) {
    GraphicsPath hoverPath;
    AddRoundedRect(hoverPath, (int)(plX - hoverW / 2),
                   (int)(controlY - hoverH / 2), (int)hoverW, (int)hoverH,
                   (int)hoverR);
    graphics.FillPath(&activeBg, &hoverPath);
  }

  if (state.isPlaying) {
    float barW = 2.5f * (float)scale;
    float barH = 12.0f * (float)scale;
    float gapW = 3.0f * (float)scale;
    float leftX = plX - (2.0f * barW + gapW) / 2.0f;

    graphics.FillRectangle(&iconBrush, leftX, (float)controlY - (barH / 2.0f),
                           barW, barH);
    graphics.FillRectangle(&iconBrush, leftX + barW + gapW,
                           (float)controlY - (barH / 2.0f), barW, barH);
  } else {
    float playW = 10.0f * (float)scale;
    float playH = 12.0f * (float)scale;
    PointF pts[3] = {
        PointF(plX - (playW / 2.0f), (float)controlY - (playH / 2.0f)),
        PointF(plX - (playW / 2.0f), (float)controlY + (playH / 2.0f)),
        PointF(plX + (playW / 2.0f), (float)controlY)};
    graphics.FillPolygon(&iconBrush, pts, 3);
  }

  // Next
  float nX = plX + gap;
  if (g_HoverState == 3) {
    GraphicsPath hoverPath;
    AddRoundedRect(hoverPath, (int)(nX - hoverW / 2),
                   (int)(controlY - hoverH / 2), (int)hoverW, (int)hoverH,
                   (int)hoverR);
    graphics.FillPath(&activeBg, &hoverPath);
  }

  {
    float barW = 2.0f * (float)scale;
    float spacing = 1.5f * (float)scale;
    float leftX = nX - iconW / 2.0f;
    float rightX = nX + iconW / 2.0f;
    float triW = iconW - barW - spacing;

    // Triângulo à esquerda (apontando para a direita)
    PointF pts[3] = {PointF(leftX, (float)controlY - (iconH / 2.0f)),
                     PointF(leftX, (float)controlY + (iconH / 2.0f)),
                     PointF(leftX + triW, (float)controlY)};
    graphics.FillPolygon(&iconBrush, pts, 3);

    // Barra à direita
    graphics.FillRectangle(&iconBrush, rightX - barW,
                           (float)controlY - (iconH / 2.0f), barW, iconH);
  }

  // 3. Text
  int textX = (int)(nX + (20 * scale));
  int textMaxW = width - textX - 10;

  wstring fullText = state.title;
  if (!state.artist.empty())
    fullText += L" • " + state.artist;

  FontFamily fontFamily(FONT_NAME, nullptr);
  Font font(&fontFamily, (REAL)g_Settings.fontSize, FontStyleBold, UnitPixel);
  SolidBrush textBrush{mainColor};

  RectF layoutRect(0, 0, 2000, 100);
  RectF boundRect;
  graphics.MeasureString(fullText.c_str(), -1, &font, layoutRect, &boundRect);
  g_TextWidth = (int)boundRect.Width;

  Region textClip(Rect(textX, 0, textMaxW, height));
  graphics.SetClip(&textClip);

  float textY = (height - boundRect.Height) / 2.0f;

  if (g_TextWidth > textMaxW) {
    g_IsScrolling = true;
    float drawX = (float)(textX - g_ScrollOffset);
    graphics.DrawString(fullText.c_str(), -1, &font, PointF(drawX, textY),
                        &textBrush);
    if (drawX + g_TextWidth < width) {
      graphics.DrawString(fullText.c_str(), -1, &font,
                          PointF(drawX + g_TextWidth + 40, textY), &textBrush);
    }
  } else {
    g_IsScrolling = false;
    g_ScrollOffset = 0;
    graphics.DrawString(fullText.c_str(), -1, &font,
                        PointF((float)textX, textY), &textBrush);
  }
}

// --- Event Hook ---
bool IsTaskbarWindow(HWND hwnd) {
  WCHAR cls[64];
  if (!hwnd)
    return false;
  GetClassNameW(hwnd, cls, ARRAYSIZE(cls));
  return wcscmp(cls, L"Shell_TrayWnd") == 0;
}

void CALLBACK TaskbarEventProc(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG,
                               LONG, DWORD, DWORD) {
  if (!IsTaskbarWindow(hwnd) || !g_hMediaWindow)
    return;
  PostMessage(g_hMediaWindow, WM_APP + 10, 0, 0);
}

// Register Event Hook scoped to Taskbar Thread
void RegisterTaskbarHook(HWND hwnd) {
  HWND hTaskbar = FindWindow(L"Shell_TrayWnd", nullptr);
  if (hTaskbar) {
    DWORD pid = 0;
    DWORD tid = GetWindowThreadProcessId(hTaskbar, &pid);
    if (tid != 0) {
      g_TaskbarHook = SetWinEventHook(
          EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE, nullptr,
          TaskbarEventProc, pid, tid,
          WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    }
  }
  PostMessage(hwnd, WM_APP + 10, 0, 0);
}

// --- Window Procedure ---
#define IDT_POLL_MEDIA 1001
#define IDT_ANIMATION 1002
#define APP_WM_CLOSE WM_APP

void RedrawLayeredWindow(HWND hwnd) {
  RECT rc;
  GetWindowRect(hwnd, &rc);
  int w = rc.right - rc.left;
  int h = rc.bottom - rc.top;
  if (w <= 0 || h <= 0)
    return;

  HDC hdcScreen = GetDC(NULL);
  HDC memDC = CreateCompatibleDC(hdcScreen);

  BITMAPINFO bmi = {0};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = w;
  bmi.bmiHeader.biHeight = -h; // Top-down
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  void *pvBits = nullptr;
  HBITMAP memBitmap =
      CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, &pvBits, NULL, 0);
  HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

  // Use a temporary GDI+ Bitmap to render first, preserving the alpha chan nel
  // with premultiplied alpha
  Bitmap bmp(w, h, PixelFormat32bppPARGB);
  {
    Graphics gTemp(&bmp);
    gTemp.SetSmoothingMode(SmoothingModeAntiAlias);
    gTemp.SetTextRenderingHint(TextRenderingHintAntiAlias);
    gTemp.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    gTemp.SetPixelOffsetMode(PixelOffsetModeHighQuality);

    DrawMediaPanel(gTemp, w, h);
  }

  // Copia os pixels da Bitmap GDI+ diretamente para o buffer DIB do memDC para
  // preservar o canal alfa intacto
  BitmapData bmpData;
  Rect rect(0, 0, w, h);
  if (bmp.LockBits(&rect, ImageLockModeRead, PixelFormat32bppPARGB, &bmpData) ==
      Ok) {
    BYTE *pSrc = (BYTE *)bmpData.Scan0;
    BYTE *pDst = (BYTE *)pvBits;
    int rowSize = w * 4;
    for (int y = 0; y < h; y++) {
      memcpy(pDst + y * rowSize, pSrc + y * bmpData.Stride, rowSize);
    }
    bmp.UnlockBits(&bmpData);
  }

  POINT ptSrc = {0, 0};
  POINT ptDest = {rc.left, rc.top};
  SIZE sizeDest = {w, h};
  BLENDFUNCTION blend = {0};
  blend.BlendOp = AC_SRC_OVER;
  blend.BlendFlags = 0;
  blend.SourceConstantAlpha = 255;
  blend.AlphaFormat = AC_SRC_ALPHA;

  UpdateLayeredWindow(hwnd, hdcScreen, &ptDest, &sizeDest, memDC, &ptSrc, 0,
                      &blend, ULW_ALPHA);

  SelectObject(memDC, oldBitmap);
  DeleteObject(memBitmap);
  DeleteDC(memDC);
  ReleaseDC(NULL, hdcScreen);

  if (g_IsScrolling) {
    SetTimer(hwnd, IDT_ANIMATION, 16, NULL);
  }
}

#define IDT_FLYOUT_ANIMATION 2001
#define IDT_FLYOUT_MOUSE_CHECK 2002

wstring FormatTime(int seconds) {
  int m = seconds / 60;
  int s = seconds % 60;
  wchar_t buf[32];
  swprintf_s(buf, L"%d:%02d", m, s);
  return buf;
}

void UpdateFlyoutComposition(HWND hwnd) {
  // Informa ao DWM qual tema (claro/escuro) aplicar na janela para a
  // borda/sombra nativa
  BOOL isDarkMode = !IsSystemLightMode();
  DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &isDarkMode,
                        sizeof(isDarkMode));

  // Utilizamos SetWindowCompositionAttribute em vez de
  // DWMWA_SYSTEMBACKDROP_TYPE. Motivo: DWMWA_SYSTEMBACKDROP_TYPE (Acrylic
  // nativo do Win11) perde o desfoque e fica opaco (esbranquiçado/acinzentado)
  // quando a janela perde o foco. Como nosso flyout tem WS_EX_NOACTIVATE (nunca
  // tem foco), ele sempre ficaria opaco. A API legada força o desfoque
  // contínuo, independente do foco.
  HMODULE hUser = GetModuleHandle(L"user32.dll");
  if (hUser) {
    auto SetComp = (pSetWindowCompositionAttribute)GetProcAddress(
        hUser, "SetWindowCompositionAttribute");
    if (SetComp) {
      ACCENT_POLICY policy = {ACCENT_DISABLED, 0, 0, 0};
      if (IsTransparencyEnabled()) {
        policy.AccentState = ACCENT_ENABLE_ACRYLICBLURBEHIND;
        policy.AccentFlags = 2; // Habilita a cor de tonalidade (tint color)
        // Canal alfa confere o tom fosco perfeito à transparência
        if (IsSystemLightMode()) {
          policy.GradientColor = 0xD8F3F3F3; // Tint claro acrílico (alfa 0xD8)
        } else {
          policy.GradientColor = 0xD8202020; // Tint escuro acrílico (alfa 0xD8)
        }
      } else {
        policy.AccentState = ACCENT_DISABLED;
      }
      WINDOWCOMPOSITIONATTRIBDATA data = {WCA_ACCENT_POLICY, &policy,
                                          sizeof(ACCENT_POLICY)};
      SetComp(hwnd, &data);
    }
  }
}

void DrawFlyoutPanel(Graphics &graphics, int w, int h) {
  graphics.SetSmoothingMode(SmoothingModeAntiAlias);
  graphics.SetTextRenderingHint(TextRenderingHintAntiAlias);
  graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
  graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);

  // Fator de escala DPI dinâmico (baseado no tamanho padrão 360x150)
  float scale = (float)w / 360.0f;

  int hMax = h; // h é hMax (altura total física da janela)
  int hBase = (int)(g_FlyoutBaseHeight * scale);
  int contentYOffset = hMax - hBase;
  int hCurr = hMax;
  float Y_top = 0.0f;

  // Windows 11 DWMWCP_ROUND usa exatamente 8px de raio nativo.
  float radius = 8.0f * scale;

  // Limpa o fundo dependendo da transparência do Windows
  Color bgColor;
  Color borderColor;
  if (IsTransparencyEnabled()) {
    // Fundo totalmente transparente para que apareça apenas o desfoque Acrylic
    bgColor = Color(0, 0, 0, 0);
    if (IsSystemLightMode()) {
      borderColor = Color(30, 0, 0, 0); // Borda cinza sutil padrão Win11
    } else {
      borderColor = Color(40, 255, 255,
                          255); // Borda branca sutil padrão Win11 (mais nítida)
    }
  } else {
    // Sem transparência: fundo 100% opaco
    if (IsSystemLightMode()) {
      bgColor = Color(255, 243, 243, 243);
      borderColor = Color(30, 0, 0, 0); // Borda sólida cinza sutil
    } else {
      bgColor = Color(255, 32, 32, 32);
      borderColor = Color(30, 255, 255, 255); // Borda sólida branca sutil
    }
  }

  graphics.Clear(
      Color(0, 0, 0, 0)); // Limpa com transparente para o contorno do flyout

  // Cria o retângulo com cantos arredondados para a área visível do Flyout
  GraphicsPath flyoutPath;
  AddRoundedRectF(flyoutPath, 0.5f, Y_top + 0.5f, (REAL)w - 1.0f,
                  (REAL)hCurr - 1.0f, radius);

  // Preenche o fundo e desenha a borda
  SolidBrush bgBrush(bgColor);
  graphics.FillPath(&bgBrush, &flyoutPath);

  Pen borderPen(borderColor, 1.0f);
  graphics.DrawPath(&borderPen, &flyoutPath);

  // Obtém o estado de mídia atual
  MediaState state;
  {
    lock_guard<mutex> guard(g_MediaState.lock);
    state.title = g_MediaState.title;
    state.artist = g_MediaState.artist;
    state.albumTitle = g_MediaState.albumTitle;
    state.albumArtist = g_MediaState.albumArtist;
    state.durationTotalSeconds = g_MediaState.durationTotalSeconds;
    state.durationPositionSeconds = g_MediaState.durationPositionSeconds;
    state.albumArt =
        g_MediaState.albumArt ? g_MediaState.albumArt->Clone() : nullptr;
    state.hasMedia = g_MediaState.hasMedia;
    state.sourceAppName = g_MediaState.sourceAppName;
    state.sourceAppIcon = g_MediaState.sourceAppIcon;
    state.appId = g_MediaState.appId;
    state.isBrowser = g_MediaState.isBrowser;
    state.webSiteName = g_MediaState.webSiteName;
    state.webSiteIcon = g_MediaState.webSiteIcon;
  }

  Color mainTextColor = GetCurrentTextColor();
  Color subTextColor =
      IsSystemLightMode() ? Color(180, 45, 45, 45) : Color(180, 255, 255, 255);
  Color detailTextColor =
      IsSystemLightMode() ? Color(130, 80, 80, 80) : Color(130, 180, 180, 180);

  StringFormat centerFormat;
  centerFormat.SetAlignment(StringAlignmentCenter);
  centerFormat.SetLineAlignment(StringAlignmentCenter);

  FontFamily testFamily(L"Segoe Fluent Icons", nullptr);
  const WCHAR *iconFontName = (testFamily.GetLastStatus() == Ok)
                                  ? L"Segoe Fluent Icons"
                                  : L"Segoe MDL2 Assets";
  FontFamily iconFontFamily(iconFontName, nullptr);
  Font iconFont(&iconFontFamily, 16.0f * scale, FontStyleRegular, UnitPixel);

  FontFamily fontFamily(FONT_NAME, nullptr);

  // Desenha a Letra da música se expandido
  if (contentYOffset > 10) {
    lock_guard<mutex> guard(g_LyricsMutex);

    // Save graphics state and clip all lyrics drawing to the active panel area
    GraphicsState lyricsStateSave = graphics.Save();
    graphics.SetClip(&flyoutPath);
    graphics.SetClip(RectF(0.0f, Y_top, (REAL)w, (REAL)contentYOffset),
                     CombineModeIntersect);

    float headerHeight = 45.0f * scale;

    // Draw header background (clipped to flyout rounded corners and
    // contentYOffset)
    SolidBrush headerBgBrush(IsSystemLightMode() ? Color(12, 0, 0, 0)
                                                 : Color(12, 255, 255, 255));
    graphics.FillRectangle(&headerBgBrush, 0.0f, Y_top, (REAL)w, headerHeight);

    // Draw separating border line
    Pen headerSepPen(borderColor, 1.0f);
    graphics.DrawLine(&headerSepPen, 0.0f, Y_top + headerHeight, (REAL)w,
                      Y_top + headerHeight);

    // Draw header text: "Letras"
    Font headerFont(&fontFamily, 12.0f * scale, FontStyleBold, UnitPixel);
    SolidBrush headerBrush(mainTextColor);
    StringFormat headerFormat;
    headerFormat.SetAlignment(StringAlignmentNear);
    headerFormat.SetLineAlignment(StringAlignmentCenter);

    RectF headerTextRect(15.0f * scale, Y_top, w - 150.0f * scale,
                         headerHeight);
    wstring headerText = GetLocalizedText(UIStringId::LyricsHeader);
    graphics.DrawString(headerText.c_str(), -1, &headerFont, headerTextRect,
                        &headerFormat, &headerBrush);

    // Measure header text to position the source text right after it
    RectF headerTextBound;
    graphics.MeasureString(headerText.c_str(), -1, &headerFont, headerTextRect,
                           &headerFormat, &headerTextBound);

    // Draw source text: "via [Fonte]"
    wstring sourceStr = L"via " + g_LyricsSource;
    Font sourceFont(&fontFamily, 9.5f * scale, FontStyleRegular, UnitPixel);
    SolidBrush sourceBrush(detailTextColor);
    RectF sourceRect(
        headerTextBound.X + headerTextBound.Width + 6.0f * scale, Y_top,
        w - (headerTextBound.X + headerTextBound.Width + 6.0f * scale) -
            100.0f * scale,
        headerHeight);
    graphics.DrawString(sourceStr.c_str(), -1, &sourceFont, sourceRect,
                        &headerFormat, &sourceBrush);

    // Draw buttons in the header
    float headerBtnH = 26.0f * scale;
    float pinBtnW = 26.0f * scale;
    float pinBtnX = w - pinBtnW - 15.0f * scale; // standard 15px right margin
    float pinBtnY = Y_top + (headerHeight - headerBtnH) / 2.0f;

    // Draw Pin Button (Icon only)
    GraphicsPath pinBtnPath;
    AddRoundedRectF(pinBtnPath, pinBtnX, pinBtnY, pinBtnW, headerBtnH,
                    4.0f * scale);
    Color accentColor = GetWindowsAccentColor();
    SolidBrush pinBtnBgBrush(g_FlyoutPinned ? accentColor
                                            : Color(30, 128, 128, 128));
    graphics.FillPath(&pinBtnBgBrush, &pinBtnPath);
    SolidBrush pinIconBrush(
        g_FlyoutPinned ? GetAccentForegroundColor(accentColor) : mainTextColor);
    RectF pinBtnRectf(pinBtnX, pinBtnY, pinBtnW, headerBtnH);

    // Smaller icon font for header buttons
    Font headerIconFont(&iconFontFamily, 12.0f * scale, FontStyleRegular,
                        UnitPixel);
    graphics.DrawString(g_FlyoutPinned ? L"\xE718" : L"\xE77A", -1,
                        &headerIconFont, pinBtnRectf, &centerFormat,
                        &pinIconBrush);

    // Draw Translate Button (Icon + Text "Traduzir")
    float transBtnW = 85.0f * scale;
    float transBtnX = pinBtnX - transBtnW - 8.0f * scale;
    float transBtnY = pinBtnY;

    GraphicsPath transBtnPath;
    AddRoundedRectF(transBtnPath, transBtnX, transBtnY, transBtnW, headerBtnH,
                    4.0f * scale);
    SolidBrush transBtnBgBrush(g_TranslateLyrics ? accentColor
                                                 : Color(30, 128, 128, 128));
    graphics.FillPath(&transBtnBgBrush, &transBtnPath);
    SolidBrush transIconBrush(g_TranslateLyrics
                                  ? GetAccentForegroundColor(accentColor)
                                  : mainTextColor);

    // Draw Translate Icon
    RectF transIconRect(transBtnX + 8.0f * scale, transBtnY, 14.0f * scale,
                        headerBtnH);
    graphics.DrawString(L"\xE8C1", -1, &headerIconFont, transIconRect,
                        &centerFormat, &transIconBrush);

    // Draw Translate Text "Traduzir"
    Font headerTextFont(&fontFamily, 9.5f * scale, FontStyleBold, UnitPixel);
    StringFormat leftFormat;
    leftFormat.SetAlignment(StringAlignmentNear);
    leftFormat.SetLineAlignment(StringAlignmentCenter);
    RectF transTextRect(transBtnX + 26.0f * scale, transBtnY,
                        transBtnW - 30.0f * scale, headerBtnH);
    wstring transText = GetLocalizedText(UIStringId::TranslateButton);
    graphics.DrawString(transText.c_str(), -1, &headerTextFont, transTextRect,
                        &leftFormat, &transIconBrush);

    float lyricsPaddingTop = 10.0f * scale;
    float lyricsPaddingBottom = 15.0f * scale;
    float lyricsHeight =
        contentYOffset - headerHeight - lyricsPaddingTop - lyricsPaddingBottom;
    RectF lyricsRect((REAL)(15 * scale),
                     (REAL)(Y_top + headerHeight + lyricsPaddingTop),
                     (REAL)(w - 30 * scale), (REAL)lyricsHeight);

    StringFormat lyricsFormat;
    lyricsFormat.SetAlignment(StringAlignmentCenter);
    lyricsFormat.SetLineAlignment(StringAlignmentCenter);

    Font lyricsFont(&fontFamily, 12.0f * scale, FontStyleRegular, UnitPixel);
    SolidBrush lyricsBrush(mainTextColor);

    if (g_IsFetchingLyrics) {
      graphics.SetClip(lyricsRect);
      wstring fetchingText = GetLocalizedText(UIStringId::FetchingLyrics);
      graphics.DrawString(fetchingText.c_str(), -1, &lyricsFont, lyricsRect,
                          &lyricsFormat, &lyricsBrush);
      graphics.ResetClip();
    } else if (!g_HasLyrics) {
      graphics.SetClip(lyricsRect);
      wstring notFoundText = GetLocalizedText(UIStringId::LyricsNotFound);
      graphics.DrawString(notFoundText.c_str(), -1, &lyricsFont, lyricsRect,
                          &lyricsFormat, &lyricsBrush);
      graphics.ResetClip();
    } else {
      // Render Lyrics
      graphics.SetClip(lyricsRect);
      if (!g_SyncedLyrics.empty()) {
        // Encontra a linha atual
        int currentSecs = state.durationPositionSeconds;
        int activeIndex = 0;
        for (size_t i = 0; i < g_SyncedLyrics.size(); ++i) {
          if (currentSecs >= g_SyncedLyrics[i].timeSeconds) {
            activeIndex = i;
          } else {
            break;
          }
        }

        bool activeHasTranslation = false;
        float origHeight = 0.0f;
        float transHeight = 0.0f;
        float normalLineH = 28.0f * scale;
        float activeLineH = 44.0f * scale;
        float activeMargin = 10.0f * scale;

        if (activeIndex >= 0 && activeIndex < (int)g_SyncedLyrics.size()) {
          Font activeFont(&fontFamily, 15.0f * scale, FontStyleBold, UnitPixel);
          RectF layoutRect(0, 0, lyricsRect.Width, 10000.0f);
          RectF origBound;
          graphics.MeasureString(g_SyncedLyrics[activeIndex].text.c_str(), -1,
                                 &activeFont, layoutRect, &origBound);
          origHeight = origBound.Height;

          activeHasTranslation =
              g_TranslateLyrics &&
              !g_SyncedLyrics[activeIndex].translatedText.empty();
          if (activeHasTranslation) {
            Font transFont(&fontFamily, 13.0f * scale, FontStyleRegular,
                           UnitPixel);
            float transLayoutW = lyricsRect.Width - 36.0f * scale;
            if (transLayoutW < 50.0f)
              transLayoutW = 50.0f;
            RectF transLayoutRect(0, 0, transLayoutW, 10000.0f);
            RectF transBound;
            graphics.MeasureString(
                g_SyncedLyrics[activeIndex].translatedText.c_str(), -1,
                &transFont, transLayoutRect, &transBound);
            transHeight = transBound.Height + 12.0f * scale;
          }

          float origTransGap = 8.0f * scale;
          activeLineH = origHeight;
          if (activeHasTranslation) {
            activeLineH += origTransGap + transHeight;
          }
        }

        float totalBeforeActive = 0.0f;
        for (int i = 0; i < activeIndex; ++i) {
          totalBeforeActive += normalLineH;
        }
        totalBeforeActive += activeMargin;

        float centerY =
            lyricsRect.Y + (lyricsHeight / 2.0f) - (activeLineH / 2.0f);
        float scrollOrigin = centerY - totalBeforeActive;

        float currentY = scrollOrigin;
        for (size_t i = 0; i < g_SyncedLyrics.size(); ++i) {
          bool isActive = ((int)i == activeIndex);
          float thisLineH = isActive ? activeLineH : normalLineH;

          if (isActive)
            currentY += activeMargin;

          if (currentY > lyricsRect.Y - thisLineH - 20.0f &&
              currentY < lyricsRect.Y + lyricsHeight + thisLineH + 20.0f) {
            float fontSize = isActive ? 15.0f * scale : 13.0f * scale;
            int fontStyle = isActive ? FontStyleBold : FontStyleRegular;
            Font lineFont(&fontFamily, fontSize, fontStyle, UnitPixel);

            Color lineColor = isActive ? mainTextColor
                                       : Color(100, mainTextColor.GetRed(),
                                               mainTextColor.GetGreen(),
                                               mainTextColor.GetBlue());
            SolidBrush lineBrush(lineColor);

            bool showTranslation = isActive && activeHasTranslation;
            if (showTranslation) {
              // Draw original text
              RectF origRect(lyricsRect.X, currentY, lyricsRect.Width,
                             origHeight);
              graphics.DrawString(g_SyncedLyrics[i].text.c_str(), -1, &lineFont,
                                  origRect, &lyricsFormat, &lineBrush);

              // Draw translation card
              float cardX = lyricsRect.X + 10.0f * scale;
              float cardW = lyricsRect.Width - 20.0f * scale;
              float cardY = currentY + origHeight + 8.0f * scale;
              float cardH = transHeight;

              GraphicsPath cardPath;
              AddRoundedRectF(cardPath, cardX, cardY, cardW, cardH,
                              6.0f * scale);

              Color accent = GetWindowsAccentColor();
              SolidBrush cardBgBrush(Color(
                  25, accent.GetRed(), accent.GetGreen(), accent.GetBlue()));
              graphics.FillPath(&cardBgBrush, &cardPath);

              Pen cardBorderPen(Color(50, accent.GetRed(), accent.GetGreen(),
                                      accent.GetBlue()),
                                1.0f);
              graphics.DrawPath(&cardBorderPen, &cardPath);

              // Draw translation text inside card
              Font transFont(&fontFamily, 13.0f * scale, FontStyleRegular,
                             UnitPixel);
              SolidBrush transBrush(subTextColor);
              RectF transTextRect(cardX + 8.0f * scale, cardY + 6.0f * scale,
                                  cardW - 16.0f * scale, cardH - 12.0f * scale);
              graphics.DrawString(g_SyncedLyrics[i].translatedText.c_str(), -1,
                                  &transFont, transTextRect, &lyricsFormat,
                                  &transBrush);
            } else {
              float textHeight = isActive ? origHeight : normalLineH;
              RectF lineRect(lyricsRect.X, currentY, lyricsRect.Width,
                             textHeight);
              graphics.DrawString(g_SyncedLyrics[i].text.c_str(), -1, &lineFont,
                                  lineRect, &lyricsFormat, &lineBrush);
            }
          }

          currentY += thisLineH;

          if (isActive)
            currentY += activeMargin;
        }
      } else {
        // Plain lyrics
        StringFormat plainFormat;
        plainFormat.SetAlignment(StringAlignmentCenter);
        RectF drawRect(lyricsRect.X, lyricsRect.Y + g_LyricsScrollY,
                       lyricsRect.Width, 10000.0f);
        graphics.DrawString(g_PlainLyrics.c_str(), -1, &lyricsFont, drawRect,
                            &plainFormat, &lyricsBrush);
      }
      graphics.ResetClip();
    }
    graphics.Restore(lyricsStateSave);
  }

  int mediaYOffset = hMax - hBase;

  // Posicionado em [15, 15, 120, 120] (escalado)
  int artSize = (int)(120 * scale);
  int artX = (int)(15 * scale);
  int artY = mediaYOffset + (int)(15 * scale);

  GraphicsPath artPath;
  AddRoundedRect(artPath, artX, artY, artSize, artSize, (int)(10 * scale));

  if (state.albumArt) {
    TextureBrush brush(state.albumArt);
    brush.SetWrapMode(WrapModeClamp);
    brush.ScaleTransform((REAL)artSize / state.albumArt->GetWidth(),
                         (REAL)artSize / state.albumArt->GetHeight());
    brush.TranslateTransform((REAL)artX, (REAL)artY, MatrixOrderAppend);
    graphics.FillPath(&brush, &artPath);
    delete state.albumArt;
  } else {
    // Desenha um fundo padrão para a capa se não houver imagem
    SolidBrush placeBrush{Color(40, 128, 128, 128)};
    graphics.FillPath(&placeBrush, &artPath);
  }

  // 2. Metadados (Lado Direito)
  // Posição x = 150, y = 15, largura máxima = w - 150 - 15 (escalado)
  int textX = (int)(150 * scale);
  int textY = mediaYOffset + (int)(15 * scale);
  int textMaxW = w - textX - (int)(15 * scale);

  // Título da Música (Negrito, maior)
  Font titleFont(&fontFamily, 13.0f * scale, FontStyleBold, UnitPixel);
  SolidBrush titleBrush(mainTextColor);

  // Artista (Regular, médio)
  Font artistFont(&fontFamily, 11.0f * scale, FontStyleRegular, UnitPixel);
  SolidBrush artistBrush(subTextColor);

  // Álbum (Regular, cinza sutil)
  Font albumFont(&fontFamily, 10.0f * scale, FontStyleRegular, UnitPixel);
  SolidBrush albumBrush(detailTextColor);

  // Exibe Título
  RectF titleRect((REAL)textX, (REAL)textY, (REAL)textMaxW, 20.0f * scale);
  StringFormat textFormat;
  textFormat.SetTrimming(StringTrimmingEllipsisCharacter);
  textFormat.SetFormatFlags(
      StringFormatFlagsNoWrap); // Não quebra linha para manter alinhado
  graphics.DrawString(state.title.c_str(), -1, &titleFont, titleRect,
                      &textFormat, &titleBrush);

  // Exibe Artista
  RectF artistRect((REAL)textX, (REAL)textY + (22.0f * scale), (REAL)textMaxW,
                   16.0f * scale);
  graphics.DrawString(state.artist.c_str(), -1, &artistFont, artistRect,
                      &textFormat, &artistBrush);

  // Exibe Álbum (se disponível)
  wstring albumText = state.albumTitle;
  if (!albumText.empty() && !state.albumArtist.empty() &&
      state.albumArtist != state.artist) {
    albumText += L" - " + state.albumArtist;
  }
  RectF albumRect((REAL)textX, (REAL)textY + (38.0f * scale), (REAL)textMaxW,
                  15.0f * scale);
  graphics.DrawString(albumText.c_str(), -1, &albumFont, albumRect, &textFormat,
                      &albumBrush);

  // 3. Barra de Progresso da Música e Duração (Slider estilo Windows)
  if (state.hasMedia && state.durationTotalSeconds > 0) {
    float barY = (float)(textY + (62.0f * scale));
    float barH = 4.0f * scale;

    // Salva coordenadas globais para hit-testing no WndProc
    g_ProgressBarX = (float)textX;
    g_ProgressBarY = barY;
    g_ProgressBarW = (float)textMaxW;
    g_ProgressBarH = barH;

    // Razão de progresso (usa a posição do drag se arrastando)
    double ratio;
    if (g_ProgressDragging) {
      ratio = g_DragRatio;
    } else {
      ratio =
          (double)state.durationPositionSeconds / state.durationTotalSeconds;
    }
    if (ratio < 0.0)
      ratio = 0.0;
    if (ratio > 1.0)
      ratio = 1.0;
    float fillW = (float)(textMaxW * ratio);

    // Retângulo de fundo da barra (parte não preenchida, depois do thumb)
    SolidBrush progressBgBrush(Color(40, mainTextColor.GetRed(),
                                     mainTextColor.GetGreen(),
                                     mainTextColor.GetBlue()));
    GraphicsPath progBgPath;
    AddRoundedRect(progBgPath, textX, (int)barY, textMaxW, (int)barH,
                   (int)(2.0f * scale));
    graphics.FillPath(&progressBgBrush, &progBgPath);

    // Retângulo preenchido do progresso (antes do thumb)
    if (fillW > 0.0f) {
      SolidBrush progressFillBrush(GetWindowsAccentColor());
      GraphicsPath progFillPath;
      AddRoundedRect(progFillPath, textX, (int)barY, (int)fillW, (int)barH,
                     (int)(2.0f * scale));
      graphics.FillPath(&progressFillBrush, &progFillPath);
    }

    // Thumb (bolinha circular estilo Windows)
    float thumbX = (float)textX + fillW;
    float thumbCenterY = barY + barH / 2.0f;

    // Anima o raio do thumb
    float smallRadius = 5.0f * scale; // raio quando não hover
    float largeRadius = 8.0f * scale; // raio quando hover/drag

    g_ThumbTargetRadius =
        (g_ProgressHover || g_ProgressDragging) ? largeRadius : smallRadius;

    // Interpolação suave do raio
    float animSpeed = 1.5f * scale;
    if (g_ThumbAnimRadius < g_ThumbTargetRadius) {
      g_ThumbAnimRadius += animSpeed;
      if (g_ThumbAnimRadius > g_ThumbTargetRadius)
        g_ThumbAnimRadius = g_ThumbTargetRadius;
    } else if (g_ThumbAnimRadius > g_ThumbTargetRadius) {
      g_ThumbAnimRadius -= animSpeed;
      if (g_ThumbAnimRadius < g_ThumbTargetRadius)
        g_ThumbAnimRadius = g_ThumbTargetRadius;
    }

    // Se o raio é 0 (inicialização), seta direto
    if (g_ThumbAnimRadius <= 0.0f)
      g_ThumbAnimRadius = smallRadius;

    // Desenha o thumb com borda
    float r = g_ThumbAnimRadius;
    SolidBrush thumbBrush(GetWindowsAccentColor());
    graphics.FillEllipse(&thumbBrush, thumbX - r, thumbCenterY - r, r * 2.0f,
                         r * 2.0f);

    // Borda sutil ao redor do thumb
    Pen thumbBorder(Color(60, 0, 0, 0), 1.0f);
    graphics.DrawEllipse(&thumbBorder, thumbX - r, thumbCenterY - r, r * 2.0f,
                         r * 2.0f);

    // Texto do tempo atual / total
    int displaySeconds = g_ProgressDragging
                             ? (int)(g_DragRatio * state.durationTotalSeconds)
                             : state.durationPositionSeconds;
    wstring timeText = FormatTime(displaySeconds) + L" / " +
                       FormatTime(state.durationTotalSeconds);
    Font timeFont(&fontFamily, 9.5f * scale, FontStyleRegular, UnitPixel);
    SolidBrush timeBrush(detailTextColor);
    RectF timeRect((REAL)textX, (REAL)textY + (72.0f * scale), (REAL)textMaxW,
                   15.0f * scale);
    graphics.DrawString(timeText.c_str(), -1, &timeFont, timeRect, &textFormat,
                        &timeBrush);
  } else {
    // Se não houver duração detalhada, apenas desenha a legenda
    Font tagFont(&fontFamily, 9.0f * scale, FontStyleRegular, UnitPixel);
    SolidBrush tagBrush(detailTextColor);
    RectF tagRect((REAL)textX, (REAL)textY + (65.0f * scale), (REAL)textMaxW,
                  15.0f * scale);
    graphics.DrawString(L"Metadados de Mídia Ativos", -1, &tagFont, tagRect,
                        nullptr, &tagBrush);
  }

  // Linha separadora entre letras e metadados
  if (contentYOffset > 10) {
    Pen sepPen(borderColor, 1.0f);
    graphics.DrawLine(&sepPen, 0.0f, (REAL)mediaYOffset, (REAL)w,
                      (REAL)mediaYOffset);
  }

  // Tag de Origem da Mídia (App/Site) no canto inferior direito
  if (state.hasMedia && !state.sourceAppName.empty()) {
    float tagH = 26.0f * scale;
    float tagPadding = 8.0f * scale;

    Font tagFont(&fontFamily, 9.5f * scale, FontStyleBold, UnitPixel);
    SolidBrush tagTextBrush(subTextColor);

    Font tagIconFont(&iconFontFamily, 10.5f * scale, FontStyleRegular,
                     UnitPixel);
    SolidBrush tagIconBrush(subTextColor);

    RectF boundRect;
    StringFormat tagTextFormat;
    tagTextFormat.SetAlignment(StringAlignmentNear);
    tagTextFormat.SetLineAlignment(StringAlignmentCenter);

    float totalW = tagPadding;
    float iconW = 0.0f;
    float iconGap = 0.0f;
    float circleSize = 0.0f;
    float circleGap = 0.0f;

    if (state.isBrowser) {
      iconW = 14.0f * scale;
      iconGap = 5.0f * scale;
      totalW += iconW + iconGap;
    } else {
      circleSize = 8.0f * scale;
      circleGap = 6.0f * scale;
      totalW += circleSize + circleGap;
    }

    graphics.MeasureString(state.sourceAppName.c_str(), -1, &tagFont,
                           RectF(0, 0, 200.0f * scale, tagH), nullptr,
                           &boundRect);
    float textW = boundRect.Width;
    totalW += textW + tagPadding;

    float tagW = totalW;
    float tagX = w - tagW - (15.0f * scale);
    float tagY = h - tagH - (18.0f * scale);

    GraphicsPath tagPath;
    AddRoundedRectF(tagPath, tagX, tagY, tagW, tagH, tagH / 2.0f);

    SolidBrush tagBgBrush(IsSystemLightMode() ? Color(15, 0, 0, 0)
                                              : Color(20, 255, 255, 255));
    graphics.FillPath(&tagBgBrush, &tagPath);

    Pen tagBorderPen(IsSystemLightMode() ? Color(25, 0, 0, 0)
                                         : Color(30, 255, 255, 255),
                     1.0f);
    graphics.DrawPath(&tagBorderPen, &tagPath);

    float currX = tagX + tagPadding;

    if (state.isBrowser) {
      // Desenha o ícone do navegador (globo)
      RectF iconRect(currX, tagY, iconW, tagH);
      graphics.DrawString(state.sourceAppIcon.c_str(), -1, &tagIconFont,
                          iconRect, &centerFormat, &tagIconBrush);
      currX += iconW + iconGap;
    } else {
      // Desenha o círculo colorido do app nativo
      Color circleColor = GetWindowsAccentColor();
      if (state.sourceAppIcon == L"spotify") {
        circleColor = Color(255, 30, 215, 96);
      }
      SolidBrush circleBrush(circleColor);
      float circleY = tagY + (tagH - circleSize) / 2.0f;
      graphics.FillEllipse(&circleBrush, currX, circleY, circleSize,
                           circleSize);

      Pen circleBorderPen(Color(40, 0, 0, 0), 1.0f);
      graphics.DrawEllipse(&circleBorderPen, currX, circleY, circleSize,
                           circleSize);

      currX += circleSize + circleGap;
    }

    // Desenha o nome
    RectF nameRect(currX, tagY, textW + 4.0f * scale, tagH);
    graphics.DrawString(state.sourceAppName.c_str(), -1, &tagFont, nameRect,
                        &tagTextFormat, &tagTextBrush);
  }

  // 4. Controlador de Volume da Mídia (ao lado da pílula)
  if (state.hasMedia) {
    float tagH = 26.0f * scale;
    float tagY = h - tagH - (18.0f * scale);

    // Calcula tagW para sabermos onde a pílula começa (tagX)
    float tagPadding = 8.0f * scale;
    float totalW = tagPadding;
    if (state.isBrowser) {
      totalW += 14.0f * scale + 5.0f * scale;
    } else {
      totalW += 8.0f * scale + 6.0f * scale;
    }
    Font tagFont(&fontFamily, 9.5f * scale, FontStyleBold, UnitPixel);
    RectF boundRect;
    graphics.MeasureString(state.sourceAppName.c_str(), -1, &tagFont,
                           RectF(0, 0, 200.0f * scale, tagH), nullptr,
                           &boundRect);
    totalW += boundRect.Width + tagPadding;

    float tagX = w - totalW - (15.0f * scale);

    // Coordenadas do volume com alinhamento à esquerda (iniciando em textX)
    float volX = (float)textX;
    float volY = tagY;

    float volW = 75.0f * scale;
    float volIconSize = 14.0f * scale;
    float volGap = 6.0f * scale;
    float volTotalW = volIconSize + volGap + volW;

    g_VolumeX = volX + volIconSize + volGap;
    g_VolumeY = volY + (tagH - 4.0f * scale) / 2.0f;
    g_VolumeW = volW;
    g_VolumeH = 4.0f * scale;

    // Coordenadas de hit-test cobrem toda a parte inferior direita da janela
    // (do ícone até a borda direita)
    g_VolumeAreaX = volX - 20.0f * scale;
    g_VolumeAreaW = (float)w - g_VolumeAreaX;
    g_VolumeIconX = volX;
    g_VolumeIconW = volIconSize;

    // Obter volume atual do aplicativo
    float currentVol = 0.0f;
    if (g_VolumeDragging) {
      currentVol = g_VolumeDragRatio;
    } else {
      currentVol = GetVolumeLevel(state.appId);
    }

    // Detecta se o áudio está silenciado
    bool isMuted = GetAppMute(state.appId);

    // Desenha o ícone do alto-falante (sempre visível na posição fixa à
    // esquerda)
    const WCHAR *volIconChar = L"\xE994";
    if (isMuted || currentVol == 0.0f)
      volIconChar = L"\xE74F";
    else if (currentVol < 0.33f)
      volIconChar = L"\xE993";
    else if (currentVol < 0.66f)
      volIconChar = L"\xE994";
    else
      volIconChar = L"\xE995";

    Font volIconFont(&iconFontFamily, 11.5f * scale, FontStyleRegular,
                     UnitPixel);
    SolidBrush volIconBrush(subTextColor);

    RectF volIconRect(volX, volY, volIconSize, tagH);
    graphics.DrawString(volIconChar, -1, &volIconFont, volIconRect,
                        &centerFormat, &volIconBrush);

    // Desenha a barra de volume e o thumb instantaneamente apenas sob hover ou
    // arrasto (sem animação de expansão)
    if (g_VolumeHover || g_VolumeDragging) {
      // Barra de volume de fundo (cinza)
      SolidBrush volBgBrush(Color(40, mainTextColor.GetRed(),
                                  mainTextColor.GetGreen(),
                                  mainTextColor.GetBlue()));
      GraphicsPath volBgPath;
      AddRoundedRect(volBgPath, (int)g_VolumeX, (int)g_VolumeY, (int)g_VolumeW,
                     (int)g_VolumeH, (int)(2.0f * scale));
      graphics.FillPath(&volBgBrush, &volBgPath);

      // Barra de volume preenchida (cor neutra do texto com opacidade)
      float volFillW = g_VolumeW * currentVol;
      if (volFillW > 0.0f) {
        SolidBrush volFillBrush(Color(180, mainTextColor.GetRed(),
                                      mainTextColor.GetGreen(),
                                      mainTextColor.GetBlue()));
        GraphicsPath volFillPath;
        AddRoundedRect(volFillPath, (int)g_VolumeX, (int)g_VolumeY,
                       (int)volFillW, (int)g_VolumeH, (int)(2.0f * scale));
        graphics.FillPath(&volFillBrush, &volFillPath);
      }

      // Thumb da barra de volume
      float volThumbX = g_VolumeX + volFillW;
      float volThumbCenterY = g_VolumeY + g_VolumeH / 2.0f;

      float r = g_VolumeDragging ? 7.0f * scale : 5.0f * scale;

      SolidBrush volThumbBrush(mainTextColor);
      graphics.FillEllipse(&volThumbBrush, volThumbX - r, volThumbCenterY - r,
                           r * 2.0f, r * 2.0f);

      Pen volThumbBorder(Color(60, 0, 0, 0), 1.0f);
      graphics.DrawEllipse(&volThumbBorder, volThumbX - r, volThumbCenterY - r,
                           r * 2.0f, r * 2.0f);
    }
  }
}

void UpdateFlyoutWindow() {
  if (!g_hFlyoutWindow || !g_hFlyoutChildWindow)
    return;

  double scale = g_Settings.buttonScale;
  int w = (int)(360 * scale);
  int hMax = (int)(g_FlyoutExpandedHeight * scale);

  if (w <= 0 || hMax <= 0)
    return;

  // O offset da animação de slide-up depende de g_FlyoutOpenProgress
  int yOffsetAnimation =
      (int)((1.0f - g_FlyoutOpenProgress) * g_FlyoutStartOffset);

  // A posição Y final do topo da janela estática no monitor
  int finalY = g_FlyoutTargetY + yOffsetAnimation;

  // Verificamos se a posição ou tamanho físico da janela mudou na tela
  bool sizeOrPosChanged =
      (g_FlyoutTargetX != g_LastFlyoutX || finalY != g_LastFlyoutY ||
       w != g_LastFlyoutW || hMax != g_LastFlyoutH);

  if (sizeOrPosChanged) {
    g_LastFlyoutX = g_FlyoutTargetX;
    g_LastFlyoutY = finalY;
    g_LastFlyoutW = w;
    g_LastFlyoutH = hMax;

    // Atualiza a posição e tamanho da janela pai para o tamanho máximo
    // permanente
    UINT parentFlags = SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS;
    if (IsTransparencyEnabled()) {
      parentFlags |= SWP_NOREDRAW;
    }
    SetWindowPos(g_hFlyoutWindow, NULL, g_FlyoutTargetX, finalY, w, hMax,
                 parentFlags);

    // Sincroniza o tamanho da janela filha para o tamanho máximo permanente
    SetWindowPos(g_hFlyoutChildWindow, NULL, 0, 0, w, hMax,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS | SWP_NOREDRAW);

    if (!IsTransparencyEnabled()) {
      InvalidateRect(g_hFlyoutWindow, NULL, TRUE);
    }
  }

  // A região visível é toda a janela expandida por padrão
  HRGN hRgn = CreateRectRgn(0, 0, w, hMax);
  SetWindowRgn(g_hFlyoutWindow, hRgn, TRUE);

  HDC hdcScreen = GetDC(NULL);
  HDC memDC = CreateCompatibleDC(hdcScreen);

  BITMAPINFO bmi = {0};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = w;
  bmi.bmiHeader.biHeight = -hMax; // Altura total física
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  void *pvBits = nullptr;
  HBITMAP memBitmap =
      CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, &pvBits, NULL, 0);
  HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

  Bitmap bmp(w, hMax, PixelFormat32bppPARGB);
  {
    Graphics gTemp(&bmp);
    gTemp.SetSmoothingMode(SmoothingModeAntiAlias);
    gTemp.SetTextRenderingHint(TextRenderingHintAntiAlias);
    gTemp.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    gTemp.SetPixelOffsetMode(PixelOffsetModeHighQuality);

    DrawFlyoutPanel(gTemp, w, hMax);
  }

  BitmapData bmpData;
  Rect rect(0, 0, w, hMax);
  if (bmp.LockBits(&rect, ImageLockModeRead, PixelFormat32bppPARGB, &bmpData) ==
      Ok) {
    BYTE *pSrc = (BYTE *)bmpData.Scan0;
    BYTE *pDst = (BYTE *)pvBits;
    int rowSize = w * 4;
    for (int y = 0; y < hMax; y++) {
      memcpy(pDst + y * rowSize, pSrc + y * bmpData.Stride, rowSize);
    }
    bmp.UnlockBits(&bmpData);
  }

  POINT ptSrc = {0, 0};
  POINT ptDest = {0, 0};
  SIZE sizeDest = {w, hMax};
  BLENDFUNCTION blend = {0};
  blend.BlendOp = AC_SRC_OVER;
  blend.BlendFlags = 0;
  blend.SourceConstantAlpha = (BYTE)g_FlyoutAlpha;
  blend.AlphaFormat = AC_SRC_ALPHA;

  UpdateLayeredWindow(g_hFlyoutChildWindow, hdcScreen, &ptDest, &sizeDest,
                      memDC, &ptSrc, 0, &blend, ULW_ALPHA);

  SelectObject(memDC, oldBitmap);
  DeleteObject(memBitmap);
  DeleteDC(memDC);
  ReleaseDC(NULL, hdcScreen);
}

LRESULT CALLBACK FlyoutChildWndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                    LPARAM lParam) {
  wstring appId = L"";
  {
    lock_guard<mutex> guard(g_MediaState.lock);
    appId = g_MediaState.appId;
  }

  RECT rcClient;
  GetClientRect(hwnd, &rcClient);
  float scale = (float)(rcClient.right - rcClient.left) / 360.0f;
  if (scale <= 0.0f)
    scale = 1.0f;

  switch (msg) {
  case WM_MOUSEACTIVATE:
    return MA_NOACTIVATE;
  case WM_ERASEBKGND:
    return 1;

  case WM_MOUSEMOVE: {
    int x = (short)LOWORD(lParam);
    int y = (short)HIWORD(lParam);

    // Hit-test da barra de progresso (com margem vertical generosa)
    float hitMargin = 12.0f;
    bool overBar = (x >= g_ProgressBarX - hitMargin &&
                    x <= g_ProgressBarX + g_ProgressBarW + hitMargin &&
                    y >= g_ProgressBarY - hitMargin &&
                    y <= g_ProgressBarY + g_ProgressBarH + hitMargin);

    // O hover do volume é detectado sobre a área total horizontal (do ícone até
    // o canto direito da janela) e com uma área vertical muito generosa (30px
    // para cima da linha do volume até a borda inferior)
    float volHitMarginV = 30.0f * scale;
    bool overVolume =
        (x >= g_VolumeAreaX && x <= g_VolumeAreaX + g_VolumeAreaW &&
         y >= g_VolumeY - volHitMarginV && y <= rcClient.bottom);

    if (g_ProgressDragging) {
      // Atualiza a posição do drag da barra de progresso
      float rel = ((float)x - g_ProgressBarX) / g_ProgressBarW;
      if (rel < 0.0f)
        rel = 0.0f;
      if (rel > 1.0f)
        rel = 1.0f;
      g_DragRatio = rel;

      HWND parent = GetParent(hwnd);
      if (parent) {
        UpdateFlyoutWindow();
      }
    } else if (g_VolumeDragging) {
      // Atualiza a posição do drag do volume
      float rel = ((float)x - g_VolumeX) / g_VolumeW;
      if (rel < 0.0f)
        rel = 0.0f;
      if (rel > 1.0f)
        rel = 1.0f;
      g_VolumeDragRatio = rel;
      SetVolumeLevel(appId, rel);

      HWND parent = GetParent(hwnd);
      if (parent) {
        UpdateFlyoutWindow();
      }
    } else {
      if (overBar != g_ProgressHover) {
        g_ProgressHover = overBar;
        HWND parent = GetParent(hwnd);
        if (parent) {
          SetTimer(parent, IDT_FLYOUT_ANIMATION, 15, NULL);
        }
      }
      if (overVolume != g_VolumeHover) {
        g_VolumeHover = overVolume;
        HWND parent = GetParent(hwnd);
        if (parent) {
          SetTimer(parent, IDT_FLYOUT_ANIMATION, 15, NULL);
        }
      }
    }

    TRACKMOUSEEVENT tme = {sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0};
    TrackMouseEvent(&tme);
    return 0;
  }

  case WM_LBUTTONDOWN: {
    int x = (short)LOWORD(lParam);
    int y = (short)HIWORD(lParam);

    float hitMargin = 12.0f;
    bool overBar = (x >= g_ProgressBarX - hitMargin &&
                    x <= g_ProgressBarX + g_ProgressBarW + hitMargin &&
                    y >= g_ProgressBarY - hitMargin &&
                    y <= g_ProgressBarY + g_ProgressBarH + hitMargin);

    float volHitMarginV = 30.0f * scale;
    bool overVolume =
        (x >= g_VolumeAreaX && x <= g_VolumeAreaX + g_VolumeAreaW &&
         y >= g_VolumeY - volHitMarginV && y <= rcClient.bottom);

    // Detecta se o clique foi especificamente no ícone do volume
    bool overIcon = (x >= g_VolumeIconX - 4.0f &&
                     x <= g_VolumeIconX + g_VolumeIconW + 4.0f);

    if (overBar && g_ProgressBarW > 0.0f) {
      g_ProgressDragging = true;
      float rel = ((float)x - g_ProgressBarX) / g_ProgressBarW;
      if (rel < 0.0f)
        rel = 0.0f;
      if (rel > 1.0f)
        rel = 1.0f;
      g_DragRatio = rel;
      SetCapture(hwnd);

      HWND parent = GetParent(hwnd);
      if (parent) {
        SetTimer(parent, IDT_FLYOUT_ANIMATION, 15, NULL);
        UpdateFlyoutWindow();
      }
    } else if (overIcon) {
      // Alterna o estado de mute (silenciar) ao clicar no ícone
      SetAppMute(appId, !GetAppMute(appId));
      HWND parent = GetParent(hwnd);
      if (parent) {
        InvalidateRect(parent, NULL, TRUE);
        UpdateFlyoutWindow();
      }
    } else if (overVolume && g_VolumeW > 0.0f) {
      g_VolumeDragging = true;
      float rel = ((float)x - g_VolumeX) / g_VolumeW;
      if (rel < 0.0f)
        rel = 0.0f;
      if (rel > 1.0f)
        rel = 1.0f;
      g_VolumeDragRatio = rel;
      SetVolumeLevel(appId, rel);
      SetCapture(hwnd);

      HWND parent = GetParent(hwnd);
      if (parent) {
        SetTimer(parent, IDT_FLYOUT_ANIMATION, 15, NULL);
        UpdateFlyoutWindow();
      }
    }
    return 0;
  }

  case WM_LBUTTONUP: {
    int x = (short)LOWORD(lParam);
    int y = (short)HIWORD(lParam);

    if (g_ProgressDragging) {
      g_ProgressDragging = false;
      ReleaseCapture();
      SeekToPosition(g_DragRatio);
      HWND parent = GetParent(hwnd);
      if (parent) {
        UpdateFlyoutWindow();
      }
      return 0;
    }

    if (g_VolumeDragging) {
      g_VolumeDragging = false;
      ReleaseCapture();
      SetVolumeLevel(appId, g_VolumeDragRatio);
      HWND parent = GetParent(hwnd);
      if (parent) {
        UpdateFlyoutWindow();
      }
      return 0;
    }

    RECT rc;
    GetClientRect(hwnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;

    scale = (float)w / 360.0f;
    float Y_top = 0.0f;
    float headerHeight = 45.0f * scale;
    float headerBtnH = 26.0f * scale;

    // Pin Button in the header (width: 26px)
    float pinBtnW = 26.0f * scale;
    float pinX1 = (float)w - pinBtnW - 15.0f * scale;
    float pinX2 = pinX1 + pinBtnW;
    float pinY1 = Y_top + (headerHeight - headerBtnH) / 2.0f;
    float pinY2 = pinY1 + headerBtnH;

    // Translate Button in the header (width: 85px)
    float transBtnW = 85.0f * scale;
    float transX1 = pinX1 - transBtnW - 8.0f * scale;
    float transX2 = transX1 + transBtnW;
    float transY1 = pinY1;
    float transY2 = transY1 + headerBtnH;

    if (x >= pinX1 && x <= pinX2 && y >= pinY1 && y <= pinY2) {
      g_FlyoutPinned = !g_FlyoutPinned;
      HWND parent = GetParent(hwnd);
      if (parent) {
        InvalidateRect(parent, NULL, TRUE);
      }
    } else if (x >= transX1 && x <= transX2 && y >= transY1 && y <= transY2) {
      g_TranslateLyrics = !g_TranslateLyrics;
      if (g_TranslateLyrics) {
        void FetchTranslationThread();
        FetchTranslationThread();
      }
      HWND parent = GetParent(hwnd);
      if (parent) {
        InvalidateRect(parent, NULL, TRUE);
      }
    }
    return 0;
  }

  case WM_MOUSEWHEEL: {
    POINT pt;
    pt.x = (short)LOWORD(lParam);
    pt.y = (short)HIWORD(lParam);
    ScreenToClient(hwnd, &pt);

    float volHitMarginV = 30.0f * scale;
    bool overVolume =
        (pt.x >= g_VolumeAreaX && pt.x <= g_VolumeAreaX + g_VolumeAreaW &&
         pt.y >= g_VolumeY - volHitMarginV && pt.y <= rcClient.bottom);

    if (overVolume) {
      int zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
      float currentVol = GetVolumeLevel(appId);
      float newVol = currentVol + (zDelta > 0 ? 0.02f : -0.02f);
      SetVolumeLevel(appId, newVol);
      HWND parent = GetParent(hwnd);
      if (parent) {
        InvalidateRect(parent, NULL, TRUE);
      }
      return 0;
    }

    if (g_HasLyrics && g_SyncedLyrics.empty() && !g_PlainLyrics.empty()) {
      int zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
      g_LyricsScrollY += (zDelta > 0) ? 60.0f : -60.0f;
      if (g_LyricsScrollY > 0.0f)
        g_LyricsScrollY = 0.0f;
      HWND parent = GetParent(hwnd);
      if (parent) {
        InvalidateRect(parent, NULL, TRUE);
      }
    }
    return 0;
  }

  case WM_MOUSELEAVE: {
    if (!g_ProgressDragging && !g_VolumeDragging) {
      g_ProgressHover = false;
      g_VolumeHover = false;
      HWND parent = GetParent(hwnd);
      if (parent) {
        SetTimer(parent, IDT_FLYOUT_ANIMATION, 15, NULL);
      }
    }
    return 0;
  }

  case WM_CAPTURECHANGED: {
    if (g_ProgressDragging) {
      g_ProgressDragging = false;
      SeekToPosition(g_DragRatio);
    }
    if (g_VolumeDragging) {
      g_VolumeDragging = false;
      SetVolumeLevel(appId, g_VolumeDragRatio);
    }
    return 0;
  }
  }
  return DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK FlyoutWndProc(HWND hwnd, UINT msg, WPARAM wParam,
                               LPARAM lParam) {
  switch (msg) {
  case WM_CREATE: {
    DWM_WINDOW_CORNER_PREFERENCE preference =
        (DWM_WINDOW_CORNER_PREFERENCE)DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &preference,
                          sizeof(preference));

    UpdateFlyoutComposition(hwnd);
  }
    return 0;

  case WM_MOUSEACTIVATE:
    return MA_NOACTIVATE;

  case WM_ERASEBKGND:
    return 1;

  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    if (!IsTransparencyEnabled()) {
      RECT rc;
      GetClientRect(hwnd, &rc);
      COLORREF bgColor =
          IsSystemLightMode() ? RGB(243, 243, 243) : RGB(32, 32, 32);
      HBRUSH hBrush = CreateSolidBrush(bgColor);
      FillRect(hdc, &rc, hBrush);
      DeleteObject(hBrush);
    }
    EndPaint(hwnd, &ps);
    return 0;
  }

  case WM_SETTINGCHANGE:
    UpdateFlyoutComposition(hwnd);
    if (g_FlyoutState == 1 || g_FlyoutState == 2) {
      UpdateFlyoutWindow();
    }
    return 0;

  case WM_TIMER:
    if (wParam == IDT_FLYOUT_ANIMATION) {
      bool stillAnimating = false;

      if (g_FlyoutState == 1) { // Opening
        // Se estava fechado ou fechando, inicializa o offset de partida correto
        // do slide-up
        if (g_FlyoutOpenProgress == 0.0f) {
          // A posição Y é checada em relação à barra de tarefas
          HWND hTaskbar = FindWindow(L"Shell_TrayWnd", nullptr);
          RECT rcTaskbar{0};
          if (hTaskbar) {
            GetWindowRect(hTaskbar, &rcTaskbar);
          } else {
            rcTaskbar.top = GetSystemMetrics(SM_CYSCREEN) - 48;
          }
          if (rcTaskbar.top < 100) {
            g_FlyoutStartOffset = -15; // Slide de cima para baixo
          } else {
            g_FlyoutStartOffset = 15; // Slide de baixo para cima
          }
        }
      }

      // 1. Interpolação da animação de abertura/fechamento do flyout
      float targetOpen =
          (g_FlyoutState == 1 || g_FlyoutState == 2) ? 1.0f : 0.0f;
      float openDiff = targetOpen - g_FlyoutOpenProgress;
      float absOpenDiff = openDiff < 0.0f ? -openDiff : openDiff;
      if (!IsAnimationEnabled() || absOpenDiff < 0.005f) {
        g_FlyoutOpenProgress = targetOpen;
        if (targetOpen == 0.0f) {
          g_FlyoutState = 0; // Closed
        } else if (g_FlyoutState == 1) {
          g_FlyoutState = 2; // Open
        }
      } else {
        g_FlyoutOpenProgress += openDiff * 0.25f;
        stillAnimating = true;
      }

      // 2. Estado de expansão da barra de volume (sem animação para desempenho
      // máximo)
      g_VolumeExpandProgress =
          (g_VolumeHover || g_VolumeDragging) ? 1.0f : 0.0f;

      // Sincroniza a opacidade alpha com base no progresso de abertura
      g_FlyoutAlpha = (int)(g_FlyoutOpenProgress * 255.0f);

      // Letras sempre expandidas por padrão
      g_LyricsExpandProgress = 1.0f;
      g_FlyoutCurrentHeight = g_FlyoutExpandedHeight;

      if (g_FlyoutState == 0) {
        KillTimer(hwnd, IDT_FLYOUT_ANIMATION);
        KillTimer(hwnd, IDT_FLYOUT_MOUSE_CHECK);
        ShowWindow(hwnd, SW_HIDE);
        ResetFlyoutPositionCache();
      } else {
        UpdateFlyoutWindow();
        if (!stillAnimating && g_FlyoutState == 2) {
          KillTimer(hwnd, IDT_FLYOUT_ANIMATION);
        }
      }
    } else if (wParam == IDT_FLYOUT_MOUSE_CHECK) {
      if (g_FlyoutState == 2) { // Open
        POINT pt;
        GetCursorPos(&pt);

        RECT rcFlyout, rcWidget;
        GetWindowRect(hwnd, &rcFlyout);
        GetWindowRect(g_hMediaWindow, &rcWidget);

        bool overFlyout = PtInRect(&rcFlyout, pt);
        bool overWidget = PtInRect(&rcWidget, pt);

        if (!overFlyout && !overWidget && !g_FlyoutPinned) {
          g_FlyoutMouseOutSeconds++;
          if (g_FlyoutMouseOutSeconds >= 10) { // 2 segundos fora
            g_FlyoutMouseOutSeconds = 0;
            if (IsAnimationEnabled()) {
              g_FlyoutState = 3; // Closing
              SetTimer(hwnd, IDT_FLYOUT_ANIMATION, 15, NULL);
            } else {
              g_FlyoutState = 0; // Closed
              g_FlyoutOpenProgress = 0.0f;
              g_FlyoutAlpha = 0;
              ShowWindow(hwnd, SW_HIDE);
              ResetFlyoutPositionCache();
            }
          }
        } else {
          g_FlyoutMouseOutSeconds = 0;
        }
      }
    }
    return 0;

  case WM_DESTROY:
    return 0;
  }
  return DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK MediaWndProc(HWND hwnd, UINT msg, WPARAM wParam,
                              LPARAM lParam) {
  switch (msg) {
  case WM_CREATE:
    UpdateAppearance(hwnd);
    SetTimer(hwnd, IDT_POLL_MEDIA, 1000, NULL);
    RegisterTaskbarHook(hwnd);
    return 0;

  case WM_ERASEBKGND:
    return 1;

  case WM_CLOSE:
    return 0;

  case APP_WM_CLOSE:
    DestroyWindow(hwnd);
    return 0;

  case WM_DESTROY:
    if (g_TaskbarHook) {
      UnhookWinEvent(g_TaskbarHook);
      g_TaskbarHook = nullptr;
    }
    if (g_hFlyoutWindow) {
      DestroyWindow(g_hFlyoutWindow);
      g_hFlyoutWindow = nullptr;
      g_hFlyoutChildWindow = nullptr;
    }
    g_SessionManager = nullptr;
    PostQuitMessage(0);
    return 0;

  case WM_SETTINGCHANGE:
    UpdateAppearance(hwnd);
    RedrawLayeredWindow(hwnd);
    if (g_hFlyoutWindow) {
      UpdateFlyoutComposition(g_hFlyoutWindow);
      if (g_FlyoutState == 1 || g_FlyoutState == 2) {
        UpdateFlyoutWindow();
      }
    }
    return 0;

  case WM_TIMER:
    if (wParam == IDT_POLL_MEDIA) {
      UpdateMediaInfo();

      bool shouldHide = false;

      // 1. Check Fullscreen
      if (g_Settings.hideFullscreen) {
        QUERY_USER_NOTIFICATION_STATE state;
        if (SUCCEEDED(SHQueryUserNotificationState(&state))) {
          if (state == QUNS_BUSY || state == QUNS_RUNNING_D3D_FULL_SCREEN ||
              state == QUNS_PRESENTATION_MODE) {
            shouldHide = true;
          }
        }
      }

      // 2. Check Idle Timeout
      bool isPlaying = false;
      {
        lock_guard<mutex> guard(g_MediaState.lock);
        isPlaying = g_MediaState.isPlaying;
      }

      if (g_Settings.idleTimeout > 0) {
        if (isPlaying) {
          g_IdleSecondsCounter = 0;
          g_IsHiddenByIdle = false;
        } else {
          g_IdleSecondsCounter++;
          if (g_IdleSecondsCounter >= g_Settings.idleTimeout) {
            g_IsHiddenByIdle = true;
          }
        }
      } else {
        g_IsHiddenByIdle = false;
      }

      if (g_IsHiddenByIdle)
        shouldHide = true;

      // 3. Final Visibility Check
      if (shouldHide && IsWindowVisible(hwnd)) {
        ShowWindow(hwnd, SW_HIDE);
      } else if (!shouldHide && !IsWindowVisible(hwnd)) {
        // Only restore if Taskbar is also visible
        HWND hTaskbar = FindWindow(L"Shell_TrayWnd", nullptr);
        if (hTaskbar && IsWindowVisible(hTaskbar)) {
          ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        }
      }

      // 4. Otimização de Redesenho Baseado em Alteração de Estado e Interações
      static wstring lastTitle = L"";
      static wstring lastArtist = L"";
      static int lastPosition = -1;
      static bool lastIsPlaying = false;
      static bool lastHasMedia = false;

      wstring currentTitle = L"";
      wstring currentArtist = L"";
      int currentPosition = 0;
      bool hasMedia = false;
      {
        lock_guard<mutex> guard(g_MediaState.lock);
        currentTitle = g_MediaState.title;
        currentArtist = g_MediaState.artist;
        currentPosition = g_MediaState.durationPositionSeconds;
        isPlaying = g_MediaState.isPlaying;
        hasMedia = g_MediaState.hasMedia;
      }

      bool metadataChanged =
          (currentTitle != lastTitle || currentArtist != lastArtist ||
           isPlaying != lastIsPlaying || hasMedia != lastHasMedia);
      bool positionChanged = (isPlaying && currentPosition != lastPosition);
      bool interactionActive = (g_ProgressDragging || g_VolumeDragging ||
                                g_VolumeHover || g_ProgressHover);
      bool shouldRedraw =
          metadataChanged || positionChanged || interactionActive;

      if (shouldRedraw) {
        RedrawLayeredWindow(hwnd);
        if (g_hFlyoutWindow && (g_FlyoutState == 1 || g_FlyoutState == 2)) {
          UpdateFlyoutWindow();
        }
      }

      lastTitle = currentTitle;
      lastArtist = currentArtist;
      lastPosition = currentPosition;
      lastIsPlaying = isPlaying;
      lastHasMedia = hasMedia;
    } else if (wParam == IDT_ANIMATION) {
      if (g_IsScrolling) {
        if (g_ScrollWait > 0) {
          g_ScrollWait--;
        } else {
          g_ScrollOffset++;
          if (g_ScrollOffset > g_TextWidth + 40) {
            g_ScrollOffset = 0;
            g_ScrollWait = 60;
          }
          RedrawLayeredWindow(hwnd);
        }
      } else {
        KillTimer(hwnd, IDT_ANIMATION);
      }
    }
    return 0;

  case WM_WINDOWPOSCHANGED:
    RedrawLayeredWindow(hwnd);
    return 0;

  case WM_APP + 10: {
    HWND hTaskbar = FindWindow(TEXT("Shell_TrayWnd"), nullptr);
    if (!hTaskbar)
      break;

    // Merged Logic: Check visibility first
    if (!IsWindowVisible(hTaskbar)) {
      if (IsWindowVisible(hwnd))
        ShowWindow(hwnd, SW_HIDE);
      return 0;
    }

    // Restore visibility if we aren't hidden by fullscreen or Idle modes
    // (The Timer loop handles fullscreen/idle hiding, this handles Taskbar
    // hiding)
    if (!g_IsHiddenByIdle && !IsWindowVisible(hwnd)) {
      // Double check fullscreen mode isn't forcing hide
      bool gameModeHide = false;
      if (g_Settings.hideFullscreen) {
        QUERY_USER_NOTIFICATION_STATE state;
        if (SUCCEEDED(SHQueryUserNotificationState(&state))) {
          if (state == QUNS_BUSY || state == QUNS_RUNNING_D3D_FULL_SCREEN ||
              state == QUNS_PRESENTATION_MODE)
            gameModeHide = true;
        }
      }
      if (!gameModeHide)
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    }

    RECT rc;
    GetWindowRect(hTaskbar, &rc);

    int x = rc.left + g_Settings.offsetX;
    int taskbarHeight = rc.bottom - rc.top;
    int y = rc.top + (taskbarHeight / 2) - (g_Settings.height / 2) +
            g_Settings.offsetY;

    RECT myRc;
    GetWindowRect(hwnd, &myRc);
    if (myRc.left != x || myRc.top != y ||
        (myRc.right - myRc.left) != g_Settings.width ||
        (myRc.bottom - myRc.top) != g_Settings.height) {
      SetWindowPos(hwnd, HWND_TOPMOST, x, y, g_Settings.width,
                   g_Settings.height, SWP_NOACTIVATE);
    }
    return 0;
  }

  case WM_MOUSEMOVE: {
    int x = LOWORD(lParam);
    int y = HIWORD(lParam);
    int artSize = g_Settings.height - 12;
    double scale = g_Settings.buttonScale;

    // Re-calculate hit targets based on scale
    int startControlX = 6 + artSize + (int)(20 * scale);
    float gap = 32.0f * (float)scale;
    float pX = (float)startControlX;
    float plX = pX + gap;
    float nX = plX + gap;
    float radius = 14.0f * (float)scale;

    int newState = 0;
    if (y > 4 && y < g_Settings.height - 4) {
      if (x >= pX - radius && x <= pX + radius)
        newState = 1;
      else if (x >= plX - radius && x <= plX + radius)
        newState = 2;
      else if (x >= nX - radius && x <= nX + radius)
        newState = 3;
    }

    if (newState != g_HoverState) {
      g_HoverState = newState;
      RedrawLayeredWindow(hwnd);
    }

    TRACKMOUSEEVENT tme = {sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0};
    TrackMouseEvent(&tme);
    return 0;
  }
  case WM_MOUSELEAVE:
    g_HoverState = 0;
    RedrawLayeredWindow(hwnd);
    break;
  case WM_LBUTTONUP: {
    if (g_HoverState > 0) {
      SendMediaCommand(g_HoverState);
    } else {
      if (g_hFlyoutWindow) {
        if (g_FlyoutState == 0 || g_FlyoutState == 3) {
          RECT rcWidget;
          GetWindowRect(hwnd, &rcWidget);
          int widgetW = rcWidget.right - rcWidget.left;

          HWND hTaskbar = FindWindow(L"Shell_TrayWnd", nullptr);
          RECT rcTaskbar{0};
          if (hTaskbar) {
            GetWindowRect(hTaskbar, &rcTaskbar);
          } else {
            rcTaskbar.top = GetSystemMetrics(SM_CYSCREEN) - 48;
          }

          RECT rcFlyoutWindow;
          GetWindowRect(g_hFlyoutWindow, &rcFlyoutWindow);
          int flyoutW = rcFlyoutWindow.right - rcFlyoutWindow.left;
          if (flyoutW <= 0)
            flyoutW = 360;

          int baseFlyoutH =
              (int)(g_FlyoutExpandedHeight * g_Settings.buttonScale);

          g_FlyoutTargetX = rcWidget.left + (widgetW - flyoutW) / 2;

          int screenW = GetSystemMetrics(SM_CXSCREEN);
          if (g_FlyoutTargetX < 10)
            g_FlyoutTargetX = 10;
          if (g_FlyoutTargetX + flyoutW > screenW - 10)
            g_FlyoutTargetX = screenW - flyoutW - 10;

          if (rcTaskbar.top < 100) {
            g_FlyoutTargetY = rcTaskbar.bottom + 8;
            g_FlyoutYOffset = -15;
          } else {
            g_FlyoutTargetY = rcTaskbar.top - baseFlyoutH - 8;
            g_FlyoutYOffset = 15;
          }

          if (IsAnimationEnabled()) {
            g_FlyoutOpenProgress = 0.0f;
            g_FlyoutAlpha = 0;
            g_FlyoutState = 1;
          } else {
            g_FlyoutOpenProgress = 1.0f;
            g_FlyoutAlpha = 255;
            g_FlyoutState = 2;
          }
          g_FlyoutMouseOutSeconds = 0;

          UpdateFlyoutComposition(g_hFlyoutWindow);
          UpdateFlyoutWindow();
          ShowWindow(g_hFlyoutWindow, SW_SHOWNOACTIVATE);

          if (IsAnimationEnabled()) {
            SetTimer(g_hFlyoutWindow, IDT_FLYOUT_ANIMATION, 15, NULL);
          }
          SetTimer(g_hFlyoutWindow, IDT_FLYOUT_MOUSE_CHECK, 200, NULL);
        } else {
          if (IsAnimationEnabled()) {
            g_FlyoutState = 3;
            SetTimer(g_hFlyoutWindow, IDT_FLYOUT_ANIMATION, 15, NULL);
          } else {
            g_FlyoutState = 0;
            g_FlyoutOpenProgress = 0.0f;
            g_FlyoutAlpha = 0;
            ShowWindow(g_hFlyoutWindow, SW_HIDE);
            ResetFlyoutPositionCache();
          }
        }
      }
    }
    return 0;
  }
  case WM_MOUSEWHEEL: {
    short zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
    keybd_event(zDelta > 0 ? VK_VOLUME_UP : VK_VOLUME_DOWN, 0, 0, 0);
    keybd_event(zDelta > 0 ? VK_VOLUME_UP : VK_VOLUME_DOWN, 0, KEYEVENTF_KEYUP,
                0);
    return 0;
  }
  case WM_PAINT: {
    PAINTSTRUCT ps;
    BeginPaint(hwnd, &ps);
    EndPaint(hwnd, &ps);
    RedrawLayeredWindow(hwnd);
    return 0;
  }
  default:
    if (msg == g_TaskbarCreatedMsg) {
      if (g_TaskbarHook) {
        UnhookWinEvent(g_TaskbarHook);
        g_TaskbarHook = nullptr;
      }
      RegisterTaskbarHook(hwnd);
      return 0;
    }
    break;
  }
  return DefWindowProc(hwnd, msg, wParam, lParam);
}

// --- Main Thread ---
void MediaThread() {
  winrt::init_apartment();

  GdiplusStartupInput gdiplusStartupInput;
  ULONG_PTR gdiplusToken;
  GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

  WNDCLASS wc = {0};
  wc.lpfnWndProc = MediaWndProc;
  wc.hInstance = GetModuleHandle(NULL);
  wc.lpszClassName = TEXT("TaskbarMusicPlayerNative_GSMTC");
  wc.hCursor = LoadCursor(NULL, IDC_HAND);
  RegisterClass(&wc);

  WNDCLASS wcFlyout = {0};
  wcFlyout.lpfnWndProc = FlyoutWndProc;
  wcFlyout.hInstance = GetModuleHandle(NULL);
  wcFlyout.lpszClassName = TEXT("TaskbarMusicPlayerNative_Flyout");
  wcFlyout.hCursor = LoadCursor(NULL, IDC_ARROW);
  RegisterClass(&wcFlyout);

  WNDCLASS wcFlyoutChild = {0};
  wcFlyoutChild.lpfnWndProc = FlyoutChildWndProc;
  wcFlyoutChild.hInstance = GetModuleHandle(NULL);
  wcFlyoutChild.lpszClassName = TEXT("TaskbarMusicPlayerNative_FlyoutChild");
  wcFlyoutChild.hCursor = LoadCursor(NULL, IDC_ARROW);
  RegisterClass(&wcFlyoutChild);

  // Try to use CreateWindowInBand
  HMODULE hUser32 = GetModuleHandle(L"user32.dll");
  pCreateWindowInBand CreateWindowInBand = nullptr;
  if (hUser32) {
    CreateWindowInBand =
        (pCreateWindowInBand)GetProcAddress(hUser32, "CreateWindowInBand");
  }

  if (CreateWindowInBand) {
    g_hMediaWindow = CreateWindowInBand(
        WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST, wc.lpszClassName,
        TEXT("MusicPlayerNative"), WS_POPUP | WS_VISIBLE, 0, 0,
        g_Settings.width, g_Settings.height, NULL, NULL, wc.hInstance, NULL,
        ZBID_IMMERSIVE_NOTIFICATION);
    if (g_hMediaWindow) {
      Wh_Log(L"Created window in ZBID_IMMERSIVE_NOTIFICATION band");
    }
  }

  if (!g_hMediaWindow) {
    Wh_Log(L"Falling back to CreateWindowEx");
    g_hMediaWindow = CreateWindowEx(
        WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST, wc.lpszClassName,
        TEXT("MusicPlayerNative"), WS_POPUP | WS_VISIBLE, 0, 0,
        g_Settings.width, g_Settings.height, NULL, NULL, wc.hInstance, NULL);
  }

  if (g_hMediaWindow) {
    g_hFlyoutWindow =
        CreateWindowEx(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST,
                       wcFlyout.lpszClassName, TEXT("MusicPlayerFlyout"),
                       WS_POPUP | WS_CLIPCHILDREN, 0, 0, 360, 400,
                       g_hMediaWindow, NULL, wcFlyout.hInstance, NULL);

    if (g_hFlyoutWindow) {
      g_hFlyoutChildWindow = CreateWindowEx(
          WS_EX_LAYERED, wcFlyoutChild.lpszClassName,
          TEXT("MusicPlayerFlyoutChild"), WS_CHILD | WS_VISIBLE, 0, 0, 360, 400,
          g_hFlyoutWindow, NULL, wcFlyoutChild.hInstance, NULL);
    }
  }

  // Layered window parameters are updated via UpdateLayeredWindow

  MSG msg;
  while (GetMessage(&msg, NULL, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }

  UnregisterClass(wc.lpszClassName, wc.hInstance);
  UnregisterClass(wcFlyout.lpszClassName, wcFlyout.hInstance);
  UnregisterClass(wcFlyoutChild.lpszClassName, wcFlyoutChild.hInstance);
  GdiplusShutdown(gdiplusToken);
  winrt::uninit_apartment();
}

std::thread *g_pMediaThread = nullptr;

// --- CALLBACKS ---
BOOL WhTool_ModInit() {
  SetCurrentProcessExplicitAppUserModelID(L"taskbar-music-player-native");
  LoadSettings();
  g_Running = true;
  g_pMediaThread = new std::thread(MediaThread);
  return TRUE;
}

void WhTool_ModUninit() {
  g_Running = false;
  if (g_hMediaWindow)
    SendMessage(g_hMediaWindow, APP_WM_CLOSE, 0, 0);
  if (g_pMediaThread) {
    if (g_pMediaThread->joinable())
      g_pMediaThread->join();
    delete g_pMediaThread;
    g_pMediaThread = nullptr;
  }

  // Liberação do cache de volume do app ativo
  {
    lock_guard<mutex> guard(g_AudioVolumeMutex);
    if (g_pActiveAppVolume) {
      g_pActiveAppVolume->Release();
      g_pActiveAppVolume = nullptr;
    }
    g_ActiveAppVolumeId = L"";
  }
}

void WhTool_ModSettingsChanged() {
  LoadSettings();
  if (g_hMediaWindow) {
    SendMessage(g_hMediaWindow, WM_TIMER, IDT_POLL_MEDIA, 0);
    SendMessage(g_hMediaWindow, WM_SETTINGCHANGE, 0, 0);
  }
}

////////////////////////////////////////////////////////////////////////////////
// Windhawk tool mod implementation for mods which don't need to inject to other
// processes or hook other functions. Context:
// https://github.com/ramensoftware/windhawk-mods/pull/1916
//
// The mod will load and run in a dedicated windhawk.exe process.
//
// Paste the code below as part of the mod code, and use these callbacks:
// * WhTool_ModInit
// * WhTool_ModSettingsChanged
// * WhTool_ModUninit
//
// Currently, other callbacks are not supported.

bool g_isToolModProcessLauncher;
HANDLE g_toolModProcessMutex;

void WINAPI EntryPoint_Hook() {
  Wh_Log(L">");
  ExitThread(0);
}

BOOL Wh_ModInit() {
  bool isService = false;
  bool isToolModProcess = false;
  bool isCurrentToolModProcess = false;
  int argc;
  LPWSTR *argv = CommandLineToArgvW(GetCommandLine(), &argc);
  if (!argv) {
    Wh_Log(L"CommandLineToArgvW failed");
    return FALSE;
  }

  for (int i = 1; i < argc; i++) {
    if (wcscmp(argv[i], L"-service") == 0) {
      isService = true;
      break;
    }
  }

  for (int i = 1; i < argc - 1; i++) {
    if (wcscmp(argv[i], L"-tool-mod") == 0) {
      isToolModProcess = true;
      if (wcscmp(argv[i + 1], WH_MOD_ID) == 0) {
        isCurrentToolModProcess = true;
      }
      break;
    }
  }

  LocalFree(argv);

  if (isService) {
    return FALSE;
  }

  if (isCurrentToolModProcess) {
    g_toolModProcessMutex =
        CreateMutex(nullptr, TRUE, L"windhawk-tool-mod_" WH_MOD_ID);
    if (!g_toolModProcessMutex) {
      Wh_Log(L"CreateMutex failed");
      ExitProcess(1);
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
      Wh_Log(L"Tool mod already running (%s)", WH_MOD_ID);
      ExitProcess(1);
    }

    if (!WhTool_ModInit()) {
      ExitProcess(1);
    }

    IMAGE_DOS_HEADER *dosHeader = (IMAGE_DOS_HEADER *)GetModuleHandle(nullptr);
    IMAGE_NT_HEADERS *ntHeaders =
        (IMAGE_NT_HEADERS *)((BYTE *)dosHeader + dosHeader->e_lfanew);

    DWORD entryPointRVA = ntHeaders->OptionalHeader.AddressOfEntryPoint;
    void *entryPoint = (BYTE *)dosHeader + entryPointRVA;

    Wh_SetFunctionHook(entryPoint, (void *)EntryPoint_Hook, nullptr);
    return TRUE;
  }

  if (isToolModProcess) {
    return FALSE;
  }

  g_isToolModProcessLauncher = true;
  return TRUE;
}

void Wh_ModAfterInit() {
  if (!g_isToolModProcessLauncher) {
    return;
  }

  WCHAR currentProcessPath[MAX_PATH];
  switch (GetModuleFileName(nullptr, currentProcessPath,
                            ARRAYSIZE(currentProcessPath))) {
  case 0:
  case ARRAYSIZE(currentProcessPath):
    Wh_Log(L"GetModuleFileName failed");
    return;
  }

  WCHAR
  commandLine[MAX_PATH + 2 +
              (sizeof(L" -tool-mod \"" WH_MOD_ID "\"") / sizeof(WCHAR)) - 1];
  swprintf_s(commandLine, L"\"%s\" -tool-mod \"%s\"", currentProcessPath,
             WH_MOD_ID);

  HMODULE kernelModule = GetModuleHandle(L"kernelbase.dll");
  if (!kernelModule) {
    kernelModule = GetModuleHandle(L"kernel32.dll");
    if (!kernelModule) {
      Wh_Log(L"No kernelbase.dll/kernel32.dll");
      return;
    }
  }

  using CreateProcessInternalW_t = BOOL(WINAPI *)(
      HANDLE hUserToken, LPCWSTR lpApplicationName, LPWSTR lpCommandLine,
      LPSECURITY_ATTRIBUTES lpProcessAttributes,
      LPSECURITY_ATTRIBUTES lpThreadAttributes, WINBOOL bInheritHandles,
      DWORD dwCreationFlags, LPVOID lpEnvironment, LPCWSTR lpCurrentDirectory,
      LPSTARTUPINFOW lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation,
      PHANDLE hRestrictedUserToken);
  CreateProcessInternalW_t pCreateProcessInternalW =
      (CreateProcessInternalW_t)GetProcAddress(kernelModule,
                                               "CreateProcessInternalW");
  if (!pCreateProcessInternalW) {
    Wh_Log(L"No CreateProcessInternalW");
    return;
  }

  STARTUPINFO si{
      .cb = sizeof(STARTUPINFO),
      .dwFlags = STARTF_FORCEOFFFEEDBACK,
  };
  PROCESS_INFORMATION pi;
  if (!pCreateProcessInternalW(nullptr, currentProcessPath, commandLine,
                               nullptr, nullptr, FALSE, NORMAL_PRIORITY_CLASS,
                               nullptr, nullptr, &si, &pi, nullptr)) {
    Wh_Log(L"CreateProcess failed");
    return;
  }

  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
}

void Wh_ModSettingsChanged() {
  if (g_isToolModProcessLauncher) {
    return;
  }

  WhTool_ModSettingsChanged();
}

void Wh_ModUninit() {
  if (g_isToolModProcessLauncher) {
    return;
  }

  WhTool_ModUninit();
  ExitProcess(0);
}