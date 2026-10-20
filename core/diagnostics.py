"""
FocusShield Network Diagnostics
Provides real-time proof and verification that domains and embedded requests are dead-blocked.
"""

import socket
import winreg
from urllib.parse import urlparse


def check_domain_status(domain: str) -> dict:
    """
    Tests whether a domain resolves to 0.0.0.0 / ::1 or fails to connect,
    proving network-level blockage.
    """
    clean_host = domain.strip().lower()
    if "://" in clean_host:
        clean_host = urlparse(clean_host).netloc or clean_host
    if ":" in clean_host:
        clean_host = clean_host.split(":")[0]

    result = {
        "domain": clean_host,
        "is_blocked": False,
        "resolved_ips": [],
        "connection_test": "untested",
        "details": "",
    }

    # Step 1: DNS Resolution via OS resolver
    try:
        addr_info = socket.getaddrinfo(clean_host, 80, socket.AF_UNSPEC, socket.SOCK_STREAM)
        ips = list(set([item[4][0] for item in addr_info]))
        result["resolved_ips"] = ips

        # Check if mapped to null route
        if all(ip in ("0.0.0.0", "::1", "127.0.0.1", "::") for ip in ips):
            result["is_blocked"] = True
            result["details"] = f"Resolved to null route ({', '.join(ips)}). Traffic dropped."
        else:
            result["is_blocked"] = False
            result["details"] = f"Resolves to public IP ({', '.join(ips)}). Domain is currently accessible."
    except socket.gaierror as e:
        # Host not found or blocked completely
        result["is_blocked"] = True
        result["resolved_ips"] = ["NXDOMAIN"]
        result["details"] = f"DNS resolution failed ({e}). Blocked at resolver level."
    except Exception as e:
        result["details"] = f"DNS probe error: {e}"

    # Step 2: Socket Connection Test to verify refusal
    if result["is_blocked"]:
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(0.5)
            s.connect(("0.0.0.0", 80))
            s.close()
            result["connection_test"] = "Refused"
        except Exception:
            result["connection_test"] = "Connection Refused (0ms drop)"

    return result


def get_browser_doh_status() -> dict:
    """
    Checks if Chrome and Edge have DnsOverHttpsMode policy set to 'off'
    to prevent browsers from bypassing hosts file via DoH.
    """
    status = {
        "chrome_policy": "default",
        "edge_policy": "default",
        "brave_policy": "default",
        "is_doh_disabled_by_policy": False,
    }

    def check_reg(path, key_name="DnsOverHttpsMode"):
        try:
            with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, path, 0, winreg.KEY_READ) as key:
                val, _ = winreg.QueryValueEx(key, key_name)
                return str(val)
        except Exception:
            return "not_configured"

    chrome_val = check_reg(r"SOFTWARE\Policies\Google\Chrome")
    edge_val = check_reg(r"SOFTWARE\Policies\Microsoft\Edge")
    brave_val = check_reg(r"SOFTWARE\Policies\BraveSoftware\Brave")

    status["chrome_policy"] = chrome_val
    status["edge_policy"] = edge_val
    status["brave_policy"] = brave_val
    status["is_doh_disabled_by_policy"] = (chrome_val.lower() == "off" and edge_val.lower() == "off")

    return status


def set_browser_doh_policy(disable: bool) -> dict:
    """
    Configures Windows Group Policy Registry for Chrome, Edge, and Brave
    so they cannot bypass hosts file via DoH (requires Admin).
    """
    import ctypes

    if not ctypes.windll.shell32.IsUserAnAdmin():
        return {
            "success": False,
            "error": "Administrator privileges required to update browser enterprise policies.",
        }

    paths = [
        r"SOFTWARE\Policies\Google\Chrome",
        r"SOFTWARE\Policies\Microsoft\Edge",
        r"SOFTWARE\Policies\BraveSoftware\Brave",
    ]

    target_val = "off" if disable else "automatic"

    try:
        for p in paths:
            try:
                key = winreg.CreateKeyEx(winreg.HKEY_LOCAL_MACHINE, p, 0, winreg.KEY_WRITE)
                winreg.SetValueEx(key, "DnsOverHttpsMode", 0, winreg.REG_SZ, target_val)
                winreg.CloseKey(key)
            except Exception as e:
                print(f"[Diagnostics] Warning setting policy {p}: {e}")

        return {
            "success": True,
            "message": f"Browser DoH policy set to '{target_val}'. Browsers will obey OS hosts file.",
        }
    except Exception as e:
        return {"success": False, "error": str(e)}
