#include "gdm/Options.h"

#include <cctype>
#include <cstdlib>
#include <sstream>

#include "gdm/ChannelMap.h"

namespace gdm {

namespace {

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

bool parseBool(const std::string& v, bool* out) {
    if (v == "1" || v == "true" || v == "on" || v == "yes") {
        *out = true;
        return true;
    }
    if (v == "0" || v == "false" || v == "off" || v == "no") {
        *out = false;
        return true;
    }
    return false;
}

bool parseDouble(const std::string& v, double* out) {
    char* end = nullptr;
    const double d = std::strtod(v.c_str(), &end);
    if (end == v.c_str() || *end != '\0') return false;
    *out = d;
    return true;
}

}  // namespace

Options parseOptions(const std::string& text, Settings& settings) {
    Options o;
    std::istringstream in(text);
    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        const size_t hash = line.find('#');
        const std::string s = trim(hash == std::string::npos ? line : line.substr(0, hash));
        if (s.empty()) continue;
        const size_t eq = s.find('=');
        const std::string where = "options line " + std::to_string(lineNo) + ": ";
        if (eq == std::string::npos) {
            o.warnings.push_back(where + "expected key=value");
            continue;
        }
        const std::string key = trim(s.substr(0, eq));
        const std::string value = trim(s.substr(eq + 1));
        bool b = false;
        double d = 0.0;

        if (key == "enabled") {
            if (parseBool(value, &b)) o.enabled = b;
            else o.warnings.push_back(where + "enabled must be 0 or 1");
        } else if (key == "swap") {
            if (parseBool(value, &b)) settings.swapLR = b;
            else o.warnings.push_back(where + "swap must be 0 or 1");
        } else if (key == "lfe_lpf") {
            if (parseBool(value, &b)) settings.lfeLowPassEnabled = b;
            else o.warnings.push_back(where + "lfe_lpf must be 0 or 1");
        } else if (key == "master_db") {
            if (parseDouble(value, &d) && d <= 24.0 && d >= -60.0) settings.masterGainDb = d;
            else o.warnings.push_back(where + "master_db must be a number in [-60, 24]");
        } else if (key == "mute") {
            uint32_t mask = 0;
            std::string name;
            std::istringstream names(value);
            while (std::getline(names, name, ',')) {
                name = trim(name);
                if (name.empty()) continue;
                const uint32_t bit = channelBitFromName(name.c_str());
                if (bit == 0) o.warnings.push_back(where + "unknown channel '" + name + "'");
                mask |= bit;
            }
            settings.muteMask = mask;
        } else if (key == "eq_file") {
            o.eqFile = value;
        } else if (key == "log_interval_s") {
            if (parseDouble(value, &d) && d >= 0.0 && d <= 3600.0) o.logIntervalS = static_cast<int>(d);
            else o.warnings.push_back(where + "log_interval_s must be in [0, 3600]");
        } else {
            o.warnings.push_back(where + "unknown key '" + key + "'");
        }
    }
    return o;
}

}  // namespace gdm
