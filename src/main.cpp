#define _SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS
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
#include <memory>
#include <windows.h>
#include <winhttp.h>

// WinRT
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>

// Locais
#include "settings.h"
#include "tray.h"

using namespace Gdiplus;
using namespace std;
using namespace winrt;
using namespace Windows::Media::Control;
using namespace Windows::Storage::Streams;

struct winrt_apartment_scope {
  winrt_apartment_scope() { winrt::init_apartment(); }
  ~winrt_apartment_scope() { winrt::uninit_apartment(); }
};

// --- Logs utilitários ---
void Log(const wchar_t *format, ...) {
  va_list args;
  va_start(args, format);
  wchar_t buf[1024];
  vswprintf_s(buf, format, args);
  va_end(args);
  OutputDebugStringW(buf);
  OutputDebugStringW(L"\n");
}

// --- Constants ---
const WCHAR *FONT_NAME = L"Segoe UI Variable Text";

const unsigned int WINDOWS_ACCENT_COLORS[50] = {
    0xFFFFB900, 0xFFFF8C00, 0xFFF7630C, 0xFFCA5010, 0xFFDA3B01, 0xFFEF6950, 0xFFD13438, 0xFFFF4343, 0xFFE74856, 0xFFE81123,
    0xFFEA005E, 0xFFC30052, 0xFFE3008C, 0xFFBF0077, 0xFFC239B3, 0xFF9A0089, 0xFF0078D7, 0xFF0063B1, 0xFF9E00FF, 0xFF8600E8,
    0xFF8E8CD8, 0xFF6B69D6, 0xFF8764B8, 0xFF744DA9, 0xFFB146C2, 0xFF0099BC, 0xFF007897, 0xFF00B7C3, 0xFF00838F, 0xFF008272,
    0xFF009688, 0xFF038387, 0xFF00B294, 0xFF018574, 0xFF00CC6A, 0xFF10893E, 0xFF107C41, 0xFF7A7574, 0xFF5D5A58, 0xFF68768A,
    0xFF567C73, 0xFF486860, 0xFF498205, 0xFF13522B, 0xFF767676, 0xFF4C4A48, 0xFF69797E, 0xFF4F5B5F, 0xFF847545, 0xFF7E735F
};

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

// --- Global State ---
HWND g_hMediaWindow = NULL;
HWND g_hFlyoutWindow = NULL;
HWND g_hFlyoutChildWindow = NULL;
bool g_Running = true;
int g_HoverState = 0;
HWINEVENTHOOK g_TaskbarHook = nullptr;
UINT g_TaskbarCreatedMsg = RegisterWindowMessage(L"TaskbarCreated");

// Cache de Cor de Sotaque do Windows
Color g_AccentColor = Color(255, 0, 120, 215);

// GSMTC Event tokens e Sessão
GlobalSystemMediaTransportControlsSession g_CurrentSession = nullptr;
winrt::event_token g_CurrentSessionChangedToken;
winrt::event_token g_SessionsChangedToken;
winrt::event_token g_MediaPropertiesChangedToken;
winrt::event_token g_PlaybackInfoChangedToken;
winrt::event_token g_TimelinePropertiesChangedToken;
mutex g_SessionMutex;

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
wstring g_OriginalPlainLyrics = L"";
wstring g_LastTargetLang = L"";
wstring g_LyricsSource = L"lrclib.net";
vector<LyricLine> g_SyncedLyrics;
mutex g_LyricsMutex;
float g_FlyoutBaseHeight = 150.0f;
float g_FlyoutTargetHeight = 400.0f;
float g_FlyoutExpandedHeight = 400.0f;
float g_FlyoutCurrentHeight = 400.0f;

// Variáveis para animação unificada baseada em progresso
float g_FlyoutOpenProgress = 0.0f;   // 0.0f = fechado, 1.0f = aberto
float g_FlyoutAnimT = 0.0f;          // Tempo linear (0.0 a 1.0)
float g_LyricsExpandProgress = 1.0f; // Sempre 1.0f (letras sempre expandidas)
int g_FlyoutStartOffset = 15;        // Offset de início do slide-up (-15 ou 15)

bool g_SettingsCardOpen = false;
int g_HoveredGridIndex = -1;
bool g_ThemeDropdownOpen = false;
bool g_AccentDropdownOpen = false;
bool g_UILangDropdownOpen = false;
bool g_TransLangDropdownOpen = false;
int g_HoveredDropdownItem = -1;
bool g_HoverThemeCombo = false;
bool g_HoverAccentCombo = false;
bool g_HoverUILangCombo = false;
bool g_HoverTransLangCombo = false;

inline float EaseOutCubic(float x) {
  float f = x - 1.0f;
  return f * f * f + 1.0f;
}

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
  std::shared_ptr<Bitmap> albumArt;
  std::shared_ptr<Bitmap> cachedMediaThumb;
  std::shared_ptr<Bitmap> cachedFlyoutThumb;
  int lastMediaThumbSize = 0;
  int lastFlyoutThumbSize = 0;
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
  LyricsNotFound,
  SettingsHeader,
  ColorMode,
  AccentColorLabel,
  WindowsColorsLabel,
  LanguageCardHeader,
  InterfaceLanguageLabel,
  TranslateToLabel,
  ThemeAuto,
  ThemeLight,
  ThemeDark,
  AccentAuto,
  AccentManual,
  LangAuto,
  LangPT,
  LangEN,
  LangES,
  LangFR,
  LangDE,
  LangIT
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

string UrlEncode(const string &s);
void FetchTranslationThread();

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
    g_OriginalPlainLyrics.clear();
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
                    g_OriginalPlainLyrics.clear();
                    g_LyricsSource = L"lrclib.net";
                    g_HasLyrics = true;
                    g_IsFetchingLyrics = false;
                  }
                  if (g_hFlyoutWindow)
                    InvalidateRect(g_hFlyoutWindow, NULL, TRUE);

                  if (g_TranslateLyrics) {
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
                          g_OriginalPlainLyrics.clear();
                          g_LyricsSource = L"NetEase";
                          g_HasLyrics = true;
                          g_IsFetchingLyrics = false;
                        }
                        if (g_hFlyoutWindow)
                          InvalidateRect(g_hFlyoutWindow, NULL, TRUE);

                        if (g_TranslateLyrics) {
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
        g_OriginalPlainLyrics = lrclibPlain;
        g_SyncedLyrics.clear();
        g_LyricsSource = L"lrclib.net";
        g_HasLyrics = true;
        g_IsFetchingLyrics = false;
      }
      if (g_hFlyoutWindow)
        InvalidateRect(g_hFlyoutWindow, NULL, TRUE);
      if (g_TranslateLyrics) {
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
                        g_OriginalPlainLyrics = vagalumeText;
                        g_SyncedLyrics.clear();
                        g_LyricsSource = L"Vagalume";
                        g_HasLyrics = true;
                        g_IsFetchingLyrics = false;
                      }
                      if (g_hFlyoutWindow)
                        InvalidateRect(g_hFlyoutWindow, NULL, TRUE);
                      if (g_TranslateLyrics) {
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
                    g_OriginalPlainLyrics = ovhText;
                    g_SyncedLyrics.clear();
                    g_LyricsSource = L"lyrics.ovh";
                    g_HasLyrics = true;
                    g_IsFetchingLyrics = false;
                  }
                  if (g_hFlyoutWindow)
                    InvalidateRect(g_hFlyoutWindow, NULL, TRUE);
                  if (g_TranslateLyrics) {
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
        g_OriginalPlainLyrics = neteasePlain;
        g_SyncedLyrics.clear();
        g_LyricsSource = L"NetEase";
        g_HasLyrics = true;
        g_IsFetchingLyrics = false;
      }
      if (g_hFlyoutWindow)
        InvalidateRect(g_hFlyoutWindow, NULL, TRUE);
      if (g_TranslateLyrics) {
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
    wstring transLangW = L"";
    if (g_Settings.translationLanguage != 0) {
      switch (g_Settings.translationLanguage) {
        case 1: transLangW = L"pt"; break;
        case 2: transLangW = L"en"; break;
        case 3: transLangW = L"es"; break;
        case 4: transLangW = L"fr"; break;
        case 5: transLangW = L"de"; break;
        case 6: transLangW = L"it"; break;
      }
    } else {
      transLangW = GetUserLanguageTag();
    }

    bool langChanged = (transLangW != g_LastTargetLang);

    wstring currentTitle;
    wstring currentArtist;
    vector<LyricLine> localLines;
    wstring localPlain;
    {
      lock_guard<mutex> guard(g_LyricsMutex);
      if (!g_HasLyrics || g_IsTranslating)
        return;

      if (langChanged) {
        for (auto &line : g_SyncedLyrics) {
          line.translatedText.clear();
        }
        if (!g_OriginalPlainLyrics.empty()) {
          g_PlainLyrics = g_OriginalPlainLyrics;
        }
      }

      currentTitle = g_CurrentLyricsTitle;
      currentArtist = g_CurrentLyricsArtist;
      localLines = g_SyncedLyrics;
      localPlain = g_PlainLyrics;
      g_IsTranslating = true;
    }

    if (!localLines.empty() && !localLines[0].translatedText.empty() && !langChanged) {
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

    string targetLang(transLangW.begin(), transLangW.end());
    int retry = 0;

    while (retry < 2) {
      string urlPath =
          "/translate_a/single?client=gtx&sl=auto&tl=" + targetLang +
          "&dt=t&q=" + UrlEncode(query);
      wstring widePath(urlPath.begin(), urlPath.end());

      HINTERNET hSession =
          WinHttpOpen(L"TaskbarMediaPlayer/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
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
                              g_LastTargetLang = transLangW;
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

  Log(L"GetFriendlySourceApp: appId='%s', albumTitle='%s', title='%s', artist='%s'",
      appId.c_str(), albumTitle.c_str(), title.c_str(), artist.c_str());

  isBrowser = (lowerId.find(L"chrome") != wstring::npos ||
               lowerId.find(L"edge") != wstring::npos ||
               lowerId.find(L"firefox") != wstring::npos ||
               lowerId.find(L"brave") != wstring::npos ||
               lowerId.find(L"browser") != wstring::npos ||
               lowerId.find(L"explorer") != wstring::npos);

  if (isBrowser) {
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
        g_MediaState.albumArt.reset();
        g_MediaState.cachedMediaThumb.reset();
        g_MediaState.cachedFlyoutThumb.reset();

        auto thumbRef = props.Thumbnail();
        if (thumbRef) {
          try {
            auto stream = thumbRef.OpenReadAsync().get();
            g_MediaState.albumArt.reset(StreamToBitmap(stream));
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
      g_MediaState.albumArt.reset();
      g_MediaState.cachedMediaThumb.reset();
      g_MediaState.cachedFlyoutThumb.reset();
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
  if (g_Settings.uiLanguage != 0) {
    switch (g_Settings.uiLanguage) {
      case 1: return L"pt";
      case 2: return L"en";
      case 3: return L"es";
      case 4: return L"fr";
      case 5: return L"de";
      case 6: return L"it";
    }
  }
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
    case UIStringId::LyricsHeader: return L"Letras";
    case UIStringId::TranslateButton: return L"Traduzir";
    case UIStringId::FetchingLyrics: return L"Buscando letras...";
    case UIStringId::LyricsNotFound: return L"Letra não encontrada para esta música.";
    case UIStringId::SettingsHeader: return L"Configurações";
    case UIStringId::ColorMode: return L"Escolher seu modo";
    case UIStringId::AccentColorLabel: return L"Cor em destaque";
    case UIStringId::WindowsColorsLabel: return L"Cores do Windows";
    case UIStringId::LanguageCardHeader: return L"Idioma";
    case UIStringId::InterfaceLanguageLabel: return L"Idioma da interface";
    case UIStringId::TranslateToLabel: return L"Traduzir letras para";
    case UIStringId::ThemeAuto: return L"Automático";
    case UIStringId::ThemeLight: return L"Claro";
    case UIStringId::ThemeDark: return L"Escuro";
    case UIStringId::AccentAuto: return L"Automático";
    case UIStringId::AccentManual: return L"Manual";
    case UIStringId::LangAuto: return L"Automático";
    case UIStringId::LangPT: return L"Português";
    case UIStringId::LangEN: return L"English";
    case UIStringId::LangES: return L"Español";
    case UIStringId::LangFR: return L"Français";
    case UIStringId::LangDE: return L"Deutsch";
    case UIStringId::LangIT: return L"Italiano";
    }
  } else if (lang == L"es") {
    switch (id) {
    case UIStringId::LyricsHeader: return L"Letras";
    case UIStringId::TranslateButton: return L"Traducir";
    case UIStringId::FetchingLyrics: return L"Buscando letras...";
    case UIStringId::LyricsNotFound: return L"Letra no encontrada para esta canción.";
    case UIStringId::SettingsHeader: return L"Configuración";
    case UIStringId::ColorMode: return L"Elegir tu modo";
    case UIStringId::AccentColorLabel: return L"Color de énfasis";
    case UIStringId::WindowsColorsLabel: return L"Colores de Windows";
    case UIStringId::LanguageCardHeader: return L"Idioma";
    case UIStringId::InterfaceLanguageLabel: return L"Idioma de la interfaz";
    case UIStringId::TranslateToLabel: return L"Traducir letras al";
    case UIStringId::ThemeAuto: return L"Automático";
    case UIStringId::ThemeLight: return L"Claro";
    case UIStringId::ThemeDark: return L"Oscuro";
    case UIStringId::AccentAuto: return L"Automático";
    case UIStringId::AccentManual: return L"Manual";
    case UIStringId::LangAuto: return L"Automático";
    case UIStringId::LangPT: return L"Português";
    case UIStringId::LangEN: return L"English";
    case UIStringId::LangES: return L"Español";
    case UIStringId::LangFR: return L"Français";
    case UIStringId::LangDE: return L"Deutsch";
    case UIStringId::LangIT: return L"Italiano";
    }
  } else if (lang == L"fr") {
    switch (id) {
    case UIStringId::LyricsHeader: return L"Paroles";
    case UIStringId::TranslateButton: return L"Traduire";
    case UIStringId::FetchingLyrics: return L"Recherche des paroles...";
    case UIStringId::LyricsNotFound: return L"Paroles non trouvées pour cette chanson.";
    case UIStringId::SettingsHeader: return L"Paramètres";
    case UIStringId::ColorMode: return L"Choisir votre mode";
    case UIStringId::AccentColorLabel: return L"Couleur d'accentuation";
    case UIStringId::WindowsColorsLabel: return L"Couleurs Windows";
    case UIStringId::LanguageCardHeader: return L"Langue";
    case UIStringId::InterfaceLanguageLabel: return L"Langue de l'interface";
    case UIStringId::TranslateToLabel: return L"Traduire les paroles en";
    case UIStringId::ThemeAuto: return L"Automatique";
    case UIStringId::ThemeLight: return L"Clair";
    case UIStringId::ThemeDark: return L"Sombre";
    case UIStringId::AccentAuto: return L"Automatique";
    case UIStringId::AccentManual: return L"Manuel";
    case UIStringId::LangAuto: return L"Automatique";
    case UIStringId::LangPT: return L"Português";
    case UIStringId::LangEN: return L"English";
    case UIStringId::LangES: return L"Español";
    case UIStringId::LangFR: return L"Français";
    case UIStringId::LangDE: return L"Deutsch";
    case UIStringId::LangIT: return L"Italiano";
    }
  } else if (lang == L"de") {
    switch (id) {
    case UIStringId::LyricsHeader: return L"Songtext";
    case UIStringId::TranslateButton: return L"Übersetzen";
    case UIStringId::FetchingLyrics: return L"Songtext wird geladen...";
    case UIStringId::LyricsNotFound: return L"Kein Songtext für diesen Titel gefunden.";
    case UIStringId::SettingsHeader: return L"Einstellungen";
    case UIStringId::ColorMode: return L"Modus auswählen";
    case UIStringId::AccentColorLabel: return L"Akzentfarbe";
    case UIStringId::WindowsColorsLabel: return L"Windows-Farben";
    case UIStringId::LanguageCardHeader: return L"Sprache";
    case UIStringId::InterfaceLanguageLabel: return L"Oberflächensprache";
    case UIStringId::TranslateToLabel: return L"Songtext übersetzen in";
    case UIStringId::ThemeAuto: return L"Automatisch";
    case UIStringId::ThemeLight: return L"Hell";
    case UIStringId::ThemeDark: return L"Dunkel";
    case UIStringId::AccentAuto: return L"Automatisch";
    case UIStringId::AccentManual: return L"Manuell";
    case UIStringId::LangAuto: return L"Automatisch";
    case UIStringId::LangPT: return L"Português";
    case UIStringId::LangEN: return L"English";
    case UIStringId::LangES: return L"Español";
    case UIStringId::LangFR: return L"Français";
    case UIStringId::LangDE: return L"Deutsch";
    case UIStringId::LangIT: return L"Italiano";
    }
  } else if (lang == L"it") {
    switch (id) {
    case UIStringId::LyricsHeader: return L"Testo";
    case UIStringId::TranslateButton: return L"Traduci";
    case UIStringId::FetchingLyrics: return L"Ricerca del testo...";
    case UIStringId::LyricsNotFound: return L"Testo non trovato per questo brano.";
    case UIStringId::SettingsHeader: return L"Impostazioni";
    case UIStringId::ColorMode: return L"Scegli la modalità";
    case UIStringId::AccentColorLabel: return L"Colore accento";
    case UIStringId::WindowsColorsLabel: return L"Colori di Windows";
    case UIStringId::LanguageCardHeader: return L"Lingua";
    case UIStringId::InterfaceLanguageLabel: return L"Lingua dell'interfaccia";
    case UIStringId::TranslateToLabel: return L"Traduci il testo in";
    case UIStringId::ThemeAuto: return L"Automatico";
    case UIStringId::ThemeLight: return L"Chiaro";
    case UIStringId::ThemeDark: return L"Scuro";
    case UIStringId::AccentAuto: return L"Automatico";
    case UIStringId::AccentManual: return L"Manuale";
    case UIStringId::LangAuto: return L"Automatico";
    case UIStringId::LangPT: return L"Português";
    case UIStringId::LangEN: return L"English";
    case UIStringId::LangES: return L"Español";
    case UIStringId::LangFR: return L"Français";
    case UIStringId::LangDE: return L"Deutsch";
    case UIStringId::LangIT: return L"Italiano";
    }
  }

  // Fallback padrão (Inglês)
  switch (id) {
  case UIStringId::LyricsHeader: return L"Lyrics";
  case UIStringId::TranslateButton: return L"Translate";
  case UIStringId::FetchingLyrics: return L"Fetching lyrics...";
  case UIStringId::LyricsNotFound: return L"Lyrics not found for this track.";
  case UIStringId::SettingsHeader: return L"Settings";
  case UIStringId::ColorMode: return L"Choose your mode";
  case UIStringId::AccentColorLabel: return L"Accent color";
  case UIStringId::WindowsColorsLabel: return L"Windows colors";
  case UIStringId::LanguageCardHeader: return L"Language";
  case UIStringId::InterfaceLanguageLabel: return L"Interface language";
  case UIStringId::TranslateToLabel: return L"Translate lyrics to";
  case UIStringId::ThemeAuto: return L"Automatic";
  case UIStringId::ThemeLight: return L"Light";
  case UIStringId::ThemeDark: return L"Dark";
  case UIStringId::AccentAuto: return L"Automatic";
  case UIStringId::AccentManual: return L"Manual";
  case UIStringId::LangAuto: return L"Automatic";
  case UIStringId::LangPT: return L"Português";
  case UIStringId::LangEN: return L"English";
  case UIStringId::LangES: return L"Español";
  case UIStringId::LangFR: return L"Français";
  case UIStringId::LangDE: return L"Deutsch";
  case UIStringId::LangIT: return L"Italiano";
  }
  return L"";
}

bool IsWindowsSystemLightMode() {
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

bool IsSystemLightMode() {
  if (g_Settings.theme == 1) return true;
  if (g_Settings.theme == 2) return false;
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

void UpdateAccentColorCache() {
  if (g_Settings.customAccent) {
    g_AccentColor = Color(g_Settings.accentColor);
    return;
  }

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

      int colorIndex = IsWindowsSystemLightMode() ? 4 : 1;

      int offset = colorIndex * 4;
      BYTE r = palette[offset];
      BYTE g = palette[offset + 1];
      BYTE b = palette[offset + 2];

      g_AccentColor = Color(255, r, g, b);
      return;
    }
    RegCloseKey(hKey);
  }

  DWORD color = 0;
  BOOL opaque = FALSE;
  if (SUCCEEDED(DwmGetColorizationColor(&color, &opaque))) {
    BYTE r = (color >> 16) & 0xFF;
    BYTE g = (color >> 8) & 0xFF;
    BYTE b = color & 0xFF;
    g_AccentColor = Color(255, r, g, b);
    return;
  }
  g_AccentColor = Color(255, 0, 120, 215);
}

Color GetWindowsAccentColor() {
  return g_AccentColor;
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
  DWM_WINDOW_CORNER_PREFERENCE preference =
      (DWM_WINDOW_CORNER_PREFERENCE)DWMWCP_DONOTROUND;
  DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &preference,
                        sizeof(preference));

  int ncrenderPolicy = DWMNCRP_DISABLED;
  DwmSetWindowAttribute(hwnd, DWMWA_NCRENDERING_POLICY, &ncrenderPolicy,
                        sizeof(ncrenderPolicy));

  DWORD borderColor = DWMWA_COLOR_NONE;
  DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &borderColor,
                        sizeof(borderColor));

  HMODULE hUser = GetModuleHandle(L"user32.dll");
  if (hUser) {
    auto SetComp = (pSetWindowCompositionAttribute)GetProcAddress(
        hUser, "SetWindowCompositionAttribute");
    if (SetComp) {
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
    SolidBrush redBrush(Color(255, 255, 0, 0));
    graphics.FillEllipse(&redBrush, x, y, w, h);

    Pen whitePen(Color(255, 255, 255, 255), w * 0.08f);
    float ringPad = w * 0.2f;
    graphics.DrawEllipse(&whitePen, x + ringPad, y + ringPad, w - 2 * ringPad,
                         h - 2 * ringPad);

    float triPadX = w * 0.38f;
    float triPadY = h * 0.32f;
    PointF pts[3] = {PointF(x + triPadX, y + triPadY),
                     PointF(x + triPadX, y + h - triPadY),
                     PointF(x + w - triPadX + w * 0.05f, y + h / 2.0f)};
    SolidBrush whiteBrush(Color(255, 255, 255, 255));
    graphics.FillPolygon(&whiteBrush, pts, 3);
  } else if (iconType == L"youtube") {
    SolidBrush redBrush(Color(255, 255, 0, 0));
    GraphicsPath path;
    AddRoundedRectF(path, x, y + h * 0.1f, w, h * 0.8f, w * 0.15f);
    graphics.FillPath(&redBrush, &path);

    float triPadX = w * 0.38f;
    float triPadY = h * 0.32f;
    PointF pts[3] = {PointF(x + triPadX, y + triPadY),
                     PointF(x + triPadX, y + h - triPadY),
                     PointF(x + w - triPadX + w * 0.05f, y + h / 2.0f)};
    SolidBrush whiteBrush(Color(255, 255, 255, 255));
    graphics.FillPolygon(&whiteBrush, pts, 3);
  } else if (iconType == L"spotify") {
    SolidBrush greenBrush(Color(255, 30, 215, 96));
    graphics.FillEllipse(&greenBrush, x, y, w, h);

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

  if (IsTransparencyEnabled()) {
    graphics.Clear(Color(0, 0, 0, 0));
  } else {
    if (IsWindowsSystemLightMode()) {
      graphics.Clear(Color(255, 243, 243, 243));
    } else {
      graphics.Clear(Color(255, 28, 28, 28));
    }
  }

  Color mainColor{IsWindowsSystemLightMode() ? 0xFF2D2D2D : 0xFFFFFFFF};

  MediaState state;
  {
    lock_guard<mutex> guard(g_MediaState.lock);
    state.title = g_MediaState.title;
    state.artist = g_MediaState.artist;
    state.albumArt = g_MediaState.albumArt;
    state.cachedMediaThumb = g_MediaState.cachedMediaThumb;
    state.lastMediaThumbSize = g_MediaState.lastMediaThumbSize;
    state.hasMedia = g_MediaState.hasMedia;
    state.isPlaying = g_MediaState.isPlaying;
  }

  int artSize = height - 12;
  int artX = 6;
  int artY = 6;

  GraphicsPath path;
  AddRoundedRect(path, artX, artY, artSize, artSize, 8);

  if (state.albumArt) {
    if (!state.cachedMediaThumb || state.lastMediaThumbSize != artSize) {
      Bitmap* bmp = new Bitmap(artSize, artSize, PixelFormat32bppARGB);
      Graphics gThumb(bmp);
      gThumb.SetInterpolationMode(InterpolationModeHighQualityBicubic);
      gThumb.DrawImage(state.albumArt.get(), 0, 0, artSize, artSize);
      state.cachedMediaThumb.reset(bmp);
      state.lastMediaThumbSize = artSize;
      
      lock_guard<mutex> guard(g_MediaState.lock);
      g_MediaState.cachedMediaThumb = state.cachedMediaThumb;
      g_MediaState.lastMediaThumbSize = state.lastMediaThumbSize;
    }
    TextureBrush brush(state.cachedMediaThumb.get());
    brush.SetWrapMode(WrapModeClamp);
    brush.TranslateTransform((REAL)artX, (REAL)artY, MatrixOrderAppend);

    graphics.FillPath(&brush, &path);
  } else {
    SolidBrush placeBrush{Color(40, 128, 128, 128)};
    graphics.FillPath(&placeBrush, &path);
  }

  double scale = g_Settings.buttonScale;
  int startControlX = artX + artSize + (int)(20 * scale);
  int controlY = height / 2;

  SolidBrush activeBg{Color(IsWindowsSystemLightMode() ? 15 : 40, mainColor.GetRed(),
                            mainColor.GetGreen(), mainColor.GetBlue())};

  float gap = 32.0f * (float)scale;

  float hoverW = 28.0f * (float)scale;
  float hoverH = 38.0f * (float)scale;
  float hoverR = 4.0f * (float)scale;

  SolidBrush iconBrush(mainColor);

  FontFamily testFamily(L"Segoe Fluent Icons", nullptr);
  const WCHAR *iconFontName = (testFamily.GetLastStatus() == Ok)
                                  ? L"Segoe Fluent Icons"
                                  : L"Segoe MDL2 Assets";
  FontFamily iconFontFamily(iconFontName, nullptr);
  Font ctrlIconFont(&iconFontFamily, 14.0f * scale, FontStyleRegular, UnitPixel);
  StringFormat centerFmt;
  centerFmt.SetAlignment(StringAlignmentCenter);
  centerFmt.SetLineAlignment(StringAlignmentCenter);

  // Prev
  float pX = (float)startControlX;
  if (g_HoverState == 1) {
    GraphicsPath hoverPath;
    AddRoundedRect(hoverPath, (int)(pX - hoverW / 2),
                   (int)(controlY - hoverH / 2), (int)hoverW, (int)hoverH,
                   (int)hoverR);
    graphics.FillPath(&activeBg, &hoverPath);
  }
  RectF prevRect(pX - hoverW/2, controlY - hoverH/2, hoverW, hoverH);
  graphics.DrawString(L"\xE892", -1, &ctrlIconFont, prevRect, &centerFmt, &iconBrush);

  // Play/Pause
  float plX = pX + gap;
  if (g_HoverState == 2) {
    GraphicsPath hoverPath;
    AddRoundedRect(hoverPath, (int)(plX - hoverW / 2),
                   (int)(controlY - hoverH / 2), (int)hoverW, (int)hoverH,
                   (int)hoverR);
    graphics.FillPath(&activeBg, &hoverPath);
  }
  RectF playRect(plX - hoverW/2, controlY - hoverH/2, hoverW, hoverH);
  graphics.DrawString(state.isPlaying ? L"\xE769" : L"\xE768", -1, &ctrlIconFont, playRect, &centerFmt, &iconBrush);

  // Next
  float nX = plX + gap;
  if (g_HoverState == 3) {
    GraphicsPath hoverPath;
    AddRoundedRect(hoverPath, (int)(nX - hoverW / 2),
                   (int)(controlY - hoverH / 2), (int)hoverW, (int)hoverH,
                   (int)hoverR);
    graphics.FillPath(&activeBg, &hoverPath);
  }
  RectF nextRect(nX - hoverW/2, controlY - hoverH/2, hoverW, hoverH);
  graphics.DrawString(L"\xE893", -1, &ctrlIconFont, nextRect, &centerFmt, &iconBrush);

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
  bmi.bmiHeader.biHeight = -h;
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  void *pvBits = nullptr;
  HBITMAP memBitmap =
      CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, &pvBits, NULL, 0);
  HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

  Bitmap bmp(w, h, PixelFormat32bppPARGB);
  {
    Graphics gTemp(&bmp);
    gTemp.SetSmoothingMode(SmoothingModeAntiAlias);
    gTemp.SetTextRenderingHint(TextRenderingHintAntiAlias);
    gTemp.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    gTemp.SetPixelOffsetMode(PixelOffsetModeHighQuality);

    DrawMediaPanel(gTemp, w, h);
  }

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
  BOOL isDarkMode = !IsSystemLightMode();
  DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &isDarkMode,
                        sizeof(isDarkMode));

  HMODULE hUser = GetModuleHandle(L"user32.dll");
  if (hUser) {
    auto SetComp = (pSetWindowCompositionAttribute)GetProcAddress(
        hUser, "SetWindowCompositionAttribute");
    if (SetComp) {
      ACCENT_POLICY policy = {ACCENT_DISABLED, 0, 0, 0};
      if (IsTransparencyEnabled()) {
        policy.AccentState = ACCENT_ENABLE_ACRYLICBLURBEHIND;
        policy.AccentFlags = 2;
        if (IsSystemLightMode()) {
          policy.GradientColor = 0xD8F3F3F3;
        } else {
          policy.GradientColor = 0xD8202020;
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

  float scale = (float)w / 360.0f;

  int hMax = h;
  int hBase = (int)(g_FlyoutBaseHeight * scale);
  int contentYOffset = hMax - hBase;
  int hCurr = hMax;
  float Y_top = 0.0f;

  float radius = 8.0f * scale;

  Color bgColor;
  Color borderColor;
  if (IsTransparencyEnabled()) {
    bgColor = Color(0, 0, 0, 0);
    if (IsSystemLightMode()) {
      borderColor = Color(30, 0, 0, 0);
    } else {
      borderColor = Color(40, 255, 255, 255);
    }
  } else {
    if (IsSystemLightMode()) {
      bgColor = Color(255, 243, 243, 243);
      borderColor = Color(30, 0, 0, 0);
    } else {
      bgColor = Color(255, 32, 32, 32);
      borderColor = Color(30, 255, 255, 255);
    }
  }

  graphics.Clear(Color(0, 0, 0, 0));

  GraphicsPath flyoutPath;
  AddRoundedRectF(flyoutPath, 0.5f, Y_top + 0.5f, (REAL)w - 1.0f,
                  (REAL)hCurr - 1.0f, radius);

  SolidBrush bgBrush(bgColor);
  graphics.FillPath(&bgBrush, &flyoutPath);

  Pen borderPen(borderColor, 1.0f);
  graphics.DrawPath(&borderPen, &flyoutPath);

  MediaState state;
  {
    lock_guard<mutex> guard(g_MediaState.lock);
    state.title = g_MediaState.title;
    state.artist = g_MediaState.artist;
    state.albumTitle = g_MediaState.albumTitle;
    state.albumArtist = g_MediaState.albumArtist;
    state.durationTotalSeconds = g_MediaState.durationTotalSeconds;
    state.durationPositionSeconds = g_MediaState.durationPositionSeconds;
    state.albumArt = g_MediaState.albumArt;
    state.cachedFlyoutThumb = g_MediaState.cachedFlyoutThumb;
    state.lastFlyoutThumbSize = g_MediaState.lastFlyoutThumbSize;
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

  if (contentYOffset > 10) {
    lock_guard<mutex> guard(g_LyricsMutex);

    GraphicsState lyricsStateSave = graphics.Save();
    graphics.SetClip(&flyoutPath);
    graphics.SetClip(RectF(0.0f, Y_top, (REAL)w, (REAL)contentYOffset),
                     CombineModeIntersect);

    float headerHeight = 45.0f * scale;

    SolidBrush headerBgBrush(IsSystemLightMode() ? Color(12, 0, 0, 0)
                                                 : Color(12, 255, 255, 255));
    graphics.FillRectangle(&headerBgBrush, 0.0f, Y_top, (REAL)w, headerHeight);

    Pen headerSepPen(borderColor, 1.0f);
    graphics.DrawLine(&headerSepPen, 0.0f, Y_top + headerHeight, (REAL)w,
                      Y_top + headerHeight);

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

    RectF headerTextBound;
    graphics.MeasureString(headerText.c_str(), -1, &headerFont, headerTextRect,
                           &headerFormat, &headerTextBound);

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

    float headerBtnH = 26.0f * scale;

    float settingsBtnW = 26.0f * scale;
    float settingsBtnX = w - settingsBtnW - 15.0f * scale;
    float settingsBtnY = Y_top + (headerHeight - headerBtnH) / 2.0f;

    GraphicsPath settingsBtnPath;
    AddRoundedRectF(settingsBtnPath, settingsBtnX, settingsBtnY, settingsBtnW, headerBtnH, 4.0f * scale);
    Color accentColor = GetWindowsAccentColor();
    SolidBrush settingsBtnBgBrush(g_SettingsCardOpen ? accentColor : Color(30, 128, 128, 128));
    graphics.FillPath(&settingsBtnBgBrush, &settingsBtnPath);
    SolidBrush settingsIconBrush(g_SettingsCardOpen ? GetAccentForegroundColor(accentColor) : mainTextColor);
    RectF settingsBtnRectf(settingsBtnX, settingsBtnY, settingsBtnW, headerBtnH);
    Font headerIconFont(&iconFontFamily, 12.0f * scale, FontStyleRegular, UnitPixel);
    graphics.DrawString(L"\xE713", -1, &headerIconFont, settingsBtnRectf, &centerFormat, &settingsIconBrush);

    float pinBtnW = 26.0f * scale;
    float pinBtnX = settingsBtnX - pinBtnW - 8.0f * scale;
    float pinBtnY = settingsBtnY;

    GraphicsPath pinBtnPath;
    AddRoundedRectF(pinBtnPath, pinBtnX, pinBtnY, pinBtnW, headerBtnH,
                    4.0f * scale);
    accentColor = GetWindowsAccentColor();
    SolidBrush pinBtnBgBrush(g_FlyoutPinned ? accentColor
                                            : Color(30, 128, 128, 128));
    graphics.FillPath(&pinBtnBgBrush, &pinBtnPath);
    SolidBrush pinIconBrush(
        g_FlyoutPinned ? GetAccentForegroundColor(accentColor) : mainTextColor);
    RectF pinBtnRectf(pinBtnX, pinBtnY, pinBtnW, headerBtnH);

    graphics.DrawString(g_FlyoutPinned ? L"\xE718" : L"\xE77A", -1,
                        &headerIconFont, pinBtnRectf, &centerFormat,
                        &pinIconBrush);

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

    RectF transIconRect(transBtnX + 8.0f * scale, transBtnY, 14.0f * scale,
                        headerBtnH);
    graphics.DrawString(L"\xE8C1", -1, &headerIconFont, transIconRect,
                        &centerFormat, &transIconBrush);

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
      graphics.SetClip(lyricsRect);
      if (!g_SyncedLyrics.empty()) {
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
              RectF origRect(lyricsRect.X, currentY, lyricsRect.Width,
                             origHeight);
              graphics.DrawString(g_SyncedLyrics[i].text.c_str(), -1, &lineFont,
                                  origRect, &lyricsFormat, &lineBrush);

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
        StringFormat plainFormat;
        plainFormat.SetAlignment(StringAlignmentCenter);
        RectF drawRect(lyricsRect.X, lyricsRect.Y + g_LyricsScrollY,
                       lyricsRect.Width, 10000.0f);
        graphics.DrawString(g_PlainLyrics.c_str(), -1, &lyricsFont, drawRect,
                            &plainFormat, &lyricsBrush);
      }
      graphics.ResetClip();
    }

    graphics.ResetClip();
    graphics.Restore(lyricsStateSave);
  }

  int mediaYOffset = hMax - hBase;

  int artSize = (int)(120 * scale);
  int artX = (int)(15 * scale);
  int artY = mediaYOffset + (int)(15 * scale);

  GraphicsPath artPath;
  AddRoundedRect(artPath, artX, artY, artSize, artSize, (int)(10 * scale));

  if (state.albumArt) {
    if (!state.cachedFlyoutThumb || state.lastFlyoutThumbSize != artSize) {
      Bitmap* bmp = new Bitmap(artSize, artSize, PixelFormat32bppARGB);
      Graphics gThumb(bmp);
      gThumb.SetInterpolationMode(InterpolationModeHighQualityBicubic);
      gThumb.DrawImage(state.albumArt.get(), 0, 0, artSize, artSize);
      state.cachedFlyoutThumb.reset(bmp);
      state.lastFlyoutThumbSize = artSize;
      
      lock_guard<mutex> guard(g_MediaState.lock);
      g_MediaState.cachedFlyoutThumb = state.cachedFlyoutThumb;
      g_MediaState.lastFlyoutThumbSize = state.lastFlyoutThumbSize;
    }
    TextureBrush brush(state.cachedFlyoutThumb.get());
    brush.SetWrapMode(WrapModeClamp);
    brush.TranslateTransform((REAL)artX, (REAL)artY, MatrixOrderAppend);
    graphics.FillPath(&brush, &artPath);
  } else {
    SolidBrush placeBrush{Color(40, 128, 128, 128)};
    graphics.FillPath(&placeBrush, &artPath);
  }

  int textX = (int)(150 * scale);
  int textY = mediaYOffset + (int)(15 * scale);
  int textMaxW = w - textX - (int)(15 * scale);

  Font titleFont(&fontFamily, 13.0f * scale, FontStyleBold, UnitPixel);
  SolidBrush titleBrush(mainTextColor);

  Font artistFont(&fontFamily, 11.0f * scale, FontStyleRegular, UnitPixel);
  SolidBrush artistBrush(subTextColor);

  Font albumFont(&fontFamily, 10.0f * scale, FontStyleRegular, UnitPixel);
  SolidBrush albumBrush(detailTextColor);

  RectF titleRect((REAL)textX, (REAL)textY, (REAL)textMaxW, 20.0f * scale);
  StringFormat textFormat;
  textFormat.SetTrimming(StringTrimmingEllipsisCharacter);
  textFormat.SetFormatFlags(StringFormatFlagsNoWrap);
  graphics.DrawString(state.title.c_str(), -1, &titleFont, titleRect,
                      &textFormat, &titleBrush);

  RectF artistRect((REAL)textX, (REAL)textY + (22.0f * scale), (REAL)textMaxW,
                   16.0f * scale);
  graphics.DrawString(state.artist.c_str(), -1, &artistFont, artistRect,
                      &textFormat, &artistBrush);

  wstring albumText = state.albumTitle;
  if (!albumText.empty() && !state.albumArtist.empty() &&
      state.albumArtist != state.artist) {
    albumText += L" - " + state.albumArtist;
  }
  RectF albumRect((REAL)textX, (REAL)textY + (38.0f * scale), (REAL)textMaxW,
                   15.0f * scale);
  graphics.DrawString(albumText.c_str(), -1, &albumFont, albumRect, &textFormat,
                      &albumBrush);

  if (state.hasMedia && state.durationTotalSeconds > 0) {
    float barY = (float)(textY + (62.0f * scale));
    float barH = 4.0f * scale;

    g_ProgressBarX = (float)textX;
    g_ProgressBarY = barY;
    g_ProgressBarW = (float)textMaxW;
    g_ProgressBarH = barH;

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

    SolidBrush progressBgBrush(Color(40, mainTextColor.GetRed(),
                                     mainTextColor.GetGreen(),
                                     mainTextColor.GetBlue()));
    GraphicsPath progBgPath;
    AddRoundedRect(progBgPath, textX, (int)barY, textMaxW, (int)barH,
                   (int)(2.0f * scale));
    graphics.FillPath(&progressBgBrush, &progBgPath);

    if (fillW > 0.0f) {
      SolidBrush progressFillBrush(GetWindowsAccentColor());
      GraphicsPath progFillPath;
      AddRoundedRect(progFillPath, textX, (int)barY, (int)fillW, (int)barH,
                     (int)(2.0f * scale));
      graphics.FillPath(&progressFillBrush, &progFillPath);
    }

    float thumbX = (float)textX + fillW;
    float thumbCenterY = barY + barH / 2.0f;

    float smallRadius = 5.0f * scale;
    float largeRadius = 8.0f * scale;

    g_ThumbTargetRadius =
        (g_ProgressHover || g_ProgressDragging) ? largeRadius : smallRadius;

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

    if (g_ThumbAnimRadius <= 0.0f)
      g_ThumbAnimRadius = smallRadius;

    float r = g_ThumbAnimRadius;
    SolidBrush thumbBrush(GetWindowsAccentColor());
    graphics.FillEllipse(&thumbBrush, thumbX - r, thumbCenterY - r, r * 2.0f,
                         r * 2.0f);

    Pen thumbBorder(Color(60, 0, 0, 0), 1.0f);
    graphics.DrawEllipse(&thumbBorder, thumbX - r, thumbCenterY - r, r * 2.0f,
                         r * 2.0f);

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
    Font tagFont(&fontFamily, 9.0f * scale, FontStyleRegular, UnitPixel);
    SolidBrush tagBrush(detailTextColor);
    RectF tagRect((REAL)textX, (REAL)textY + (65.0f * scale), (REAL)textMaxW,
                  15.0f * scale);
    graphics.DrawString(L"Metadados de Mídia Ativos", -1, &tagFont, tagRect,
                        nullptr, &tagBrush);
  }

  if (contentYOffset > 10) {
    Pen sepPen(borderColor, 1.0f);
    graphics.DrawLine(&sepPen, 0.0f, (REAL)mediaYOffset, (REAL)w,
                      (REAL)mediaYOffset);
  }

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
      RectF iconRect(currX, tagY, iconW, tagH);
      graphics.DrawString(state.sourceAppIcon.c_str(), -1, &tagIconFont,
                          iconRect, &centerFormat, &tagIconBrush);
      currX += iconW + iconGap;
    } else {
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

    RectF nameRect(currX, tagY, textW + 4.0f * scale, tagH);
    graphics.DrawString(state.sourceAppName.c_str(), -1, &tagFont, nameRect,
                        &tagTextFormat, &tagTextBrush);
  }

  if (state.hasMedia) {
    float tagH = 26.0f * scale;
    float tagY = h - tagH - (18.0f * scale);

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

    g_VolumeAreaX = volX - 20.0f * scale;
    g_VolumeAreaW = (float)w - g_VolumeAreaX;
    g_VolumeIconX = volX;
    g_VolumeIconW = volIconSize;

    float currentVol = 0.0f;
    if (g_VolumeDragging) {
      currentVol = g_VolumeDragRatio;
    } else {
      currentVol = GetVolumeLevel(state.appId);
    }

    bool isMuted = GetAppMute(state.appId);

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

    if (g_VolumeHover || g_VolumeDragging) {
      SolidBrush volBgBrush(Color(40, mainTextColor.GetRed(),
                                  mainTextColor.GetGreen(),
                                  mainTextColor.GetBlue()));
      GraphicsPath volBgPath;
      AddRoundedRect(volBgPath, (int)g_VolumeX, (int)g_VolumeY, (int)g_VolumeW,
                     (int)g_VolumeH, (int)(2.0f * scale));
      graphics.FillPath(&volBgBrush, &volBgPath);

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

  if (g_SettingsCardOpen) {
    SolidBrush overlayBrush(Color(150, 0, 0, 0));
    graphics.FillRectangle(&overlayBrush, 0.0f, 0.0f, (REAL)w, (REAL)hMax);

    float cardW = 310.0f * scale;
    float cardH = 372.0f * scale;
    float cardX = (w - cardW) / 2.0f;
    float cardY = (hMax - cardH) / 2.0f;

    GraphicsPath cardPath;
    AddRoundedRectF(cardPath, cardX, cardY, cardW, cardH, 8.0f * scale);
    SolidBrush cardBgBrush(IsSystemLightMode() ? Color(255, 245, 245, 245)
                                              : Color(255, 32, 32, 32));
    graphics.FillPath(&cardBgBrush, &cardPath);
    Pen cardBorderPen(borderColor, 1.0f);
    graphics.DrawPath(&cardBorderPen, &cardPath);

    Font titleFont(&fontFamily, 13.0f * scale, FontStyleBold, UnitPixel);
    SolidBrush titleBrush(mainTextColor);
    RectF titleRect(cardX, cardY + 12.0f * scale, cardW, 20.0f * scale);
    graphics.DrawString(GetLocalizedText(UIStringId::SettingsHeader).c_str(), -1, &titleFont, titleRect,
                        &centerFormat, &titleBrush);

    Font labelFont(&fontFamily, 10.0f * scale, FontStyleBold, UnitPixel);
    SolidBrush labelBrush(mainTextColor);
    StringFormat leftFmt;
    leftFmt.SetAlignment(StringAlignmentNear);
    leftFmt.SetLineAlignment(StringAlignmentCenter);

    SolidBrush subCardBgBrush(IsSystemLightMode() ? Color(255, 255, 255, 255)
                                                 : Color(255, 38, 38, 38));
    Pen subCardBorderPen(IsSystemLightMode() ? Color(30, 0, 0, 0)
                                             : Color(25, 255, 255, 255), 1.0f);
    Pen dividerPen(IsSystemLightMode() ? Color(15, 0, 0, 0)
                                       : Color(15, 255, 255, 255), 1.0f);

    float subCardX = cardX + 10.0f * scale;
    float subCardW = cardW - 20.0f * scale;
    float comboW = 100.0f * scale;
    float comboH = 24.0f * scale;
    Font comboFont(&fontFamily, 9.5f * scale, FontStyleRegular, UnitPixel);
    Font iconFontSmall(&iconFontFamily, 7.5f * scale, FontStyleRegular, UnitPixel);

    // --- CARD 1: APARÊNCIA ---
    float card1X = subCardX;
    float card1Y = cardY + 36.0f * scale;
    float card1W = subCardW;
    float card1H = 242.0f * scale;

    GraphicsPath card1Path;
    AddRoundedRectF(card1Path, card1X, card1Y, card1W, card1H, 6.0f * scale);
    graphics.FillPath(&subCardBgBrush, &card1Path);
    graphics.DrawPath(&subCardBorderPen, &card1Path);

    // Row 1: Modo de cor
    float row1Y = card1Y + 6.0f * scale;
    RectF label1Rect(card1X + 12.0f * scale, row1Y, card1W - comboW - 20.0f * scale, 36.0f * scale);
    graphics.DrawString(GetLocalizedText(UIStringId::ColorMode).c_str(), -1, &labelFont, label1Rect, &leftFmt, &labelBrush);

    float combo1X = card1X + card1W - comboW - 12.0f * scale;
    float combo1Y = card1Y + 12.0f * scale;
    GraphicsPath combo1Path;
    AddRoundedRectF(combo1Path, combo1X, combo1Y, comboW, comboH, 4.0f * scale);
    Color combo1Bg = g_HoverThemeCombo ? (IsSystemLightMode() ? Color(40, 0, 0, 0) : Color(45, 255, 255, 255))
                                       : (IsSystemLightMode() ? Color(20, 0, 0, 0) : Color(25, 255, 255, 255));
    SolidBrush combo1BgBrush(combo1Bg);
    graphics.FillPath(&combo1BgBrush, &combo1Path);
    graphics.DrawPath(&subCardBorderPen, &combo1Path);

    wstring themeName = GetLocalizedText(UIStringId::ThemeAuto);
    if (g_Settings.theme == 1) themeName = GetLocalizedText(UIStringId::ThemeLight);
    else if (g_Settings.theme == 2) themeName = GetLocalizedText(UIStringId::ThemeDark);

    RectF text1Rect(combo1X + 8.0f * scale, combo1Y, comboW - 24.0f * scale, comboH);
    graphics.DrawString(themeName.c_str(), -1, &comboFont, text1Rect, &leftFmt, &labelBrush);

    RectF arrow1Rect(combo1X + comboW - 20.0f * scale, combo1Y, 14.0f * scale, comboH);
    graphics.DrawString(L"\xE70D", -1, &iconFontSmall, arrow1Rect, &centerFormat, &labelBrush);

    // Divider 1
    float div1Y = card1Y + 42.0f * scale;
    graphics.DrawLine(&dividerPen, card1X + 12.0f * scale, div1Y, card1X + card1W - 12.0f * scale, div1Y);

    // Row 2: Cor em destaque
    float row2Y = card1Y + 43.0f * scale;
    RectF label2Rect(card1X + 12.0f * scale, row2Y, card1W - comboW - 20.0f * scale, 36.0f * scale);
    graphics.DrawString(GetLocalizedText(UIStringId::AccentColorLabel).c_str(), -1, &labelFont, label2Rect, &leftFmt, &labelBrush);

    float combo2X = combo1X;
    float combo2Y = card1Y + 49.0f * scale;
    GraphicsPath combo2Path;
    AddRoundedRectF(combo2Path, combo2X, combo2Y, comboW, comboH, 4.0f * scale);
    Color combo2Bg = g_HoverAccentCombo ? (IsSystemLightMode() ? Color(40, 0, 0, 0) : Color(45, 255, 255, 255))
                                       : (IsSystemLightMode() ? Color(20, 0, 0, 0) : Color(25, 255, 255, 255));
    SolidBrush combo2BgBrush(combo2Bg);
    graphics.FillPath(&combo2BgBrush, &combo2Path);
    graphics.DrawPath(&subCardBorderPen, &combo2Path);

    wstring accentModeName = g_Settings.customAccent ? GetLocalizedText(UIStringId::AccentManual) : GetLocalizedText(UIStringId::AccentAuto);
    RectF text2Rect(combo2X + 8.0f * scale, combo2Y, comboW - 24.0f * scale, comboH);
    graphics.DrawString(accentModeName.c_str(), -1, &comboFont, text2Rect, &leftFmt, &labelBrush);

    RectF arrow2Rect(combo2X + comboW - 20.0f * scale, combo2Y, 14.0f * scale, comboH);
    graphics.DrawString(L"\xE70D", -1, &iconFontSmall, arrow2Rect, &centerFormat, &labelBrush);

    // Label: Cores do Windows
    float label3Y = card1Y + 86.0f * scale;
    RectF label3Rect(card1X + 12.0f * scale, label3Y, card1W - 24.0f * scale, 16.0f * scale);
    graphics.DrawString(GetLocalizedText(UIStringId::WindowsColorsLabel).c_str(), -1, &labelFont, label3Rect, &leftFmt, &labelBrush);

    // Grade de cores
    float gridWidth = 10 * 22.0f * scale + 9 * 4.0f * scale; // 256.0f * scale
    float rowX = card1X + (card1W - gridWidth) / 2.0f;
    float rowY = card1Y + 106.0f * scale;

    float squareSize = 22.0f * scale;
    float spacing = 4.0f * scale;

    for (int i = 0; i < 50; i++) {
      int row = i / 10;
      int col = i % 10;
      float sx = rowX + col * (squareSize + spacing);
      float sy = rowY + row * (squareSize + spacing);

      Color squareColor(WINDOWS_ACCENT_COLORS[i]);
      SolidBrush squareBrush(squareColor);

      GraphicsPath squarePath;
      AddRoundedRectF(squarePath, sx, sy, squareSize, squareSize, 3.0f * scale);
      graphics.FillPath(&squareBrush, &squarePath);

      bool isSelected = g_Settings.customAccent && g_Settings.accentColor == WINDOWS_ACCENT_COLORS[i];
      bool isHovered = (i == g_HoveredGridIndex);

      if (isSelected) {
        Pen borderPen(mainTextColor, 2.0f * scale);
        graphics.DrawPath(&borderPen, &squarePath);

        Font iconFont2(&iconFontFamily, 11.0f * scale, FontStyleRegular, UnitPixel);
        SolidBrush checkBrush(GetAccentForegroundColor(squareColor));
        graphics.DrawString(L"\xE73E", -1, &iconFont2, RectF(sx, sy, squareSize, squareSize), &centerFormat, &checkBrush);
      } else if (isHovered) {
        Pen hoverPen(Color(180, mainTextColor.GetRed(), mainTextColor.GetGreen(), mainTextColor.GetBlue()), 1.5f * scale);
        graphics.DrawPath(&hoverPen, &squarePath);
      } else {
        Pen borderPen(Color(30, 128, 128, 128), 1.0f);
        graphics.DrawPath(&borderPen, &squarePath);
      }
    }


    // --- CARD 2: IDIOMA ---
    float card2X = subCardX;
    float card2Y = cardY + 284.0f * scale;
    float card2W = subCardW;
    float card2H = 80.0f * scale;

    GraphicsPath card2Path;
    AddRoundedRectF(card2Path, card2X, card2Y, card2W, card2H, 6.0f * scale);
    graphics.FillPath(&subCardBgBrush, &card2Path);
    graphics.DrawPath(&subCardBorderPen, &card2Path);

    // Row 1: Idioma da interface
    float row3Y = card2Y + 4.0f * scale;
    RectF label4Rect(card2X + 12.0f * scale, row3Y, card2W - comboW - 20.0f * scale, 36.0f * scale);
    graphics.DrawString(GetLocalizedText(UIStringId::InterfaceLanguageLabel).c_str(), -1, &labelFont, label4Rect, &leftFmt, &labelBrush);

    float combo3X = card2X + card2W - comboW - 12.0f * scale;
    float combo3Y = card2Y + 10.0f * scale;
    GraphicsPath combo3Path;
    AddRoundedRectF(combo3Path, combo3X, combo3Y, comboW, comboH, 4.0f * scale);
    Color combo3Bg = g_HoverUILangCombo ? (IsSystemLightMode() ? Color(40, 0, 0, 0) : Color(45, 255, 255, 255))
                                       : (IsSystemLightMode() ? Color(20, 0, 0, 0) : Color(25, 255, 255, 255));
    SolidBrush combo3BgBrush(combo3Bg);
    graphics.FillPath(&combo3BgBrush, &combo3Path);
    graphics.DrawPath(&subCardBorderPen, &combo3Path);

    wstring uiLangName = GetLocalizedText(UIStringId::LangAuto);
    if (g_Settings.uiLanguage == 1) uiLangName = GetLocalizedText(UIStringId::LangPT);
    else if (g_Settings.uiLanguage == 2) uiLangName = GetLocalizedText(UIStringId::LangEN);
    else if (g_Settings.uiLanguage == 3) uiLangName = GetLocalizedText(UIStringId::LangES);
    else if (g_Settings.uiLanguage == 4) uiLangName = GetLocalizedText(UIStringId::LangFR);
    else if (g_Settings.uiLanguage == 5) uiLangName = GetLocalizedText(UIStringId::LangDE);
    else if (g_Settings.uiLanguage == 6) uiLangName = GetLocalizedText(UIStringId::LangIT);

    RectF text3Rect(combo3X + 8.0f * scale, combo3Y, comboW - 24.0f * scale, comboH);
    graphics.DrawString(uiLangName.c_str(), -1, &comboFont, text3Rect, &leftFmt, &labelBrush);

    RectF arrow3Rect(combo3X + comboW - 20.0f * scale, combo3Y, 14.0f * scale, comboH);
    graphics.DrawString(L"\xE70D", -1, &iconFontSmall, arrow3Rect, &centerFormat, &labelBrush);

    // Divider 2
    float div2Y = card2Y + 40.0f * scale;
    graphics.DrawLine(&dividerPen, card2X + 12.0f * scale, div2Y, card2X + card2W - 12.0f * scale, div2Y);

    // Row 2: Traduzir letras para
    float row4Y = card2Y + 40.0f * scale;
    RectF label5Rect(card2X + 12.0f * scale, row4Y, card2W - comboW - 20.0f * scale, 36.0f * scale);
    graphics.DrawString(GetLocalizedText(UIStringId::TranslateToLabel).c_str(), -1, &labelFont, label5Rect, &leftFmt, &labelBrush);

    float combo4X = combo3X;
    float combo4Y = card2Y + 46.0f * scale;
    GraphicsPath combo4Path;
    AddRoundedRectF(combo4Path, combo4X, combo4Y, comboW, comboH, 4.0f * scale);
    Color combo4Bg = g_HoverTransLangCombo ? (IsSystemLightMode() ? Color(40, 0, 0, 0) : Color(45, 255, 255, 255))
                                          : (IsSystemLightMode() ? Color(20, 0, 0, 0) : Color(25, 255, 255, 255));
    SolidBrush combo4BgBrush(combo4Bg);
    graphics.FillPath(&combo4BgBrush, &combo4Path);
    graphics.DrawPath(&subCardBorderPen, &combo4Path);

    wstring transLangName = GetLocalizedText(UIStringId::LangAuto);
    if (g_Settings.translationLanguage == 1) transLangName = GetLocalizedText(UIStringId::LangPT);
    else if (g_Settings.translationLanguage == 2) transLangName = GetLocalizedText(UIStringId::LangEN);
    else if (g_Settings.translationLanguage == 3) transLangName = GetLocalizedText(UIStringId::LangES);
    else if (g_Settings.translationLanguage == 4) transLangName = GetLocalizedText(UIStringId::LangFR);
    else if (g_Settings.translationLanguage == 5) transLangName = GetLocalizedText(UIStringId::LangDE);
    else if (g_Settings.translationLanguage == 6) transLangName = GetLocalizedText(UIStringId::LangIT);

    RectF text4Rect(combo4X + 8.0f * scale, combo4Y, comboW - 24.0f * scale, comboH);
    graphics.DrawString(transLangName.c_str(), -1, &comboFont, text4Rect, &leftFmt, &labelBrush);

    RectF arrow4Rect(combo4X + comboW - 20.0f * scale, combo4Y, 14.0f * scale, comboH);
    graphics.DrawString(L"\xE70D", -1, &iconFontSmall, arrow4Rect, &centerFormat, &labelBrush);


    // --- Dropdowns Flutuantes (Desenhar por Último) ---
    wstring themeItems[3] = {
      GetLocalizedText(UIStringId::ThemeAuto),
      GetLocalizedText(UIStringId::ThemeLight),
      GetLocalizedText(UIStringId::ThemeDark)
    };

    wstring accentItems[2] = {
      GetLocalizedText(UIStringId::AccentAuto),
      GetLocalizedText(UIStringId::AccentManual)
    };

    wstring langItems[7] = {
      GetLocalizedText(UIStringId::LangAuto),
      GetLocalizedText(UIStringId::LangPT),
      GetLocalizedText(UIStringId::LangEN),
      GetLocalizedText(UIStringId::LangES),
      GetLocalizedText(UIStringId::LangFR),
      GetLocalizedText(UIStringId::LangDE),
      GetLocalizedText(UIStringId::LangIT)
    };

    // 1. Dropdown do Modo de Cor
    if (g_ThemeDropdownOpen) {
      float dropX = combo1X;
      float dropY = combo1Y + comboH + 2.0f * scale;
      float dropW = comboW;
      float dropH = 3 * 28.0f * scale;

      GraphicsPath dropPath;
      AddRoundedRectF(dropPath, dropX, dropY, dropW, dropH, 4.0f * scale);
      SolidBrush dropBgBrush(IsSystemLightMode() ? Color(255, 255, 255, 255) : Color(255, 45, 45, 45));
      graphics.FillPath(&dropBgBrush, &dropPath);
      Pen dropBorderPen(borderColor, 1.0f);
      graphics.DrawPath(&dropBorderPen, &dropPath);

      for (int i = 0; i < 3; i++) {
        float itemY = dropY + i * 28.0f * scale;

        if (g_HoveredDropdownItem == i) {
          GraphicsPath itemPath;
          AddRoundedRectF(itemPath, dropX + 2.0f * scale, itemY + 2.0f * scale, dropW - 4.0f * scale, 24.0f * scale, 3.0f * scale);
          SolidBrush itemHoverBg(IsSystemLightMode() ? Color(25, 0, 0, 0) : Color(25, 255, 255, 255));
          graphics.FillPath(&itemHoverBg, &itemPath);
        }

        if (g_Settings.theme == i) {
          SolidBrush barBrush(GetWindowsAccentColor());
          graphics.FillRectangle(&barBrush, dropX + 4.0f * scale, itemY + 6.0f * scale, 3.0f * scale, 16.0f * scale);
        }

        RectF textRect(dropX + 12.0f * scale, itemY, dropW - 16.0f * scale, 28.0f * scale);
        graphics.DrawString(themeItems[i].c_str(), -1, &comboFont, textRect, &leftFmt, &labelBrush);
      }
    }

    // 2. Dropdown da Cor em Destaque
    if (g_AccentDropdownOpen) {
      float dropX = combo2X;
      float dropY = combo2Y + comboH + 2.0f * scale;
      float dropW = comboW;
      float dropH = 2 * 28.0f * scale;

      GraphicsPath dropPath;
      AddRoundedRectF(dropPath, dropX, dropY, dropW, dropH, 4.0f * scale);
      SolidBrush dropBgBrush(IsSystemLightMode() ? Color(255, 255, 255, 255) : Color(255, 45, 45, 45));
      graphics.FillPath(&dropBgBrush, &dropPath);
      Pen dropBorderPen(borderColor, 1.0f);
      graphics.DrawPath(&dropBorderPen, &dropPath);

      for (int i = 0; i < 2; i++) {
        float itemY = dropY + i * 28.0f * scale;

        if (g_HoveredDropdownItem == i) {
          GraphicsPath itemPath;
          AddRoundedRectF(itemPath, dropX + 2.0f * scale, itemY + 2.0f * scale, dropW - 4.0f * scale, 24.0f * scale, 3.0f * scale);
          SolidBrush itemHoverBg(IsSystemLightMode() ? Color(25, 0, 0, 0) : Color(25, 255, 255, 255));
          graphics.FillPath(&itemHoverBg, &itemPath);
        }

        bool isItemSelected = (i == 0 && !g_Settings.customAccent) || (i == 1 && g_Settings.customAccent);
        if (isItemSelected) {
          SolidBrush barBrush(GetWindowsAccentColor());
          graphics.FillRectangle(&barBrush, dropX + 4.0f * scale, itemY + 6.0f * scale, 3.0f * scale, 16.0f * scale);
        }

        RectF textRect(dropX + 12.0f * scale, itemY, dropW - 16.0f * scale, 28.0f * scale);
        graphics.DrawString(accentItems[i].c_str(), -1, &comboFont, textRect, &leftFmt, &labelBrush);
      }
    }

    // 3. Dropdown do Idioma da Interface
    if (g_UILangDropdownOpen) {
      float dropX = combo3X;
      float dropY = combo3Y + comboH + 2.0f * scale;
      float dropW = comboW;
      float dropH = 7 * 28.0f * scale;

      GraphicsPath dropPath;
      AddRoundedRectF(dropPath, dropX, dropY, dropW, dropH, 4.0f * scale);
      SolidBrush dropBgBrush(IsSystemLightMode() ? Color(255, 255, 255, 255) : Color(255, 45, 45, 45));
      graphics.FillPath(&dropBgBrush, &dropPath);
      Pen dropBorderPen(borderColor, 1.0f);
      graphics.DrawPath(&dropBorderPen, &dropPath);

      for (int i = 0; i < 7; i++) {
        float itemY = dropY + i * 28.0f * scale;

        if (g_HoveredDropdownItem == i) {
          GraphicsPath itemPath;
          AddRoundedRectF(itemPath, dropX + 2.0f * scale, itemY + 2.0f * scale, dropW - 4.0f * scale, 24.0f * scale, 3.0f * scale);
          SolidBrush itemHoverBg(IsSystemLightMode() ? Color(25, 0, 0, 0) : Color(25, 255, 255, 255));
          graphics.FillPath(&itemHoverBg, &itemPath);
        }

        if (g_Settings.uiLanguage == i) {
          SolidBrush barBrush(GetWindowsAccentColor());
          graphics.FillRectangle(&barBrush, dropX + 4.0f * scale, itemY + 6.0f * scale, 3.0f * scale, 16.0f * scale);
        }

        RectF textRect(dropX + 12.0f * scale, itemY, dropW - 16.0f * scale, 28.0f * scale);
        graphics.DrawString(langItems[i].c_str(), -1, &comboFont, textRect, &leftFmt, &labelBrush);
      }
    }

    // 4. Dropdown do Idioma da Tradução (Abre para CIMA)
    if (g_TransLangDropdownOpen) {
      float dropX = combo4X;
      float dropW = comboW;
      float dropH = 7 * 28.0f * scale;
      float dropY = combo4Y - dropH - 2.0f * scale; // UP

      GraphicsPath dropPath;
      AddRoundedRectF(dropPath, dropX, dropY, dropW, dropH, 4.0f * scale);
      SolidBrush dropBgBrush(IsSystemLightMode() ? Color(255, 255, 255, 255) : Color(255, 45, 45, 45));
      graphics.FillPath(&dropBgBrush, &dropPath);
      Pen dropBorderPen(borderColor, 1.0f);
      graphics.DrawPath(&dropBorderPen, &dropPath);

      for (int i = 0; i < 7; i++) {
        float itemY = dropY + i * 28.0f * scale;

        if (g_HoveredDropdownItem == i) {
          GraphicsPath itemPath;
          AddRoundedRectF(itemPath, dropX + 2.0f * scale, itemY + 2.0f * scale, dropW - 4.0f * scale, 24.0f * scale, 3.0f * scale);
          SolidBrush itemHoverBg(IsSystemLightMode() ? Color(25, 0, 0, 0) : Color(25, 255, 255, 255));
          graphics.FillPath(&itemHoverBg, &itemPath);
        }

        if (g_Settings.translationLanguage == i) {
          SolidBrush barBrush(GetWindowsAccentColor());
          graphics.FillRectangle(&barBrush, dropX + 4.0f * scale, itemY + 6.0f * scale, 3.0f * scale, 16.0f * scale);
        }

        RectF textRect(dropX + 12.0f * scale, itemY, dropW - 16.0f * scale, 28.0f * scale);
        graphics.DrawString(langItems[i].c_str(), -1, &comboFont, textRect, &leftFmt, &labelBrush);
      }
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

  int yOffsetAnimation =
      (int)((1.0f - g_FlyoutOpenProgress) * g_FlyoutStartOffset);

  int finalY = g_FlyoutTargetY + yOffsetAnimation;

  bool sizeOrPosChanged =
      (g_FlyoutTargetX != g_LastFlyoutX || finalY != g_LastFlyoutY ||
       w != g_LastFlyoutW || hMax != g_LastFlyoutH);

  if (sizeOrPosChanged) {
    g_LastFlyoutX = g_FlyoutTargetX;
    g_LastFlyoutY = finalY;
    g_LastFlyoutW = w;
    g_LastFlyoutH = hMax;

    UINT parentFlags = SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS;
    if (IsTransparencyEnabled()) {
      parentFlags |= SWP_NOREDRAW;
    }
    SetWindowPos(g_hFlyoutWindow, NULL, g_FlyoutTargetX, finalY, w, hMax,
                 parentFlags);

    SetWindowPos(g_hFlyoutChildWindow, NULL, g_FlyoutTargetX, finalY, w, hMax,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS | SWP_NOREDRAW);

    if (!IsTransparencyEnabled()) {
      InvalidateRect(g_hFlyoutWindow, NULL, TRUE);
    }
  }

  HRGN hRgn = CreateRectRgn(0, 0, w, hMax);
  SetWindowRgn(g_hFlyoutWindow, hRgn, TRUE);

  HDC hdcScreen = GetDC(NULL);
  HDC memDC = CreateCompatibleDC(hdcScreen);

  BITMAPINFO bmi = {0};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = w;
  bmi.bmiHeader.biHeight = -hMax;
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
  POINT ptDest = {g_FlyoutTargetX, finalY};
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

    float hitMargin = 12.0f;
    bool overBar = false;
    bool overVolume = false;

    if (!g_SettingsCardOpen) {
      overBar = (x >= g_ProgressBarX - hitMargin &&
                 x <= g_ProgressBarX + g_ProgressBarW + hitMargin &&
                 y >= g_ProgressBarY - hitMargin &&
                 y <= g_ProgressBarY + g_ProgressBarH + hitMargin);

      float volHitMarginV = 30.0f * scale;
      overVolume =
          (x >= g_VolumeAreaX && x <= g_VolumeAreaX + g_VolumeAreaW &&
           y >= g_VolumeY - volHitMarginV && y <= rcClient.bottom);
    }

    if (g_ProgressDragging) {
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

      // Lógica de hover para o card de configurações
      if (g_SettingsCardOpen) {
        int w = rcClient.right - rcClient.left;
        int h = rcClient.bottom - rcClient.top;
        float cardW = 310.0f * scale;
        float cardH = 372.0f * scale;
        float cardX = (w - cardW) / 2.0f;
        float cardY = (h - cardH) / 2.0f;

        float subCardX = cardX + 10.0f * scale;
        float subCardW = cardW - 20.0f * scale;
        float comboW = 100.0f * scale;
        float comboH = 24.0f * scale;

        float card1X = subCardX;
        float card1Y = cardY + 36.0f * scale;
        float card1W = subCardW;
        float combo1X = card1X + card1W - comboW - 12.0f * scale;
        float combo1Y = card1Y + 12.0f * scale;
        float combo2X = combo1X;
        float combo2Y = card1Y + 49.0f * scale;

        float card2X = subCardX;
        float card2Y = cardY + 284.0f * scale;
        float card2W = subCardW;
        float combo3X = card2X + card2W - comboW - 12.0f * scale;
        float combo3Y = card2Y + 10.0f * scale;
        float combo4X = combo3X;
        float combo4Y = card2Y + 46.0f * scale;

        bool repaintNeeded = false;

        // Se algum dropdown estiver aberto, verifica hover em seus itens
        if (g_ThemeDropdownOpen) {
          float dropX = combo1X;
          float dropY = combo1Y + comboH + 2.0f * scale;
          float dropW = comboW;
          float dropH = 3 * 28.0f * scale;

          int hoveredItem = -1;
          if (x >= dropX && x <= dropX + dropW && y >= dropY && y <= dropY + dropH) {
            hoveredItem = (int)((y - dropY) / (28.0f * scale));
            if (hoveredItem < 0) hoveredItem = 0;
            if (hoveredItem > 2) hoveredItem = 2;
          }
          if (hoveredItem != g_HoveredDropdownItem) {
            g_HoveredDropdownItem = hoveredItem;
            repaintNeeded = true;
          }
        }
        else if (g_AccentDropdownOpen) {
          float dropX = combo2X;
          float dropY = combo2Y + comboH + 2.0f * scale;
          float dropW = comboW;
          float dropH = 2 * 28.0f * scale;

          int hoveredItem = -1;
          if (x >= dropX && x <= dropX + dropW && y >= dropY && y <= dropY + dropH) {
            hoveredItem = (int)((y - dropY) / (28.0f * scale));
            if (hoveredItem < 0) hoveredItem = 0;
            if (hoveredItem > 1) hoveredItem = 1;
          }
          if (hoveredItem != g_HoveredDropdownItem) {
            g_HoveredDropdownItem = hoveredItem;
            repaintNeeded = true;
          }
        }
        else if (g_UILangDropdownOpen) {
          float dropX = combo3X;
          float dropY = combo3Y + comboH + 2.0f * scale;
          float dropW = comboW;
          float dropH = 7 * 28.0f * scale;

          int hoveredItem = -1;
          if (x >= dropX && x <= dropX + dropW && y >= dropY && y <= dropY + dropH) {
            hoveredItem = (int)((y - dropY) / (28.0f * scale));
            if (hoveredItem < 0) hoveredItem = 0;
            if (hoveredItem > 6) hoveredItem = 6;
          }
          if (hoveredItem != g_HoveredDropdownItem) {
            g_HoveredDropdownItem = hoveredItem;
            repaintNeeded = true;
          }
        }
        else if (g_TransLangDropdownOpen) {
          float dropX = combo4X;
          float dropW = comboW;
          float dropH = 7 * 28.0f * scale;
          float dropY = combo4Y - dropH - 2.0f * scale; // UP

          int hoveredItem = -1;
          if (x >= dropX && x <= dropX + dropW && y >= dropY && y <= dropY + dropH) {
            hoveredItem = (int)((y - dropY) / (28.0f * scale));
            if (hoveredItem < 0) hoveredItem = 0;
            if (hoveredItem > 6) hoveredItem = 6;
          }
          if (hoveredItem != g_HoveredDropdownItem) {
            g_HoveredDropdownItem = hoveredItem;
            repaintNeeded = true;
          }
        }
        // Estado padrão (nenhum dropdown aberto)
        else {
          bool overThemeCombo = (x >= combo1X && x <= combo1X + comboW && y >= combo1Y && y <= combo1Y + comboH);
          bool overAccentCombo = (x >= combo2X && x <= combo2X + comboW && y >= combo2Y && y <= combo2Y + comboH);
          bool overUILangCombo = (x >= combo3X && x <= combo3X + comboW && y >= combo3Y && y <= combo3Y + comboH);
          bool overTransLangCombo = (x >= combo4X && x <= combo4X + comboW && y >= combo4Y && y <= combo4Y + comboH);

          if (overThemeCombo != g_HoverThemeCombo) {
            g_HoverThemeCombo = overThemeCombo;
            repaintNeeded = true;
          }
          if (overAccentCombo != g_HoverAccentCombo) {
            g_HoverAccentCombo = overAccentCombo;
            repaintNeeded = true;
          }
          if (overUILangCombo != g_HoverUILangCombo) {
            g_HoverUILangCombo = overUILangCombo;
            repaintNeeded = true;
          }
          if (overTransLangCombo != g_HoverTransLangCombo) {
            g_HoverTransLangCombo = overTransLangCombo;
            repaintNeeded = true;
          }

          // Hover na grade de cores
          float gridWidth = 10 * 22.0f * scale + 9 * 4.0f * scale;
          float rowX = card1X + (card1W - gridWidth) / 2.0f;
          float rowY = card1Y + 106.0f * scale;

          float squareSize = 22.0f * scale;
          float spacing = 4.0f * scale;

          int hoveredGridIndex = -1;
          for (int i = 0; i < 50; i++) {
            int row = i / 10;
            int col = i % 10;
            float sx = rowX + col * (squareSize + spacing);
            float sy = rowY + row * (squareSize + spacing);

            if (x >= sx && x <= sx + squareSize && y >= sy && y <= sy + squareSize) {
              hoveredGridIndex = i;
              break;
            }
          }

          if (hoveredGridIndex != g_HoveredGridIndex) {
            g_HoveredGridIndex = hoveredGridIndex;
            repaintNeeded = true;
          }
        }

        if (repaintNeeded) {
          HWND parent = GetParent(hwnd);
          if (parent) {
            InvalidateRect(parent, NULL, TRUE);
            UpdateFlyoutWindow();
          }
        }
      }
    }

    TRACKMOUSEEVENT tme = {sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0};
    TrackMouseEvent(&tme);
    return 0;
  }

  case WM_LBUTTONDOWN: {
    if (g_SettingsCardOpen) {
      return 0;
    }
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

    float settingsBtnW = 26.0f * scale;
    float settingsBtnX1 = (float)w - settingsBtnW - 15.0f * scale;
    float settingsBtnX2 = settingsBtnX1 + settingsBtnW;
    float settingsBtnY1 = Y_top + (headerHeight - headerBtnH) / 2.0f;
    float settingsBtnY2 = settingsBtnY1 + headerBtnH;

    float pinBtnW = 26.0f * scale;
    float pinX1 = settingsBtnX1 - pinBtnW - 8.0f * scale;
    float pinX2 = pinX1 + pinBtnW;
    float pinY1 = settingsBtnY1;
    float pinY2 = pinY1 + headerBtnH;

    float transBtnW = 85.0f * scale;
    float transX1 = pinX1 - transBtnW - 8.0f * scale;
    float transX2 = transX1 + transBtnW;
    float transY1 = pinY1;
    float transY2 = transY1 + headerBtnH;

    if (g_SettingsCardOpen) {
        float cardW = 310.0f * scale;
        float cardH = 372.0f * scale;
        float cardX = (w - cardW) / 2.0f;
        float cardY = (h - cardH) / 2.0f;

        float subCardX = cardX + 10.0f * scale;
        float subCardW = cardW - 20.0f * scale;
        float comboW = 100.0f * scale;
        float comboH = 24.0f * scale;

        float card1X = subCardX;
        float card1Y = cardY + 36.0f * scale;
        float card1W = subCardW;
        float combo1X = card1X + card1W - comboW - 12.0f * scale;
        float combo1Y = card1Y + 12.0f * scale;
        float combo2X = combo1X;
        float combo2Y = card1Y + 49.0f * scale;

        float card2X = subCardX;
        float card2Y = cardY + 284.0f * scale;
        float card2W = subCardW;
        float combo3X = card2X + card2W - comboW - 12.0f * scale;
        float combo3Y = card2Y + 10.0f * scale;
        float combo4X = combo3X;
        float combo4Y = card2Y + 46.0f * scale;

        bool clickedInside = (x >= cardX && x <= cardX + cardW && y >= cardY && y <= cardY + cardH);
        bool clickedOption = false;

        if (!clickedInside) {
            g_SettingsCardOpen = false;
            g_ThemeDropdownOpen = false;
            g_AccentDropdownOpen = false;
            g_UILangDropdownOpen = false;
            g_TransLangDropdownOpen = false;
        } else {
            // 1. Se o dropdown de temas estiver aberto, intercepta o clique
            if (g_ThemeDropdownOpen) {
                float dropX = combo1X;
                float dropY = combo1Y + comboH + 2.0f * scale;
                float dropW = comboW;
                float dropH = 3 * 28.0f * scale;

                if (x >= dropX && x <= dropX + dropW && y >= dropY && y <= dropY + dropH) {
                    int clickedItem = (int)((y - dropY) / (28.0f * scale));
                    if (clickedItem >= 0 && clickedItem <= 2) {
                        g_Settings.theme = clickedItem;
                        SaveSettings();
                    }
                }
                g_ThemeDropdownOpen = false;
                clickedOption = true;
            }
            // 2. Se o dropdown de cores de destaque estiver aberto
            else if (g_AccentDropdownOpen) {
                float dropX = combo2X;
                float dropY = combo2Y + comboH + 2.0f * scale;
                float dropW = comboW;
                float dropH = 2 * 28.0f * scale;

                if (x >= dropX && x <= dropX + dropW && y >= dropY && y <= dropY + dropH) {
                    int clickedItem = (int)((y - dropY) / (28.0f * scale));
                    if (clickedItem == 0) {
                        g_Settings.customAccent = false;
                        UpdateAccentColorCache();
                        SaveSettings();
                    } else if (clickedItem == 1) {
                        g_Settings.customAccent = true;
                        bool isValid = false;
                        for (int i = 0; i < 50; i++) {
                            if (g_Settings.accentColor == WINDOWS_ACCENT_COLORS[i]) {
                                isValid = true;
                                break;
                            }
                        }
                        if (!isValid) {
                            g_Settings.accentColor = WINDOWS_ACCENT_COLORS[16]; // Azul padrão
                        }
                        UpdateAccentColorCache();
                        SaveSettings();
                    }
                }
                g_AccentDropdownOpen = false;
                clickedOption = true;
            }
            // 3. Se o dropdown de idioma da interface estiver aberto
            else if (g_UILangDropdownOpen) {
                float dropX = combo3X;
                float dropY = combo3Y + comboH + 2.0f * scale;
                float dropW = comboW;
                float dropH = 7 * 28.0f * scale;

                if (x >= dropX && x <= dropX + dropW && y >= dropY && y <= dropY + dropH) {
                    int clickedItem = (int)((y - dropY) / (28.0f * scale));
                    if (clickedItem >= 0 && clickedItem <= 6) {
                        g_Settings.uiLanguage = clickedItem;
                        SaveSettings();
                    }
                }
                g_UILangDropdownOpen = false;
                clickedOption = true;
            }
            // 4. Se o dropdown de idioma da tradução estiver aberto (abre para CIMA)
            else if (g_TransLangDropdownOpen) {
                float dropX = combo4X;
                float dropW = comboW;
                float dropH = 7 * 28.0f * scale;
                float dropY = combo4Y - dropH - 2.0f * scale; // UP

                if (x >= dropX && x <= dropX + dropW && y >= dropY && y <= dropY + dropH) {
                    int clickedItem = (int)((y - dropY) / (28.0f * scale));
                    if (clickedItem >= 0 && clickedItem <= 6) {
                        g_Settings.translationLanguage = clickedItem;
                        SaveSettings();

                        // Recalcula tradução se ativa
                        if (g_TranslateLyrics) {
                            FetchTranslationThread();
                        }
                    }
                }
                g_TransLangDropdownOpen = false;
                clickedOption = true;
            }
            // 5. Comportamento padrão (sem dropdowns abertos)
            else {
                // Clique no combobox de tema
                if (x >= combo1X && x <= combo1X + comboW && y >= combo1Y && y <= combo1Y + comboH) {
                    g_ThemeDropdownOpen = true;
                    clickedOption = true;
                }
                // Clique no combobox de cor de destaque
                else if (x >= combo2X && x <= combo2X + comboW && y >= combo2Y && y <= combo2Y + comboH) {
                    g_AccentDropdownOpen = true;
                    clickedOption = true;
                }
                // Clique no combobox de idioma da interface
                else if (x >= combo3X && x <= combo3X + comboW && y >= combo3Y && y <= combo3Y + comboH) {
                    g_UILangDropdownOpen = true;
                    clickedOption = true;
                }
                // Clique no combobox de idioma da tradução
                else if (x >= combo4X && x <= combo4X + comboW && y >= combo4Y && y <= combo4Y + comboH) {
                    g_TransLangDropdownOpen = true;
                    clickedOption = true;
                }
                // Clique na grade de cores (sempre visível)
                else {
                    float gridWidth = 10 * 22.0f * scale + 9 * 4.0f * scale;
                    float rowX = card1X + (card1W - gridWidth) / 2.0f;
                    float rowY = card1Y + 106.0f * scale;

                    float squareSize = 22.0f * scale;
                    float spacing = 4.0f * scale;

                    for (int i = 0; i < 50; i++) {
                        int row = i / 10;
                        int col = i % 10;
                        float sx = rowX + col * (squareSize + spacing);
                        float sy = rowY + row * (squareSize + spacing);

                        if (x >= sx && x <= sx + squareSize && y >= sy && y <= sy + squareSize) {
                            g_Settings.customAccent = true;
                            g_Settings.accentColor = WINDOWS_ACCENT_COLORS[i];
                            UpdateAccentColorCache();
                            SaveSettings();
                            clickedOption = true;
                            break;
                        }
                    }
                }

                // Se clicou em outra parte de dentro do card, mantém o menu aberto
                if (!clickedOption) {
                    clickedOption = true;
                }
            }
        }

        HWND parent = GetParent(hwnd);
        if (parent) {
            InvalidateRect(parent, NULL, TRUE);
            PostMessage(g_hMediaWindow, WM_SETTINGCHANGE, 0, 0); 
        }
        return 0;
    }

    if (x >= settingsBtnX1 && x <= settingsBtnX2 && y >= settingsBtnY1 && y <= settingsBtnY2) {
      g_SettingsCardOpen = true;
      HWND parent = GetParent(hwnd);
      if (parent) {
        InvalidateRect(parent, NULL, TRUE);
      }
    } else if (x >= pinX1 && x <= pinX2 && y >= pinY1 && y <= pinY2) {
      g_FlyoutPinned = !g_FlyoutPinned;
      HWND parent = GetParent(hwnd);
      if (parent) {
        InvalidateRect(parent, NULL, TRUE);
      }
    } else if (x >= transX1 && x <= transX2 && y >= transY1 && y <= transY2) {
      g_TranslateLyrics = !g_TranslateLyrics;
      if (g_TranslateLyrics) {
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
    if (g_SettingsCardOpen) {
      return 0;
    }
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
    bool repaintNeeded = false;
    if (!g_ProgressDragging && !g_VolumeDragging) {
      g_ProgressHover = false;
      g_VolumeHover = false;
      HWND parent = GetParent(hwnd);
      if (parent) {
        SetTimer(parent, IDT_FLYOUT_ANIMATION, 15, NULL);
      }
    }
    if (g_HoverThemeCombo || g_HoverAccentCombo || g_HoverUILangCombo || g_HoverTransLangCombo ||
        g_HoveredDropdownItem != -1 || g_HoveredGridIndex != -1) {
      g_HoverThemeCombo = false;
      g_HoverAccentCombo = false;
      g_HoverUILangCombo = false;
      g_HoverTransLangCombo = false;
      g_HoveredDropdownItem = -1;
      g_HoveredGridIndex = -1;
      repaintNeeded = true;
    }
    if (repaintNeeded) {
      HWND parent = GetParent(hwnd);
      if (parent) {
        InvalidateRect(parent, NULL, TRUE);
        UpdateFlyoutWindow();
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

      if (g_FlyoutState == 1) {
        if (g_FlyoutAnimT == 0.0f) {
          HWND hTaskbar = FindWindow(L"Shell_TrayWnd", nullptr);
          RECT rcTaskbar{0};
          if (hTaskbar) {
            GetWindowRect(hTaskbar, &rcTaskbar);
          } else {
            rcTaskbar.top = GetSystemMetrics(SM_CYSCREEN) - 48;
          }
          if (rcTaskbar.top < 100) {
            g_FlyoutStartOffset = -15;
          } else {
            g_FlyoutStartOffset = 15;
          }
        }
      }

      float targetT = (g_FlyoutState == 1 || g_FlyoutState == 2) ? 1.0f : 0.0f;
      
      if (!IsAnimationEnabled()) {
         g_FlyoutAnimT = targetT;
      } else {
         float step = 0.07f; // ~14 frames (210ms) a 60fps
         if (g_FlyoutAnimT < targetT) {
             g_FlyoutAnimT += step;
             if (g_FlyoutAnimT > targetT) g_FlyoutAnimT = targetT;
         } else if (g_FlyoutAnimT > targetT) {
             g_FlyoutAnimT -= step;
             if (g_FlyoutAnimT < targetT) g_FlyoutAnimT = targetT;
         }
      }

      g_FlyoutOpenProgress = EaseOutCubic(g_FlyoutAnimT);
      stillAnimating = (g_FlyoutAnimT != targetT);

      if (!stillAnimating) {
        if (targetT == 0.0f) {
          g_FlyoutState = 0;
        } else if (g_FlyoutState == 1) {
          g_FlyoutState = 2;
        }
      }

      g_VolumeExpandProgress =
          (g_VolumeHover || g_VolumeDragging) ? 1.0f : 0.0f;

      g_FlyoutAlpha = (int)(g_FlyoutOpenProgress * 255.0f);

      g_LyricsExpandProgress = 1.0f;
      g_FlyoutCurrentHeight = g_FlyoutExpandedHeight;

      if (g_FlyoutState == 0) {
        KillTimer(hwnd, IDT_FLYOUT_ANIMATION);
        KillTimer(hwnd, IDT_FLYOUT_MOUSE_CHECK);
        ShowWindow(hwnd, SW_HIDE);
        ShowWindow(g_hFlyoutChildWindow, SW_HIDE);
        ResetFlyoutPositionCache();
      } else {
        UpdateFlyoutWindow();
        if (!stillAnimating && g_FlyoutState == 2) {
          KillTimer(hwnd, IDT_FLYOUT_ANIMATION);
        }
      }
    } else if (wParam == IDT_FLYOUT_MOUSE_CHECK) {
      if (g_FlyoutState == 2) {
        POINT pt;
        GetCursorPos(&pt);

        RECT rcFlyout, rcWidget;
        GetWindowRect(hwnd, &rcFlyout);
        GetWindowRect(g_hMediaWindow, &rcWidget);

        bool overFlyout = PtInRect(&rcFlyout, pt);
        bool overWidget = PtInRect(&rcWidget, pt);

        if (!overFlyout && !overWidget && !g_FlyoutPinned) {
          g_FlyoutMouseOutSeconds++;
          if (g_FlyoutMouseOutSeconds >= 10) {
            g_FlyoutMouseOutSeconds = 0;
            if (IsAnimationEnabled()) {
              g_FlyoutState = 3;
              SetTimer(hwnd, IDT_FLYOUT_ANIMATION, 15, NULL);
            } else {
              g_FlyoutState = 0;
              g_FlyoutOpenProgress = 0.0f;
              g_FlyoutAlpha = 0;
              ShowWindow(hwnd, SW_HIDE);
              ShowWindow(g_hFlyoutChildWindow, SW_HIDE);
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

void SyncProgressTimer(HWND hwnd) {
  bool isPlaying = false;
  bool hasMedia = false;
  {
    lock_guard<mutex> guard(g_MediaState.lock);
    isPlaying = g_MediaState.isPlaying;
    hasMedia = g_MediaState.hasMedia;
  }
  
  static bool s_TimerActive = false;
  if (hasMedia && isPlaying) {
    if (!s_TimerActive) {
      SetTimer(hwnd, IDT_POLL_MEDIA, 1000, NULL);
      s_TimerActive = true;
      Log(L"Progress Timer ACTIVATED");
    }
  } else {
    if (s_TimerActive) {
      KillTimer(hwnd, IDT_POLL_MEDIA);
      s_TimerActive = false;
      Log(L"Progress Timer DEACTIVATED");
    }
  }
}

void RegisterSessionEvents(GlobalSystemMediaTransportControlsSession session) {
  lock_guard<mutex> guard(g_SessionMutex);
  
  if (g_CurrentSession) {
    if (g_MediaPropertiesChangedToken) {
      g_CurrentSession.MediaPropertiesChanged(g_MediaPropertiesChangedToken);
      g_MediaPropertiesChangedToken = {};
    }
    if (g_PlaybackInfoChangedToken) {
      g_CurrentSession.PlaybackInfoChanged(g_PlaybackInfoChangedToken);
      g_PlaybackInfoChangedToken = {};
    }
    if (g_TimelinePropertiesChangedToken) {
      g_CurrentSession.TimelinePropertiesChanged(g_TimelinePropertiesChangedToken);
      g_TimelinePropertiesChangedToken = {};
    }
  }
  
  g_CurrentSession = session;
  
  if (g_CurrentSession) {
    g_MediaPropertiesChangedToken = g_CurrentSession.MediaPropertiesChanged([](auto&&, auto&&) {
      Log(L"Event: MediaPropertiesChanged");
      UpdateMediaInfo();
      PostMessage(g_hMediaWindow, WM_APP + 11, 0, 0);
    });
    
    g_PlaybackInfoChangedToken = g_CurrentSession.PlaybackInfoChanged([](auto&&, auto&&) {
      Log(L"Event: PlaybackInfoChanged");
      UpdateMediaInfo();
      PostMessage(g_hMediaWindow, WM_APP + 11, 0, 0);
    });
    
    g_TimelinePropertiesChangedToken = g_CurrentSession.TimelinePropertiesChanged([](auto&&, auto&&) {
      Log(L"Event: TimelinePropertiesChanged");
      UpdateMediaInfo();
      PostMessage(g_hMediaWindow, WM_APP + 11, 0, 0);
    });
  }
}

void OnCurrentSessionChanged(GlobalSystemMediaTransportControlsSessionManager const& sender, CurrentSessionChangedEventArgs const& args) {
  Log(L"Event: CurrentSessionChanged");
  RegisterSessionEvents(sender.GetCurrentSession());
  UpdateMediaInfo();
  PostMessage(g_hMediaWindow, WM_APP + 11, 0, 0);
}

void OnSessionsChanged(GlobalSystemMediaTransportControlsSessionManager const& sender, SessionsChangedEventArgs const& args) {
  Log(L"Event: SessionsChanged");
  GlobalSystemMediaTransportControlsSession activeSession = nullptr;
  auto sessions = sender.GetSessions();
  for (auto const& s : sessions) {
    auto pb = s.GetPlaybackInfo();
    if (pb && pb.PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing) {
      activeSession = s;
      break;
    }
  }
  if (!activeSession) {
    activeSession = sender.GetCurrentSession();
  }
  RegisterSessionEvents(activeSession);
  UpdateMediaInfo();
  PostMessage(g_hMediaWindow, WM_APP + 11, 0, 0);
}

LRESULT CALLBACK MediaWndProc(HWND hwnd, UINT msg, WPARAM wParam,
                              LPARAM lParam) {
  switch (msg) {
  case WM_CREATE:
    UpdateAppearance(hwnd);
    CreateTrayIcon(hwnd);
    UpdateMediaInfo();
    SyncProgressTimer(hwnd);
    RegisterTaskbarHook(hwnd);
    return 0;

  case WM_ERASEBKGND:
    return 1;

  case WM_CLOSE:
    DestroyWindow(hwnd);
    return 0;

  case WM_DESTROY:
    RemoveTrayIcon();
    if (g_TaskbarHook) {
      UnhookWinEvent(g_TaskbarHook);
      g_TaskbarHook = nullptr;
    }
    if (g_hFlyoutWindow) {
      DestroyWindow(g_hFlyoutWindow);
      g_hFlyoutWindow = nullptr;
      g_hFlyoutChildWindow = nullptr;
    }
    
    // Remover eventos do SessionManager e da sessão
    if (g_SessionManager) {
      if (g_CurrentSessionChangedToken) {
        g_SessionManager.CurrentSessionChanged(g_CurrentSessionChangedToken);
        g_CurrentSessionChangedToken = {};
      }
      if (g_SessionsChangedToken) {
        g_SessionManager.SessionsChanged(g_SessionsChangedToken);
        g_SessionsChangedToken = {};
      }
    }
    RegisterSessionEvents(nullptr);
    g_SessionManager = nullptr;
    
    PostQuitMessage(0);
    return 0;

  case WM_TRAYICON: {
    UINT uMouseMsg = (UINT)lParam;
    if (uMouseMsg == WM_RBUTTONUP) {
      POINT pt;
      GetCursorPos(&pt);
      ShowTrayMenu(hwnd, pt);
    } else if (uMouseMsg == WM_LBUTTONUP) {
      // Alterna exibição do flyout simulando um clique no widget principal
      PostMessage(hwnd, WM_LBUTTONUP, 0, 0);
    }
    return 0;
  }

  case WM_COMMAND: {
    int wmId = LOWORD(wParam);
    switch (wmId) {
    case ID_TRAY_EXIT:
      DestroyWindow(hwnd);
      break;
    case ID_TRAY_SETTINGS: {
      std::wstring filePath = GetSettingsFilePath();
      ShellExecuteW(NULL, L"open", filePath.c_str(), NULL, NULL, SW_SHOWNORMAL);
      break;
    }
    case ID_TRAY_PLAYPAUSE:
      SendMediaCommand(2);
      break;
    case ID_TRAY_NEXT:
      SendMediaCommand(3);
      break;
    case ID_TRAY_PREV:
      SendMediaCommand(1);
      break;
    case ID_TRAY_STARTUP:
      ToggleStartupState();
      break;
    }
    return 0;
  }

  case WM_SETTINGCHANGE:
    UpdateAccentColorCache();
    UpdateAppearance(hwnd);
    RedrawLayeredWindow(hwnd);
    if (g_hFlyoutWindow) {
      UpdateFlyoutComposition(g_hFlyoutWindow);
      if (g_FlyoutState == 1 || g_FlyoutState == 2) {
        UpdateFlyoutWindow();
      }
    }
    return 0;

  case WM_DWMCOLORIZATIONCOLORCHANGED:
    UpdateAccentColorCache();
    RedrawLayeredWindow(hwnd);
    if (g_hFlyoutWindow && (g_FlyoutState == 1 || g_FlyoutState == 2)) {
      UpdateFlyoutWindow();
    }
    return 0;

  case WM_SHOWWINDOW: {
    BOOL show = (BOOL)wParam;
    if (!show) {
      KillTimer(hwnd, IDT_ANIMATION);
      Log(L"Animation Timer SUSPENDED (Window Hidden)");
    } else if (g_IsScrolling) {
      SetTimer(hwnd, IDT_ANIMATION, 16, NULL);
      Log(L"Animation Timer RESUMED (Window Shown)");
    }
    return 0;
  }

  case WM_APP + 11:
    SyncProgressTimer(hwnd);
    RedrawLayeredWindow(hwnd);
    if (g_hFlyoutWindow && (g_FlyoutState == 1 || g_FlyoutState == 2)) {
      UpdateFlyoutWindow();
    }
    return 0;

  case WM_TIMER:
    if (wParam == IDT_POLL_MEDIA) {
      UpdateMediaInfo();
      SyncProgressTimer(hwnd);

      bool shouldHide = false;

      if (g_Settings.hideFullscreen) {
        QUERY_USER_NOTIFICATION_STATE state;
        if (SUCCEEDED(SHQueryUserNotificationState(&state))) {
          if (state == QUNS_BUSY || state == QUNS_RUNNING_D3D_FULL_SCREEN ||
              state == QUNS_PRESENTATION_MODE) {
            shouldHide = true;
          }
        }
      }

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

      if (shouldHide && IsWindowVisible(hwnd)) {
        ShowWindow(hwnd, SW_HIDE);
      } else if (!shouldHide && !IsWindowVisible(hwnd)) {
        HWND hTaskbar = FindWindow(L"Shell_TrayWnd", nullptr);
        if (hTaskbar && IsWindowVisible(hTaskbar)) {
          ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        }
      }

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

    if (!IsWindowVisible(hTaskbar)) {
      if (IsWindowVisible(hwnd))
        ShowWindow(hwnd, SW_HIDE);
      return 0;
    }

    if (!g_IsHiddenByIdle && !IsWindowVisible(hwnd)) {
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
            g_FlyoutAnimT = 0.0f;
            g_FlyoutAlpha = 0;
            g_FlyoutState = 1;
          } else {
            g_FlyoutOpenProgress = 1.0f;
            g_FlyoutAnimT = 1.0f;
            g_FlyoutAlpha = 255;
            g_FlyoutState = 2;
          }
          g_FlyoutMouseOutSeconds = 0;

          UpdateFlyoutComposition(g_hFlyoutWindow);
          UpdateFlyoutWindow();
          ShowWindow(g_hFlyoutWindow, SW_SHOWNOACTIVATE);
          ShowWindow(g_hFlyoutChildWindow, SW_SHOWNOACTIVATE);

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
            g_FlyoutAnimT = 0.0f;
            g_FlyoutAlpha = 0;
            ShowWindow(g_hFlyoutWindow, SW_HIDE);
            ShowWindow(g_hFlyoutChildWindow, SW_HIDE);
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
    keybd_event(zDelta > 0 ? VK_VOLUME_UP : VK_VOLUME_DOWN, 0, KEYEVENTF_KEYUP, 0);
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

// --- Thread de Inicialização da Janela e Mídia ---
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

  Log(L"Creating window using CreateWindowEx");
  HWND hTaskbar = FindWindow(L"Shell_TrayWnd", nullptr);
  g_hMediaWindow = CreateWindowEx(
      WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST, wc.lpszClassName,
      TEXT("MusicPlayerNative"), WS_POPUP | WS_VISIBLE, 0, 0,
      g_Settings.width, g_Settings.height, hTaskbar, NULL, wc.hInstance, NULL);

  if (g_hMediaWindow) {
    g_hFlyoutWindow =
        CreateWindowEx(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST,
                       wcFlyout.lpszClassName, TEXT("MusicPlayerFlyout"),
                       WS_POPUP | WS_CLIPCHILDREN, 0, 0, 360, 400,
                       g_hMediaWindow, NULL, wcFlyout.hInstance, NULL);

      g_hFlyoutChildWindow = CreateWindowEx(
          WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOPMOST, wcFlyoutChild.lpszClassName,
          TEXT("MusicPlayerFlyoutChild"), WS_POPUP, 0, 0, 360, 400,
          g_hFlyoutWindow, NULL, wcFlyoutChild.hInstance, NULL);
  }

  MSG msg;
  
  // Registrar eventos GSMTC
  try {
    if (!g_SessionManager) {
      g_SessionManager =
          GlobalSystemMediaTransportControlsSessionManager::RequestAsync()
              .get();
    }
    if (g_SessionManager) {
      g_CurrentSessionChangedToken = g_SessionManager.CurrentSessionChanged(OnCurrentSessionChanged);
      g_SessionsChangedToken = g_SessionManager.SessionsChanged(OnSessionsChanged);
      
      GlobalSystemMediaTransportControlsSession initialSession = nullptr;
      auto sessions = g_SessionManager.GetSessions();
      for (auto const& s : sessions) {
        auto pb = s.GetPlaybackInfo();
        if (pb && pb.PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing) {
          initialSession = s;
          break;
        }
      }
      if (!initialSession) {
        initialSession = g_SessionManager.GetCurrentSession();
      }
      RegisterSessionEvents(initialSession);
      
      UpdateMediaInfo();
      PostMessage(g_hMediaWindow, WM_APP + 11, 0, 0);
    }
  } catch (...) {
    Log(L"Failed to initialize GSMTC Manager or events");
  }

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

// --- Ponto de Entrada WinMain ---
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
  // Controle de Instância Única
  HANDLE hMutex = CreateMutexW(NULL, TRUE, L"TaskbarMediaPlayerSingleInstanceMutex");
  if (hMutex == NULL || GetLastError() == ERROR_ALREADY_EXISTS) {
    if (hMutex) CloseHandle(hMutex);
    return 0;
  }

  // Carrega configurações locais
  LoadSettings();

  // Inicializa o cache de cor de sotaque do Windows
  UpdateAccentColorCache();

  g_Running = true;
  std::thread mediaThread(MediaThread);

  // Aguarda a thread de mídia terminar
  if (mediaThread.joinable()) {
    mediaThread.join();
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

  CloseHandle(hMutex);
  return 0;
}
