#pragma once

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/render/Texture.hpp>
#include <hyprland/src/config/values/types/BoolValue.hpp>
#include <hyprland/src/config/values/types/IntValue.hpp>
#include <hyprland/src/config/values/types/StringValue.hpp>
#include <hyprland/src/config/values/types/ColorValue.hpp>
#include <hyprland/src/config/values/types/FontWeightValue.hpp>

inline HANDLE PHANDLE = nullptr;

struct SHyprButton {
    std::string          cmd     = "";
    bool                 userfg  = false;
    CHyprColor           fgcol   = CHyprColor(0, 0, 0, 0);
    CHyprColor           bgcol   = CHyprColor(0, 0, 0, 0);
    float                size    = 10;
    std::string          icon    = "";
    SP<Render::ITexture> iconTex;

    // Prebaked art for the button. When set, it replaces BOTH the rounded rect
    // and the glyph: a pixel-art box cannot be assembled from a rounded rect
    // and a font character.
    std::string          image   = "";
    SP<Render::ITexture> imageTex;
    // Keyed on the file's mtime, not a one-shot "tried" flag: a theme switch
    // rewrites the art in place and the button must follow it without needing
    // the plugin reloaded (which is the one operation that can take the
    // compositor down).
    uint64_t             imageStamp = UINT64_MAX;

    // A NATIVE dispatcher name, run in-process instead of shelling out.
    //
    // `cmd` shells out, which costs a process per click and -- worse -- acts on
    // whatever window is ACTIVE, not the one whose button was pressed. It also
    // cannot express arguments the Lua layer does not expose: Hyprland's
    // fullscreen binding ignores its mode argument, so there is no way to ask
    // for FSMODE_MAXIMIZED (a maximise that keeps the decorations) from a
    // string. A title-bar button that can only go true-fullscreen hides its own
    // title bar and leaves no way back.
    //
    // One of "close", "kill", "maximize", "fullscreen", "float", "pin".
    std::string          dispatch = "";

    // "left" / "right", or empty to follow bar_buttons_alignment. OS 9 puts the
    // close box alone on the left and collapse+zoom on the right, which upstream
    // cannot express -- it only has one global side.
    std::string          side    = "";
};

class CHyprBar;

struct SGlobalState {
    // Set the instant PLUGIN_EXIT begins. Everything that runs inside a render
    // pass checks it, because the render pass can fire between the plugin's
    // config values being torn down and the decorations actually going away.
    bool                      shuttingDown = false;
    std::vector<SHyprButton>  buttons;
    std::vector<WP<CHyprBar>> bars;
    uint32_t                  nobarRuleIdx      = 0;
    uint32_t                  barColorRuleIdx   = 0;
    uint32_t                  titleColorRuleIdx = 0;

    struct {
        SP<Config::Values::CColorValue>      barColor, textColor, inactiveButtonColor;
        SP<Config::Values::CIntValue>        barHeight;
        SP<Config::Values::CIntValue>        barTextSize;
        SP<Config::Values::CFontWeightValue> barTextWeight;
        SP<Config::Values::CIntValue>        barPadding;
        SP<Config::Values::CIntValue>        barButtonPadding;
        SP<Config::Values::CBoolValue>       barBlur, barTitleEnabled, barPartOfWindow, barPrecedenceOverBorder, enabled, iconOnHover;
        SP<Config::Values::CStringValue>     barTextFont, barTextAlign, barButtonsAlignment, onDoubleClick;
        SP<Config::Values::CStringValue>     barTexture, barTextureBorder;
        SP<Config::Values::CStringValue>     frameTexture, frameTextureBorder, frameInset;
        SP<Config::Values::CColorValue>      barClearColor;
        SP<Config::Values::CBoolValue>       frameTextureUnscaled, frameOverWindow, frameForceSquare, frameForceFlat;
        SP<Config::Values::CIntValue>        barTextClearPad, barButtonClearPad;
        SP<Config::Values::CIntValue>        barClearInsetTop, barClearInsetBottom;
    } config;
};

inline UP<SGlobalState> g_pGlobalState;

// EVERY entry point Hyprland can call on a bar has to ask this first.
//
// Once PLUGIN_EXIT begins, this plugin's config values are destroyed, but the
// decorations can still be called -- to render, to be positioned, to be damaged
// -- and every one of those paths reads config.<something>->value(). Reading a
// destroyed value THROWS, and these are called from inside Hyprland's render
// and layout passes, where an escaping exception is std::terminate.
//
// Three separate crashes traced back to exactly this, at three different call
// sites: CIntValue::value() from renderPass, and CStringValue::value() from
// frameMetrics. Guarding one site at a time is how you get the third one.
inline bool barsShuttingDown() {
    return !g_pGlobalState || g_pGlobalState->shuttingDown;
}
