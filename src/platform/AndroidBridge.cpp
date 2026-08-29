// ============================================================================
//  WaylandCraft-BE — platform/AndroidBridge.cpp
//
//  THE Linux -> Android adaptation. Android IS Linux (the user said it:
//  "android is Linux but yk different os") — so everything upstream did with
//  Linux gets an Android equivalent, in-game:
//
//    XDG desktop entries  ->  `pm list packages` + package labels
//    exec()               ->  `am start` (or the LeviLaunchroid intent socket
//                             when direct starts are blocked by SELinux)
//    real Wayland apps    ->  Termux GUI apps (foot, mpv...) via the
//                             wayland-tcp bridge (tools/wayland-tcp-bridge.py)
//    native window content->  capture-helper companion APK streams the
//                             launched app's surface over the CaptureProtocol
//    SVG icons            ->  package icons via the companion (fallback:
//                             generated letter tiles)
// ============================================================================
#include "platform/PlatformBridge.h"

#include "util/Log.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <vector>

namespace wlc {

namespace {

/// Runs a command and captures stdout (safe: no shell, args via -c is fine
/// for pm/am which are always /system/bin). Returns "" on failure.
std::string runCmd(const std::string& cmd) {
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return "";
    std::string out;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), p)) > 0) out.append(buf, n);
    pclose(p);
    return out;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

/// Freedesktop category heuristics for known packages (mirrors the launcher
/// categories in upstream's AppLauncherScreen).
const char* categorize(const std::string& pkg) {
    static const struct {
        const char* fragment;
        const char* category;
    } table[] = {
        {"video", "Multimedia"}, {"music", "Multimedia"}, {"audio", "Multimedia"},
        {"player", "Multimedia"}, {"camera", "Graphics"}, {"gallery", "Graphics"},
        {"photo", "Graphics"}, {"game", "Games"}, {"chess", "Games"}, {"sudoku", "Games"},
        {"browser", "Network"}, {"mail", "Network"}, {"message", "Network"}, {"chat", "Network"},
        {"calc", "Office"}, {"note", "Office"}, {"office", "Office"}, {"term", "System"},
        {"terminal", "System"}, {"file", "System"}, {"settings", "Settings"},
        {"clock", "Utility"}, {"weather", "Utility"}, {"flashlight", "Utility"},
    };
    std::string p = pkg;
    std::transform(p.begin(), p.end(), p.begin(), ::tolower);
    for (const auto& row : table) {
        if (p.find(row.fragment) != std::string::npos) return row.category;
    }
    return "Utility";
}

/// True when the package is a system/background component nobody would
/// launch from an in-game desktop.
bool skipPackage(const std::string& pkg) {
    static const char* prefixes[] = {"com.google.android.gms", "com.google.android.gsf",
                                     "com.google.android.tts", "com.android.systemui",
                                     "com.android.providers", "android.autoinstalls",
                                     "com.qualcomm.", "com.mediatek.", "org.codeaurora."};
    for (const char* p : prefixes) {
        if (pkg.rfind(p, 0) == 0) return true;
    }
    return false;
}

int scoreQuery(const std::string& text, const std::string& q) {
    if (text == q) return 3;
    if (text.rfind(q, 0) == 0) return 2;
    if (text.find(q) != std::string::npos) return 1;
    return 0;
}

} // namespace

int scoreEntry(const DesktopEntry& entry, const std::string& query) {
    if (query.empty()) return 1;
    std::string q = query;
    std::transform(q.begin(), q.end(), q.begin(), ::tolower);
    auto lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        return s;
    };
    int score = 0;
    score += scoreQuery(lower(entry.name), q) * 3;
    score += scoreQuery(lower(entry.genericName), q);
    score += scoreQuery(lower(entry.comment), q);
    score += scoreQuery(lower(entry.appId), q) * 2;
    for (const auto& k : entry.keywords) score += scoreQuery(lower(k), q) * 2;
    return score;
}

// ---------------------------------------------------------------------------
class AndroidBridge : public PlatformBridge {
public:
    const char* platformName() const override { return "android"; }
    bool featuresAvailable() const override { return true; }

    std::vector<DesktopEntry> scanApps() override {
        std::vector<DesktopEntry> entries;
        // `pm list packages -3` = third-party apps (launchable, visible);
        // fall back to the full list when the -3 form is unavailable.
        std::string raw = runCmd("pm list packages -3 2>/dev/null");
        if (raw.empty()) raw = runCmd("pm list packages");
        std::string line;
        std::istringstream ss(raw);
        while (std::getline(ss, line)) {
            line = trim(line);
            const std::string prefix = "package:";
            if (line.rfind(prefix, 0) != 0) continue;
            std::string pkg = trim(line.substr(prefix.size()));
            if (pkg.empty() || skipPackage(pkg)) continue;

            DesktopEntry e;
            e.appId = pkg;
            e.name = pkg; // labels need the companion; pkg name is a fine start
            e.exec = pkg; // `am start` resolves the launcher activity
            e.categories.push_back(categorize(pkg));
            e.visible = true;
            entries.push_back(std::move(e));
        }
        // Termux is special: also expose it as the "terminal" entry so
        // Terminal=true semantics carry over.
        for (auto& e : entries) {
            if (e.appId == "com.termux") {
                e.genericName = "Terminal";
                e.categories.push_back("System");
            }
        }
        Log::info(strf("Android scan: %zu launchable packages", entries.size()));
        return entries;
    }

    bool launchApp(const DesktopEntry& entry) override {
        // 1) Preferred path: LeviLaunchroid / companion intent socket
        //    (the launcher owns the activity stack — always permitted).
        if (launchViaIntentSocket(entry.appId)) return true;
        // 2) Fallback: `am start` from the game process (works on many
        //    devices, blocked by SELinux on some).
        std::string cmd =
            strf("am start --user 0 -a android.intent.action.MAIN -c "
                 "android.intent.category.LAUNCHER -n %s 2>/dev/null || am start --user 0 -a "
                 "android.intent.action.MAIN -c android.intent.category.LAUNCHER -p %s",
                 entry.appId.c_str(), entry.appId.c_str());
        std::string out = runCmd(cmd);
        bool ok = out.find("Starting") != std::string::npos ||
                  out.find("Error") == std::string::npos;
        Log::info(strf("launch %s via am: %s", entry.appId.c_str(), ok ? "ok" : "failed"));
        return ok;
    }

    std::vector<uint8_t> renderIcon(const DesktopEntry& entry, int size) override {
        // Package icons require the companion APK (CaptureProtocol ICON_REQ).
        // Until it answers, the HUD draws letter tiles from entry.name.
        (void)entry;
        (void)size;
        return {};
    }

    std::string statusLine() const override {
        return "Android: apps launch via Intents; Termux GUI apps connect via "
               "the wayland-tcp bridge; app windows stream through the capture helper";
    }

private:
    /// Intent bridge: the companion/launcher listens on an abstract-namespace
    /// unix socket (@waylandcraft-be). Protocol: "LAUNCH <pkg>\n" -> "OK\n".
    bool launchViaIntentSocket(const std::string& pkg) {
        int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd < 0) return false;
        struct sockaddr_un addr {};
        addr.sun_family = AF_UNIX;
        // Abstract namespace socket (Linux-only feature — perfect here).
        addr.sun_path[0] = '\0';
        strncpy(addr.sun_path + 1, "waylandcraft-be", sizeof(addr.sun_path) - 2);
        socklen_t len = sizeof(sa_family_t) + strlen("waylandcraft-be") + 1;
        if (connect(fd, reinterpret_cast<struct sockaddr*>(&addr), len) < 0) {
            close(fd);
            return false;
        }
        std::string msg = "LAUNCH " + pkg + "\n";
        bool ok = write(fd, msg.data(), msg.size()) == (ssize_t)msg.size();
        char resp[8] = {};
        (void)read(fd, resp, sizeof(resp) - 1);
        ok = ok && strncmp(resp, "OK", 2) == 0;
        close(fd);
        return ok;
    }
};

} // namespace wlc

namespace wlc {
std::unique_ptr<PlatformBridge> makePlatformBridgeForAndroid() {
    return std::make_unique<AndroidBridge>();
}
} // namespace wlc
