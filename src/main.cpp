#include <Geode/Geode.hpp>
#include <Geode/modify/EditorUI.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <type_traits>

#include "tl_interp.hpp"

using namespace geode::prelude;

// ---------------------------------------------------------------------------
// Compile-safe access to GD members / methods.
// If a member or method doesn't exist in the bindings of your Geode version,
// the helper silently returns false instead of breaking the build, and the
// script reports "setting not available".
// ---------------------------------------------------------------------------
#define TL_SET(NAME, EXPR)                                                                              \
    template <class T>                                                                                  \
    auto NAME##_i(T* o, double v, int)                                                                  \
        -> decltype((void)(EXPR = static_cast<std::remove_cvref_t<decltype(EXPR)>>(v)), true) {         \
        EXPR = static_cast<std::remove_cvref_t<decltype(EXPR)>>(v);                                     \
        return true;                                                                                    \
    }                                                                                                   \
    template <class T>                                                                                  \
    bool NAME##_i(T*, double, ...) {                                                                    \
        return false;                                                                                   \
    }                                                                                                   \
    static bool NAME(LevelEditorLayer* l, double v) { return NAME##_i(l, v, 0); }

TL_SET(tlSetBg, o->m_levelSettings->m_backgroundIndex)
TL_SET(tlSetGround, o->m_levelSettings->m_groundIndex)
TL_SET(tlSetGroundLine, o->m_levelSettings->m_groundLineIndex)
TL_SET(tlSetFont, o->m_levelSettings->m_fontIndex)
TL_SET(tlSetSpeed, o->m_levelSettings->m_startSpeed)
TL_SET(tlSetStartMode, o->m_levelSettings->m_startMode)
TL_SET(tlSetMini, o->m_levelSettings->m_startMini)
TL_SET(tlSetDual, o->m_levelSettings->m_startDual)
TL_SET(tlSetTwoPlayer, o->m_levelSettings->m_twoPlayerMode)
TL_SET(tlSetAudioTrack, o->m_level->m_audioTrack)
TL_SET(tlSetSongID, o->m_level->m_songID)

// object -> save string (tries getSaveString(layer), then getSaveString(), then a minimal fallback)
template <class O, class L>
auto tlSave_i(O* o, L* l, int) -> decltype(std::string(o->getSaveString(l).c_str())) {
    return std::string(o->getSaveString(l).c_str());
}
template <class O, class L>
auto tlSave_i(O* o, L*, long) -> decltype(std::string(o->getSaveString().c_str())) {
    return std::string(o->getSaveString().c_str());
}
template <class O, class L>
std::string tlSave_i(O* o, L*, ...) {
    return "1," + std::to_string(o->m_objectID) + ",2," + tl::detail::fmt(o->getPositionX()) + ",3," +
           tl::detail::fmt(o->getPositionY()) + ",6," + tl::detail::fmt(o->getRotation());
}

// create objects from a level-string fragment
template <class L>
auto tlCreate_i(L* l, std::string const& s, int)
    -> decltype(l->createObjectsFromString(gd::string(s), true, true), void()) {
    l->createObjectsFromString(gd::string(s), true, true);
}
template <class L>
auto tlCreate_i(L* l, std::string const& s, long)
    -> decltype(l->createObjectsFromString(gd::string(s), true), void()) {
    l->createObjectsFromString(gd::string(s), true);
}

// delete one object
template <class L>
auto tlRemove_i(L* l, GameObject* o, int) -> decltype(l->removeObject(o, true), void()) {
    l->removeObject(o, true);
}
template <class L>
auto tlRemove_i(L* l, GameObject* o, long) -> decltype(l->removeObject(o), void()) {
    l->removeObject(o);
}

// ---------------------------------------------------------------------------
class GDBackend : public tl::Backend {
public:
    explicit GDBackend(LevelEditorLayer* e) : ed(e) {}

    std::vector<tl::ObjInfo> list() override {
        std::vector<tl::ObjInfo> out;
        auto arr = ed->m_objects;
        if (!arr) return out;
        out.reserve(arr->count());
        for (unsigned i = 0; i < arr->count(); ++i) {
            auto obj = static_cast<GameObject*>(arr->objectAtIndex(i));
            if (!obj) continue;
            tl::ObjInfo o;
            o.handle = obj;
            o.id = obj->m_objectID;
            o.x = obj->getPositionX();
            o.y = obj->getPositionY();
            out.push_back(o);
        }
        return out;
    }

    std::string save(void* h) override {
        return tlSave_i(static_cast<GameObject*>(h), ed, 0);
    }

    void remove(void* h) override { tlRemove_i(ed, static_cast<GameObject*>(h), 0); }

    void create(std::string const& s) override { tlCreate_i(ed, s, 0); }

    bool setInt(std::string const& w, double v) override {
        if (w == "bg") return tlSetBg(ed, v);
        if (w == "ground") return tlSetGround(ed, v);
        if (w == "groundline") return tlSetGroundLine(ed, v);
        if (w == "font") return tlSetFont(ed, v);
        if (w == "speed") return tlSetSpeed(ed, v);
        if (w == "startmode") return tlSetStartMode(ed, v);
        if (w == "mini") return tlSetMini(ed, v);
        if (w == "dual") return tlSetDual(ed, v);
        if (w == "twoplayer") return tlSetTwoPlayer(ed, v);
        if (w == "song") {
            bool a = tlSetAudioTrack(ed, v);
            tlSetSongID(ed, 0);
            return a;
        }
        if (w == "customsong") return tlSetSongID(ed, v);
        return false;
    }

    bool setName(std::string const& n) override {
        if (!ed->m_level) return false;
        ed->m_level->m_levelName = gd::string(n);
        return true;
    }

private:
    LevelEditorLayer* ed;
};

// ---------------------------------------------------------------------------
static const char* kSample =
    "# Text Level example. Edit me, then press \"File\" in the editor.\n"
    "# Units: 30 = 1 block. The ground surface is y = 0, so a block on the ground has y = 15.\n"
    "name Text Level Demo\n"
    "bg 1\n"
    "ground 1\n"
    "speed 1\n"
    "colorset bg 20 40 90\n"
    "colorset g1 10 25 60\n"
    "\n"
    "# 10 spikes, one every 10 blocks\n"
    "for i 0 9\n"
    "  place spike 600+$i*300 15\n"
    "endfor\n"
    "\n"
    "# a little staircase\n"
    "place block 1200 15\n"
    "place block 1230 15\n"
    "place block 1230 45\n"
    "\n"
    "# a ship section with a ceiling\n"
    "place ship 1800 105\n"
    "fill block 1830 285 2400 285\n"
    "place cube 2700 105\n"
    "\n"
    "# move trigger: shifts group 1 up when the player passes x=900\n"
    "place block 900 105 group=1\n"
    "trig move 900 300 target=1 dx=0 dy=60 time=1 ease=inout\n"
    "\n"
    "text 300 150 \"Hello from text!\" scale=0.5\n";

static std::filesystem::path tlScriptPath() {
    return Mod::get()->getSaveDir() / "level.txt";
}

$on_mod(Loaded) {
    auto p = tlScriptPath();
    std::ifstream t(p);
    if (!t.good()) {
        std::ofstream o(p, std::ios::binary);
        o << kSample;
    }
}

static std::string tlSanitize(std::string s) {
    for (auto& c : s)
        if (c == '<' || c == '>') c = ' ';
    return s;
}

static void tlAlert(std::string const& title, std::string const& body) {
    FLAlertLayer::create(title.c_str(), tlSanitize(body), "OK")->show();
}

static void tlAlertScroll(std::string const& title, std::string const& body) {
    FLAlertLayer::create(nullptr, title.c_str(), tlSanitize(body), "OK", nullptr, 420.f, true, 290.f, 1.f)->show();
}

static const char* kHelp =
    "Buttons: Paste = run script from clipboard, File = run level.txt from the mod save folder, "
    "Dump = copy the current level as script to clipboard.\n\n"
    "Commands (one per line or separated by ;):\n"
    "place ID x y [rot= scale= group=1,2 color=5 dcolor=3 z= flipx=on ...]\n"
    "trig NAME [x y] target= dx= dy= time= ease= ...  (NAME: move color alpha pulse toggle spawn rotate shake ...)\n"
    "colorset CH R G B [time=] [x y]  (CH: bg g1 line obj p1 p2 or number)\n"
    "fill ID x1 y1 x2 y2 [step=30]   line ID x1 y1 x2 y2   repeat ID x y N [dx dy]\n"
    "del ID [x y] [in x1 y1 x2 y2] [ingroup=N]   (ID may be * for any)\n"
    "move ID x y to X Y   |   move ID [in ...] by dx dy\n"
    "paint ID x y color=5 dcolor=3 (also: any property)\n"
    "text x y \"string\" [scale=]   raw 1,8,2,15,3,15   clear\n"
    "bg N  ground N  song N  customsong N  speed 0.5|1|2|3|4  startmode ship  mini on  name Title\n"
    "let W = 30   for i 0 9 ... endfor   units block|px\n"
    "Numbers accept expressions: 600+$i*300, (a+b)/2, sin(), cos(), min(), max()\n"
    "Any property can be a number key: 57=1.2 or 28=300.\n\n"
    "Background / ground changes show after you save and re-enter the editor.";

// ---------------------------------------------------------------------------
class $modify(TLEditorUI, EditorUI) {
    $override bool init(LevelEditorLayer* layer) {
        if (!EditorUI::init(layer)) return false;

        auto win = CCDirector::sharedDirector()->getWinSize();
        float px = (float)Mod::get()->getSettingValue<int64_t>("pos-x");
        float py = (float)Mod::get()->getSettingValue<int64_t>("pos-y");

        auto menu = CCMenu::create();
        menu->setID("textlevel-menu"_spr);
        menu->setPosition(ccp(win.width * px / 100.f, win.height * py / 100.f));
        this->addChild(menu, 100);

        const char* caps[] = {"Paste", "File", "Dump", "Help"};
        SEL_MenuHandler sels[] = {
            menu_selector(TLEditorUI::onTLPaste),
            menu_selector(TLEditorUI::onTLFile),
            menu_selector(TLEditorUI::onTLDump),
            menu_selector(TLEditorUI::onTLHelp),
        };
        for (int i = 0; i < 4; ++i) {
            auto spr = ButtonSprite::create(caps[i]);
            spr->setScale(0.5f);
            auto btn = CCMenuItemSpriteExtra::create(spr, this, sels[i]);
            btn->setPosition(ccp(0.f, -24.f * i));
            menu->addChild(btn);
        }
        return true;
    }

    void tlRun(std::string const& script) {
        if (tl::detail::trim(script).empty()) {
            tlAlert("Text Level", "The script is empty.");
            return;
        }
        GDBackend be(this->m_editorLayer);
        tl::Interpreter it(be);
        auto r = it.run(script);
        tlAlert(r.errors.empty() ? "Text Level: done" : "Text Level: done with errors", r.summary());
    }

    void onTLPaste(CCObject*) { tlRun(geode::utils::clipboard::read()); }

    void onTLFile(CCObject*) {
        auto p = tlScriptPath();
        std::ifstream f(p, std::ios::binary);
        if (!f.good()) {
            tlAlert("Text Level", "File not found:\n" + p.string());
            return;
        }
        std::stringstream ss;
        ss << f.rdbuf();
        tlRun(ss.str());
    }

    void onTLDump(CCObject*) {
        GDBackend be(this->m_editorLayer);
        auto text = tl::Interpreter::dump(be);
        geode::utils::clipboard::write(text);
        tlAlert("Text Level", "Level copied to clipboard as script (" + std::to_string(text.size()) + " chars).");
    }

    void onTLHelp(CCObject*) {
        tlAlertScroll("Text Level", std::string(kHelp) + "\n\nScript file:\n" + tlScriptPath().string());
    }
};
