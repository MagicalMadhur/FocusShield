#ifndef REGISTRY_MANAGER_H
#define REGISTRY_MANAGER_H

#include <windows.h>
#include <winerror.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>

#ifndef SUCCEEDED
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#endif

namespace RegistryManager {

const char* const RUN_KEY = "Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const char* const APP_NAME = "FocusShield";

inline std::string get_app_data_path() {
    char path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, path))) {
        std::string dir = std::string(path) + "\\FocusShield";
        CreateDirectoryA(dir.c_str(), NULL);
        return dir + "\\blocklist.txt";
    }
    return "blocklist.txt";
}

inline std::string get_exe_path() {
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    return std::string(path);
}

inline bool is_startup_enabled() {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        char val[MAX_PATH];
        DWORD size = sizeof(val);
        DWORD type = REG_SZ;
        LONG res = RegQueryValueExA(hKey, APP_NAME, NULL, &type, (LPBYTE)val, &size);
        RegCloseKey(hKey);
        return (res == ERROR_SUCCESS);
    }
    return false;
}

inline bool set_startup_enabled(bool enable) {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
        if (enable) {
            std::string cmd = "\"" + get_exe_path() + "\" -startup";
            RegSetValueExA(hKey, APP_NAME, 0, REG_SZ, (const BYTE*)cmd.c_str(), (DWORD)cmd.size() + 1);
        } else {
            RegDeleteValueA(hKey, APP_NAME);
        }
        RegCloseKey(hKey);
        return true;
    }
    return false;
}

inline std::vector<std::string> load_saved_domains() {
    std::string path = get_app_data_path();
    std::ifstream file(path);
    if (!file.is_open()) {
        return {"youtube.com", "instagram.com", "x.com", "reddit.com"};
    }
    std::vector<std::string> domains;
    std::string line;
    while (std::getline(file, line)) {
        size_t first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) continue;
        size_t last = line.find_last_not_of(" \t\r\n");
        std::string d = line.substr(first, last - first + 1);
        if (!d.empty()) domains.push_back(d);
    }
    return domains;
}

inline void save_domains(const std::vector<std::string>& domains) {
    std::string path = get_app_data_path();
    std::ofstream file(path, std::ios::out | std::ios::trunc);
    if (!file.is_open()) return;
    for (const auto& d : domains) {
        file << d << "\n";
    }
}

} // namespace RegistryManager

#endif // REGISTRY_MANAGER_H
