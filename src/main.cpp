#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#ifndef _WIN32_IE
#define _WIN32_IE 0x0600
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <commctrl.h>
#include <winerror.h>

#ifndef NO_ERROR
#define NO_ERROR 0L
#endif

#include <gdiplus.h>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <map>

#include "ui_theme.h"
#include "domain_expander.h"
#include "hosts_manager.h"
#include "registry_manager.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdiplus.lib")

#ifndef IDI_SHIELD
#define IDI_SHIELD MAKEINTRESOURCE(32518)
#endif

// Control IDs
enum {
    ID_LST_DOMAINS = 101,
    ID_EDT_INPUT = 102,
    ID_BTN_ADD = 103,
    ID_BTN_REMOVE = 104,
    ID_BTN_CLEAR_ALL = 105,
    ID_CHK_STARTUP = 106,
    ID_EDT_TEST = 107,
    ID_BTN_TEST = 108,
    ID_BTN_DOH = 109,
    ID_BTN_MIN_TRAY = 110,
    ID_BTN_PRESET_YT = 201,
    ID_BTN_PRESET_IG = 202,
    ID_BTN_PRESET_TW = 203,
    ID_BTN_PRESET_TT = 204,
    ID_BTN_PRESET_RD = 205,
    ID_BTN_PRESET_NF = 206,
    ID_BTN_PRESET_ADULT = 207,
    ID_TRAY_OPEN = 301,
    ID_TRAY_EXIT = 302
};

#define WM_TRAYICON (WM_USER + 1)
#define TRAY_UID 1001

// Global State
HWND g_hWnd = NULL;
HWND g_hLstDomains = NULL;
HWND g_hEdtInput = NULL;
HWND g_hEdtTest = NULL;
HWND g_hChkStartup = NULL;

NOTIFYICONDATAA g_nid = { sizeof(g_nid) };
ULONG_PTR g_gdiplusToken = 0;

HBRUSH g_hBrushBg = NULL;
HBRUSH g_hBrushCard = NULL;
HBRUSH g_hBrushInput = NULL;
HFONT g_hFontInput = NULL;
HFONT g_hFontBold = NULL;
HFONT g_hFontNormal = NULL;

std::vector<std::string> g_domains;
std::map<HWND, bool> g_btnHover;
std::string g_testResultText = "Click 'Test Socket' to verify null-route OS drop.";
bool g_testResultBlocked = false;
bool g_testResultTested = false;

// Custom Button Subclass for hover state and eliminating rectangular window chrome
LRESULT CALLBACK ButtonSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1; // Prevent Windows from erasing with ugly default button face
    case WM_NCPAINT:
        return 0; // Cut off default 3D / rectangular non-client borders!
    case WM_MOUSEMOVE: {
        if (!g_btnHover[hWnd]) {
            g_btnHover[hWnd] = true;
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hWnd, 0 };
            TrackMouseEvent(&tme);
            InvalidateRect(hWnd, NULL, FALSE);
        }
        break;
    }
    case WM_MOUSELEAVE: {
        g_btnHover[hWnd] = false;
        InvalidateRect(hWnd, NULL, FALSE);
        break;
    }
    case WM_NCDESTROY:
        RemoveWindowSubclass(hWnd, ButtonSubclassProc, uIdSubclass);
        break;
    }
    return DefSubclassProc(hWnd, msg, wParam, lParam);
}

void RegisterButtonHover(HWND hBtn) {
    SetWindowSubclass(hBtn, ButtonSubclassProc, 0, 0);
}

template<typename T>
inline std::wstring to_wstr(const T& val) {
    std::wostringstream wss;
    wss << val;
    return wss.str();
}

// Prototypes
void SyncAndApply();
void RefreshDomainList();
void AddDomain(const std::string& domain_str);
void TestSocketDomain(const std::string& domain_str);
void SetupTrayIcon(HWND hWnd);
void RemoveTrayIcon();
void EnableDarkTitlebar(HWND hWnd);

int GetTotalLockedHostnames() {
    int total = 0;
    for (const auto& d : g_domains) {
        total += (int)DomainExpander::expand(d).size();
    }
    return total;
}

void SyncAndApply() {
    RegistryManager::save_domains(g_domains);

    std::vector<std::string> all_hosts;
    for (const auto& d : g_domains) {
        auto exp = DomainExpander::expand(d);
        all_hosts.insert(all_hosts.end(), exp.begin(), exp.end());
    }

    bool ok = HostsManager::apply_blocks(all_hosts);
    if (!ok) {
        MessageBoxA(g_hWnd, 
            "Could not write to C:\\Windows\\System32\\drivers\\etc\\hosts.\n\n"
            "Please ensure FocusShield is running with Administrator rights.",
            "Permission Error", MB_ICONERROR | MB_OK);
    }

    InvalidateRect(g_hWnd, NULL, FALSE);
}

void RefreshDomainList() {
    SendMessage(g_hLstDomains, LB_RESETCONTENT, 0, 0);
    for (const auto& d : g_domains) {
        SendMessageA(g_hLstDomains, LB_ADDSTRING, 0, (LPARAM)d.c_str());
    }
    InvalidateRect(g_hWnd, NULL, FALSE);
}

void AddDomain(const std::string& raw) {
    std::string clean = DomainExpander::clean_domain(raw);
    if (clean.empty() || clean.find('.') == std::string::npos) {
        MessageBoxA(g_hWnd, "Please enter a valid website domain name (e.g. youtube.com).", "Invalid Input", MB_ICONWARNING | MB_OK);
        return;
    }

    for (const auto& d : g_domains) {
        if (d == clean) {
            MessageBoxA(g_hWnd, ("'" + clean + "' is already in your blocklist.").c_str(), "Already Blocked", MB_ICONINFORMATION | MB_OK);
            return;
        }
    }

    g_domains.push_back(clean);
    RefreshDomainList();
    SyncAndApply();
}

void TestSocketDomain(const std::string& raw) {
    std::string clean = DomainExpander::clean_domain(raw);
    if (clean.empty()) clean = "youtube.com";

    struct addrinfo hints = {0};
    struct addrinfo *res = NULL;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int err = getaddrinfo(clean.c_str(), "80", &hints, &res);
    g_testResultTested = true;

    if (err != 0) {
        g_testResultBlocked = true;
        g_testResultText = "[BLOCKED] RESOLUTION KILLED: " + clean + " dropped at OS resolver. All traffic refused.";
        InvalidateRect(g_hWnd, NULL, FALSE);
        return;
    }

    bool is_null_route = true;
    std::string ips = "";
    for (struct addrinfo *p = res; p != NULL; p = p->ai_next) {
        char ipstr[INET6_ADDRSTRLEN] = {0};
        getnameinfo(p->ai_addr, (socklen_t)p->ai_addrlen, ipstr, sizeof(ipstr), NULL, 0, NI_NUMERICHOST);

        if (!ips.empty()) ips += ", ";
        ips += ipstr;

        if (std::string(ipstr) != "0.0.0.0" && std::string(ipstr) != "::1" && std::string(ipstr) != "127.0.0.1") {
            is_null_route = false;
        }
    }
    freeaddrinfo(res);

    if (is_null_route) {
        g_testResultBlocked = true;
        g_testResultText = "[BLOCKED] 0ms DROP: " + clean + " maps to 0.0.0.0. Traffic refused. CDNs & iframes killed.";
    } else {
        g_testResultBlocked = false;
        g_testResultText = "[REACHABLE] PUBLIC ROUTE: " + clean + " resolves to (" + ips + ").";
    }

    InvalidateRect(g_hWnd, NULL, FALSE);
}

void SetupTrayIcon(HWND hWnd) {
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hWnd;
    g_nid.uID = TRAY_UID;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    HICON hIco = (HICON)LoadImage(NULL, IDI_SHIELD, IMAGE_ICON, 0, 0, LR_SHARED);
    if (!hIco) hIco = LoadIcon(NULL, IDI_APPLICATION);
    g_nid.hIcon = hIco;
    strcpy(g_nid.szTip, "FocusShield - Permanent Network Blocker");
    Shell_NotifyIconA(NIM_ADD, &g_nid);
}

void RemoveTrayIcon() {
    Shell_NotifyIconA(NIM_DELETE, &g_nid);
}

void EnableDarkTitlebar(HWND hWnd) {
    typedef HRESULT (WINAPI *DwmSetWindowAttributeFn)(HWND, DWORD, LPCVOID, DWORD);
    HMODULE hDwm = LoadLibraryA("dwmapi.dll");
    if (hDwm) {
        DwmSetWindowAttributeFn fn = (DwmSetWindowAttributeFn)GetProcAddress(hDwm, "DwmSetWindowAttribute");
        if (fn) {
            BOOL dark = TRUE;
            fn(hWnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
            fn(hWnd, 19 /* Windows 10 older build fallback */, &dark, sizeof(dark));
        }
        FreeLibrary(hDwm);
    }
}

// Custom Draw for Owner-Drawn Buttons
void DrawCustomButton(LPDRAWITEMSTRUCT pDIS) {
    HWND hBtn = pDIS->hwndItem;
    HDC hdc = pDIS->hDC;
    RECT rc = pDIS->rcItem;
    int id = pDIS->CtlID;
    bool isPressed = (pDIS->itemState & ODS_SELECTED);
    bool isHovered = g_btnHover[hBtn];

    char text[128] = {0};
    GetWindowTextA(hBtn, text, sizeof(text));

    // Fallback for known IDs if GetWindowTextA is empty
    if (strlen(text) == 0) {
        if (id == ID_BTN_ADD) strcpy(text, "+ Block Site");
        else if (id == ID_BTN_REMOVE) strcpy(text, "Remove Selected");
        else if (id == ID_BTN_CLEAR_ALL) strcpy(text, "Unblock All");
        else if (id == ID_BTN_DOH) strcpy(text, "Enforce Anti-DoH");
        else if (id == ID_BTN_MIN_TRAY) strcpy(text, "Minimize to Tray");
        else if (id == ID_BTN_TEST) strcpy(text, "Test Socket");
        else if (id == ID_BTN_PRESET_YT) strcpy(text, "+ YouTube");
        else if (id == ID_BTN_PRESET_IG) strcpy(text, "+ Instagram");
        else if (id == ID_BTN_PRESET_TW) strcpy(text, "+ X / Twitter");
        else if (id == ID_BTN_PRESET_TT) strcpy(text, "+ TikTok");
        else if (id == ID_BTN_PRESET_RD) strcpy(text, "+ Reddit");
        else if (id == ID_BTN_PRESET_NF) strcpy(text, "+ Netflix");
        else if (id == ID_BTN_PRESET_ADULT) strcpy(text, "+ Adult");
    }

    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;

    // Double buffer button paint to ensure smooth, clean rendering
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBM = CreateCompatibleBitmap(hdc, w, h);
    HBITMAP oldBM = (HBITMAP)SelectObject(memDC, memBM);

    Gdiplus::Graphics g(memDC);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    // 1. Fill entire rectangular DC with background color
    Gdiplus::SolidBrush brParentBg(Theme::CLR_BG);
    g.FillRectangle(&brParentBg, 0, 0, w, h);

    Gdiplus::Color bgClr, borderClr;
    COLORREF gdiTextClr;
    float radius = 7.0f;

    if (id == ID_BTN_ADD) {
        // Emerald Primary Action Button (+ Block Site)
        bgClr = isPressed ? Gdiplus::Color(255, 4, 120, 87) : (isHovered ? Gdiplus::Color(255, 16, 205, 145) : Gdiplus::Color(255, 16, 185, 129));
        borderClr = isHovered ? Gdiplus::Color(255, 110, 231, 183) : Gdiplus::Color(255, 52, 211, 153);
        gdiTextClr = RGB(255, 255, 255);
        radius = 8.0f;
    } else if (id == ID_BTN_REMOVE || id == ID_BTN_CLEAR_ALL) {
        // Danger / Crimson Action Button (Remove Selected, Unblock All)
        bgClr = isPressed ? Gdiplus::Color(255, 60, 16, 26) : (isHovered ? Gdiplus::Color(255, 54, 18, 28) : Gdiplus::Color(255, 30, 14, 22));
        borderClr = isHovered ? Gdiplus::Color(255, 244, 63, 94) : Gdiplus::Color(255, 96, 28, 44);
        gdiTextClr = isHovered ? RGB(255, 255, 255) : RGB(251, 113, 133);
    } else if (id == ID_BTN_DOH || id == ID_BTN_MIN_TRAY || id == ID_BTN_TEST) {
        // Secondary Utility Actions (Enforce Anti-DoH, Minimize, Test Socket)
        bgClr = isPressed ? Gdiplus::Color(255, 14, 20, 30) : (isHovered ? Gdiplus::Color(255, 28, 38, 56) : Gdiplus::Color(255, 18, 24, 36));
        borderClr = isHovered ? Gdiplus::Color(255, 6, 182, 212) : Gdiplus::Color(255, 36, 48, 70);
        gdiTextClr = isHovered ? RGB(255, 255, 255) : RGB(203, 213, 225);
    } else {
        // Presets row (+ YouTube, + Instagram, etc.)
        bgClr = isPressed ? Gdiplus::Color(255, 12, 16, 26) : (isHovered ? Gdiplus::Color(255, 26, 36, 52) : Gdiplus::Color(255, 16, 22, 34));
        borderClr = isHovered ? Gdiplus::Color(255, 59, 130, 246) : Gdiplus::Color(255, 36, 48, 70);
        gdiTextClr = isHovered ? RGB(255, 255, 255) : RGB(203, 213, 225);
    }

    // Draw the rounded pill button
    Gdiplus::SolidBrush br(bgClr);
    Gdiplus::Pen pen(borderClr, 1.2f);
    Theme::FillAndDrawRoundedRect(g, &br, &pen, 0.5f, 0.5f, (float)w - 1.0f, (float)h - 1.0f, radius);

    // Draw text with Win32 DrawTextA (rock-solid, crisp, guaranteed to display)
    SetBkMode(memDC, TRANSPARENT);
    SetTextColor(memDC, gdiTextClr);
    SelectObject(memDC, (id == ID_BTN_ADD) ? g_hFontBold : g_hFontNormal);
    RECT textRc = { 0, 0, w, h };
    DrawTextA(memDC, text, -1, &textRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);

    SelectObject(memDC, oldBM);
    DeleteObject(memBM);
    DeleteDC(memDC);
}

// Window Procedure
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        INITCOMMONCONTROLSEX icex;
        icex.dwSize = sizeof(icex);
        icex.dwICC = ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES;
        InitCommonControlsEx(&icex);

        EnableDarkTitlebar(hWnd);

        // Brushes
        g_hBrushBg = CreateSolidBrush(RGB_BG);
        g_hBrushCard = CreateSolidBrush(RGB_CARD);
        g_hBrushInput = CreateSolidBrush(RGB_INPUT_BG);

        // Native Input & Button Fonts
        g_hFontInput = CreateFontA(15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
        g_hFontBold = CreateFontA(14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
        g_hFontNormal = CreateFontA(13, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");

        // Helper macro for creating custom owner-draw buttons with rounded clipping
        auto make_btn = [&](const char* title, int id, int x, int y, int w, int h) -> HWND {
            HWND b = CreateWindowA("BUTTON", title, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, x, y, w, h, hWnd, (HMENU)(INT_PTR)id, NULL, NULL);

            // 1. Turn off default Windows UxTheme frame
            typedef HRESULT (WINAPI *SetWindowThemeFn)(HWND, LPCWSTR, LPCWSTR);
            HMODULE hUx = LoadLibraryA("uxtheme.dll");
            if (hUx) {
                SetWindowThemeFn fn = (SetWindowThemeFn)GetProcAddress(hUx, "SetWindowTheme");
                if (fn) fn(b, L"", L"");
                FreeLibrary(hUx);
            }

            // 2. Physically shape the window into a rounded region.
            // This physically ELIMINATES all 4 rectangular corners from the HWND!
            HRGN hRgn = CreateRoundRectRgn(0, 0, w + 1, h + 1, 14, 14);
            SetWindowRgn(b, hRgn, TRUE);

            RegisterButtonHover(b);
            return b;
        };

        // Presets row (Y: 180, H: 30)
        int px = 24, py = 180, pw = 94, ph = 30;
        make_btn("+ YouTube", ID_BTN_PRESET_YT, px, py, pw, ph); px += pw + 8;
        make_btn("+ Instagram", ID_BTN_PRESET_IG, px, py, pw, ph); px += pw + 8;
        make_btn("+ X / Twitter", ID_BTN_PRESET_TW, px, py, pw, ph); px += pw + 8;
        make_btn("+ TikTok", ID_BTN_PRESET_TT, px, py, pw, ph); px += pw + 8;
        make_btn("+ Reddit", ID_BTN_PRESET_RD, px, py, pw, ph); px += pw + 8;
        make_btn("+ Netflix", ID_BTN_PRESET_NF, px, py, pw, ph); px += pw + 8;
        make_btn("+ Adult", ID_BTN_PRESET_ADULT, px, py, pw, ph);

        // Add domain input row (Y: 248) - NO WS_EX_CLIENTEDGE to avoid ugly 90s white sunken bevel!
        g_hEdtInput = CreateWindowExA(0, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 28, 252, 532, 24, hWnd, (HMENU)ID_EDT_INPUT, NULL, NULL);
        SendMessage(g_hEdtInput, WM_SETFONT, (WPARAM)g_hFontInput, TRUE);
        make_btn("+ Block Site", ID_BTN_ADD, 576, 248, 160, 32);

        // Domains ListBox (Y: 318, H: 160) - Owner-Drawn Fixed height, NO WS_EX_CLIENTEDGE!
        g_hLstDomains = CreateWindowExA(0, "LISTBOX", "", WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS, 25, 319, 538, 158, hWnd, (HMENU)ID_LST_DOMAINS, NULL, NULL);

        // Actions Column on right of ListBox
        int ax = 576, ay = 318, aw = 160, ah = 32, agap = 42;
        make_btn("Remove Selected", ID_BTN_REMOVE, ax, ay, aw, ah);
        make_btn("Unblock All", ID_BTN_CLEAR_ALL, ax, ay + agap, aw, ah);
        make_btn("Enforce Anti-DoH", ID_BTN_DOH, ax, ay + agap * 2, aw, ah);
        make_btn("Minimize to Tray", ID_BTN_MIN_TRAY, ax, ay + agap * 3, aw, ah);

        // Startup Checkbox (Y: 494)
        g_hChkStartup = CreateWindowA("BUTTON", "Start on Windows Startup (Auto-Enforce 24/7 at boot)", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 24, 494, 540, 24, hWnd, (HMENU)ID_CHK_STARTUP, NULL, NULL);
        SendMessage(g_hChkStartup, WM_SETFONT, (WPARAM)g_hFontInput, TRUE);
        
        // Turn off UxTheme visual style on checkbox so WM_CTLCOLORSTATIC text color works!
        typedef HRESULT (WINAPI *SetWindowThemeFn)(HWND, LPCWSTR, LPCWSTR);
        HMODULE hUx = LoadLibraryA("uxtheme.dll");
        if (hUx) {
            SetWindowThemeFn fn = (SetWindowThemeFn)GetProcAddress(hUx, "SetWindowTheme");
            if (fn) fn(g_hChkStartup, L"", L"");
            FreeLibrary(hUx);
        }

        if (RegistryManager::is_startup_enabled()) {
            SendMessage(g_hChkStartup, BM_SETCHECK, BST_CHECKED, 0);
        }

        // Test Socket row (Y: 574)
        g_hEdtTest = CreateWindowExA(0, "EDIT", "youtube.com", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 28, 578, 532, 24, hWnd, (HMENU)ID_EDT_TEST, NULL, NULL);
        SendMessage(g_hEdtTest, WM_SETFONT, (WPARAM)g_hFontInput, TRUE);
        make_btn("Test Socket", ID_BTN_TEST, 576, 574, 160, 30);

        // Load saved list and auto-apply
        g_domains = RegistryManager::load_saved_domains();
        RefreshDomainList();
        SyncAndApply();
        SetupTrayIcon(hWnd);
        break;
    }

    case WM_MEASUREITEM: {
        LPMEASUREITEMSTRUCT pMIS = (LPMEASUREITEMSTRUCT)lParam;
        if (pMIS->CtlID == ID_LST_DOMAINS) {
            pMIS->itemHeight = 36; // Spacious modern row height
            return TRUE;
        }
        break;
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT pDIS = (LPDRAWITEMSTRUCT)lParam;
        if (pDIS->CtlType == ODT_BUTTON) {
            DrawCustomButton(pDIS);
            return TRUE;
        } else if (pDIS->CtlID == ID_LST_DOMAINS) {
            // Custom Owner-Drawn ListBox Row
            HDC hdc = pDIS->hDC;
            RECT rc = pDIS->rcItem;
            int idx = pDIS->itemID;
            if (idx < 0 || idx >= (int)g_domains.size()) return TRUE;

            std::string domain = g_domains[idx];
            auto exp = DomainExpander::expand(domain);
            bool isSelected = (pDIS->itemState & ODS_SELECTED);

            Gdiplus::Graphics g(hdc);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

            // Row background
            Gdiplus::Color rowBg = isSelected ? Gdiplus::Color(255, 26, 38, 58) : (idx % 2 == 0 ? Theme::CLR_CARD : Gdiplus::Color(255, 15, 21, 32));
            Gdiplus::SolidBrush brRow(rowBg);
            g.FillRectangle(&brRow, (INT)rc.left, (INT)rc.top, (INT)(rc.right - rc.left), (INT)(rc.bottom - rc.top));

            // Accent indicator bar on selected row
            if (isSelected) {
                Gdiplus::SolidBrush brAccent(Theme::CLR_EMERALD);
                g.FillRectangle(&brAccent, (INT)rc.left, (INT)rc.top, 4, (INT)(rc.bottom - rc.top));
            }

            // Domain Text with GDI+ Font
            Gdiplus::Font fontDomain(L"Segoe UI", 9.5f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
            Gdiplus::SolidBrush brText(Theme::CLR_TEXT_TITLE);
            std::wstring wDomain(domain.begin(), domain.end());
            g.DrawString(wDomain.c_str(), -1, &fontDomain, Gdiplus::PointF((float)rc.left + 14.0f, (float)rc.top + 8.0f), &brText);

            // Subdomain / CDN badge pill
            std::string badgeText = "+" + std::to_string(exp.size()) + " endpoints & CDNs killed";
            std::wstring wBadge(badgeText.begin(), badgeText.end());

            Gdiplus::Font fontBadge(L"Segoe UI", 8.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
            Gdiplus::SolidBrush brBadgeBg(Gdiplus::Color(255, 10, 36, 48));
            Gdiplus::Pen penBadge(Gdiplus::Color(255, 16, 75, 100), 1.0f);

            float badgeW = 180.0f;
            float badgeH = 22.0f;
            float badgeX = (float)rc.right - badgeW - 12.0f;
            float badgeY = (float)rc.top + 7.0f;

            Theme::FillAndDrawRoundedRect(g, &brBadgeBg, &penBadge, badgeX, badgeY, badgeW, badgeH, 6.0f);

            Gdiplus::SolidBrush brBadgeText(Theme::CLR_CYAN);
            Gdiplus::StringFormat sf;
            sf.SetAlignment(Gdiplus::StringAlignmentCenter);
            sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            g.DrawString(wBadge.c_str(), -1, &fontBadge, Gdiplus::RectF(badgeX, badgeY, badgeW, badgeH), &sf, &brBadgeText);

            return TRUE;
        }
        break;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdcStatic = (HDC)wParam;
        HWND hStatic = (HWND)lParam;
        SetBkMode(hdcStatic, TRANSPARENT);
        if (hStatic == g_hChkStartup) {
            SetTextColor(hdcStatic, RGB(226, 232, 240));
        } else {
            SetTextColor(hdcStatic, RGB_TEXT_MUTED);
        }
        return (INT_PTR)g_hBrushBg;
    }

    case WM_CTLCOLORBTN: {
        return (INT_PTR)g_hBrushBg;
    }

    case WM_CTLCOLOREDIT: {
        HDC hdcEdit = (HDC)wParam;
        SetBkColor(hdcEdit, RGB(18, 24, 36));
        SetTextColor(hdcEdit, RGB(255, 255, 255));
        return (INT_PTR)g_hBrushCard;
    }

    case WM_CTLCOLORLISTBOX: {
        HDC hdcList = (HDC)wParam;
        SetBkColor(hdcList, RGB(18, 24, 36));
        SetTextColor(hdcList, RGB(255, 255, 255));
        return (INT_PTR)g_hBrushCard;
    }

    case WM_ERASEBKGND:
        return 1; // Handled in WM_PAINT via double-buffering

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        RECT clientRc;
        GetClientRect(hWnd, &clientRc);
        int w = clientRc.right - clientRc.left;
        int h = clientRc.bottom - clientRc.top;

        // Double-buffer DC to eliminate flicker
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBM = CreateCompatibleBitmap(hdc, w, h);
        HBITMAP oldBM = (HBITMAP)SelectObject(memDC, memBM);

        Gdiplus::Graphics g(memDC);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

        // 1. Fill main background
        Gdiplus::SolidBrush brBg(Theme::CLR_BG);
        g.FillRectangle(&brBg, 0, 0, w, h);

        // 2. Header Hero Card (y: 16, h: 72)
        Gdiplus::SolidBrush brCard(Theme::CLR_CARD);
        Gdiplus::Pen penBorder(Theme::CLR_CARD_BORDER, 1.0f);
        Theme::FillAndDrawRoundedRect(g, &brCard, &penBorder, 24.0f, 16.0f, (float)(w - 48), 72.0f, 12.0f);

        // Crisp Vector Shield Icon inside emerald box (40, 30, 44, 44)
        Gdiplus::SolidBrush brShieldBox(Theme::CLR_EMERALD_BG);
        Gdiplus::Pen penShieldBox(Theme::CLR_EMERALD, 1.5f);
        Theme::FillAndDrawRoundedRect(g, &brShieldBox, &penShieldBox, 40.0f, 30.0f, 44.0f, 44.0f, 10.0f);

        // Native Vector Shield Path
        Gdiplus::GraphicsPath shieldPath;
        float sx = 52.0f, sy = 39.0f;
        shieldPath.AddLine(sx, sy, sx + 20.0f, sy);
        shieldPath.AddLine(sx + 20.0f, sy, sx + 20.0f, sy + 13.0f);
        shieldPath.AddBezier(sx + 20.0f, sy + 13.0f, sx + 18.0f, sy + 23.0f, sx + 10.0f, sy + 26.0f, sx + 10.0f, sy + 26.0f);
        shieldPath.AddBezier(sx + 10.0f, sy + 26.0f, sx + 2.0f, sy + 23.0f, sx, sy + 13.0f, sx, sy + 13.0f);
        shieldPath.CloseFigure();

        Gdiplus::SolidBrush brShieldFill(Theme::CLR_EMERALD);
        g.FillPath(&brShieldFill, &shieldPath);

        // White Checkmark inside shield
        Gdiplus::Pen penCheck(Gdiplus::Color(255, 255, 255, 255), 2.2f);
        penCheck.SetStartCap(Gdiplus::LineCapRound);
        penCheck.SetEndCap(Gdiplus::LineCapRound);
        g.DrawLine(&penCheck, sx + 5.5f, sy + 12.5f, sx + 9.0f, sy + 16.5f);
        g.DrawLine(&penCheck, sx + 9.0f, sy + 16.5f, sx + 15.0f, sy + 8.5f);

        // Header Title
        Gdiplus::Font fontTitle(L"Segoe UI", 16.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
        Gdiplus::SolidBrush brWhite(Theme::CLR_TEXT_TITLE);
        g.DrawString(L"FocusShield", -1, &fontTitle, Gdiplus::PointF(96.0f, 26.0f), &brWhite);

        // Header Subtitle (Cyan)
        Gdiplus::Font fontSub(L"Segoe UI", 8.5f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
        Gdiplus::SolidBrush brCyan(Theme::CLR_CYAN);
        g.DrawString(L"NATIVE NETWORK SHIELD  |  24/7 OS SOCKET DEFENSE", -1, &fontSub, Gdiplus::PointF(98.0f, 54.0f), &brCyan);

        // Status Pill on top-right of Header Card
        float pillW = 190.0f;
        float pillH = 30.0f;
        float pillX = (float)w - 24.0f - pillW - 16.0f;
        float pillY = 37.0f;

        Gdiplus::SolidBrush brPillBg(Gdiplus::Color(255, 12, 42, 32));
        Gdiplus::Pen penPill(Theme::CLR_EMERALD, 1.2f);
        Theme::FillAndDrawRoundedRect(g, &brPillBg, &penPill, pillX, pillY, pillW, pillH, 15.0f);

        Gdiplus::SolidBrush brDot(Theme::CLR_EMERALD);
        g.FillEllipse(&brDot, pillX + 14.0f, pillY + 10.0f, 9.0f, 9.0f);

        Gdiplus::Font fontPill(L"Segoe UI", 8.5f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
        Gdiplus::SolidBrush brPillText(Gdiplus::Color(255, 52, 211, 153));
        g.DrawString(L"SHIELD ACTIVE (24/7)", -1, &fontPill, Gdiplus::PointF(pillX + 30.0f, pillY + 6.0f), &brPillText);

        // 3. Metrics Bar (y: 98, h: 54)
        float mw = ((float)w - 48.0f - 16.0f) / 3.0f;
        Gdiplus::Font fontVal(L"Segoe UI", 12.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
        Gdiplus::Font fontLbl(L"Segoe UI", 7.5f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
        Gdiplus::SolidBrush brLbl(Gdiplus::Color(255, 148, 163, 184)); // High-contrast clear gray

        auto draw_metric = [&](float x, const std::wstring& val, const std::wstring& label, Gdiplus::Color valClr) {
            Theme::FillAndDrawRoundedRect(g, &brCard, &penBorder, x, 98.0f, mw, 54.0f, 10.0f);
            Gdiplus::SolidBrush brVal(valClr);
            g.DrawString(val.c_str(), -1, &fontVal, Gdiplus::PointF(x + 14.0f, 105.0f), &brVal);
            g.DrawString(label.c_str(), -1, &fontLbl, Gdiplus::PointF(x + 14.0f, 129.0f), &brLbl);
        };

        draw_metric(24.0f, to_wstr(g_domains.size()) + L" Sites", L"TARGET BLOCKLIST", Theme::CLR_TEXT_TITLE);
        draw_metric(24.0f + mw + 8.0f, to_wstr(GetTotalLockedHostnames()) + L" Hostnames", L"LOCKED ENDPOINTS & CDNS", Theme::CLR_CYAN);
        draw_metric(24.0f + (mw + 8.0f) * 2.0f, L"0 ms (Null Route)", L"SOCKET DROP LATENCY", Theme::CLR_EMERALD);

        // 4. Section Labels (Crisp, High-Contrast Segoe UI Bold)
        Gdiplus::Font fontSection(L"Segoe UI", 8.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
        Gdiplus::SolidBrush brSection(Gdiplus::Color(255, 148, 163, 184));
        g.DrawString(L"QUICK DISTRACTION PRESETS (1-CLICK PERMANENT SHIELD):", -1, &fontSection, Gdiplus::PointF(24.0f, 164.0f), &brSection);
        g.DrawString(L"ADD WEBSITE TO BLOCK PERMANENTLY (DOMAIN OR URL):", -1, &fontSection, Gdiplus::PointF(24.0f, 230.0f), &brSection);
        g.DrawString(L"MANAGED OS BLOCKLIST (NULL-ROUTED TO 0.0.0.0 & ::1):", -1, &fontSection, Gdiplus::PointF(24.0f, 300.0f), &brSection);

        // Sleek Custom Border Frames around Edit Input and ListBox
        Gdiplus::Pen penInputFrame(Theme::CLR_CARD_BORDER, 1.0f);
        Theme::FillAndDrawRoundedRect(g, &brCard, &penInputFrame, 24.0f, 248.0f, 540.0f, 32.0f, 6.0f);
        Theme::FillAndDrawRoundedRect(g, &brCard, &penInputFrame, 24.0f, 318.0f, 540.0f, 160.0f, 6.0f);

        // 5. Test Socket Card (y: 532, h: 104)
        Theme::FillAndDrawRoundedRect(g, &brCard, &penBorder, 24.0f, 532.0f, (float)(w - 48), 104.0f, 10.0f);
        g.DrawString(L"LIVE NETWORK VERIFIER (TEST 0ms OS REFUSAL WITHOUT OPENING BROWSER):", -1, &fontSection, Gdiplus::PointF(38.0f, 544.0f), &brSection);

        // Card frame around test socket edit box
        Theme::FillAndDrawRoundedRect(g, &brCard, &penInputFrame, 24.0f, 574.0f, 540.0f, 30.0f, 6.0f);

        // Test result text (y: 612)
        Gdiplus::Font fontResult(L"Segoe UI", 8.5f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
        Gdiplus::Color resClr = g_testResultTested ? (g_testResultBlocked ? Theme::CLR_EMERALD : Theme::CLR_CRIMSON) : Gdiplus::Color(255, 148, 163, 184);
        Gdiplus::SolidBrush brResult(resClr);
        std::wstring wResult(g_testResultText.begin(), g_testResultText.end());
        g.DrawString(wResult.c_str(), -1, &fontResult, Gdiplus::PointF(38.0f, 614.0f), &brResult);

        // Blit back buffer to screen
        BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);

        SelectObject(memDC, oldBM);
        DeleteObject(memBM);
        DeleteDC(memDC);

        EndPaint(hWnd, &ps);
        break;
    }

    case WM_COMMAND: {
        int wmId = LOWORD(wParam);
        switch (wmId) {
        case ID_BTN_ADD: {
            char buf[256] = {0};
            GetWindowTextA(g_hEdtInput, buf, sizeof(buf));
            AddDomain(buf);
            SetWindowTextA(g_hEdtInput, "");
            SetFocus(g_hEdtInput);
            break;
        }

        case ID_BTN_REMOVE: {
            int sel = (int)SendMessage(g_hLstDomains, LB_GETCURSEL, 0, 0);
            if (sel != LB_ERR && sel >= 0 && sel < (int)g_domains.size()) {
                g_domains.erase(g_domains.begin() + sel);
                RefreshDomainList();
                SyncAndApply();
            }
            break;
        }

        case ID_BTN_CLEAR_ALL: {
            if (MessageBoxA(hWnd, "Are you sure you want to unblock all websites and restore hosts file?", "Confirm Unblock", MB_ICONQUESTION | MB_YESNO) == IDYES) {
                g_domains.clear();
                RefreshDomainList();
                HostsManager::clear_blocks();
                SyncAndApply();
                MessageBoxA(hWnd, "All website blocks removed. Hosts file restored to normal.", "Restored", MB_ICONINFORMATION | MB_OK);
            }
            break;
        }

        case ID_BTN_PRESET_YT: AddDomain("youtube.com"); break;
        case ID_BTN_PRESET_IG: AddDomain("instagram.com"); break;
        case ID_BTN_PRESET_TW: AddDomain("x.com"); break;
        case ID_BTN_PRESET_TT: AddDomain("tiktok.com"); break;
        case ID_BTN_PRESET_RD: AddDomain("reddit.com"); break;
        case ID_BTN_PRESET_NF: AddDomain("netflix.com"); break;
        case ID_BTN_PRESET_ADULT: AddDomain("pornhub.com"); break;

        case ID_CHK_STARTUP: {
            LRESULT chk = SendMessage(g_hChkStartup, BM_GETCHECK, 0, 0);
            bool enable = (chk == BST_CHECKED);
            RegistryManager::set_startup_enabled(enable);
            break;
        }

        case ID_BTN_DOH: {
            HostsManager::set_browser_doh_policy(true);
            MessageBoxA(hWnd, "Browser Anti-DoH Shield Enforced!\n\nChrome, Edge, and Brave are now configured via Group Policy to obey the local OS hosts resolver.", "Browser Shield", MB_ICONINFORMATION | MB_OK);
            break;
        }

        case ID_BTN_TEST: {
            char buf[256] = {0};
            GetWindowTextA(g_hEdtTest, buf, sizeof(buf));
            TestSocketDomain(buf);
            break;
        }

        case ID_BTN_MIN_TRAY: {
            ShowWindow(hWnd, SW_HIDE);
            break;
        }

        case ID_TRAY_OPEN: {
            ShowWindow(hWnd, SW_RESTORE);
            SetForegroundWindow(hWnd);
            break;
        }

        case ID_TRAY_EXIT: {
            DestroyWindow(hWnd);
            break;
        }
        }
        break;
    }

    case WM_TRAYICON: {
        if (lParam == WM_LBUTTONDBLCLK) {
            ShowWindow(hWnd, SW_RESTORE);
            SetForegroundWindow(hWnd);
        } else if (lParam == WM_RBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();
            AppendMenuA(hMenu, MF_STRING, ID_TRAY_OPEN, "Open FocusShield");
            AppendMenuA(hMenu, MF_STRING | MF_DISABLED, 0, "Network Shield: Active 24/7");
            AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuA(hMenu, MF_STRING, ID_TRAY_EXIT, "Exit FocusShield");

            SetForegroundWindow(hWnd);
            TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, NULL);
            DestroyMenu(hMenu);
        }
        break;
    }

    case WM_CLOSE: {
        ShowWindow(hWnd, SW_HIDE);
        return 0;
    }

    case WM_DESTROY: {
        RemoveTrayIcon();
        if (g_hFontInput) DeleteObject(g_hFontInput);
        if (g_hFontBold) DeleteObject(g_hFontBold);
        if (g_hFontNormal) DeleteObject(g_hFontNormal);
        if (g_hBrushBg) DeleteObject(g_hBrushBg);
        if (g_hBrushCard) DeleteObject(g_hBrushCard);
        if (g_hBrushInput) DeleteObject(g_hBrushInput);
        PostQuitMessage(0);
        break;
    }

    default:
        return DefWindowProcA(hWnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // WinSock init
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    // GDI+ init
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, NULL);

    // Check if launched at Windows Startup
    std::string cmd = lpCmdLine ? lpCmdLine : "";
    bool is_startup = (cmd.find("-startup") != std::string::npos);

    // Register Window Class
    WNDCLASSEXA wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    HICON hIco = (HICON)LoadImage(NULL, IDI_SHIELD, IMAGE_ICON, 0, 0, LR_SHARED);
    if (!hIco) hIco = LoadIcon(NULL, IDI_APPLICATION);
    wc.hIcon = hIco;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)CreateSolidBrush(RGB_BG);
    wc.lpszClassName = "FocusShieldProClass";

    if (!RegisterClassExA(&wc)) {
        return 1;
    }

    // Create Main Window (Centered, Fixed sleek size: 760x690)
    int width = 760;
    int height = 690;
    int x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;

    g_hWnd = CreateWindowExA(
        WS_EX_APPWINDOW,
        wc.lpszClassName,
        "FocusShield - Network-Level Website Blocker",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        x, y, width, height,
        NULL, NULL, hInstance, NULL
    );

    if (!g_hWnd) {
        return 1;
    }

    if (is_startup) {
        ShowWindow(g_hWnd, SW_HIDE);
    } else {
        ShowWindow(g_hWnd, nCmdShow);
        UpdateWindow(g_hWnd);
    }

    // Message Loop
    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    Gdiplus::GdiplusShutdown(g_gdiplusToken);
    WSACleanup();
    return (int)msg.wParam;
}
