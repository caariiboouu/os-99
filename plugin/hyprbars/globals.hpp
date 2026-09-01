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

    // What the box says when the cursor rests on it, and what it says while it
    // is LATCHED -- a box that toggles has to name the way back out, not repeat
    // the way in. Empty tooltip means the box explains itself and stays quiet.
    std::string          tooltip       = "";
    std::string          tooltipActive = "";

    // The window state that latches this box: "floating", "pinned", "shaded",
    // "maximized" or "fullscreen". Empty means it never latches -- closing and
    // force-quitting are not states a window can sit in.
    //
    // Read live off the window at draw time and never stored, so a change made
    // by a keybind, another box, or the window itself is reflected without
    // anything having to notify us.
    std::string          activeWhen    = "";

    // The dished-in art, drawn while the box is latched: a latched control is
    // held down, which is how every toggle in this era showed itself. Loaded
    // from <image>_pressed.png and keyed on mtime exactly like imageTex, so a
    // theme switch repaints it without a plugin reload.
    SP<Render::ITexture> imagePressedTex;
    uint64_t             imagePressedStamp = UINT64_MAX;
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
        SP<Config::Values::CBoolValue>       barBlur, barTitleEnabled, barPartOfWindow, barPrecedenceOverBorder, enabled, iconOnHover, barTooltips;
        SP<Config::Values::CStringValue>     barTextFont, barTextAlign, barButtonsAlignment, onDoubleClick, barMenuCommand;
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
