#ifndef DOMAIN_EXPANDER_H
#define DOMAIN_EXPANDER_H

#include <string>
#include <vector>
#include <set>
#include <algorithm>
#include <cctype>

namespace DomainExpander {

inline std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return std::tolower(c);
    });
    return s;
}

inline std::string clean_domain(std::string input) {
    input = to_lower(input);

    // Trim whitespace
    size_t first = input.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = input.find_last_not_of(" \t\r\n");
    input = input.substr(first, last - first + 1);

    // Strip http:// or https://
    if (input.find("https://") == 0) input = input.substr(8);
    else if (input.find("http://") == 0) input = input.substr(7);

    // Strip path and query (e.g. /watch?v=...)
    size_t slash = input.find('/');
    if (slash != std::string::npos) input = input.substr(0, slash);

    // Strip port
    size_t colon = input.find(':');
    if (colon != std::string::npos) input = input.substr(0, colon);

    // Strip leading www. or m.
    if (input.find("www.") == 0) input = input.substr(4);
    else if (input.find("m.") == 0) input = input.substr(2);
    else if (input.find("mobile.") == 0) input = input.substr(7);

    return input;
}

inline std::vector<std::string> expand(const std::string& raw_input) {
    std::string base = clean_domain(raw_input);
    if (base.empty() || base.find('.') == std::string::npos) return {};

    std::set<std::string> out;

    // Platform-specific expansion maps (kills embedded iframes, CDNs, mobile APIs)
    if (base == "youtube.com" || base.find(".youtube.com") != std::string::npos || base == "youtu.be") {
        const char* yt[] = {
            "youtube.com", "www.youtube.com", "m.youtube.com", "music.youtube.com",
            "youtu.be", "youtube-nocookie.com", "www.youtube-nocookie.com",
            "googlevideo.com", "redirector.googlevideo.com",
            "ytimg.com", "i.ytimg.com", "s.ytimg.com", "yt3.ggpht.com", "yt4.ggpht.com",
            "wide-youtube.l.google.com", "youtubei.googleapis.com"
        };
        for (const char* h : yt) out.insert(h);
        return std::vector<std::string>(out.begin(), out.end());
    }

    if (base == "instagram.com" || base.find(".instagram.com") != std::string::npos || base == "threads.net") {
        const char* ig[] = {
            "instagram.com", "www.instagram.com", "m.instagram.com",
            "cdninstagram.com", "scontent.cdninstagram.com", "graph.instagram.com",
            "threads.net", "www.threads.net"
        };
        for (const char* h : ig) out.insert(h);
        return std::vector<std::string>(out.begin(), out.end());
    }

    if (base == "twitter.com" || base == "x.com" || base.find(".twitter.com") != std::string::npos || base.find(".x.com") != std::string::npos) {
        const char* tw[] = {
            "twitter.com", "www.twitter.com", "mobile.twitter.com", "api.twitter.com",
            "x.com", "www.x.com", "mobile.x.com", "api.x.com",
            "t.co", "twimg.com", "pbs.twimg.com", "abs.twimg.com", "video.twimg.com"
        };
        for (const char* h : tw) out.insert(h);
        return std::vector<std::string>(out.begin(), out.end());
    }

    if (base == "tiktok.com" || base.find(".tiktok.com") != std::string::npos) {
        const char* tt[] = {
            "tiktok.com", "www.tiktok.com", "m.tiktok.com",
            "tiktokcdn.com", "byteoversea.com", "ibytedtos.com",
            "musical.ly", "v16-webapp-prime.tiktok.com"
        };
        for (const char* h : tt) out.insert(h);
        return std::vector<std::string>(out.begin(), out.end());
    }

    if (base == "reddit.com" || base.find(".reddit.com") != std::string::npos || base == "redd.it") {
        const char* rd[] = {
            "reddit.com", "www.reddit.com", "old.reddit.com", "new.reddit.com",
            "redd.it", "v.redd.it", "preview.redd.it", "i.redd.it", "redditmedia.com"
        };
        for (const char* h : rd) out.insert(h);
        return std::vector<std::string>(out.begin(), out.end());
    }

    if (base == "facebook.com" || base.find(".facebook.com") != std::string::npos || base == "fb.com") {
        const char* fb[] = {
            "facebook.com", "www.facebook.com", "m.facebook.com",
            "fbcdn.net", "fbsbx.com", "fb.com", "messenger.com", "www.messenger.com"
        };
        for (const char* h : fb) out.insert(h);
        return std::vector<std::string>(out.begin(), out.end());
    }

    if (base == "netflix.com" || base.find(".netflix.com") != std::string::npos) {
        const char* nf[] = {
            "netflix.com", "www.netflix.com", "nflxext.com", "nflximg.net", "nflxvideo.net", "nflxso.net"
        };
        for (const char* h : nf) out.insert(h);
        return std::vector<std::string>(out.begin(), out.end());
    }

    if (base == "twitch.tv" || base.find(".twitch.tv") != std::string::npos) {
        const char* tw[] = {
            "twitch.tv", "www.twitch.tv", "m.twitch.tv", "ttvnw.net", "jtvnw.net"
        };
        for (const char* h : tw) out.insert(h);
        return std::vector<std::string>(out.begin(), out.end());
    }

    if (base == "pornhub.com" || base == "xvideos.com" || base == "xnxx.com") {
        out.insert(base);
        out.insert("www." + base);
        out.insert("phncdn.com");
        out.insert("xvideos-cdn.com");
        out.insert("xnxx-cdn.com");
        return std::vector<std::string>(out.begin(), out.end());
    }

    // Generic domain umbrella
    out.insert(base);
    out.insert("www." + base);
    out.insert("m." + base);
    out.insert("mobile." + base);
    out.insert("api." + base);
    out.insert("cdn." + base);
    out.insert("embed." + base);

    return std::vector<std::string>(out.begin(), out.end());
}

} // namespace DomainExpander

#endif // DOMAIN_EXPANDER_H
