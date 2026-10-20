"""
FocusShield Local Server
Serves the premium web dashboard and provides REST API for network-level website blocking.
Uses only standard library (no pip install required!).
"""

import sys
import os
import json
import time
import subprocess
from http.server import ThreadingHTTPServer, SimpleHTTPRequestHandler
from pathlib import Path
from urllib.parse import urlparse, parse_qs

# Add root directory to python path
BASE_DIR = Path(__file__).parent.resolve()
sys.path.insert(0, str(BASE_DIR))

from core.config_manager import ConfigManager
from core import hosts_manager
from core import diagnostics
from core.domain_expander import expand_domain, clean_domain_input

PORT = 52771
FRONTEND_DIR = BASE_DIR / "frontend"


class FocusShieldHandler(SimpleHTTPRequestHandler):
    config_mgr = ConfigManager()

    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(FRONTEND_DIR), **kwargs)

    def _send_json(self, data: dict, status_code: int = 200):
        body = json.dumps(data, indent=2).encode("utf-8")
        self.send_response(status_code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()
        self.wfile.write(body)

    def _read_json_body(self) -> dict:
        content_len = int(self.headers.get("Content-Length", 0))
        if content_len == 0:
            return {}
        raw = self.rfile.read(content_len).decode("utf-8")
        try:
            return json.loads(raw)
        except Exception:
            return {}

    def do_OPTIONS(self):
        self.send_response(200)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()

    def do_GET(self):
        parsed = urlparse(self.path)
        path = parsed.path

        if path == "/api/status":
            is_adm = hosts_manager.is_admin()
            active_hosts = self.config_mgr.get_all_active_hostnames()
            system_blocked = hosts_manager.get_current_blocked_hosts()
            doh_info = diagnostics.get_browser_doh_status()

            # Refresh session status
            session = self.config_mgr.config.get("focus_session", {})
            if session.get("active", False) and time.time() >= session.get("end_time", 0):
                self.config_mgr.sync_to_system()

            data = {
                "shield_active": self.config_mgr.config.get("shield_active", False),
                "is_admin": is_adm,
                "total_active_hostnames": len(active_hosts),
                "system_blocked_count": len(system_blocked),
                "custom_domains": self.config_mgr.config.get("custom_domains", []),
                "presets": self.config_mgr.config.get("presets", {}),
                "focus_session": self.config_mgr.config.get("focus_session", {}),
                "stats": self.config_mgr.config.get("stats", {}),
                "browser_doh": doh_info,
                "current_time": int(time.time()),
            }
            return self._send_json(data)

        if path == "/api/preview-domain":
            params = parse_qs(parsed.query)
            q = params.get("q", [""])[0]
            clean = clean_domain_input(q)
            expanded = expand_domain(clean) if clean else []
            return self._send_json({
                "query": q,
                "clean": clean,
                "count": len(expanded),
                "expanded": expanded,
            })

        # Fallback to serving frontend static files
        return super().do_GET()

    def do_POST(self):
        parsed = urlparse(self.path)
        path = parsed.path
        body = self._read_json_body()

        if path == "/api/shield/toggle":
            state = body.get("state")
            res = self.config_mgr.toggle_shield(state)
            return self._send_json(res)

        if path == "/api/domain/add":
            domain = body.get("domain", "")
            res = self.config_mgr.add_custom_domain(domain)
            return self._send_json(res)

        if path == "/api/domain/remove":
            domain = body.get("domain", "")
            res = self.config_mgr.remove_custom_domain(domain)
            return self._send_json(res)

        if path == "/api/domain/toggle":
            domain = body.get("domain", "")
            enabled = body.get("enabled")
            res = self.config_mgr.toggle_custom_domain(domain, enabled)
            return self._send_json(res)

        if path == "/api/preset/toggle":
            preset_id = body.get("preset_id", "")
            enabled = body.get("enabled")
            res = self.config_mgr.toggle_preset(preset_id, enabled)
            return self._send_json(res)

        if path == "/api/timer/start":
            minutes = body.get("duration_minutes", 25)
            strict = body.get("strict_mode", False)
            res = self.config_mgr.start_focus_session(minutes, strict)
            return self._send_json(res)

        if path == "/api/timer/stop":
            force = body.get("force", False)
            pledge = body.get("pledge", "").strip().lower()
            expected_pledge = "i choose long-term focus over short-term distraction"

            session = self.config_mgr.config.get("focus_session", {})
            if session.get("strict_mode", False) and pledge != expected_pledge and not force:
                return self._send_json({
                    "success": False,
                    "error": "Pledge confirmation does not match. Stay focused!",
                    "strict_locked": True,
                })

            res = self.config_mgr.stop_focus_session(force=True)
            return self._send_json(res)

        if path == "/api/diagnostics/test":
            domain = body.get("domain", "")
            res = diagnostics.check_domain_status(domain)
            return self._send_json(res)

        if path == "/api/system/doh-policy":
            disable = body.get("disable", True)
            res = diagnostics.set_browser_doh_policy(disable)
            return self._send_json(res)

        if path == "/api/system/elevate":
            # Relaunch current server as Administrator
            try:
                bat_path = BASE_DIR / "Launch-FocusShield.bat"
                subprocess.Popen(
                    ["powershell", "-NoProfile", "-Command", f"Start-Process '{str(bat_path)}' -Verb RunAs"],
                    creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0,
                )
                return self._send_json({"success": True, "message": "Elevation prompt requested."})
            except Exception as e:
                return self._send_json({"success": False, "error": str(e)})

        self.send_error(404, "Endpoint Not Found")


def run_server():
    server_address = ("127.0.0.1", PORT)
    httpd = ThreadingHTTPServer(server_address, FocusShieldHandler)
    print(f"======================================================")
    print(f" FocusShield - Network Level Website Blocker")
    print(f" Running at: http://127.0.0.1:{PORT}")
    print(f" Admin privileges: {hosts_manager.is_admin()}")
    print(f"======================================================")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down FocusShield server.")
        httpd.server_close()


if __name__ == "__main__":
    run_server()
