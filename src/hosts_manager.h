#ifndef HOSTS_MANAGER_H
#define HOSTS_MANAGER_H

#include <windows.h>
#include <winerror.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iostream>
#include <set>

namespace HostsManager {

const char* const HOSTS_PATH = "C:\\Windows\\System32\\drivers\\etc\\hosts";
const char* const BACKUP_PATH = "C:\\Windows\\System32\\drivers\\etc\\hosts.focus_shield_backup";
const char* const START_MARKER = "# === FOCUS_SHIELD_MANAGED_BLOCK_START ===";
const char* const END_MARKER = "# === FOCUS_SHIELD_MANAGED_BLOCK_END ===";

typedef BOOL (WINAPI *DnsFlushResolverCacheFn)();

inline void flush_dns() {
    // 1. Direct Win32 API call to dnsapi.dll
    HMODULE hDns = LoadLibraryA("dnsapi.dll");
    if (hDns) {
        DnsFlushResolverCacheFn pfnFlush = (DnsFlushResolverCacheFn)GetProcAddress(hDns, "DnsFlushResolverCache");
        if (pfnFlush) {
            pfnFlush();
        }
        FreeLibrary(hDns);
    }

    // 2. Fallback execution without console window
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    char cmd[] = "ipconfig /flushdns";
    if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 3000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}

inline std::string read_hosts() {
    std::ifstream file(HOSTS_PATH, std::ios::in | std::ios::binary);
    if (!file.is_open()) return "";
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

inline void backup_hosts() {
    CopyFileA(HOSTS_PATH, BACKUP_PATH, FALSE);
}

inline bool apply_blocks(const std::vector<std::string>& hostnames) {
    std::string content = read_hosts();

    // Strip previous FocusShield block if present
    size_t start_pos = content.find(START_MARKER);
    size_t end_pos = content.find(END_MARKER);

    std::string base_content;
    if (start_pos != std::string::npos && end_pos != std::string::npos && end_pos > start_pos) {
        std::string prefix = content.substr(0, start_pos);
        std::string suffix = content.substr(end_pos + strlen(END_MARKER));
        base_content = prefix + suffix;
    } else {
        base_content = content;
    }

    // Trim trailing whitespace from base_content
    while (!base_content.empty() && (base_content.back() == '\r' || base_content.back() == '\n' || base_content.back() == ' ')) {
        base_content.pop_back();
    }

    std::ostringstream new_file;
    new_file << base_content << "\r\n\r\n";

    if (!hostnames.empty()) {
        new_file << START_MARKER << "\r\n";
        new_file << "# Managed by FocusShield Native (C++) - OS Level Socket Null Route\r\n";
        new_file << "# Total host targets locked: " << hostnames.size() << "\r\n";

        std::set<std::string> unique_hosts(hostnames.begin(), hostnames.end());
        for (const auto& h : unique_hosts) {
            if (!h.empty()) {
                new_file << "0.0.0.0       " << h << "\r\n";
                new_file << "::1           " << h << "\r\n";
            }
        }
        new_file << END_MARKER << "\r\n";
    }

    backup_hosts();

    // Write back to hosts
    std::ofstream out(HOSTS_PATH, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }

    std::string data = new_file.str();
    out.write(data.c_str(), data.size());
    out.close();

    flush_dns();
    return true;
}

inline bool clear_blocks() {
    return apply_blocks({});
}

inline bool set_browser_doh_policy(bool disable_doh) {
    const char* policies[] = {
        "SOFTWARE\\Policies\\Google\\Chrome",
        "SOFTWARE\\Policies\\Microsoft\\Edge",
        "SOFTWARE\\Policies\\BraveSoftware\\Brave"
    };

    const char* val = disable_doh ? "off" : "automatic";

    for (const char* p : policies) {
        HKEY hKey;
        if (RegCreateKeyExA(HKEY_LOCAL_MACHINE, p, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
            RegSetValueExA(hKey, "DnsOverHttpsMode", 0, REG_SZ, (const BYTE*)val, (DWORD)strlen(val) + 1);
            RegCloseKey(hKey);
        }
    }
    return true;
}

} // namespace HostsManager

#endif // HOSTS_MANAGER_H
