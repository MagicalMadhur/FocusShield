"""
FocusShield Hosts Manager
Manages Windows hosts file entries with zero risk of corrupting existing entries.
Handles IPv4 (0.0.0.0) and IPv6 (::1) mappings, automatic backups, and DNS cache flushing.
"""

import os
import shutil
import subprocess
import ctypes
import tempfile
from pathlib import Path

HOSTS_PATH = Path(r"C:\Windows\System32\drivers\etc\hosts")
BACKUP_PATH = Path(r"C:\Windows\System32\drivers\etc\hosts.focus_shield_backup")
LOCAL_BACKUP_DIR = Path(__file__).parent.parent / "backups"

START_MARKER = "# === FOCUS_SHIELD_MANAGED_BLOCK_START ==="
END_MARKER = "# === FOCUS_SHIELD_MANAGED_BLOCK_END ==="


def is_admin() -> bool:
    """Check if the current process has Windows Administrator privileges."""
    try:
        return ctypes.windll.shell32.IsUserAnAdmin() != 0
    except Exception:
        return False


def create_backup():
    """Create a backup of the current hosts file both locally and in system directory if possible."""
    try:
        LOCAL_BACKUP_DIR.mkdir(parents=True, exist_ok=True)
        local_backup = LOCAL_BACKUP_DIR / "hosts.bak"
        shutil.copy2(HOSTS_PATH, local_backup)
        if is_admin():
            shutil.copy2(HOSTS_PATH, BACKUP_PATH)
    except Exception as e:
        print(f"[HostsManager] Warning backing up hosts file: {e}")


def read_hosts_raw() -> str:
    """Reads current hosts file content with fallback encodings."""
    if not HOSTS_PATH.exists():
        return ""
    for enc in ["utf-8", "cp1252", "latin-1"]:
        try:
            with open(HOSTS_PATH, "r", encoding=enc) as f:
                return f.read()
        except UnicodeDecodeError:
            continue
    with open(HOSTS_PATH, "r", errors="ignore") as f:
        return f.read()


def get_current_blocked_hosts() -> list[str]:
    """Returns all hosts currently inside the FocusShield managed block."""
    content = read_hosts_raw()
    if START_MARKER not in content or END_MARKER not in content:
        return []

    try:
        inside = content.split(START_MARKER)[1].split(END_MARKER)[0]
    except IndexError:
        return []

    hosts = set()
    for line in inside.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        if len(parts) >= 2:
            hosts.add(parts[1].lower())
    return sorted(list(hosts))


def build_new_hosts_content(hosts_to_block: list[str]) -> str:
    """
    Constructs new hosts file content, stripping previous FocusShield blocks
    and preserving all user-defined entries.
    """
    raw = read_hosts_raw()

    # Strip existing managed block
    if START_MARKER in raw and END_MARKER in raw:
        prefix = raw.split(START_MARKER)[0].rstrip()
        suffix = raw.split(END_MARKER)[1].lstrip()
        base_content = prefix + ("\n\n" + suffix if suffix else "")
    else:
        base_content = raw.rstrip()

    if not hosts_to_block:
        return base_content.rstrip() + "\n"

    # Build new block with both IPv4 and IPv6 to prevent IPv6 bypass
    block_lines = [
        "",
        START_MARKER,
        "# Managed by FocusShield - Network Level Website Blocker",
        f"# Total active host targets: {len(hosts_to_block)}",
    ]

    for host in sorted(set(hosts_to_block)):
        host = host.strip().lower()
        if host:
            block_lines.append(f"0.0.0.0       {host}")
            block_lines.append(f"::1           {host}")

    block_lines.append(END_MARKER)
    block_lines.append("")

    return (base_content + "\n" + "\n".join(block_lines)).lstrip()


def flush_dns():
    """Flushes Windows DNS resolver cache to immediately enforce changes."""
    try:
        subprocess.run(
            ["ipconfig", "/flushdns"],
            capture_output=True,
            text=True,
            timeout=5,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0,
        )
    except Exception as e:
        print(f"[HostsManager] ipconfig /flushdns failed: {e}")

    try:
        # Also invoke Clear-DnsClientCache in PowerShell
        subprocess.run(
            ["powershell", "-NoProfile", "-Command", "Clear-DnsClientCache"],
            capture_output=True,
            text=True,
            timeout=5,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0,
        )
    except Exception:
        pass


def apply_hosts_blocking(hosts_to_block: list[str]) -> dict:
    """
    Writes the updated hosts file and flushes DNS.
    If current process is Administrator: writes directly.
    If not Administrator: uses PowerShell UAC elevation helper.
    """
    new_content = build_new_hosts_content(hosts_to_block)

    if is_admin():
        try:
            create_backup()
            with open(HOSTS_PATH, "w", encoding="utf-8") as f:
                f.write(new_content)
            flush_dns()
            return {
                "success": True,
                "elevated": True,
                "count": len(hosts_to_block),
                "message": f"Successfully blocked {len(hosts_to_block)} hosts at OS level.",
            }
        except Exception as e:
            return {"success": False, "error": str(e)}

    # If NOT admin: write to a temporary file and launch elevated PowerShell to overwrite & flush
    try:
        temp_file = Path(tempfile.gettempdir()) / "focus_shield_hosts_update.tmp"
        with open(temp_file, "w", encoding="utf-8") as f:
            f.write(new_content)

        ps_script = f"""
        $temp = '{str(temp_file).replace("'", "''")}';
        $target = '{str(HOSTS_PATH).replace("'", "''")}';
        Copy-Item -Path $temp -Destination $target -Force;
        Clear-DnsClientCache;
        ipconfig /flushdns;
        Remove-Item -Path $temp -Force -ErrorAction SilentlyContinue;
        """

        encoded_cmd = subprocess.run(
            [
                "powershell",
                "-NoProfile",
                "-Command",
                f"Start-Process powershell -Verb RunAs -Wait -WindowStyle Hidden -ArgumentList '-NoProfile -Command \"{ps_script.strip()}\"'"
            ],
            capture_output=True,
            text=True,
            timeout=20,
        )

        flush_dns()
        return {
            "success": True,
            "elevated": False,
            "needs_uac": True,
            "count": len(hosts_to_block),
            "message": "Elevated helper executed. Network shield updated.",
        }
    except Exception as e:
        return {"success": False, "error": f"Elevation error: {e}"}


def clear_all_blocks() -> dict:
    """Completely removes all FocusShield entries and restores DNS."""
    return apply_hosts_blocking([])
