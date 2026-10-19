#include "gdm/ApoParser.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>

namespace gdm {

namespace {

enum class Target { Common, Left, Right, Ignore };

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::vector<std::string> tokenize(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (std::isspace(static_cast<unsigned char>(c)) || c == ',' || c == ';') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

bool parseNumber(const std::string& token, double* value) {
    if (token.empty()) return false;
    const char* begin = token.c_str();
    char* end = nullptr;
    const double v = std::strtod(begin, &end);
    if (end == begin) return false;
    // Accept a glued unit such as "23Hz" or "-1.5dB".
    const std::string rest = lower(std::string(end));
    if (!rest.empty() && rest != "hz" && rest != "khz" && rest != "db") return false;
    *value = rest == "khz" ? v * 1000.0 : v;
    return std::isfinite(*value);
}

std::string at(int lineNo, const std::string& msg) {
    return "line " + std::to_string(lineNo) + ": " + msg;
}

// Bandwidth in octaves -> Q (RBJ cookbook relation for the analogue prototype).
double octavesToQ(double bw) {
    const double p = std::pow(2.0, bw);
    return std::sqrt(p) / (p - 1.0);
}

struct TypeInfo {
    const char* name;
    FilterType type;
    bool needsGain;
    bool needsQ;   // false: Q optional, defaults to 1/sqrt(2)
};

constexpr TypeInfo kTypes[] = {
    {"pk", FilterType::Peak, true, true},
    {"peq", FilterType::Peak, true, true},
    {"modal", FilterType::Peak, true, true},
    {"lsc", FilterType::LowShelf, true, false},
    {"hsc", FilterType::HighShelf, true, false},
    {"lp", FilterType::LowPass, false, false},
    {"lpq", FilterType::LowPass, false, true},
    {"hp", FilterType::HighPass, false, false},
    {"hpq", FilterType::HighPass, false, true},
    {"bp", FilterType::BandPass, false, true},
    {"no", FilterType::Notch, false, true},
    {"ap", FilterType::AllPass, false, true},
};

PeqSection* sectionFor(ApoProfile& p, Target t) {
    switch (t) {
        case Target::Common: return &p.common;
        case Target::Left: return &p.left;
        case Target::Right: return &p.right;
        case Target::Ignore: return nullptr;
    }
    return nullptr;
}

void addFilter(PeqSection* section, const FilterSpec& spec, int lineNo, ApoProfile& p) {
    if (static_cast<int>(section->filters.size()) >= kMaxPeqFilters) {
        p.warnings.push_back(at(lineNo, "more than " + std::to_string(kMaxPeqFilters) +
                                            " filters in one section; filter ignored"));
        return;
    }
    section->filters.push_back(spec);
}

void parseFilter(const std::string& value, int lineNo, Target target, ApoProfile& p) {
    const std::vector<std::string> tok = tokenize(value);
    if (tok.empty()) {
        p.warnings.push_back(at(lineNo, "empty filter line"));
        return;
    }
    const std::string state = lower(tok[0]);
    if (state == "off") return;
    if (state != "on") {
        p.warnings.push_back(at(lineNo, "expected ON or OFF, got '" + tok[0] + "'"));
        return;
    }
    if (tok.size() < 2) {
        p.warnings.push_back(at(lineNo, "missing filter type"));
        return;
    }

    const std::string typeName = lower(tok[1]);
    if (typeName == "none") return;
    if (typeName == "ls" || typeName == "hs") {
        p.warnings.push_back(at(lineNo, "filter type '" + tok[1] +
                                            "' (center-frequency shelf) is not supported; "
                                            "use LSC/HSC. Filter ignored"));
        return;
    }
    const TypeInfo* info = nullptr;
    for (const auto& t : kTypes) {
        if (typeName == t.name) info = &t;
    }
    if (info == nullptr) {
        p.warnings.push_back(at(lineNo, "unsupported filter type '" + tok[1] + "'; filter ignored"));
        return;
    }

    double fc = NAN, gain = NAN, q = NAN;
    for (size_t i = 2; i < tok.size(); ++i) {
        const std::string key = lower(tok[i]);
        double v = 0.0;
        if (key == "fc" && i + 1 < tok.size() && parseNumber(tok[i + 1], &v)) {
            fc = v;
            ++i;
            if (i + 1 < tok.size() && lower(tok[i + 1]) == "khz") {
                fc *= 1000.0;
                ++i;
            }
        } else if (key == "gain" && i + 1 < tok.size() && parseNumber(tok[i + 1], &v)) {
            gain = v;
            ++i;
        } else if (key == "q" && i + 1 < tok.size() && parseNumber(tok[i + 1], &v)) {
            q = v;
            ++i;
        } else if (key == "bw") {
            size_t j = i + 1;
            if (j < tok.size() && lower(tok[j]) == "oct") ++j;
            if (j < tok.size() && parseNumber(tok[j], &v) && v > 0.0) {
                q = octavesToQ(v);
                i = j;
            } else {
                p.warnings.push_back(at(lineNo, "could not read BW value; filter ignored"));
                return;
            }
        } else if (key == "hz" || key == "db") {
            // unit after a number already consumed
        } else if (i == 2 && parseNumber(tok[i], &v)) {
            p.warnings.push_back(at(lineNo, "slope form of '" + tok[1] +
                                                "' is not supported; filter ignored"));
            return;
        } else {
            p.warnings.push_back(at(lineNo, "unexpected token '" + tok[i] + "' ignored"));
        }
    }

    if (!(fc > 0.0)) {
        p.warnings.push_back(at(lineNo, "missing or invalid Fc; filter ignored"));
        return;
    }
    if (info->needsGain && std::isnan(gain)) {
        p.warnings.push_back(at(lineNo, "missing Gain; filter ignored"));
        return;
    }
    if (std::isnan(q)) {
        if (info->needsQ) {
            p.warnings.push_back(at(lineNo, "missing Q; filter ignored"));
            return;
        }
        q = 0.70710678118654752;
    }
    if (!(q > 0.0)) {
        p.warnings.push_back(at(lineNo, "Q must be positive; filter ignored"));
        return;
    }

    PeqSection* section = sectionFor(p, target);
    if (section == nullptr) return;
    FilterSpec spec;
    spec.type = info->type;
    spec.freqHz = fc;
    spec.gainDb = info->needsGain ? gain : 0.0;
    spec.q = q;
    addFilter(section, spec, lineNo, p);
}

Target parseChannel(const std::string& value, int lineNo, ApoProfile& p) {
    bool l = false, r = false, all = false;
    std::vector<std::string> unknown;
    for (const std::string& t : tokenize(value)) {
        const std::string k = lower(t);
        if (k == "l" || k == "1") l = true;
        else if (k == "r" || k == "2") r = true;
        else if (k == "all") all = true;
        else unknown.push_back(t);
    }
    if (!unknown.empty()) {
        std::string list;
        for (const auto& u : unknown) list += (list.empty() ? "" : " ") + u;
        p.warnings.push_back(at(lineNo, "channel(s) '" + list +
                                            "' do not exist after the downmix and are ignored"));
    }
    if (all || (l && r)) return Target::Common;
    if (l) return Target::Left;
    if (r) return Target::Right;
    return Target::Ignore;
}

std::string decodeText(const std::string& raw) {
    auto u8 = [](unsigned char c) { return c; };
    if (raw.size() >= 2 && u8(raw[0]) == 0xFF && u8(raw[1]) == 0xFE) {
        std::string out;
        for (size_t i = 2; i + 1 < raw.size(); i += 2) {
            const unsigned cu = u8(raw[i]) | (u8(raw[i + 1]) << 8);
            out.push_back(cu < 0x80 ? static_cast<char>(cu) : '?');
        }
        return out;
    }
    if (raw.size() >= 2 && u8(raw[0]) == 0xFE && u8(raw[1]) == 0xFF) {
        std::string out;
        for (size_t i = 2; i + 1 < raw.size(); i += 2) {
            const unsigned cu = (u8(raw[i]) << 8) | u8(raw[i + 1]);
            out.push_back(cu < 0x80 ? static_cast<char>(cu) : '?');
        }
        return out;
    }
    if (raw.size() >= 3 && u8(raw[0]) == 0xEF && u8(raw[1]) == 0xBB && u8(raw[2]) == 0xBF) {
        return raw.substr(3);
    }
    return raw;
}

}  // namespace

ApoProfile parseApo(const std::string& rawText) {
    ApoProfile p;
    const std::string text = decodeText(rawText);
    std::istringstream in(text);
    std::string line;
    int lineNo = 0;
    Target target = Target::Common;

    while (std::getline(in, line)) {
        ++lineNo;
        const std::string s = trim(line);
        if (s.empty() || s[0] == '#') continue;

        const size_t colon = s.find(':');
        if (colon == std::string::npos) {
            p.warnings.push_back(at(lineNo, "not a 'Command: value' line; ignored"));
            continue;
        }
        const std::string key = lower(trim(s.substr(0, colon)));
        const std::string value = trim(s.substr(colon + 1));

        if (key == "preamp") {
            const std::vector<std::string> tok = tokenize(value);
            double v = 0.0;
            if (tok.empty() || !parseNumber(tok[0], &v)) {
                p.warnings.push_back(at(lineNo, "could not read Preamp value"));
                continue;
            }
            // Multiple Preamp lines accumulate, as in APO.
            if (PeqSection* section = sectionFor(p, target)) section->preampDb += v;
        } else if (key.rfind("filter", 0) == 0) {
            parseFilter(value, lineNo, target, p);
        } else if (key == "channel") {
            target = parseChannel(value, lineNo, p);
        } else {
            p.warnings.push_back(at(lineNo, "command '" + trim(s.substr(0, colon)) +
                                                "' is not supported; ignored"));
        }
    }
    return p;
}

ApoProfile parseApoFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        ApoProfile p;
        p.warnings.push_back("cannot open file: " + path);
        return p;
    }
    const std::string raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return parseApo(raw);
}

}  // namespace gdm
