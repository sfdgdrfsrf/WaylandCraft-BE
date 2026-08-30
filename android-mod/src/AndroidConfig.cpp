// ============================================================================
//  WaylandCraft-BE — android-mod/src/AndroidConfig.cpp
//
//  Minimal flat-JSON implementation of the Config storage for the
//  LeviLaunchroid lane. Deliberately tiny: the file is owned by the user,
//  keys are known, values are scalars. A full JSON library would be the
//  only third-party link dependency of the whole .so — not worth it here.
// ============================================================================

#include "AndroidConfig.h"

#include "config/Config.h"
#include "util/Log.h"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

namespace wlc {

// ---- storage (replaces the ll::api reflection in src/config/Config.cpp) ----

static Config gConfig;

Config&       Config::get() { return gConfig; }
bool          Config::load() { return true; }  // real load happens in AndroidConfig::loadFromFile
bool          Config::save() { return false; } // saveToFile() is the android path

namespace AndroidConfig {

namespace {

std::map<std::string, std::string> parseFlatJson(const std::string& text) {
    std::map<std::string, std::string> out;
    enum class Tok { None, Key, Str, Val };
    size_t i = 0;
    const size_t n = text.size();
    auto skipWs = [&] {
        while (i < n && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
    };
    std::string key, val;
    bool inKey = false, inVal = false, inStr = false, escaped = false;

    auto flushPair = [&] {
        if (!key.empty()) out[key] = val;
        key.clear(); val.clear();
        inKey = inVal = false;
    };

    for (; i < n; ++i) {
        const char c = text[i];
        if (inStr) {
            if (escaped) {
                if (inKey) key.push_back(c);
                else if (inVal) val.push_back(c);
                escaped = false;
                continue;
            }
            if (c == '\\') { escaped = true; continue; }
            if (c == '"') {
                inStr = false;
                if (inKey) { inKey = false; }        // finished key
                else if (inVal) { flushPair(); }     // finished string value
                continue;
            }
            if (inKey) key.push_back(c);
            else if (inVal) val.push_back(c);
            continue;
        }
        if (c == '"') {
            inStr = true;
            // A string starting while expecting a value vs a new key:
            if (!inKey && key.empty()) inKey = true;
            else if (!inKey && !key.empty() && !inVal) inVal = true;
            continue;
        }
        if (c == ':') { if (!key.empty()) inVal = true; continue; }
        if (c == ',' || c == '{' || c == '}') { if (inVal) flushPair(); continue; }
        if (inVal && !std::isspace(static_cast<unsigned char>(c))) val.push_back(c);
    }
    if (inVal) flushPair();
    return out;
}

std::string jsonEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\') out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

} // namespace

bool loadFromFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false; // first run — defaults apply

    std::stringstream ss;
    ss << in.rdbuf();
    auto kv = parseFlatJson(ss.str());

    Config& c = Config::get();
    auto str = [&](const char* k, std::string& dst) {
        auto it = kv.find(k);
        if (it != kv.end() && !it->second.empty()) dst = it->second;
    };
    auto num = [&](const char* k, int& dst) {
        auto it = kv.find(k);
        if (it != kv.end()) dst = std::atoi(it->second.c_str());
    };
    auto boolean = [&](const char* k, bool& dst) {
        auto it = kv.find(k);
        if (it != kv.end()) dst = it->second == "true";
    };

    str("runtimeDir", c.runtimeDir);
    str("socketName", c.socketName);
    num("tcpBridgePort", c.tcpBridgePort);
    num("capturePort", c.capturePort);
    num("pixelsPerBlock", c.pixelsPerBlock);
    boolean("tcpBridgeEnabled", c.tcpBridgeEnabled);
    boolean("captureEnabled", c.captureEnabled);
    boolean("focusOnHover", c.focusOnHover);
    boolean("hudClock", c.hudClock);
    boolean("hudAppList", c.hudAppList);
    boolean("hudVideoPin", c.hudVideoPin);

    wlc::Log::info(wlc::strf("config loaded (%zu keys) — socket %s, tcp %d, capture %d",
                             kv.size(), c.socketName.c_str(),
                             c.tcpBridgeEnabled ? c.tcpBridgePort : 0,
                             c.captureEnabled ? c.capturePort : 0));
    return true;
}

bool saveToFile(const std::string& path) {
    const Config& c = Config::get();
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << "{\n";
    out << "  \"runtimeDir\": \"" << jsonEscape(c.runtimeDir) << "\",\n";
    out << "  \"socketName\": \"" << jsonEscape(c.socketName) << "\",\n";
    out << "  \"tcpBridgeEnabled\": " << (c.tcpBridgeEnabled ? "true" : "false") << ",\n";
    out << "  \"tcpBridgePort\": " << c.tcpBridgePort << ",\n";
    out << "  \"captureEnabled\": " << (c.captureEnabled ? "true" : "false") << ",\n";
    out << "  \"capturePort\": " << c.capturePort << ",\n";
    out << "  \"pixelsPerBlock\": " << c.pixelsPerBlock << ",\n";
    out << "  \"focusOnHover\": " << (c.focusOnHover ? "true" : "false") << "\n";
    out << "}\n";
    return static_cast<bool>(out);
}

} // namespace AndroidConfig

} // namespace wlc
