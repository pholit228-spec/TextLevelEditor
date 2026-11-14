#pragma once
// Text Level script interpreter. Pure C++17: no Geode / GD dependency.
// The Geode side (main.cpp) only implements tl::Backend.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace tl {

struct ObjInfo {
    void* handle = nullptr;
    int id = 0;
    double x = 0;
    double y = 0;
};

struct Backend {
    virtual ~Backend() = default;
    virtual std::vector<ObjInfo> list() = 0;                       // every object in the level
    virtual std::string save(void* handle) = 0;                    // GD save string of one object
    virtual void remove(void* handle) = 0;                         // delete one object
    virtual void create(std::string const& objects) = 0;           // "1,1,2,15,3,15;1,8,..;"
    virtual bool setInt(std::string const& what, double value) = 0;// level settings
    virtual bool setName(std::string const& name) = 0;
};

struct Result {
    int commands = 0, placed = 0, removed = 0, moved = 0, edited = 0;
    std::vector<std::string> errors;

    std::string summary(size_t maxErrors = 12) const {
        std::string s = "Commands: " + std::to_string(commands) + "\nPlaced: " + std::to_string(placed) +
                        "  Removed: " + std::to_string(removed) + "\nMoved: " + std::to_string(moved) +
                        "  Edited: " + std::to_string(edited);
        if (!errors.empty()) {
            s += "\n\nErrors (" + std::to_string(errors.size()) + "):";
            for (size_t i = 0; i < errors.size() && i < maxErrors; ++i) s += "\n" + errors[i];
            if (errors.size() > maxErrors) s += "\n...";
        }
        return s;
    }
};

namespace detail {

using Vars = std::map<std::string, double>;

inline std::string lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

inline std::string trim(std::string const& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

inline std::string fmt(double v) {
    char b[64];
    if (std::fabs(v - std::round(v)) < 1e-9) {
        std::snprintf(b, sizeof b, "%lld", (long long)std::llround(v));
        return b;
    }
    std::snprintf(b, sizeof b, "%.4f", v);
    std::string s = b;
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

inline bool isInt(std::string const& s) {
    if (s.empty()) return false;
    for (char c : s)
        if (!std::isdigit((unsigned char)c)) return false;
    return true;
}

inline std::string b64url(std::string const& in) {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    size_t i = 0;
    while (i + 2 < in.size()) {
        unsigned v = ((unsigned char)in[i] << 16) | ((unsigned char)in[i + 1] << 8) | (unsigned char)in[i + 2];
        out += T[(v >> 18) & 63];
        out += T[(v >> 12) & 63];
        out += T[(v >> 6) & 63];
        out += T[v & 63];
        i += 3;
    }
    if (i + 1 == in.size()) {
        unsigned v = ((unsigned char)in[i] << 16);
        out += T[(v >> 18) & 63];
        out += T[(v >> 12) & 63];
        out += "==";
    } else if (i + 2 == in.size()) {
        unsigned v = ((unsigned char)in[i] << 16) | ((unsigned char)in[i + 1] << 8);
        out += T[(v >> 18) & 63];
        out += T[(v >> 12) & 63];
        out += T[(v >> 6) & 63];
        out += '=';
    }
    return out;
}

// ---------------------------------------------------------------- expressions
struct Expr {
    std::string s;
    size_t p = 0;
    bool ok = true;
    Vars const* vars = nullptr;

    void ws() {
        while (p < s.size() && std::isspace((unsigned char)s[p])) ++p;
    }
    double expr() {
        double v = term();
        for (;;) {
            ws();
            if (p < s.size() && (s[p] == '+' || s[p] == '-')) {
                char o = s[p++];
                double r = term();
                v = (o == '+') ? v + r : v - r;
            } else break;
        }
        return v;
    }
    double term() {
        double v = unary();
        for (;;) {
            ws();
            if (p < s.size() && (s[p] == '*' || s[p] == '/' || s[p] == '%')) {
                char o = s[p++];
                double r = unary();
                if (o == '*') v *= r;
                else if (r == 0) { ok = false; v = 0; }
                else if (o == '/') v /= r;
                else v = std::fmod(v, r);
            } else break;
        }
        return v;
    }
    double unary() {
        ws();
        if (p < s.size() && s[p] == '-') { ++p; return -unary(); }
        if (p < s.size() && s[p] == '+') { ++p; return unary(); }
        return power();
    }
    double power() {
        double b = atom();
        ws();
        if (p < s.size() && s[p] == '^') { ++p; return std::pow(b, unary()); }
        return b;
    }
    double call(std::string const& n, std::vector<double> const& a) {
        const double rad = 3.14159265358979323846 / 180.0;
        if (a.size() == 1) {
            if (n == "abs") return std::fabs(a[0]);
            if (n == "floor" || n == "int") return std::floor(a[0]);
            if (n == "ceil") return std::ceil(a[0]);
            if (n == "round") return std::round(a[0]);
            if (n == "sqrt") return a[0] < 0 ? (ok = false, 0.0) : std::sqrt(a[0]);
            if (n == "sin") return std::sin(a[0] * rad);   // degrees
            if (n == "cos") return std::cos(a[0] * rad);   // degrees
        }
        if (a.size() == 2) {
            if (n == "min") return std::min(a[0], a[1]);
            if (n == "max") return std::max(a[0], a[1]);
            if (n == "pow") return std::pow(a[0], a[1]);
            if (n == "mod") return a[1] == 0 ? (ok = false, 0.0) : std::fmod(a[0], a[1]);
        }
        ok = false;
        return 0;
    }
    double atom() {
        ws();
        if (p >= s.size()) { ok = false; return 0; }
        char c = s[p];
        if (c == '(') {
            ++p;
            double v = expr();
            ws();
            if (p < s.size() && s[p] == ')') ++p; else ok = false;
            return v;
        }
        if (std::isdigit((unsigned char)c) || c == '.') {
            char* end = nullptr;
            const char* b = s.c_str() + p;
            double v = std::strtod(b, &end);
            if (end == b) { ok = false; return 0; }
            p += (size_t)(end - b);
            return v;
        }
        if (c == '$' || c == '_' || std::isalpha((unsigned char)c)) {
            if (c == '$') ++p;
            size_t st = p;
            while (p < s.size() && (std::isalnum((unsigned char)s[p]) || s[p] == '_')) ++p;
            std::string name = s.substr(st, p - st);
            if (name.empty()) { ok = false; return 0; }
            if (p < s.size() && s[p] == '(') {
                ++p;
                std::vector<double> args;
                ws();
                if (p < s.size() && s[p] == ')') ++p;
                else {
                    for (;;) {
                        args.push_back(expr());
                        ws();
                        if (p < s.size() && s[p] == ',') { ++p; continue; }
                        if (p < s.size() && s[p] == ')') { ++p; break; }
                        ok = false;
                        break;
                    }
                }
                return call(lower(name), args);
            }
            auto it = vars->find(name);
            if (it != vars->end()) return it->second;
            if (lower(name) == "pi") return 3.14159265358979323846;
            ok = false;
            return 0;
        }
        ok = false;
        return 0;
    }
};

inline bool eval(std::string const& str, Vars const& vars, double& out) {
    Expr e;
    e.s = str;
    e.vars = &vars;
    double v = e.expr();
    e.ws();
    if (!e.ok || e.p != e.s.size() || !std::isfinite(v)) return false;
    out = v;
    return true;
}

// ---------------------------------------------------------------- statements
struct Stmt {
    int line;
    std::string text;
};

// Statements are separated by newlines or ';'. Comments: '#' or '//'.
inline std::vector<Stmt> split(std::string const& script) {
    std::vector<Stmt> out;
    std::string cur;
    int line = 1, depth = 0;
    bool inq = false;
    auto flush = [&] {
        auto t = trim(cur);
        if (!t.empty()) out.push_back({line, t});
        cur.clear();
    };
    for (size_t i = 0; i < script.size(); ++i) {
        char c = script[i];
        if (c == '\r') continue;
        if (c == '\n') { flush(); ++line; inq = false; depth = 0; continue; }
        if (!inq) {
            bool comment = c == '#' || (c == '/' && i + 1 < script.size() && script[i + 1] == '/');
            if (comment) {
                while (i + 1 < script.size() && script[i + 1] != '\n') ++i;
                continue;
            }
            if (c == ';' && depth == 0) { flush(); continue; }
            if (c == '(') ++depth;
            else if (c == ')' && depth > 0) --depth;
        }
        if (c == '"') inq = !inq;
        cur += c;
    }
    flush();
    return out;
}

struct Tok {
    std::string s;
    bool pure = false;   // token was a "quoted string" on its own
};

inline std::vector<Tok> tokenize(std::string const& line) {
    std::vector<Tok> out;
    size_t i = 0, n = line.size();
    while (i < n) {
        while (i < n && std::isspace((unsigned char)line[i])) ++i;
        if (i >= n) break;
        Tok t;
        if (line[i] == '"') {
            ++i;
            while (i < n && line[i] != '"') t.s += line[i++];
            if (i < n) ++i;
            t.pure = true;
            out.push_back(t);
            continue;
        }
        int depth = 0;
        while (i < n) {
            char c = line[i];
            if (depth == 0 && std::isspace((unsigned char)c)) break;
            if (c == '"') {
                ++i;
                while (i < n && line[i] != '"') t.s += line[i++];
                if (i < n) ++i;
                continue;
            }
            if (c == '(') ++depth;
            else if (c == ')' && depth > 0) --depth;
            t.s += c;
            ++i;
        }
        out.push_back(t);
    }
    return out;
}

// ---------------------------------------------------------------- name tables
using NameMap = std::map<std::string, int>;

inline NameMap const& triggerNames() {
    static const NameMap m = {
        {"color", 899}, {"move", 901}, {"pulse", 1006}, {"alpha", 1007}, {"toggle", 1049},
        {"spawn", 1268}, {"rotate", 1346}, {"follow", 1347}, {"shake", 1520}, {"animate", 1585},
        {"touch", 1595}, {"count", 1611}, {"stop", 1616}, {"instantcount", 1811}, {"ondeath", 1812},
        {"followy", 1814}, {"collision", 1815}, {"collisionblock", 1816}, {"pickup", 1817},
        {"bgeffecton", 1818}, {"bgeffectoff", 1819}, {"random", 1912}, {"zoom", 1913},
        {"camerastatic", 1914}, {"cameraoffset", 1916}, {"reverse", 1917}, {"song", 1934},
        {"timewarp", 1935}, {"camerarotate", 2015}, {"cameraedge", 2062}, {"scale", 2067},
        {"advrandom", 2068}, {"end", 3600}, {"sfx", 3602},
    };
    return m;
}

inline NameMap const& objectNames() {
    static const NameMap m = {
        {"block", 1}, {"spike", 8}, {"spike_small", 39}, {"startpos", 31}, {"coin", 1329},
        // portals
        {"cube", 12}, {"ship", 13}, {"ball", 47}, {"ufo", 111}, {"wave", 660}, {"robot", 745},
        {"spider", 1331}, {"swing", 1933},
        {"speed05", 200}, {"speed1", 201}, {"speed2", 202}, {"speed3", 203}, {"speed4", 1334},
        {"grav_down", 10}, {"grav_up", 11}, {"mini", 101}, {"big", 99}, {"dual", 286}, {"single", 287},
        {"mirror_on", 45}, {"mirror_off", 46}, {"teleport", 747},
        // orbs & pads
        {"yellow_orb", 36}, {"yellow_pad", 35}, {"pink_orb", 141}, {"pink_pad", 140},
        {"blue_orb", 84}, {"blue_pad", 67}, {"green_orb", 1022}, {"red_orb", 1333}, {"red_pad", 1332},
        {"black_orb", 1330}, {"dash_green", 1704}, {"dash_pink", 1751},
    };
    return m;
}

inline bool isTriggerId(int id) {
    static std::set<int> s;
    if (s.empty())
        for (auto& kv : triggerNames()) s.insert(kv.second);
    return s.count(id) > 0;
}

inline NameMap const& keyNames() {
    static const NameMap m = {
        {"rot", 6}, {"flipx", 4}, {"flipy", 5}, {"scale", 32}, {"group", 57}, {"groups", 57},
        {"z", 24}, {"zorder", 25}, {"layer", 20}, {"color", 21}, {"dcolor", 22},
        {"target", 51}, {"time", 10}, {"dur", 10}, {"duration", 10}, {"dx", 28}, {"dy", 29},
        {"ease", 30}, {"easerate", 85}, {"r", 7}, {"g", 8}, {"b", 9}, {"channel", 23}, {"ch", 23},
        {"opacity", 35}, {"blend", 17}, {"p1", 15}, {"p2", 16}, {"toggle", 56}, {"activate", 56},
        {"touch", 11}, {"spawn", 62}, {"multi", 87}, {"lockx", 58}, {"locky", 59},
        {"deg", 68}, {"times", 69}, {"center", 71}, {"strength", 75}, {"interval", 84},
        {"fadein", 45}, {"hold", 46}, {"fadeout", 47}, {"pulsemode", 48}, {"pulsetarget", 52},
        {"exclusive", 86}, {"item", 80}, {"count", 77}, {"delay", 63}, {"text", 31},
        {"copyid", 50}, {"hsv", 49}, {"lockrot", 70}, {"xmod", 72}, {"ymod", 73},
    };
    return m;
}

inline NameMap const& channelNames() {
    static const NameMap m = {
        {"bg", 1000}, {"g1", 1001}, {"ground", 1001}, {"line", 1002}, {"3dl", 1003},
        {"obj", 1004}, {"object", 1004}, {"p1", 1005}, {"p2", 1006}, {"lbg", 1007}, {"g2", 1009},
    };
    return m;
}

inline std::set<int> const& flagKeys() {
    static const std::set<int> s = {4, 5, 11, 17, 56, 58, 59, 62, 64, 65, 66, 67, 70, 87, 93, 94, 96, 98, 99};
    return s;
}

inline std::string buildObj(int id, double x, double y, std::map<int, std::string> const& keys) {
    std::string s = "1," + std::to_string(id) + ",2," + fmt(x) + ",3," + fmt(y);
    for (auto& kv : keys)
        if (kv.first != 1 && kv.first != 2 && kv.first != 3) s += "," + std::to_string(kv.first) + "," + kv.second;
    s += ";";
    return s;
}

inline std::vector<std::pair<int, std::string>> parseSave(std::string save) {
    while (!save.empty() && (save.back() == ';' || save.back() == ',')) save.pop_back();
    std::vector<std::string> parts;
    std::string cur;
    for (char c : save) {
        if (c == ',') { parts.push_back(cur); cur.clear(); }
        else cur += c;
    }
    parts.push_back(cur);
    std::vector<std::pair<int, std::string>> out;
    for (size_t i = 0; i + 1 < parts.size(); i += 2) out.push_back({std::atoi(parts[i].c_str()), parts[i + 1]});
    return out;
}

inline std::string patchSave(std::string const& save, std::map<int, std::string> const& patch) {
    auto kv = parseSave(save);
    for (auto& p : patch) {
        bool found = false;
        for (auto& e : kv)
            if (e.first == p.first) { e.second = p.second; found = true; }
        if (!found) kv.push_back({p.first, p.second});
    }
    std::string s;
    for (auto& e : kv) {
        if (!s.empty()) s += ",";
        s += std::to_string(e.first) + "," + e.second;
    }
    return s + ";";
}

inline bool inGroup(std::string const& save, int g) {
    for (auto& e : parseSave(save)) {
        if (e.first != 57) continue;
        std::string cur;
        auto test = [&](std::string const& part) { return !part.empty() && std::atoi(part.c_str()) == g; };
        for (char c : e.second) {
            if (c == '.') { if (test(cur)) return true; cur.clear(); }
            else cur += c;
        }
        if (test(cur)) return true;
    }
    return false;
}

} // namespace detail

// ================================================================= interpreter
class Interpreter {
public:
    explicit Interpreter(Backend& b) : be(b) {}

    Result run(std::string const& script) {
        res = Result{};
        vars.clear();
        pending.clear();
        autoN = 0;
        totalPlaced = 0;
        halted = false;
        blockMode = false;
        line = 0;

        auto stmts = detail::split(script);
        const size_t NONE = (size_t)-1;
        std::vector<size_t> match(stmts.size(), NONE);
        {
            std::vector<size_t> st;
            for (size_t i = 0; i < stmts.size(); ++i) {
                auto w = detail::lower(firstWord(stmts[i].text));
                if (w == "for") st.push_back(i);
                else if (w == "endfor" || w == "end") {
                    if (st.empty()) { line = stmts[i].line; err("'" + w + "' without 'for'"); }
                    else { match[st.back()] = i; st.pop_back(); }
                }
            }
            for (auto i : st) { line = stmts[i].line; err("'for' without 'endfor'"); }
        }

        struct Frame { std::string var; double cur, to, step; size_t start; };
        std::vector<Frame> frames;
        size_t pc = 0;
        long long steps = 0;
        while (pc < stmts.size() && !halted) {
            line = stmts[pc].line;
            if (++steps > 3000000) { err("too many steps (limit 3,000,000)"); break; }
            auto toks = detail::tokenize(stmts[pc].text);
            if (toks.empty()) { ++pc; continue; }
            auto cmd = detail::lower(toks[0].s);

            if (cmd == "for") {
                if (match[pc] == NONE) { ++pc; continue; }
                size_t skipTo = match[pc] + 1;
                double a = 0, b = 0, s = 1;
                if (toks.size() < 4 || !num(toks[2].s, a) || !num(toks[3].s, b)) {
                    err("usage: for VAR FROM TO [STEP]");
                    pc = skipTo;
                    continue;
                }
                s = (b >= a) ? 1 : -1;
                if (toks.size() > 4 && !num(toks[4].s, s)) { err("bad step '" + toks[4].s + "'"); pc = skipTo; continue; }
                if (s == 0) { err("step must not be 0"); pc = skipTo; continue; }
                if (std::fabs((b - a) / s) > 1000000) { err("loop too long (limit 1,000,000)"); pc = skipTo; continue; }
                if ((s > 0 && a > b + 1e-9) || (s < 0 && a < b - 1e-9)) { pc = skipTo; continue; }
                std::string var = toks[1].s;
                if (!var.empty() && var[0] == '$') var.erase(0, 1);
                vars[var] = a;
                frames.push_back({var, a, b, s, pc + 1});
                ++pc;
                continue;
            }
            if (cmd == "endfor" || cmd == "end") {
                if (frames.empty()) { ++pc; continue; }
                auto& f = frames.back();
                f.cur += f.step;
                bool more = f.step > 0 ? f.cur <= f.to + 1e-9 : f.cur >= f.to - 1e-9;
                if (more) { vars[f.var] = f.cur; pc = f.start; continue; }
                frames.pop_back();
                ++pc;
                continue;
            }
            exec(cmd, toks);
            ++pc;
        }
        flush();
        return res;
    }

    // Export the whole level as script text ("place" lines).
    static std::string dump(Backend& be) {
        auto objs = be.list();
        std::string out = "# exported level: " + std::to_string(objs.size()) + " objects\n";
        for (auto& o : objs) {
            out += "place " + std::to_string(o.id) + " " + detail::fmt(o.x) + " " + detail::fmt(o.y);
            for (auto& e : detail::parseSave(be.save(o.handle)))
                if (e.first != 1 && e.first != 2 && e.first != 3) out += " " + std::to_string(e.first) + "=" + e.second;
            out += "\n";
        }
        return out;
    }

private:
    Backend& be;
    Result res;
    detail::Vars vars;
    std::string pending;
    int autoN = 0;
    int totalPlaced = 0;
    bool halted = false;
    bool blockMode = false;
    int line = 0;
    static constexpr int kMaxObjects = 400000;

    // ------------------------------------------------------------ helpers
    static std::string firstWord(std::string const& s) {
        size_t i = 0;
        while (i < s.size() && !std::isspace((unsigned char)s[i])) ++i;
        return s.substr(0, i);
    }
    void err(std::string const& m) {
        if (res.errors.size() >= 200) { halted = true; return; }
        res.errors.push_back("line " + std::to_string(line) + ": " + m);
    }
    bool num(std::string const& s, double& out) const { return detail::eval(s, vars, out); }
    double px(double v) const { return blockMode ? v * 30 + 15 : v; }
    double dd(double v) const { return blockMode ? v * 30 : v; }

    void flush() {
        if (!pending.empty()) {
            be.create(pending);
            pending.clear();
        }
    }
    bool emit(int id, double x, double y, std::map<int, std::string> const& keys) {
        if (totalPlaced >= kMaxObjects) { err("object limit reached (400000)"); halted = true; return false; }
        pending += detail::buildObj(id, x, y, keys);
        ++res.placed;
        ++totalPlaced;
        if (pending.size() > (1u << 20)) flush();
        return true;
    }
    void autoPos(double& x, double& y) {
        x = 30.0 * (autoN / 20);
        y = 450.0 + 30.0 * (autoN % 20);
        ++autoN;
    }

    bool resolveId(std::string const& tok, int& id, bool& wild) const {
        auto l = detail::lower(tok);
        wild = false;
        if (l == "*" || l == "any" || l == "all") { wild = true; id = 0; return true; }
        auto it = detail::objectNames().find(l);
        if (it != detail::objectNames().end()) { id = it->second; return true; }
        auto it2 = detail::triggerNames().find(l);
        if (it2 != detail::triggerNames().end()) { id = it2->second; return true; }
        double v;
        if (num(tok, v) && v >= 1 && v < 100000) { id = (int)std::llround(v); return true; }
        return false;
    }

    struct Args {
        std::string idTok;
        std::vector<double> nums;
        std::vector<std::string> strs;
        bool hasIn = false, hasTo = false, hasBy = false;
        double in[4] = {0, 0, 0, 0}, to[2] = {0, 0}, by[2] = {0, 0};
        std::vector<std::pair<std::string, std::string>> kv;
    };

    bool parseArgs(std::vector<detail::Tok> const& t, size_t from, bool idFirst, Args& a) {
        size_t i = from;
        if (idFirst) {
            if (i >= t.size()) { err("missing object id"); return false; }
            a.idTok = t[i].s;
            ++i;
        }
        auto readNums = [&](size_t n, double* out, const char* what) -> bool {
            if (i + n > t.size()) { err(std::string("'") + what + "' needs " + std::to_string(n) + " numbers"); return false; }
            for (size_t k = 0; k < n; ++k)
                if (!num(t[i + k].s, out[k])) { err("bad number '" + t[i + k].s + "'"); return false; }
            i += n;
            return true;
        };
        while (i < t.size()) {
            auto const& tk = t[i];
            if (tk.pure) { a.strs.push_back(tk.s); ++i; continue; }
            std::string w = detail::lower(tk.s);
            if (w == "in" || w == "to" || w == "by" || w == "at") {
                ++i;
                if (w == "in") { a.hasIn = true; if (!readNums(4, a.in, "in")) return false; }
                else if (w == "to") { a.hasTo = true; if (!readNums(2, a.to, "to")) return false; }
                else if (w == "by") { a.hasBy = true; if (!readNums(2, a.by, "by")) return false; }
                else {
                    double v[2];
                    if (!readNums(2, v, "at")) return false;
                    a.nums.push_back(v[0]);
                    a.nums.push_back(v[1]);
                }
                continue;
            }
            auto eq = tk.s.find('=');
            if (eq != std::string::npos && eq > 0) {
                a.kv.push_back({tk.s.substr(0, eq), tk.s.substr(eq + 1)});
                ++i;
                continue;
            }
            double v;
            if (!num(tk.s, v)) { err("can't understand '" + tk.s + "'"); return false; }
            a.nums.push_back(v);
            ++i;
        }
        return true;
    }

    // property name/value -> save-string key/value
    std::string convertValue(int k, std::string const& prop, std::string const& val) {
        std::string lv = detail::lower(val);
        if (k == 31) return detail::b64url(val);
        if (k == 57) {
            std::string v = val;
            if (v.find(',') == std::string::npos && v.find('.') == std::string::npos) {
                double d;
                if (num(v, d)) return detail::fmt(d);
                err("bad value '" + val + "' for '" + prop + "'");
                return "";
            }
            std::string out, cur;
            auto add = [&](std::string const& part) -> bool {
                double d;
                if (part.empty() || !num(part, d)) return false;
                if (!out.empty()) out += ".";
                out += detail::fmt(d);
                return true;
            };
            for (char c : v) {
                if (c == ',' || c == '.') { if (!add(cur)) { err("bad group list '" + val + "'"); return ""; } cur.clear(); }
                else cur += c;
            }
            if (!add(cur)) { err("bad group list '" + val + "'"); return ""; }
            return out;
        }
        if (k == 21 || k == 22 || k == 23) {
            auto it = detail::channelNames().find(lv);
            if (it != detail::channelNames().end()) return std::to_string(it->second);
        }
        if (k == 30) {
            if (lv == "none") return "0";
            if (lv == "inout" || lv == "easeinout") return "1";
            if (lv == "in" || lv == "easein") return "2";
            if (lv == "out" || lv == "easeout") return "3";
        }
        if (detail::flagKeys().count(k)) {
            if (lv == "on" || lv == "true" || lv == "yes") return "1";
            if (lv == "off" || lv == "false" || lv == "no") return "0";
        }
        double d;
        if (num(val, d)) return detail::fmt(d);
        err("bad value '" + val + "' for '" + prop + "'");
        return "";
    }

    bool applyKV(std::map<int, std::string>& keys, std::string const& rawKey, std::string const& val) {
        std::string key = detail::lower(rawKey);
        int k = 0;
        bool numeric = detail::isInt(key);
        if (numeric) k = std::atoi(key.c_str());
        else {
            auto it = detail::keyNames().find(key);
            if (it == detail::keyNames().end()) {
                err("unknown property '" + rawKey + "' (any key works as a number, e.g. 57=1.2)");
                return false;
            }
            k = it->second;
        }
        std::string v;
        if (numeric) {
            v = val;   // numeric keys are passed through verbatim (unless they use $vars or (...))
            if (val.find('$') != std::string::npos || val.find('(') != std::string::npos) {
                double d;
                if (num(val, d)) v = detail::fmt(d);
            }
        } else {
            v = convertValue(k, rawKey, val);
            if (v.empty()) return false;
        }
        keys[k] = v;
        return true;
    }

    bool applyAll(Args const& a, std::map<int, std::string>& keys) {
        bool ok = true;
        for (auto& e : a.kv) ok = applyKV(keys, e.first, e.second) && ok;
        return ok;
    }

    // pull "name=value" options that are not object properties out of the list
    bool takeOpt(Args& a, std::string const& name, double& out) {
        for (auto it = a.kv.begin(); it != a.kv.end(); ++it) {
            if (detail::lower(it->first) != name) continue;
            double d;
            if (!num(it->second, d)) { err("bad value '" + it->second + "' for '" + name + "'"); return false; }
            out = d;
            a.kv.erase(it);
            return true;
        }
        return false;
    }

    // ------------------------------------------------------------ commands
    void exec(std::string const& cmd, std::vector<detail::Tok> const& t) {
        bool ok = true;
        if (cmd == "place") ok = cmdPlace(t, false);
        else if (cmd == "trig" || cmd == "trigger") ok = cmdPlace(t, true);
        else if (cmd == "fill") ok = cmdFill(t);
        else if (cmd == "line") ok = cmdLine(t);
        else if (cmd == "repeat" || cmd == "row") ok = cmdRepeat(t);
        else if (cmd == "text") ok = cmdText(t);
        else if (cmd == "raw") ok = cmdRaw(t);
        else if (cmd == "colorset") ok = cmdColorSet(t);
        else if (cmd == "del" || cmd == "delete" || cmd == "remove") ok = cmdDel(t);
        else if (cmd == "move") ok = cmdMove(t);
        else if (cmd == "paint" || cmd == "set" || cmd == "edit") ok = cmdPaint(t);
        else if (cmd == "clear") { flush(); for (auto& o : be.list()) { be.remove(o.handle); ++res.removed; } }
        else if (cmd == "let") ok = cmdLet(t);
        else if (cmd == "units") ok = cmdUnits(t);
        else if (cmd == "name") ok = cmdName(t);
        else if (cmd == "bg" || cmd == "ground" || cmd == "groundline" || cmd == "font" || cmd == "song" ||
                 cmd == "customsong" || cmd == "speed" || cmd == "startmode" || cmd == "mini" ||
                 cmd == "dual" || cmd == "twoplayer")
            ok = cmdSetting(cmd, t);
        else { err("unknown command '" + cmd + "'"); ok = false; }
        if (ok) ++res.commands;
    }

    bool cmdPlace(std::vector<detail::Tok> const& t, bool trig) {
        Args a;
        if (!parseArgs(t, 1, true, a)) return false;
        int id; bool wild;
        if (!resolveId(a.idTok, id, wild) || wild) { err("unknown object '" + a.idTok + "'"); return false; }
        double x = 0, y = 0;
        if (a.nums.size() >= 2) { x = px(a.nums[0]); y = px(a.nums[1]); }
        else if (trig || detail::isTriggerId(id)) autoPos(x, y);
        else { err("'place' needs x and y"); return false; }
        std::map<int, std::string> keys;
        bool ok = applyAll(a, keys);
        if (detail::isTriggerId(id) && !keys.count(36)) keys[36] = "1";
        emit(id, x, y, keys);
        return ok;
    }

    bool cmdFill(std::vector<detail::Tok> const& t) {
        Args a;
        if (!parseArgs(t, 1, true, a)) return false;
        int id; bool wild;
        if (!resolveId(a.idTok, id, wild) || wild) { err("unknown object '" + a.idTok + "'"); return false; }
        if (a.nums.size() < 4) { err("usage: fill ID x1 y1 x2 y2 [step=N]"); return false; }
        double x1 = px(std::min(a.nums[0], a.nums[2])), x2 = px(std::max(a.nums[0], a.nums[2]));
        double y1 = px(std::min(a.nums[1], a.nums[3])), y2 = px(std::max(a.nums[1], a.nums[3]));
        double sx = 30, sy = 30, st = 0;
        if (takeOpt(a, "step", st)) { sx = sy = dd(st); }
        if (takeOpt(a, "stepx", st)) sx = dd(st);
        if (takeOpt(a, "stepy", st)) sy = dd(st);
        if (sx <= 0 || sy <= 0) { err("step must be > 0"); return false; }
        double nx = std::floor((x2 - x1) / sx + 1e-9) + 1, ny = std::floor((y2 - y1) / sy + 1e-9) + 1;
        if (nx * ny > kMaxObjects) { err("fill too big"); return false; }
        std::map<int, std::string> keys;
        bool ok = applyAll(a, keys);
        if (detail::isTriggerId(id) && !keys.count(36)) keys[36] = "1";
        for (int i = 0; i < (int)nx; ++i)
            for (int j = 0; j < (int)ny; ++j)
                if (!emit(id, x1 + i * sx, y1 + j * sy, keys)) return false;
        return ok;
    }

    bool cmdLine(std::vector<detail::Tok> const& t) {
        Args a;
        if (!parseArgs(t, 1, true, a)) return false;
        int id; bool wild;
        if (!resolveId(a.idTok, id, wild) || wild) { err("unknown object '" + a.idTok + "'"); return false; }
        if (a.nums.size() < 4) { err("usage: line ID x1 y1 x2 y2 [step=N]"); return false; }
        double x1 = px(a.nums[0]), y1 = px(a.nums[1]), x2 = px(a.nums[2]), y2 = px(a.nums[3]);
        double step = 30, st = 0;
        if (takeOpt(a, "step", st)) step = dd(st);
        if (step <= 0) { err("step must be > 0"); return false; }
        double len = std::hypot(x2 - x1, y2 - y1);
        double n = std::max(1.0, std::round(len / step));
        if (n > kMaxObjects) { err("line too long"); return false; }
        std::map<int, std::string> keys;
        bool ok = applyAll(a, keys);
        if (detail::isTriggerId(id) && !keys.count(36)) keys[36] = "1";
        for (int i = 0; i <= (int)n; ++i) {
            double f = (double)i / n;
            if (!emit(id, x1 + (x2 - x1) * f, y1 + (y2 - y1) * f, keys)) return false;
        }
        return ok;
    }

    bool cmdRepeat(std::vector<detail::Tok> const& t) {
        Args a;
        if (!parseArgs(t, 1, true, a)) return false;
        int id; bool wild;
        if (!resolveId(a.idTok, id, wild) || wild) { err("unknown object '" + a.idTok + "'"); return false; }
        if (a.nums.size() < 3) { err("usage: repeat ID x y COUNT [dx dy]"); return false; }
        double x = px(a.nums[0]), y = px(a.nums[1]);
        int n = (int)std::llround(a.nums[2]);
        double dx = a.nums.size() > 3 ? dd(a.nums[3]) : 30, dy = a.nums.size() > 4 ? dd(a.nums[4]) : 0;
        if (n < 1 || n > kMaxObjects) { err("bad count"); return false; }
        std::map<int, std::string> keys;
        bool ok = applyAll(a, keys);
        if (detail::isTriggerId(id) && !keys.count(36)) keys[36] = "1";
        for (int i = 0; i < n; ++i)
            if (!emit(id, x + dx * i, y + dy * i, keys)) return false;
        return ok;
    }

    bool cmdText(std::vector<detail::Tok> const& t) {
        Args a;
        if (!parseArgs(t, 1, false, a)) return false;
        if (a.nums.size() < 2 || a.strs.empty()) { err("usage: text x y \"string\" [scale=1]"); return false; }
        std::map<int, std::string> keys;
        bool ok = applyAll(a, keys);
        keys[31] = detail::b64url(a.strs[0]);
        emit(914, px(a.nums[0]), px(a.nums[1]), keys);
        return ok;
    }

    bool cmdRaw(std::vector<detail::Tok> const& t) {
        if (t.size() < 2) { err("usage: raw 1,1,2,15,3,15"); return false; }
        std::string s;
        for (size_t i = 1; i < t.size(); ++i) s += t[i].s;
        if (s.empty()) return false;
        auto kv = detail::parseSave(s);
        if (kv.empty()) { err("bad raw string"); return false; }
        if (s.back() != ';') s += ";";
        pending += s;
        ++res.placed;
        ++totalPlaced;
        return true;
    }

    bool cmdColorSet(std::vector<detail::Tok> const& t) {
        Args a;
        if (!parseArgs(t, 1, true, a)) return false;
        auto lc = detail::lower(a.idTok);
        double ch;
        auto it = detail::channelNames().find(lc);
        if (it != detail::channelNames().end()) ch = it->second;
        else if (!num(a.idTok, ch)) { err("unknown color channel '" + a.idTok + "'"); return false; }
        double r = 255, g = 255, b = 255;
        size_t used = 0;
        std::string hex;
        for (auto it2 = a.kv.begin(); it2 != a.kv.end(); ++it2)
            if (detail::lower(it2->first) == "hex") { hex = it2->second; a.kv.erase(it2); break; }
        if (!hex.empty()) {
            if (!hex.empty() && hex[0] == '#') hex.erase(0, 1);
            if (hex.size() != 6) { err("hex must be RRGGBB"); return false; }
            unsigned v = (unsigned)std::strtoul(hex.c_str(), nullptr, 16);
            r = (v >> 16) & 255; g = (v >> 8) & 255; b = v & 255;
        } else {
            if (a.nums.size() < 3) { err("usage: colorset CHANNEL R G B [time=0] [opacity=1] [x y]"); return false; }
            r = a.nums[0]; g = a.nums[1]; b = a.nums[2];
            used = 3;
        }
        auto cl = [](double v) { return std::max(0.0, std::min(255.0, std::round(v))); };
        double x = 0, y = 0;
        if (a.nums.size() >= used + 2) { x = px(a.nums[used]); y = px(a.nums[used + 1]); }
        else autoPos(x, y);
        std::map<int, std::string> keys;
        bool ok = applyAll(a, keys);
        keys[23] = detail::fmt(ch);
        keys[7] = detail::fmt(cl(r));
        keys[8] = detail::fmt(cl(g));
        keys[9] = detail::fmt(cl(b));
        if (!keys.count(10)) keys[10] = "0";
        keys[36] = "1";
        emit(899, x, y, keys);
        return ok;
    }

    bool select(Args& a, std::vector<ObjInfo>& out) {
        int id = 0; bool wild = false;
        if (!resolveId(a.idTok, id, wild)) { err("unknown object '" + a.idTok + "'"); return false; }
        double tol = 0.6, grp = -1;
        takeOpt(a, "tol", tol);
        takeOpt(a, "ingroup", grp);
        flush();
        auto objs = be.list();
        bool havePos = a.nums.size() >= 2;
        double X = havePos ? px(a.nums[0]) : 0, Y = havePos ? px(a.nums[1]) : 0;
        double x1 = 0, y1 = 0, x2 = 0, y2 = 0;
        if (a.hasIn) {
            x1 = px(std::min(a.in[0], a.in[2])); x2 = px(std::max(a.in[0], a.in[2]));
            y1 = px(std::min(a.in[1], a.in[3])); y2 = px(std::max(a.in[1], a.in[3]));
        }
        for (auto& o : objs) {
            if (!wild && o.id != id) continue;
            if (havePos && (std::fabs(o.x - X) > tol || std::fabs(o.y - Y) > tol)) continue;
            if (a.hasIn && (o.x < x1 - tol || o.x > x2 + tol || o.y < y1 - tol || o.y > y2 + tol)) continue;
            if (grp >= 0 && !detail::inGroup(be.save(o.handle), (int)grp)) continue;
            out.push_back(o);
        }
        return true;
    }

    bool cmdDel(std::vector<detail::Tok> const& t) {
        Args a;
        if (!parseArgs(t, 1, true, a)) return false;
        std::vector<ObjInfo> sel;
        if (!select(a, sel)) return false;
        for (auto& o : sel) { be.remove(o.handle); ++res.removed; }
        return true;
    }

    bool cmdMove(std::vector<detail::Tok> const& t) {
        Args a;
        if (!parseArgs(t, 1, true, a)) return false;
        double dx = 0, dy = 0;
        if (a.hasBy) { dx = dd(a.by[0]); dy = dd(a.by[1]); }
        else if (a.hasTo) {
            if (a.nums.size() < 2) { err("'to' needs the current position (x y); use 'by dx dy' for areas"); return false; }
            dx = px(a.to[0]) - px(a.nums[0]);
            dy = px(a.to[1]) - px(a.nums[1]);
        } else { err("move needs 'to X Y' or 'by DX DY'"); return false; }
        std::vector<ObjInfo> sel;
        if (!select(a, sel)) return false;
        std::string out;
        for (auto& o : sel) {
            std::map<int, std::string> patch = {{2, detail::fmt(o.x + dx)}, {3, detail::fmt(o.y + dy)}};
            out += detail::patchSave(be.save(o.handle), patch);
            be.remove(o.handle);
            ++res.moved;
        }
        if (!out.empty()) be.create(out);
        return true;
    }

    bool cmdPaint(std::vector<detail::Tok> const& t) {
        Args a;
        if (!parseArgs(t, 1, true, a)) return false;
        std::vector<ObjInfo> sel;
        if (!select(a, sel)) return false;
        std::map<int, std::string> patch;
        bool ok = applyAll(a, patch);
        if (patch.empty()) { if (ok) err("nothing to change (e.g. color=5 dcolor=3 scale=2 group=1,2)"); return false; }
        std::string out;
        for (auto& o : sel) {
            out += detail::patchSave(be.save(o.handle), patch);
            be.remove(o.handle);
            ++res.edited;
        }
        if (!out.empty()) be.create(out);
        return ok;
    }

    bool cmdLet(std::vector<detail::Tok> const& t) {
        if (t.size() < 3) { err("usage: let NAME = EXPR"); return false; }
        std::string name = t[1].s;
        if (!name.empty() && name[0] == '$') name.erase(0, 1);
        size_t from = 2;
        if (t[2].s == "=") from = 3;
        std::string e;
        for (size_t i = from; i < t.size(); ++i) e += (i > from ? " " : "") + t[i].s;
        double d;
        if (name.empty() || !num(e, d)) { err("bad expression '" + e + "'"); return false; }
        vars[name] = d;
        return true;
    }

    bool cmdUnits(std::vector<detail::Tok> const& t) {
        if (t.size() < 2) { err("usage: units px|block"); return false; }
        auto v = detail::lower(t[1].s);
        if (v == "px" || v == "units" || v == "gd") blockMode = false;
        else if (v == "block" || v == "blocks") blockMode = true;
        else { err("units must be px or block"); return false; }
        return true;
    }

    bool cmdName(std::vector<detail::Tok> const& t) {
        std::string s;
        for (size_t i = 1; i < t.size(); ++i) s += (i > 1 ? " " : "") + t[i].s;
        if (s.empty()) { err("usage: name My Level"); return false; }
        if (!be.setName(s)) { err("level name can't be set here"); return false; }
        return true;
    }

    bool cmdSetting(std::string const& cmd, std::vector<detail::Tok> const& t) {
        if (t.size() < 2) { err("'" + cmd + "' needs a value"); return false; }
        std::string v = detail::lower(t[1].s);
        double d = 0;
        if (cmd == "speed") {
            // GD start-speed codes: 0 = 1x, 1 = 0.5x, 2 = 2x, 3 = 3x, 4 = 4x
            if (v == "slow" || v == "0.5") d = 1;
            else if (v == "normal" || v == "1") d = 0;
            else if (v == "fast" || v == "2") d = 2;
            else if (v == "faster" || v == "3") d = 3;
            else if (v == "fastest" || v == "4") d = 4;
            else { err("speed must be 0.5, 1, 2, 3 or 4"); return false; }
        } else if (cmd == "startmode") {
            static const std::map<std::string, int> modes = {{"cube", 0}, {"ship", 1}, {"ball", 2}, {"ufo", 3},
                                                              {"wave", 4}, {"robot", 5}, {"spider", 6}, {"swing", 7}};
            auto it = modes.find(v);
            if (it != modes.end()) d = it->second;
            else if (!num(t[1].s, d)) { err("startmode: cube|ship|ball|ufo|wave|robot|spider|swing"); return false; }
        } else if (cmd == "mini" || cmd == "dual" || cmd == "twoplayer") {
            if (v == "on" || v == "true" || v == "1" || v == "yes") d = 1;
            else if (v == "off" || v == "false" || v == "0" || v == "no") d = 0;
            else { err("'" + cmd + "' must be on or off"); return false; }
        } else if (!num(t[1].s, d)) { err("bad number '" + t[1].s + "'"); return false; }
        if (!be.setInt(cmd, d)) {
            err("setting '" + cmd + "' is not available in this GD/Geode version");
            return false;
        }
        return true;
    }
};

} // namespace tl
