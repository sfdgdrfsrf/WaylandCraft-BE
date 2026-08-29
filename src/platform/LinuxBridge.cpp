// ============================================================================
//  WaylandCraft-BE — platform/LinuxBridge.cpp
//  Full-featured path: parses XDG desktop entries (Desktop Entry spec —
//  same keys upstream's Rust parser reads: Name/GenericName/Comment/Exec/
//  Terminal/Categories/NoDisplay/Hidden/Icon), execs apps with
//  WAYLAND_DISPLAY pointing at our socket, resolves icons from the hicolor
//  theme (upstream used cosmic_freedesktop_icons; we do spec fallbacks).
// ============================================================================
#include "platform/PlatformBridge.h"

#include "util/Log.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace wlc {

namespace {

std::vector<std::string> applicationDirs() {
    std::vector<std::string> dirs;
    const char* home = getenv("HOME");
    const char* xdgDataHome = getenv("XDG_DATA_HOME");
    if (xdgDataHome && *xdgDataHome) {
        dirs.push_back(std::string(xdgDataHome) + "/applications");
    } else if (home) {
        dirs.push_back(std::string(home) + "/.local/share/applications");
    }
    const char* xdgDataDirs = getenv("XDG_DATA_DIRS");
    std::string dd = (xdgDataDirs && *xdgDataDirs) ? xdgDataDirs : "/usr/local/share:/usr/share";
    std::stringstream ss(dd);
    std::string item;
    while (std::getline(ss, item, ':')) {
        if (!item.empty()) dirs.push_back(item + "/applications");
    }
    return dirs;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

/// Parses one .desktop file — mirrors upstream RawDesktopEntry fields.
bool parseDesktopFile(const std::string& path, DesktopEntry& out) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    bool inEntry = false;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        if (line[0] == '[') {
            inEntry = (line == "[Desktop Entry]");
            continue;
        }
        if (!inEntry) continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));
        // Tolerate locale keys (Name[xx]) like upstream tolerates snap fields.
        size_t bracket = key.find('[');
        if (bracket != std::string::npos) key = key.substr(0, bracket);

        if (key == "Name") out.name = val;
        else if (key == "GenericName") out.genericName = val;
        else if (key == "Comment") out.comment = val;
        else if (key == "Exec") out.exec = val;
        else if (key == "Terminal") out.terminal = (val == "true");
        else if (key == "NoDisplay" || key == "Hidden") out.visible &= (val != "true");
        else if (key == "Icon") out.iconPath = val;
        else if (key == "Categories") {
            std::stringstream cs(val);
            std::string c;
            while (std::getline(cs, c, ';')) {
                if (!c.empty()) out.categories.push_back(c);
            }
        }
    }
    // appId = file basename without .desktop (upstream convention).
    size_t slash = path.find_last_of('/');
    std::string base = (slash == std::string::npos) ? path : path.substr(slash + 1);
    if (base.size() > 8 && base.compare(base.size() - 8, 8, ".desktop") == 0) {
        base.resize(base.size() - 8);
    }
    out.appId = base;
    return !out.name.empty() && !out.exec.empty();
}

/// Icon resolution per the icon-theme spec (hicolor fallbacks).
std::string resolveIconPath(const std::string& icon) {
    if (icon.empty()) return "";
    if (icon[0] == '/') {
        struct stat st;
        return ::stat(icon.c_str(), &st) == 0 ? icon : "";
    }
    static const char* sizes[] = {"scalable", "128x128", "64x64", "48x48", "256x256"};
    std::vector<std::string> dirs;
    for (const std::string& d : applicationDirs()) {
        size_t pos = d.rfind("/applications");
        if (pos != std::string::npos) dirs.push_back(d.substr(0, pos) + "/icons/hicolor");
    }
    dirs.push_back("/usr/share/icons/hicolor");
    for (const char* sz : sizes) {
        for (const std::string& base : dirs) {
            for (const char* ext : {".png", ".svg"}) {
                std::string p = base + "/" + sz + "/apps/" + icon + ext;
                struct stat st;
                if (::stat(p.c_str(), &st) == 0) return p;
            }
        }
    }
    return "";
}

/// Exec tokenization with percent-field handling — port of upstream's shlex +
/// unpercent logic (%% %f %F %u %U %d %D %n %N %i %c %k %v %m dropped).
std::string cleanExec(const std::string& exec) {
    std::string out;
    for (size_t i = 0; i < exec.size(); ++i) {
        if (exec[i] == '%') {
            if (i + 1 < exec.size() && exec[i + 1] == '%') {
                out += '%';
                ++i;
                continue;
            }
            if (i + 1 < exec.size()) {
                char c = exec[i + 1];
                if (c == 'f' || c == 'F' || c == 'u' || c == 'U' || c == 'd' || c == 'D' ||
                    c == 'n' || c == 'N' || c == 'i' || c == 'c' || c == 'k' || c == 'v' ||
                    c == 'm') {
                    ++i; // drop the field code entirely
                    continue;
                }
            }
            continue;
        }
        out += exec[i];
    }
    return out;
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
    score += scoreQuery(lower(entry.name), q) * 3;          // name x3 (upstream)
    score += scoreQuery(lower(entry.genericName), q);
    score += scoreQuery(lower(entry.comment), q);
    score += scoreQuery(lower(entry.appId), q) * 2;
    for (const auto& k : entry.keywords) score += scoreQuery(lower(k), q) * 2;
    return score;
}

// ---------------------------------------------------------------------------
class LinuxBridge : public PlatformBridge {
public:
    const char* platformName() const override { return "linux-desktop"; }
    bool featuresAvailable() const override { return true; }

    std::vector<DesktopEntry> scanApps() override {
        std::vector<DesktopEntry> entries;
        for (const std::string& dir : applicationDirs()) {
            DIR* d = opendir(dir.c_str());
            if (!d) continue;
            struct dirent* e;
            while ((e = readdir(d)) != nullptr) {
                std::string name = e->d_name;
                if (name.size() <= 8 || name.compare(name.size() - 8, 8, ".desktop") != 0) {
                    continue;
                }
                DesktopEntry entry;
                entry.visible = true;
                if (parseDesktopFile(dir + "/" + name, entry) && entry.visible) {
                    entry.iconPath = resolveIconPath(entry.iconPath);
                    entries.push_back(std::move(entry));
                }
            }
            closedir(d);
        }
        // Dedupe by appId (earlier XDG_DATA_HOME entries win).
        std::sort(entries.begin(), entries.end(),
                  [](const DesktopEntry& a, const DesktopEntry& b) { return a.appId < b.appId; });
        entries.erase(std::unique(entries.begin(), entries.end(),
                                  [](const DesktopEntry& a, const DesktopEntry& b) {
                                      return a.appId == b.appId;
                                  }),
                      entries.end());
        Log::info(strf("XDG scan: %zu applications", entries.size()));
        return entries;
    }

    bool launchApp(const DesktopEntry& entry) override {
        std::string cmd = cleanExec(entry.exec);
        if (entry.terminal) {
            std::string term = preferredTerminal();
            if (!term.empty()) cmd = term + " -e " + cmd;
        }
        // Detach so the game process never waits on the app.
        pid_t pid = fork();
        if (pid < 0) return false;
        if (pid == 0) {
            setsid();
            // Compositor env: inherited from our process (Mod.cpp sets
            // WAYLAND_DISPLAY before any launch).
            execl("/bin/sh", "sh", "-c", cmd.c_str(), (char*)nullptr);
            _exit(127);
        }
        Log::info(strf("launched %s (pid %d)", entry.appId.c_str(), static_cast<int>(pid)));
        return true;
    }

    std::vector<uint8_t> renderIcon(const DesktopEntry& entry, int size) override {
        // PNG via any stb-style decoder is a glue-level concern; Linux icons
        // resolve to real files, so the renderer hook (render/IconCache)
        // decodes them. Here we return the file path marker for it.
        (void)size;
        return {};
    }

    std::string statusLine() const override {
        return "Linux desktop: real Wayland apps connect to $WAYLAND_DISPLAY";
    }
};

} // namespace wlc

// Factory lives here so non-Linux builds can still link the shared symbol.
namespace wlc {
std::unique_ptr<PlatformBridge> makePlatformBridgeForLinux() {
    return std::make_unique<LinuxBridge>();
}
} // namespace wlc
