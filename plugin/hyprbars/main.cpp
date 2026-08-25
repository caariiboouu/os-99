#define WLR_USE_UNSTABLE

#include <unistd.h>

#include <any>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/config/shared/parserUtils/ParserUtils.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/desktop/rule/windowRule/WindowRuleEffectContainer.hpp>
#include <hyprland/src/config/lua/bindings/LuaBindingsInternal.hpp>
#include <hyprland/src/config/lua/types/LuaConfigColor.hpp>
#include <hyprland/src/state/MonitorState.hpp>

#include <hyprutils/string/VarList.hpp>

#include <algorithm>

#include "barDeco.hpp"
#include "globals.hpp"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

// Do NOT change this function.
APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

static void onNewWindow(PHLWINDOW window) {
    if (!window->m_X11DoesntWantBorders) {
        if (std::ranges::any_of(window->m_windowDecorations, [](const auto& d) { return d->getDisplayName() == "Hyprbar"; }))
            return;

        auto bar = makeUnique<CHyprBar>(window);
        g_pGlobalState->bars.emplace_back(bar);
        bar->m_self = bar;
        HyprlandAPI::addWindowDecoration(PHANDLE, window, std::move(bar));
    }
}

static void onPreConfigReload() {
    g_pGlobalState->buttons.clear();
}

static void onConfigReloaded() {
    for (auto& b : g_pGlobalState->bars) {
        if (!b)
            continue;

        b->onConfigReloaded();
    }
}

static void onUpdateWindowRules(PHLWINDOW window) {
    const auto BARIT = std::find_if(g_pGlobalState->bars.begin(), g_pGlobalState->bars.end(), [window](const auto& bar) { return bar->getOwner() == window; });

    if (BARIT == g_pGlobalState->bars.end())
        return;

    (*BARIT)->updateRules();
    window->updateWindowDecos();
}

Hyprlang::CParseResult onNewButton(const char* K, const char* V) {
    std::string                 v = V;
    Hyprutils::String::CVarList vars(v);

    Hyprlang::CParseResult      result;

    // hyprbars-button = bgcolor, size, icon, action, fgcolor, image, side
    // image and side are fork additions and both optional.

    if (vars[0].empty() || vars[1].empty()) {
        result.setError("bgcolor and size cannot be empty");
        return result;
    }

    float size = 10;
    try {
        size = std::stof(vars[1]);
    } catch (std::exception& e) {
        result.setError("failed to parse size");
        return result;
    }

    bool userfg  = false;
    auto fgcolor = Config::ParserUtils::parseColor("rgb(ffffff)");
    auto bgcolor = Config::ParserUtils::parseColor(vars[0]);

    if (!bgcolor) {
        result.setError("invalid bgcolor");
        return result;
    }

    if (vars.size() >= 5 && !vars[4].empty()) {
        userfg  = true;
        fgcolor = Config::ParserUtils::parseColor(vars[4]);
    }

    if (!fgcolor) {
        result.setError("invalid fgcolor");
        return result;
    }

    SHyprButton button{vars[3], userfg, *fgcolor, *bgcolor, size, vars[2]};
    if (vars.size() >= 6)
        button.image = vars[5];
    if (vars.size() >= 7)
        button.side = vars[6];

    g_pGlobalState->buttons.push_back(std::move(button));

    for (auto& b : g_pGlobalState->bars) {
        b->m_bButtonsDirty = true;
    }

    return result;
}

// Buttons added through the Lua API do not survive a config reload: hyprbars
// clears the whole button list in onPreConfigReload so that `hyprbars-button`
// keyword lines can be re-parsed without duplicating. Keyword buttons get
// repopulated by that re-parse; runtime ones simply vanish. Since `omarchy
// theme set` ends in `hyprctl reload`, that is exactly why the title bar came
// back with no boxes after every theme switch.
//
// Rather than try to detect the loss, give callers a way to be idempotent:
// clear, then add the set you want, every time.
// Windowshade on the focused window, exposed so it can be bound to a key as
// well as reached through the collapse box:
//     hl.bind("SUPER + S", hl.dsp.exec_cmd("hyprctl eval 'hl.plugin.hyprbars.shade()'"))
// The action lives on the bar rather than on the window because the bar is what
// remembers the pre-shade size and position.
// Minimize the focused window, exposed so it can be bound to a key as well as
// reached through the collapse box:
//     hl.bind("SUPER + M", hl.dsp.exec_cmd("hyprctl eval 'hl.plugin.hyprbars.minimize()'"))
// Restoring is deliberately NOT here. A minimized window has no bar on screen
// to click, so the way back has to be something that can see windows the user
// cannot -- os99-minimize restore, or the bar widget that calls it.
int luaMinimize(lua_State* L) {
    const auto WINDOW = Desktop::focusState()->window();
    if (!WINDOW)
        return 0;

    for (auto& b : g_pGlobalState->bars) {
        if (b && b->getOwner() == WINDOW) {
            b->minimizeWindow();
            break;
        }
    }

    return 0;
}

int luaShade(lua_State* L) {
    const auto WINDOW = Desktop::focusState()->window();
    if (!WINDOW)
        return 0;

    for (auto& b : g_pGlobalState->bars) {
        if (b && b->getOwner() == WINDOW) {
            b->toggleShade();
            break;
        }
    }

    return 0;
}

int clearLuaButtons(lua_State* L) {
    g_pGlobalState->buttons.clear();

    for (auto& b : g_pGlobalState->bars) {
        b->m_bButtonsDirty = true;
    }

    return 0;
}

int newLuaButton(lua_State* L) {
    if (!lua_istable(L, 1))
        return Config::Lua::Bindings::Internal::configError(L, "add_button: expected a table { bg_color, fg_color, size, icon, action, [image], [side] }");

    SHyprButton button;

    {
        Hyprutils::Utils::CScopeGuard x([L] { lua_pop(L, 1); });

        lua_getfield(L, 1, "bg_color");

        Config::Lua::CLuaConfigColor parser(0);
        auto                         err = parser.parse(L);
        if (err.errorCode != Config::Lua::PARSE_ERROR_OK)
            return Config::Lua::Bindings::Internal::configError(L, "add_button: failed to parse bg_color");

        button.bgcol = parser.parsed();
    }

    {
        Hyprutils::Utils::CScopeGuard x([L] { lua_pop(L, 1); });

        lua_getfield(L, 1, "fg_color");

        Config::Lua::CLuaConfigColor parser(0);
        auto                         err = parser.parse(L);
        if (err.errorCode != Config::Lua::PARSE_ERROR_OK)
            return Config::Lua::Bindings::Internal::configError(L, "add_button: failed to parse fg_color");

        button.userfg = true;
        button.fgcol = parser.parsed();
    }

    {
        Hyprutils::Utils::CScopeGuard x([L] { lua_pop(L, 1); });

        lua_getfield(L, 1, "size");

        if (!lua_isnumber(L, -1))
            return Config::Lua::Bindings::Internal::configError(L, "add_button: size must be an integer");

        button.size = lua_tointeger(L, -1);
    }

    {
        Hyprutils::Utils::CScopeGuard x([L] { lua_pop(L, 1); });

        lua_getfield(L, 1, "icon");

        if (!lua_isstring(L, -1))
            return Config::Lua::Bindings::Internal::configError(L, "add_button: icon must be a string");

        button.icon = lua_tostring(L, -1);
    }

    {
        Hyprutils::Utils::CScopeGuard x([L] { lua_pop(L, 1); });

        lua_getfield(L, 1, "action");

        // Optional now that `dispatch` exists, but exactly one of the two must
        // be given -- a button that does nothing is a bug, not a style.
        if (lua_isstring(L, -1))
            button.cmd = lua_tostring(L, -1);
    }

    // Optional fields: absent is fine, so these do not error out like the rest.
    {
        Hyprutils::Utils::CScopeGuard x([L] { lua_pop(L, 1); });
        lua_getfield(L, 1, "image");
        if (lua_isstring(L, -1))
            button.image = lua_tostring(L, -1);
    }

    {
        Hyprutils::Utils::CScopeGuard x([L] { lua_pop(L, 1); });
        lua_getfield(L, 1, "side");
        if (lua_isstring(L, -1))
            button.side = lua_tostring(L, -1);
    }

    {
        Hyprutils::Utils::CScopeGuard x([L] { lua_pop(L, 1); });
        lua_getfield(L, 1, "dispatch");
        if (lua_isstring(L, -1))
            button.dispatch = lua_tostring(L, -1);
    }

    if (button.cmd.empty() && button.dispatch.empty())
        return Config::Lua::Bindings::Internal::configError(L, "add_button: needs either action (a shell command) or dispatch (a native dispatcher name)");

    g_pGlobalState->buttons.push_back(std::move(button));

    for (auto& b : g_pGlobalState->bars) {
        b->m_bButtonsDirty = true;
    }

    return 0;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    const std::string HASH        = __hyprland_api_get_hash();
    const std::string CLIENT_HASH = __hyprland_api_get_client_hash();

    if (HASH != CLIENT_HASH) {
        HyprlandAPI::addNotification(PHANDLE, "[hyprbars] Failure in initialization: Version mismatch (headers ver is not equal to running hyprland ver)",
                                     CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
        throw std::runtime_error("[hb] Version mismatch");
    }

    g_pGlobalState                    = makeUnique<SGlobalState>();
    g_pGlobalState->nobarRuleIdx      = Desktop::Rule::windowEffects()->registerEffect("hyprbars:no_bar");
    g_pGlobalState->barColorRuleIdx   = Desktop::Rule::windowEffects()->registerEffect("hyprbars:bar_color");
    g_pGlobalState->titleColorRuleIdx = Desktop::Rule::windowEffects()->registerEffect("hyprbars:title_color");

    static auto P  = Event::bus()->m_events.window.open.listen([&](PHLWINDOW w) { onNewWindow(w); });
    static auto P3 = Event::bus()->m_events.window.updateRules.listen([&](PHLWINDOW w) { onUpdateWindowRules(w); });

    g_pGlobalState->config.barColor            = makeShared<Config::Values::CColorValue>("plugin:hyprbars:bar_color", "Change the bar color", 0x88333333);
    g_pGlobalState->config.textColor           = makeShared<Config::Values::CColorValue>("plugin:hyprbars:col.text", "Change the text color", 0xffffffff);
    g_pGlobalState->config.inactiveButtonColor = makeShared<Config::Values::CColorValue>(
        "plugin:hyprbars:inactive_button_color", "Change the inactive button's color. 0x00000000 means unset", 0x00000000);
    g_pGlobalState->config.barHeight       = makeShared<Config::Values::CIntValue>("plugin:hyprbars:bar_height", "Change the bar's height", 15);
    g_pGlobalState->config.barTextSize     = makeShared<Config::Values::CIntValue>("plugin:hyprbars:bar_text_size", "Change the bar's text size", 10);
    g_pGlobalState->config.barTextWeight   = makeShared<Config::Values::CFontWeightValue>("plugin:hyprbars:bar_text_weight", "Bar's title text weight (e.g. \"bold\" or an integer 100-1000)", 400);
    g_pGlobalState->config.barTitleEnabled = makeShared<Config::Values::CBoolValue>("plugin:hyprbars:bar_title_enabled", "Whether to enable titles in the bar", true);
    g_pGlobalState->config.barBlur         = makeShared<Config::Values::CBoolValue>("plugin:hyprbars:bar_blur", "Whether to enable blur of the bar", false);
    g_pGlobalState->config.barTextFont     = makeShared<Config::Values::CStringValue>("plugin:hyprbars:bar_text_font", "Bar's text font", "Sans");
    g_pGlobalState->config.barTexture      = makeShared<Config::Values::CStringValue>(
        "plugin:hyprbars:bar_texture", "Nine-patch bar texture path prefix; loads <prefix>_active.png and <prefix>_inactive.png. Empty disables texturing.", "");
    g_pGlobalState->config.barTextureBorder = makeShared<Config::Values::CStringValue>(
        "plugin:hyprbars:bar_texture_border", "Nine-patch border widths in texture px, CSS order: top right bottom left", "0 0 0 0");
    g_pGlobalState->config.barTextAlign    = makeShared<Config::Values::CStringValue>("plugin:hyprbars:bar_text_align", "Bar's text alignment", "center");
    g_pGlobalState->config.frameTexture = makeShared<Config::Values::CStringValue>(
        "plugin:hyprbars:frame_texture", "Nine-patch art for a frame around the WHOLE window (path prefix -> <prefix>_active.png / _inactive.png). Set it and the bar becomes the frame's top edge", "");
    g_pGlobalState->config.frameTextureBorder = makeShared<Config::Values::CStringValue>(
        "plugin:hyprbars:frame_texture_border", "Frame nine-patch borders, \"top right bottom left\" in texture px. Top should match bar_height", "0");
    g_pGlobalState->config.frameInset = makeShared<Config::Values::CStringValue>(
        "plugin:hyprbars:frame_inset", "How much room the frame reserves, \"top right bottom left\" in LOGICAL px. Defaults to frame_texture_border, which is only correct while the art is authored in logical px too", "");
    g_pGlobalState->config.barClearColor = makeShared<Config::Values::CColorValue>(
        "plugin:hyprbars:bar_clear_color", "Colour painted behind the title and the buttons, clearing a patterned bar texture. Fully transparent disables it", 0x00000000);
    g_pGlobalState->config.frameTextureUnscaled = makeShared<Config::Values::CBoolValue>(
        "plugin:hyprbars:frame_texture_unscaled", "Treat frame art as DEVICE pixels and blit it 1:1 instead of scaling by the monitor scale. Kills resampling moire on fractional scales", false);
    g_pGlobalState->config.frameOverWindow = makeShared<Config::Values::CBoolValue>(
        "plugin:hyprbars:frame_over_window", "Draw the frame ABOVE the client instead of under it, so the innermost pixels of the bevel are not covered by the surface", false);
    g_pGlobalState->config.frameForceSquare = makeShared<Config::Values::CBoolValue>(
        "plugin:hyprbars:frame_force_square", "While a frame texture is set, hold window rounding at 0. A nine-patch frame paints square corners and rounding clips them", true);
    g_pGlobalState->config.frameForceFlat = makeShared<Config::Values::CBoolValue>(
        "plugin:hyprbars:frame_force_flat", "While a frame texture is set, disable blur and inactive-dim on framed windows. Platinum chrome is flat and opaque", true);
    g_pGlobalState->config.barTextClearPad = makeShared<Config::Values::CIntValue>(
        "plugin:hyprbars:bar_text_clear_pad", "Padding either side of the title for bar_clear_color", 0);
    g_pGlobalState->config.barButtonClearPad = makeShared<Config::Values::CIntValue>(
        "plugin:hyprbars:bar_button_clear_pad", "Padding around each button for bar_clear_color", 0);
    g_pGlobalState->config.barClearInsetTop = makeShared<Config::Values::CIntValue>(
        "plugin:hyprbars:bar_clear_inset_top", "Rows at the TOP of the bar that bar_clear_color must not touch -- they belong to the frame bezel", 0);
    g_pGlobalState->config.barClearInsetBottom = makeShared<Config::Values::CIntValue>(
        "plugin:hyprbars:bar_clear_inset_bottom", "Rows at the BOTTOM of the bar that bar_clear_color must not touch", 0);
    g_pGlobalState->config.barPartOfWindow =
        makeShared<Config::Values::CBoolValue>("plugin:hyprbars:bar_part_of_window", "Whether the bar is a part of the window (reserves space)", true);
    g_pGlobalState->config.barPrecedenceOverBorder =
        makeShared<Config::Values::CBoolValue>("plugin:hyprbars:bar_precedence_over_border", "Whether the bar is before, or after the border", false);
    g_pGlobalState->config.barButtonsAlignment = makeShared<Config::Values::CStringValue>("plugin:hyprbars:bar_buttons_alignment", "Alignment of the bar buttons", "right");
    g_pGlobalState->config.barPadding          = makeShared<Config::Values::CIntValue>("plugin:hyprbars:bar_padding", "Padding of the bar", 7);
    g_pGlobalState->config.barButtonPadding    = makeShared<Config::Values::CIntValue>("plugin:hyprbars:bar_button_padding", "Padding of the bar buttons", 5);
    // Default OFF. The bars belong to an OS 99 theme, and that theme's hyprland.lua
    // re-applies bars.lua (which sets enabled = true) on every config load. Any
    // other theme sets nothing, so the bars stay off -- including across a
    // `hyprctl reload`, which is where a default of true resurrected them.
    g_pGlobalState->config.enabled             = makeShared<Config::Values::CBoolValue>("plugin:hyprbars:enabled", "Whether bars are enabled", false);
    g_pGlobalState->config.iconOnHover         = makeShared<Config::Values::CBoolValue>("plugin:hyprbars:icon_on_hover", "Whether to use an icon on hover of the buttons", false);
    g_pGlobalState->config.onDoubleClick       = makeShared<Config::Values::CStringValue>("plugin:hyprbars:on_double_click", "Action to execute on double click of the bar", "");

    HyprlandAPI::addConfigValueV2(PHANDLE, g_pGlobalState->config.barColor);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_pGlobalState->config.textColor);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_pGlobalState->config.inactiveButtonColor);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_pGlobalState->config.barHeight);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_pGlobalState->config.barTextSize);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_pGlobalState->config.barTextWeight);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_pGlobalState->config.barTitleEnabled);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_pGlobalState->config.barBlur);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_pGlobalState->config.barTextFont);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_pGlobalState->config.barTexture);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_pGlobalState->config.barTextureBorder);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_pGlobalState->config.barTextAlign);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_pGlobalState->config.frameTexture);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_pGlobalState->config.frameTextureBorder);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_pGlobalState->config.frameInset);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_pGlobalState->config.barClearColor);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_pGlobalState->config.frameTextureUnscaled);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_pGlobalState->config.frameOverWindow);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_pGlobalState->config.frameForceSquare);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_pGlobalState->config.frameForceFlat);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_pGlobalState->config.barTextClearPad);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_pGlobalState->config.barButtonClearPad);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_pGlobalState->config.barClearInsetTop);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_pGlobalState->config.barClearInsetBottom);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_pGlobalState->config.barPartOfWindow);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_pGlobalState->config.barPrecedenceOverBorder);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_pGlobalState->config.barButtonsAlignment);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_pGlobalState->config.barPadding);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_pGlobalState->config.barButtonPadding);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_pGlobalState->config.enabled);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_pGlobalState->config.iconOnHover);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_pGlobalState->config.onDoubleClick);

    if (Config::mgr()->type() == Config::CONFIG_LEGACY)
        HyprlandAPI::addConfigKeyword(PHANDLE, "plugin:hyprbars:hyprbars-button", onNewButton, Hyprlang::SHandlerOptions{});
    else
        HyprlandAPI::addLuaFunction(PHANDLE, "hyprbars", "add_button", ::newLuaButton);
        HyprlandAPI::addLuaFunction(PHANDLE, "hyprbars", "clear_buttons", ::clearLuaButtons);
        HyprlandAPI::addLuaFunction(PHANDLE, "hyprbars", "shade", ::luaShade);
        HyprlandAPI::addLuaFunction(PHANDLE, "hyprbars", "minimize", ::luaMinimize);
    static auto P4 = Event::bus()->m_events.config.preReload.listen([&] { onPreConfigReload(); });
    static auto P5 = Event::bus()->m_events.config.reloaded.listen([&] { onConfigReloaded(); });

    // add deco to existing windows
    for (auto& w : Desktop::windowState()->windows()) {
        if (w->isHidden() || !w->m_isMapped)
            continue;

        onNewWindow(w);
    }

    HyprlandAPI::reloadConfig();

    return {"hyprbars", "A plugin to add title bars to windows.", "Vaxry", "1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    // ORDER MATTERS. Raise the flag before touching anything else: from here on
    // a render pass may fire at any moment, and the only safe thing for it to do
    // is nothing. removeAllOfType() alone was not enough, because the
    // DECORATIONS survive it and simply queue a fresh element on the next frame.
    if (g_pGlobalState)
        g_pGlobalState->shuttingDown = true;

    for (auto& m : State::monitorState()->monitors())
        m->m_scheduledRecalc = true;

    g_pHyprRenderer->m_renderPass.removeAllOfType("CBarPassElement");

    // REMOVE THE DECORATIONS. This is not optional, and an earlier version of
    // this file wrongly claimed it was.
    //
    // Each CHyprBar holds event-bus listeners -- mouse button, mouse move, three
    // touch handlers -- whose lambdas live in THIS shared object. Hyprland does
    // not destroy them for us on unload, so if the bars outlive the plugin those
    // listeners keep firing into memory that is no longer mapped. The crash has
    // no plugin frames at all, just a jump to an address belonging to no object:
    //     #7  0x00007f...500 n/a (n/a + 0x0)
    // which is what makes it look like a mystery rather than a use-after-unload.
    //
    // THE ORDER MATTERS. Removing a decoration destroys the CHyprBar, and
    // ~CHyprBar does std::erase(g_pGlobalState->bars, m_self). Iterating the
    // live vector while that happens invalidates the iterator underneath us and
    // aborts inside SGlobalState's deleter -- a worse crash than this one, and
    // the reason the removal was taken out the first time. Copy the handles,
    // clear the live list so that erase becomes a no-op, then remove.
    auto bars = g_pGlobalState->bars;
    g_pGlobalState->bars.clear();
    for (auto& b : bars) {
        if (b)
            HyprlandAPI::removeWindowDecoration(PHANDLE, b.get());
    }

    // And hand back the window-rule effects, which are keyed on indices this
    // plugin owns. Leaving them registered after unload is the same class of
    // dangling reference as leaving the decorations alive.
    Desktop::Rule::windowEffects()->unregisterEffect(g_pGlobalState->barColorRuleIdx);
    Desktop::Rule::windowEffects()->unregisterEffect(g_pGlobalState->titleColorRuleIdx);
    Desktop::Rule::windowEffects()->unregisterEffect(g_pGlobalState->nobarRuleIdx);
}
