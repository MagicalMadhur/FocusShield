"""
FocusShield Domain Intelligence & Subdomain Expander
Expands entered URLs or domains into their full family of subdomains, CDNs,
and embedded player hosts to guarantee that embedded widgets, iframes,
and app webviews fail to load completely.
"""

import re
from urllib.parse import urlparse

# Predefined domain knowledge maps for major distraction networks.
# Crucial for neutralizing embedded video players, CDNs, and API endpoints.
PLATFORM_EXPANSIONS = {
    "youtube.com": [
        "youtube.com",
        "www.youtube.com",
        "m.youtube.com",
        "music.youtube.com",
        "youtu.be",
        "youtube-nocookie.com",
        "www.youtube-nocookie.com",
        "googlevideo.com",
        "redirector.googlevideo.com",
        "ytimg.com",
        "i.ytimg.com",
        "s.ytimg.com",
        "yt3.ggpht.com",
        "yt4.ggpht.com",
        "wide-youtube.l.google.com",
        "youtubei.googleapis.com",
    ],
    "instagram.com": [
        "instagram.com",
        "www.instagram.com",
        "m.instagram.com",
        "cdninstagram.com",
        "scontent.cdninstagram.com",
        "graph.instagram.com",
        "threads.net",
        "www.threads.net",
    ],
    "twitter.com": [
        "twitter.com",
        "www.twitter.com",
        "mobile.twitter.com",
        "api.twitter.com",
        "x.com",
        "www.x.com",
        "mobile.x.com",
        "api.x.com",
        "t.co",
        "twimg.com",
        "pbs.twimg.com",
        "abs.twimg.com",
        "video.twimg.com",
    ],
    "x.com": [
        "x.com",
        "www.x.com",
        "mobile.x.com",
        "api.x.com",
        "twitter.com",
        "www.twitter.com",
        "mobile.twitter.com",
        "t.co",
        "twimg.com",
        "pbs.twimg.com",
        "abs.twimg.com",
        "video.twimg.com",
    ],
    "tiktok.com": [
        "tiktok.com",
        "www.tiktok.com",
        "m.tiktok.com",
        "tiktokcdn.com",
        "byteoversea.com",
        "ibytedtos.com",
        "musical.ly",
        "v16-webapp-prime.tiktok.com",
    ],
    "reddit.com": [
        "reddit.com",
        "www.reddit.com",
        "old.reddit.com",
        "new.reddit.com",
        "redd.it",
        "v.redd.it",
        "preview.redd.it",
        "i.redd.it",
        "redditmedia.com",
        "redditlist.com",
    ],
    "facebook.com": [
        "facebook.com",
        "www.facebook.com",
        "m.facebook.com",
        "fbcdn.net",
        "fbsbx.com",
        "fb.com",
        "www.fb.com",
        "messenger.com",
        "www.messenger.com",
    ],
    "twitch.tv": [
        "twitch.tv",
        "www.twitch.tv",
        "m.twitch.tv",
        "ttvnw.net",
        "jtvnw.net",
    ],
    "netflix.com": [
        "netflix.com",
        "www.netflix.com",
        "nflxext.com",
        "nflximg.net",
        "nflxvideo.net",
        "nflxso.net",
    ],
    "discord.com": [
        "discord.com",
        "www.discord.com",
        "discordapp.com",
        "discord.gg",
        "discordapp.net",
    ],
    "pinterest.com": [
        "pinterest.com",
        "www.pinterest.com",
        "pinimg.com",
        "i.pinimg.com",
    ],
    "pornhub.com": [
        "pornhub.com",
        "www.pornhub.com",
        "phncdn.com",
        "pornhubpremium.com",
    ],
    "xvideos.com": [
        "xvideos.com",
        "www.xvideos.com",
        "xvideos-cdn.com",
    ],
    "xnxx.com": [
        "xnxx.com",
        "www.xnxx.com",
        "xnxx-cdn.com",
    ],
    "steamcommunity.com": [
        "steamcommunity.com",
        "store.steampowered.com",
        "steampowered.com",
    ],
}


def clean_domain_input(user_input: str) -> str:
    """
    Cleans raw user input into a canonical hostname.
    Handles inputs like:
      - 'https://www.youtube.com/watch?v=123' -> 'youtube.com'
      - 'instagram.com/reels' -> 'instagram.com'
      - '   Twitter.Com  ' -> 'twitter.com'
      - 'm.reddit.com' -> 'reddit.com'
    """
    text = (user_input or "").strip().lower()
    if not text:
        return ""

    # Add dummy scheme if not present so urlparse works properly
    if not text.startswith("http://") and not text.startswith("https://"):
        text = "https://" + text

    parsed = urlparse(text)
    host = parsed.netloc or parsed.path

    # Remove port if present
    if ":" in host:
        host = host.split(":")[0]

    # Strip leading/trailing dots or slashes
    host = host.strip("./")

    # Remove standard 'www.' or 'm.' or 'mobile.' prefix for base matching
    cleaned = re.sub(r"^(www\.|m\.|mobile\.)", "", host)
    return cleaned


def expand_domain(domain: str) -> list[str]:
    """
    Returns the comprehensive list of hostnames to block for a given domain,
    ensuring embedded players and CDN streams are killed.
    """
    base = clean_domain_input(domain)
    if not base:
        return []

    # Check if we have an explicit platform map
    for key, host_list in PLATFORM_EXPANSIONS.items():
        if base == key or base.endswith("." + key):
            # Return unique sorted list
            return sorted(list(set(host_list)))

    # For any general domain: generate typical subdomains
    candidates = {
        base,
        f"www.{base}",
        f"m.{base}",
        f"mobile.{base}",
        f"api.{base}",
        f"cdn.{base}",
        f"embed.{base}",
    }
    return sorted(list(candidates))
