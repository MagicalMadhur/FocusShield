"""
FocusShield Configuration & State Management
Saves and loads user blocklists, category presets, focus timers, and logs.
"""

import json
import time
from pathlib import Path
from core.domain_expander import expand_domain, clean_domain_input
from core import hosts_manager

CONFIG_PATH = Path(__file__).parent.parent / "config.json"

DEFAULT_PRESETS = {
    "video_streaming": {
        "id": "video_streaming",
        "name": "Video & Streaming",
        "description": "YouTube, Netflix, Twitch & streaming media",
        "icon": "tv",
        "enabled": False,
        "domains": [
            "youtube.com",
            "netflix.com",
            "twitch.tv",
            "disneyplus.com",
            "hulu.com",
            "primevideo.com",
            "vimeo.com",
        ],
    },
    "social_media": {
        "id": "social_media",
        "name": "Social Networks",
        "description": "Instagram, Twitter/X, TikTok, Facebook",
        "icon": "share",
        "enabled": False,
        "domains": [
            "instagram.com",
            "twitter.com",
            "x.com",
            "tiktok.com",
            "facebook.com",
            "threads.net",
            "snapchat.com",
        ],
    },
    "doomscrolling": {
        "id": "doomscrolling",
        "name": "Doomscrolling & Feeds",
        "description": "Reddit, 9GAG, Imgur, Pinterest, Quora",
        "icon": "compass",
        "enabled": False,
        "domains": [
            "reddit.com",
            "9gag.com",
            "imgur.com",
            "pinterest.com",
            "buzzfeed.com",
            "quora.com",
        ],
    },
    "gaming": {
        "id": "gaming",
        "name": "Gaming & Communities",
        "description": "Steam Community, Roblox, Discord Web",
        "icon": "gamepad",
        "enabled": False,
        "domains": [
            "steamcommunity.com",
            "roblox.com",
            "discord.com",
            "epicgames.com",
        ],
    },
    "adult_nsfw": {
        "id": "adult_nsfw",
        "name": "Adult & NSFW",
        "description": "Major adult sites, webcam portals & CDNs",
        "icon": "shield",
        "enabled": False,
        "domains": [
            "pornhub.com",
            "xvideos.com",
            "xnxx.com",
            "redtube.com",
            "youporn.com",
            "onlyfans.com",
            "chaturbate.com",
        ],
    },
    "news_tabloids": {
        "id": "news_tabloids",
        "name": "Tabloids & Clickbait",
        "description": "DailyMail, TMZ, BuzzFeed, NYPost",
        "icon": "newspaper",
        "enabled": False,
        "domains": [
            "dailymail.co.uk",
            "tmz.com",
            "buzzfeed.com",
            "nypost.com",
        ],
    },
}

DEFAULT_CONFIG = {
    "shield_active": False,
    "custom_domains": [
        {"domain": "youtube.com", "enabled": True, "created_at": 0},
        {"domain": "instagram.com", "enabled": True, "created_at": 0},
        {"domain": "x.com", "enabled": True, "created_at": 0},
    ],
    "presets": DEFAULT_PRESETS,
    "focus_session": {
        "active": False,
        "start_time": 0,
        "duration_seconds": 0,
        "end_time": 0,
        "strict_mode": False,
    },
    "stats": {
        "total_sessions": 0,
        "total_focus_minutes": 0,
        "last_sync_time": 0,
    },
}


class ConfigManager:
    def __init__(self):
        self.config = self._load()

    def _load(self) -> dict:
        if not CONFIG_PATH.exists():
            self._save(DEFAULT_CONFIG)
            return json.loads(json.dumps(DEFAULT_CONFIG))

        try:
            with open(CONFIG_PATH, "r", encoding="utf-8") as f:
                data = json.load(f)
                # Ensure all default keys exist
                for k, v in DEFAULT_CONFIG.items():
                    if k not in data:
                        data[k] = v
                # Merge presets
                if "presets" not in data:
                    data["presets"] = DEFAULT_PRESETS
                else:
                    for pid, pdata in DEFAULT_PRESETS.items():
                        if pid not in data["presets"]:
                            data["presets"][pid] = pdata
                return data
        except Exception as e:
            print(f"[ConfigManager] Error reading config: {e}. Falling back to defaults.")
            return json.loads(json.dumps(DEFAULT_CONFIG))

    def _save(self, data: dict = None):
        if data is None:
            data = self.config
        try:
            CONFIG_PATH.parent.mkdir(parents=True, exist_ok=True)
            with open(CONFIG_PATH, "w", encoding="utf-8") as f:
                json.dump(data, f, indent=2)
        except Exception as e:
            print(f"[ConfigManager] Error saving config: {e}")

    def get_all_active_hostnames(self) -> list[str]:
        """
        Gathers all unique target domains and expands each one into its
        full family of subdomains, CDNs, and embedded player hostnames.
        """
        targets = set()

        # Add enabled custom domains
        for item in self.config.get("custom_domains", []):
            if item.get("enabled", True):
                targets.add(item["domain"])

        # Add domains from enabled presets
        for preset in self.config.get("presets", {}).values():
            if preset.get("enabled", False):
                for d in preset.get("domains", []):
                    targets.add(d)

        # Expand every target
        all_hosts = set()
        for t in targets:
            expanded = expand_domain(t)
            all_hosts.update(expanded)

        return sorted(list(all_hosts))

    def sync_to_system(self) -> dict:
        """
        Applies changes to Windows hosts file based on current shield_active state.
        """
        is_shield_on = self.config.get("shield_active", False)
        # Check if focus session is running
        session = self.config.get("focus_session", {})
        now = time.time()
        if session.get("active", False):
            if now < session.get("end_time", 0):
                is_shield_on = True
            else:
                # Timer expired
                session["active"] = False
                self.config["focus_session"] = session
                self._save()

        if is_shield_on:
            hosts_to_block = self.get_all_active_hostnames()
            res = hosts_manager.apply_hosts_blocking(hosts_to_block)
        else:
            res = hosts_manager.clear_all_blocks()

        self.config["stats"]["last_sync_time"] = int(time.time())
        self._save()
        return res

    def toggle_shield(self, state: bool = None) -> dict:
        """Toggles master network shield on/off."""
        if state is None:
            state = not self.config.get("shield_active", False)

        # Prevent disabling if strict focus mode is active
        session = self.config.get("focus_session", {})
        if not state and session.get("active", False) and session.get("strict_mode", False):
            if time.time() < session.get("end_time", 0):
                return {
                    "success": False,
                    "error": "Strict Focus Mode is active! Complete your commitment pledge to unlock.",
                    "strict_locked": True,
                }

        self.config["shield_active"] = state
        self._save()
        res = self.sync_to_system()
        res["shield_active"] = state
        return res

    def add_custom_domain(self, domain_input: str) -> dict:
        """Adds a new domain, cleans it, and triggers sync."""
        clean = clean_domain_input(domain_input)
        if not clean or "." not in clean:
            return {"success": False, "error": "Invalid website domain name."}

        # Check existing
        for item in self.config["custom_domains"]:
            if item["domain"] == clean:
                return {"success": False, "error": f"'{clean}' is already in your blocklist."}

        self.config["custom_domains"].append({
            "domain": clean,
            "enabled": True,
            "created_at": int(time.time()),
        })
        self._save()

        # If shield is active, sync
        if self.config.get("shield_active", False):
            self.sync_to_system()

        expanded = expand_domain(clean)
        return {
            "success": True,
            "domain": clean,
            "expanded_count": len(expanded),
            "expanded_hosts": expanded,
        }

    def remove_custom_domain(self, domain: str) -> dict:
        clean = clean_domain_input(domain)
        self.config["custom_domains"] = [
            d for d in self.config["custom_domains"] if d["domain"] != clean
        ]
        self._save()
        if self.config.get("shield_active", False):
            self.sync_to_system()
        return {"success": True, "domain": clean}

    def toggle_custom_domain(self, domain: str, enabled: bool = None) -> dict:
        clean = clean_domain_input(domain)
        found = False
        new_state = False
        for item in self.config["custom_domains"]:
            if item["domain"] == clean:
                if enabled is None:
                    item["enabled"] = not item.get("enabled", True)
                else:
                    item["enabled"] = bool(enabled)
                new_state = item["enabled"]
                found = True
                break

        if not found:
            return {"success": False, "error": "Domain not found."}

        self._save()
        if self.config.get("shield_active", False):
            self.sync_to_system()
        return {"success": True, "domain": clean, "enabled": new_state}

    def toggle_preset(self, preset_id: str, enabled: bool = None) -> dict:
        presets = self.config.get("presets", {})
        if preset_id not in presets:
            return {"success": False, "error": f"Unknown preset: {preset_id}"}

        if enabled is None:
            presets[preset_id]["enabled"] = not presets[preset_id].get("enabled", False)
        else:
            presets[preset_id]["enabled"] = bool(enabled)

        self._save()
        if self.config.get("shield_active", False):
            self.sync_to_system()

        return {
            "success": True,
            "preset_id": preset_id,
            "enabled": presets[preset_id]["enabled"],
        }

    def start_focus_session(self, duration_minutes: int, strict_mode: bool = False) -> dict:
        """Starts a timed focus session."""
        duration_sec = max(60, int(duration_minutes) * 60)
        now = time.time()
        session = {
            "active": True,
            "start_time": int(now),
            "duration_seconds": duration_sec,
            "end_time": int(now + duration_sec),
            "strict_mode": bool(strict_mode),
        }
        self.config["focus_session"] = session
        self.config["shield_active"] = True
        self.config["stats"]["total_sessions"] += 1
        self.config["stats"]["total_focus_minutes"] += int(duration_minutes)
        self._save()

        sync_res = self.sync_to_system()
        return {"success": True, "session": session, "sync": sync_res}

    def stop_focus_session(self, force: bool = False) -> dict:
        """Stops the current focus session."""
        session = self.config.get("focus_session", {})
        if not session.get("active", False):
            return {"success": True, "message": "No active session."}

        # Check strict mode
        now = time.time()
        if session.get("strict_mode", False) and now < session.get("end_time", 0) and not force:
            return {
                "success": False,
                "error": "Strict mode is active. Unlock requires confirmation pledge.",
                "strict_locked": True,
            }

        session["active"] = False
        self.config["focus_session"] = session
        self._save()
        sync_res = self.sync_to_system()
        return {"success": True, "session": session, "sync": sync_res}
