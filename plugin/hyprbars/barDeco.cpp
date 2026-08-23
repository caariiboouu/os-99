#include "barDeco.hpp"
#include <hyprland/src/config/shared/actions/ConfigActions.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include "ninePatch.hpp"

#include <sys/stat.h>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/desktop/state/LayerState.hpp>
#include <hyprland/src/desktop/state/ViewHitTester.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/desktop/view/LayerSurface.hpp>
#include <hyprland/src/helpers/MiscFunctions.hpp>
#include <hyprland/src/managers/SeatManager.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/config/shared/animation/AnimationTree.hpp>
#include <hyprland/src/config/shared/parserUtils/ParserUtils.hpp>
#include <hyprland/src/config/supplementary/executor/Executor.hpp>
#include <hyprland/src/config/shared/actions/ConfigActions.hpp>
#include <hyprland/src/animation/AnimationManager.hpp>
#include <hyprland/src/protocols/LayerShell.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/layout/LayoutManager.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/state/MonitorState.hpp>


#include "globals.hpp"
#include "BarPassElement.hpp"

#include <climits>

using namespace Render::GL;

static CHyprColor configColor(Config::INTEGER color) {
    return CHyprColor{static_cast<uint64_t>(color)};
}

CHyprBar::CHyprBar(PHLWINDOW pWindow) : IHyprWindowDecoration(pWindow) {
    m_pWindow = pWindow;

    const auto PMONITOR         = pWindow->m_monitor.lock();
    PMONITOR->m_scheduledRecalc = true;

    // button events
    m_pMouseButtonCallback = Event::bus()->m_events.input.mouse.button.listen([&](IPointer::SButtonEvent e, Event::SCallbackInfo& info) { onMouseButton(info, e); });
    m_pTouchDownCallback   = Event::bus()->m_events.input.touch.down.listen([&](ITouch::SDownEvent e, Event::SCallbackInfo& info) { onTouchDown(info, e); });
    m_pTouchUpCallback     = Event::bus()->m_events.input.touch.up.listen([&](ITouch::SUpEvent e, Event::SCallbackInfo& info) { onTouchUp(info, e); });

    // move events
    m_pTouchMoveCallback = Event::bus()->m_events.input.touch.motion.listen([&](ITouch::SMotionEvent e, Event::SCallbackInfo& info) { onTouchMove(info, e); });
    m_pMouseMoveCallback = Event::bus()->m_events.input.mouse.move.listen([&](Vector2D c, Event::SCallbackInfo& info) { onMouseMove(c); });

    Animation::mgr()->createAnimation(configColor(g_pGlobalState->config.barColor->value()), m_cRealBarColor, Config::animationTree()->getAnimationPropertyConfig("border"),
                                      pWindow, AVARDAMAGE_NONE);
    m_cRealBarColor->setUpdateCallback([&](auto) { damageEntire(); });

    syncFrameRounding();
}

CHyprBar::~CHyprBar() {
    // Hand rounding back before we go, or unloading the plugin leaves every
    // window square with nothing left to explain why.
    if (m_bForcedSquare || m_bForcedFlat) {
        const auto PWINDOW = m_pWindow.lock();
        if (PWINDOW && PWINDOW->m_ruleApplicator) {
            if (m_bForcedSquare)
                PWINDOW->m_ruleApplicator->rounding().unset(Desktop::Types::PRIORITY_WINDOW_RULE);
            if (m_bForcedFlat) {
                PWINDOW->m_ruleApplicator->noBlur().unset(Desktop::Types::PRIORITY_WINDOW_RULE);
                PWINDOW->m_ruleApplicator->noDim().unset(Desktop::Types::PRIORITY_WINDOW_RULE);
            }
        }
    }

    std::erase(g_pGlobalState->bars, m_self);
}

static struct SFrameMetrics frameMetrics();

// Frame mode. The decoration reserves a ring around the whole window and draws
// one nine-patch into it, so the bezel runs unbroken from the title bar down
// the sides and across the bottom -- which is what an OS 9 window frame is.
// Hyprland's own border cannot do this: it interpolates a single gradient
// across the entire window, so it can never place a crisp 1px highlight along
// the top-left and a 1px shadow along the bottom-right.
struct SFrameMetrics {
    bool on = false;
    int  top = 0, right = 0, bottom = 0, left = 0;
};

// Rounded corners and a nine-patch frame are mutually exclusive. The frame
// paints hard square corners into the ring it reserves; Hyprland then rounds the
// window on top of that and clips exactly the corner pixels the bezel is made
// of, so the frame stops looking like one continuous piece.
//
// This used to be fixed OUTSIDE the plugin, in the user's own looknfeel.lua,
// which meant the whole look depended on a file no theme can ship -- a theme's
// hyprland.lua is loaded BEFORE looknfeel.lua, so the user's value wins. Owning
// it here is what lets the theme be installed rather than assembled by hand.
//
// PRIORITY_WINDOW_RULE, deliberately: it beats the global decoration:rounding,
// but still loses to `hyprctl setprop`, so there is an escape hatch per window.
// We only ever unset a priority we set ourselves (m_bForcedSquare), so a user's
// own rounding windowrule is not clobbered on the way out.
void CHyprBar::syncFrameRounding() {
    if (barsShuttingDown())
        return;

    const auto PWINDOW = m_pWindow.lock();
    if (!PWINDOW || !PWINDOW->m_ruleApplicator)
        return;

    const bool FRAMED = frameMetrics().on;

    // Corners. Rounding CLIPS the frame, so this one is correctness, not taste.
    const bool WANTSQUARE = FRAMED && g_pGlobalState->config.frameForceSquare->value();
    if (WANTSQUARE != m_bForcedSquare) {
        if (WANTSQUARE)
            PWINDOW->m_ruleApplicator->rounding().set(0, Desktop::Types::PRIORITY_WINDOW_RULE);
        else
            PWINDOW->m_ruleApplicator->rounding().unset(Desktop::Types::PRIORITY_WINDOW_RULE);
        m_bForcedSquare = WANTSQUARE;
    }

    // Blur and inactive-dim. These do not break the frame, they just are not
    // Platinum: OS 9 chrome is flat and opaque, and it signalled focus with the
    // title-bar texture rather than by dimming the whole window. Separate key
    // from the rounding one precisely because this half is taste -- someone who
    // wants the frame but likes blur can keep it.
    //
    // Per-window rules, not the global decoration settings, so the theme stops
    // depending on the user's looknfeel.lua, which loads after the theme's own
    // hyprland.lua and would otherwise win.
    const bool WANTFLAT = FRAMED && g_pGlobalState->config.frameForceFlat->value();
    if (WANTFLAT != m_bForcedFlat) {
        if (WANTFLAT) {
            PWINDOW->m_ruleApplicator->noBlur().set(true, Desktop::Types::PRIORITY_WINDOW_RULE);
            PWINDOW->m_ruleApplicator->noDim().set(true, Desktop::Types::PRIORITY_WINDOW_RULE);
        } else {
            PWINDOW->m_ruleApplicator->noBlur().unset(Desktop::Types::PRIORITY_WINDOW_RULE);
            PWINDOW->m_ruleApplicator->noDim().unset(Desktop::Types::PRIORITY_WINDOW_RULE);
        }
        m_bForcedFlat = WANTFLAT;
    }
}

static SFrameMetrics frameMetrics() {
    if (barsShuttingDown())
        return {};

    SFrameMetrics     metrics;
    const std::string PREFIX = g_pGlobalState->config.frameTexture->value();
    if (PREFIX.empty())
        return metrics;

    // These are the LOGICAL insets: how much room the frame reserves and how
    // far frameBoxGlobal() reaches past the client. They are NOT the same
    // numbers as frame_texture_border once the art is authored at device
    // resolution -- that spec is in device pixels and describes where to SLICE
    // the png, nothing more. Conflating the two fed device-sized values into
    // the positioner and corrupted the heap.
    const std::string INSET = g_pGlobalState->config.frameInset->value();
    parseBorders(INSET.empty() ? g_pGlobalState->config.frameTextureBorder->value() : INSET, metrics.top, metrics.right, metrics.bottom, metrics.left);
    // A frame with no top band is not a frame; treat it as unconfigured rather
    // than reserving a ring the art cannot fill.
    metrics.on = metrics.top > 0;
    return metrics;
}

SDecorationPositioningInfo CHyprBar::getPositioningInfo() {
    if (barsShuttingDown())
        return SDecorationPositioningInfo{};

    const auto                 HEIGHT     = g_pGlobalState->config.barHeight->value();
    const auto                 ENABLED    = g_pGlobalState->config.enabled->value();
    const auto                 PRECEDENCE = g_pGlobalState->config.barPrecedenceOverBorder->value();

    const auto                 FRAME      = frameMetrics();

    SDecorationPositioningInfo info;

    // ABSOLUTE is the only policy that takes more than one edge -- STICKY is
    // documented as one edge only -- so the whole-window frame has to use it.
    if (FRAME.on && !m_hidden && ENABLED) {
        info.policy   = DECORATION_POSITION_ABSOLUTE;
        info.edges    = DECORATION_EDGE_TOP | DECORATION_EDGE_BOTTOM | DECORATION_EDGE_LEFT | DECORATION_EDGE_RIGHT;
        info.priority = PRECEDENCE ? 10005 : 5000;
        info.reserved = true;
        // The top band IS the title bar, so it is bar_height deep, not the
        // art's top border. The art border only says where to slice the png.
        info.desiredExtents = {{(double)FRAME.left, (double)HEIGHT}, {(double)FRAME.right, (double)FRAME.bottom}};
        return info;
    }

    info.policy         = m_hidden ? DECORATION_POSITION_ABSOLUTE : DECORATION_POSITION_STICKY;
    info.edges          = DECORATION_EDGE_TOP;
    info.priority       = PRECEDENCE ? 10005 : 5000;
    info.reserved       = true;
    info.desiredExtents = {{0, m_hidden || !ENABLED ? 0 : HEIGHT}, {0, 0}};
    return info;
}

// Reload the art whenever the configured path or borders change. Loading here
// (rather than at config-parse time) keeps it on the render thread, which is
// where the GL context is current.
static void ensureNinePatches(const std::string& prefix, const std::string& borderSpec) {
    static std::string cachedKey;

    // Key on mtime as well as path: editing the art in place leaves the path
    // identical, and keying on path alone would serve a stale texture forever.
    struct stat st {};
    std::string  stamp;
    for (const auto& suffix : {"_active.png", "_inactive.png"}) {
        if (::stat((prefix + suffix).c_str(), &st) == 0)
            stamp += "|" + std::to_string(st.st_mtime) + ":" + std::to_string(st.st_size);
    }

    const std::string key = prefix + "|" + borderSpec + stamp;
    if (key == cachedKey)
        return;
    cachedKey = key;

    int t = 0, r = 0, b = 0, l = 0;
    parseBorders(borderSpec, t, r, b, l);
    loadNinePatch(g_ninePatchActive, prefix + "_active.png", t, r, b, l);
    loadNinePatch(g_ninePatchInactive, prefix + "_inactive.png", t, r, b, l);
}

// Same idea for the frame art, with its own cache key so bar and frame can be
// reloaded independently.
static void ensureFrameNinePatches(const std::string& prefix, const std::string& borderSpec) {
    static std::string cachedKey;

    struct stat st {};
    std::string stamp;
    for (const auto& suffix : {"_active.png", "_inactive.png"}) {
        if (::stat((prefix + suffix).c_str(), &st) == 0)
            stamp += "|" + std::to_string(st.st_mtime) + ":" + std::to_string(st.st_size);
    }

    const std::string key = prefix + "|" + borderSpec + stamp;
    if (key == cachedKey)
        return;
    cachedKey = key;

    int t = 0, r = 0, b = 0, l = 0;
    parseBorders(borderSpec, t, r, b, l);
    loadNinePatch(g_ninePatchFrameActive, prefix + "_active.png", t, r, b, l);
    loadNinePatch(g_ninePatchFrameInactive, prefix + "_inactive.png", t, r, b, l);
}

void CHyprBar::onPositioningReply(const SDecorationPositioningReply& reply) {
    if (barsShuttingDown())
        return;

    if (reply.assignedGeometry.size() != m_bAssignedBox.size())
        m_bWindowSizeChanged = true;

    m_bAssignedBox = reply.assignedGeometry;
}

std::string CHyprBar::getDisplayName() {
    return "Hyprbar";
}

bool CHyprBar::inputIsValid() {
    if (m_hidden)
        return false;

    if (g_pSeatManager->m_seatGrab && !g_pSeatManager->m_seatGrab->accepts(m_pWindow->wlSurface()->resource()))
        return false;

    const auto MOUSE    = g_pInputManager->getMouseCoordsInternal();
    auto       PMONITOR = Desktop::focusState()->monitor();

    if (!PMONITOR)
        return false;

    Desktop::CViewHitTester hitTester{*Desktop::viewState()};

    const auto              WINDOWATCURSOR = hitTester.windowAt(MOUSE, Desktop::View::RESERVED_EXTENTS | Desktop::View::INPUT_EXTENTS | Desktop::View::ALLOW_FLOATING);

    auto                    focusState = Desktop::focusState();
    auto                    window     = focusState->window();

    if (WINDOWATCURSOR != m_pWindow && m_pWindow != window)
        return false;

    PHLLS    foundSurface = nullptr;
    Vector2D surfaceCoords;

    // Check Top Layer
    hitTester.layerSurfaceAt(MOUSE, &PMONITOR->m_layerSurfaceLayers[ZWLR_LAYER_SHELL_V1_LAYER_TOP], &surfaceCoords, &foundSurface);
    if (foundSurface)
        return false;

    // Check Overlay Layer
    hitTester.layerSurfaceAt(MOUSE, &PMONITOR->m_layerSurfaceLayers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY], &surfaceCoords, &foundSurface);
    if (foundSurface)
        return false;

    return true;
}

void CHyprBar::onMouseButton(Event::SCallbackInfo& info, IPointer::SButtonEvent e) {
    if (!inputIsValid())
        return;

    if (e.state != WL_POINTER_BUTTON_STATE_PRESSED) {
        handleUpEvent(info);
        return;
    }

    handleDownEvent(info, std::nullopt);
}

void CHyprBar::onTouchDown(Event::SCallbackInfo& info, ITouch::SDownEvent e) {
    // Don't do anything if you're already grabbed a window with another finger
    if (!inputIsValid() || e.touchID != 0)
        return;

    handleDownEvent(info, e);
}

void CHyprBar::onTouchUp(Event::SCallbackInfo& info, ITouch::SUpEvent e) {
    if (!m_bDragPending || !m_bTouchEv || e.touchID != m_touchId)
        return;

    handleUpEvent(info);
}

void CHyprBar::onMouseMove(Vector2D coords) {
    if (barsShuttingDown())
        return;

    // ensure proper redraws of button icons on hover when using hardware cursors
    if (g_pGlobalState->config.iconOnHover->value())
        damageOnButtonHover();

    if (!m_bDragPending || m_bTouchEv || !validMapped(m_pWindow) || m_touchId != 0)
        return;

    m_bDragPending = false;
    handleMovement();
}

void CHyprBar::onTouchMove(Event::SCallbackInfo& info, ITouch::SMotionEvent e) {
    if (!m_bDragPending || !m_bTouchEv || !validMapped(m_pWindow) || e.touchID != m_touchId)
        return;

    auto PMONITOR     = m_pWindow->m_monitor.lock();
    PMONITOR          = PMONITOR ? PMONITOR : Desktop::focusState()->monitor();
    const auto COORDS = Vector2D(PMONITOR->m_position.x + e.pos.x * PMONITOR->m_size.x, PMONITOR->m_position.y + e.pos.y * PMONITOR->m_size.y);

    if (!m_bDraggingThis) {
        // Initial setup for dragging a window.
        g_pKeybindManager->m_dispatchers["setfloating"]("activewindow");
        g_pKeybindManager->m_dispatchers["resizewindowpixel"]("exact 50% 50%,activewindow");
        // pin it so you can change workspaces while dragging a window
        g_pKeybindManager->m_dispatchers["pin"]("activewindow");
    }
    g_pKeybindManager->m_dispatchers["movewindowpixel"](std::format("exact {} {},activewindow", (int)(COORDS.x - (assignedBoxGlobal().w / 2)), (int)COORDS.y));
    m_bDraggingThis = true;
}

void CHyprBar::handleDownEvent(Event::SCallbackInfo& info, std::optional<ITouch::SDownEvent> touchEvent) {
    if (barsShuttingDown())
        return;

    m_bTouchEv = touchEvent.has_value();
    if (m_bTouchEv)
        m_touchId = touchEvent.value().touchID;

    const auto PWINDOW = m_pWindow.lock();

    auto       COORDS = cursorRelativeToBar();
    if (m_bTouchEv) {
        ITouch::SDownEvent e        = touchEvent.value();
        PHLMONITOR         PMONITOR = nullptr;
        for (auto& m : State::monitorState()->monitors()) {
            if (m->m_name == (!e.device->m_boundOutput.empty() ? e.device->m_boundOutput : "")) {
                PMONITOR = m;
                break;
            }
        }
        PMONITOR = PMONITOR ? PMONITOR : Desktop::focusState()->monitor();
        COORDS   = Vector2D(PMONITOR->m_position.x + e.pos.x * PMONITOR->m_size.x, PMONITOR->m_position.y + e.pos.y * PMONITOR->m_size.y) - assignedBoxGlobal().pos();
    }

    const auto HEIGHT           = g_pGlobalState->config.barHeight->value();
    const auto BARBUTTONPADDING = g_pGlobalState->config.barButtonPadding->value();
    const auto BARPADDING       = g_pGlobalState->config.barPadding->value();
    const auto ALIGNBUTTONS     = g_pGlobalState->config.barButtonsAlignment->value();
    const auto ON_DOUBLE_CLICK  = g_pGlobalState->config.onDoubleClick->value();

    const bool BUTTONSRIGHT = ALIGNBUTTONS != "left";

    if (!VECINRECT(COORDS, 0, 0, assignedBoxGlobal().w, HEIGHT - 1)) {

        if (m_bDraggingThis) {
            if (m_bTouchEv)
                g_pKeybindManager->m_dispatchers["settiled"]("activewindow");
            g_pKeybindManager->m_dispatchers["mouse"]("0movewindow");
            Log::logger->log(Log::DEBUG, "[hyprbars] Dragging ended on {:x}", (uintptr_t)PWINDOW.get());
        }

        m_bDraggingThis = false;
        m_bDragPending  = false;
        m_bTouchEv      = false;
        return;
    }

    if (Desktop::focusState()->window() != PWINDOW)
        Desktop::focusState()->fullWindowFocus(PWINDOW, Desktop::FOCUS_REASON_CLICK);

    if (PWINDOW->m_isFloating)
        Desktop::windowState()->raise(PWINDOW);

    info.cancelled   = true;
    m_bCancelledDown = true;

    if (doButtonPress(BARPADDING, BARBUTTONPADDING, HEIGHT, COORDS, BUTTONSRIGHT))
        return;

    if (!ON_DOUBLE_CLICK.empty() &&
        std::chrono::duration_cast<std::chrono::milliseconds>(Time::steadyNow() - m_lastMouseDown).count() < 400 /* Arbitrary delay I found suitable */) {
        Config::Supplementary::executor()->spawn(ON_DOUBLE_CLICK);
        m_bDragPending = false;
    } else {
        m_lastMouseDown = Time::steadyNow();
        m_bDragPending  = true;
    }
}

void CHyprBar::handleUpEvent(Event::SCallbackInfo& info) {
    if (m_pWindow.lock() != Desktop::focusState()->window())
        return;

    if (m_bCancelledDown)
        info.cancelled = true;

    m_bCancelledDown = false;

    if (m_bDraggingThis) {
        g_pKeybindManager->changeMouseBindMode(MBIND_INVALID);
        m_bDraggingThis = false;
        if (m_bTouchEv)
            (void)Config::Actions::floatWindow(Config::Actions::eTogglableAction::TOGGLE_ACTION_DISABLE);

        Log::logger->log(Log::DEBUG, "[hyprbars] Dragging ended on {:x}", (uintptr_t)m_pWindow.lock().get());
    }

    m_bDragPending = false;
    m_bTouchEv     = false;
    m_touchId      = 0;
}

void CHyprBar::handleMovement() {
    g_pKeybindManager->changeMouseBindMode(MBIND_MOVE);
    m_bDraggingThis = true;
    Log::logger->log(Log::DEBUG, "[hyprbars] Dragging initiated on {:x}", (uintptr_t)m_pWindow.lock().get());
    return;
}

// The small set of actions a title-bar box can sensibly perform, called in
// process so they can be aimed at this window and given arguments the Lua
// binding does not expose.
//
// "maximize" is the reason this exists. Hyprland's fullscreen dispatcher takes
// an eFullscreenMode, but the Lua binding ignores the argument -- every form of
// hl.dsp.window.fullscreen(...) lands on FSMODE_FULLSCREEN, which strips the
// decorations. A title-bar button that hides its own title bar cannot be
// clicked again to undo itself. FSMODE_MAXIMIZED keeps the frame, so the box
// stays reachable and the action stays reversible.
void CHyprBar::runNativeDispatch(const std::string& what) {
    const auto PWINDOW = m_pWindow.lock();
    if (!PWINDOW)
        return;

    using namespace Config::Actions;

    // These return std::expected and are [[nodiscard]] for a reason: an action
    // can legitimately refuse (a window that cannot float, say). Swallowing that
    // is how a button becomes mysteriously inert, which is exactly the failure
    // this whole change is fixing -- so say something.
    const auto REPORT = [&what](ActionResult r) {
        if (!r)
            Log::logger->log(Log::ERR, "[hyprbars] button dispatch '{}' failed: {}", what, r.error().message);
    };

    if (what == "close")
        REPORT(closeWindow(PWINDOW));
    else if (what == "kill")
        REPORT(killWindow(PWINDOW));
    else if (what == "maximize")
        REPORT(fullscreenWindow(Fullscreen::FSMODE_MAXIMIZED, true, PWINDOW));
    else if (what == "fullscreen")
        REPORT(fullscreenWindow(Fullscreen::FSMODE_FULLSCREEN, true, PWINDOW));
    else if (what == "float")
        REPORT(floatWindow(TOGGLE_ACTION_TOGGLE, PWINDOW));
    else if (what == "pin")
        REPORT(pinWindow(TOGGLE_ACTION_TOGGLE, PWINDOW));
    else
        Log::logger->log(Log::ERR, "[hyprbars] unknown button dispatch '{}'", what);
}

bool CHyprBar::doButtonPress(Config::INTEGER barPadding, Config::INTEGER barButtonPadding, Config::INTEGER barHeight, Vector2D COORDS, const bool BUTTONSRIGHT) {
    // Hit testing reads the same slots the renderer draws, so a box is always
    // clickable exactly where it appears.
    for (const auto& SLOT : buttonSlots(Vector2D{(double)(int)assignedBoxGlobal().w, (double)barHeight}, 1.F)) {
        const auto& BOX = SLOT.box;

        if (VECINRECT(COORDS, BOX.x, BOX.y, BOX.x + BOX.w, BOX.y + BOX.h)) {
            const auto& BUTTON = g_pGlobalState->buttons[SLOT.index];

            // Native dispatchers act on THIS bar's window, not on whatever
            // happens to be active. That matters: clicking the close box of an
            // unfocused window should close that window, and shelling out to
            // `hyprctl dispatch` cannot express it.
            if (!BUTTON.dispatch.empty()) {
                runNativeDispatch(BUTTON.dispatch);
                return true;
            }

            g_pKeybindManager->m_dispatchers["exec"](BUTTON.cmd);
            return true;
        }
    }
    return false;
}

void CHyprBar::renderBarTitle(const Vector2D& bufferSize, const float scale) {
    const auto COLORVAL = g_pGlobalState->config.textColor->value();
    const auto SIZE     = g_pGlobalState->config.barTextSize->value();
    const auto WEIGHT   = g_pGlobalState->config.barTextWeight->value();
    const auto FONT     = g_pGlobalState->config.barTextFont->value();

    const int  scaledSize = std::round(SIZE * scale);
    const auto REGION     = titleRegion(bufferSize, scale);
    const int  maxWidth   = static_cast<int>(REGION.w);

    if (m_szLastTitle.empty() || maxWidth < 1 || scaledSize < 1) {
        m_pTextTex = nullptr;
        return;
    }

    const CHyprColor COLOR = m_bForcedTitleColor.value_or(configColor(COLORVAL));

    // DO NOT swap this for renderText(STextResourceData&&). That overload does
    // enqueue() then await() on CAsyncResourceGatherer, i.e. it BLOCKS the
    // calling thread on a worker -- and this runs inside the render pass. Doing
    // so wedged the whole compositor in futex_do_wait: alive, socket open, IPC
    // dead, desktop frozen. (2026-08-21.) This overload renders synchronously
    // with cairo on the calling thread, which is the only safe option here.
    //
    // It applies the family via pango_font_description_set_family_static, so
    // bar_text_font works; it sets pango_layout_set_width plus ellipsize, so
    // maxWidth truncates; and it sizes the texture to the text's own extents.
    // A natural-width texture means alignment is purely a matter of where the
    // box is placed, which titleRegion() below handles.
    m_pTextTex = g_pHyprRenderer->renderText(m_szLastTitle, COLOR, scaledSize, false, FONT, maxWidth, WEIGHT.m_value);
}

// The strip of bar a title may occupy: the full width less padding at both
// ends, less whatever the buttons claim. For a centred title the LARGER side is
// reserved on both ends, so the title sits in the middle of the window rather
// than in the middle of what the boxes left over.
CHyprBar::STitleRegion CHyprBar::titleRegion(const Vector2D& barSize, const float scale) {
    const auto ALIGN      = g_pGlobalState->config.barTextAlign->value();
    const auto BARPADDING = g_pGlobalState->config.barPadding->value();
    const auto PADDING    = BARPADDING * scale;

    float leftButtons = 0, rightButtons = 0;
    buttonExtents(barSize, scale, leftButtons, rightButtons);

    STitleRegion region;
    if (ALIGN != "left" && ALIGN != "right") {
        const float RESERVE = std::max(leftButtons, rightButtons);
        region.x = PADDING + RESERVE;
        region.w = barSize.x - 2 * (PADDING + RESERVE);
    } else {
        region.x = PADDING + leftButtons;
        region.w = barSize.x - 2 * PADDING - leftButtons - rightButtons;
    }

    region.w = std::max(region.w, 0.F);
    return region;
}

std::vector<CHyprBar::SButtonSlot> CHyprBar::buttonSlots(const Vector2D& barSize, const float scale) {
    const auto BARPADDING       = g_pGlobalState->config.barPadding->value();
    const auto BARBUTTONPADDING = g_pGlobalState->config.barButtonPadding->value();
    const auto ALIGNBUTTONS     = g_pGlobalState->config.barButtonsAlignment->value();
    const bool DEFAULTRIGHT     = ALIGNBUTTONS != "left";

    std::vector<SButtonSlot> slots;
    float                    leftCursor = BARPADDING * scale, rightCursor = BARPADDING * scale;

    for (size_t i = 0; i < g_pGlobalState->buttons.size(); ++i) {
        const auto& BUTTON  = g_pGlobalState->buttons[i];
        const bool  ONRIGHT = BUTTON.side.empty() ? DEFAULTRIGHT : BUTTON.side == "right";
        const float SIZE    = BUTTON.size * scale;
        const float PAD     = BARBUTTONPADDING * scale;

        // Both sides eat the same bar, so the check has to consider both.
        if (leftCursor + rightCursor + SIZE > barSize.x)
            break;

        float& cursor = ONRIGHT ? rightCursor : leftCursor;
        const float X = ONRIGHT ? barSize.x - cursor - SIZE : cursor;

        slots.push_back(SButtonSlot{i, CBox{X, (barSize.y - SIZE) / 2.0, SIZE, SIZE}, ONRIGHT});
        cursor += PAD + SIZE;
    }

    return slots;
}

void CHyprBar::buttonExtents(const Vector2D& barSize, const float scale, float& leftOut, float& rightOut) {
    const auto BARBUTTONPADDING = g_pGlobalState->config.barButtonPadding->value();

    leftOut = rightOut = 0;
    for (const auto& SLOT : buttonSlots(barSize, scale)) {
        float& side = SLOT.right ? rightOut : leftOut;
        side += g_pGlobalState->buttons[SLOT.index].size * scale + BARBUTTONPADDING * scale;
    }
}

size_t CHyprBar::getVisibleButtonCount(Config::INTEGER barButtonPadding, Config::INTEGER barPadding, const Vector2D& bufferSize, const float scale) {
    float  availableSpace = bufferSize.x - barPadding * scale * 2;
    size_t count          = 0;

    for (const auto& button : g_pGlobalState->buttons) {
        const float buttonSpace = (button.size + barButtonPadding) * scale;
        if (availableSpace >= buttonSpace) {
            count++;
            availableSpace -= buttonSpace;
        } else
            break;
    }

    return count;
}

void CHyprBar::renderBarButtons(CBox* barBox, const float scale, const float a) {
    const auto INACTIVECOLOR   = g_pGlobalState->config.inactiveButtonColor->value();
    const bool INVALIDATEICONS = m_bButtonsDirty || m_bWindowSizeChanged;

    for (const auto& SLOT : buttonSlots(Vector2D{barBox->w, barBox->h}, scale)) {
        auto& button = g_pGlobalState->buttons[SLOT.index];

        CBox  buttonBox = SLOT.box;
        buttonBox.translate(barBox->pos());
        buttonBox.round();

        // Prebaked art wins: draw it and skip both the rect and the glyph. A
        // Platinum box cannot be assembled from a rounded rect plus a font
        // character -- the bevel and the glyph are one drawing.
        if (!button.image.empty()) {
            struct stat    st {};
            const uint64_t STAMP = (::stat(button.image.c_str(), &st) == 0) ? (uint64_t)st.st_mtime : 0;
            if (button.imageStamp != STAMP) {
                button.imageStamp = STAMP;
                button.imageTex   = loadPixelTexture(button.image);
            }

            if (button.imageTex) {
                g_pHyprOpenGL->renderTexture(button.imageTex, buttonBox, {.a = a});
                continue;
            }
        }

        auto color = button.bgcol;

        if (INACTIVECOLOR > 0) {
            color = m_bWindowHasFocus ? color : configColor(INACTIVECOLOR);
            if (INVALIDATEICONS && button.userfg && button.iconTex)
                button.iconTex = nullptr;
        }

        color.a *= a;

        g_pHyprOpenGL->renderRect(buttonBox, color, {.round = static_cast<int>(std::round(buttonBox.w / 2.0)), .roundingPower = 2.F});
    }
}

void CHyprBar::renderBarButtonsText(CBox* barBox, const float scale, const float a) {
    const auto HEIGHT      = g_pGlobalState->config.barHeight->value();
    const auto ICONONHOVER = g_pGlobalState->config.iconOnHover->value();
    const auto COORDS      = cursorRelativeToBar();

    // Unscaled slots share the layout used for drawing, so the hover box is
    // exactly the box the user sees.
    const auto HITSLOTS = buttonSlots(Vector2D{(double)(int)assignedBoxGlobal().w, (double)HEIGHT}, 1.F);
    const auto SLOTS    = buttonSlots(Vector2D{barBox->w, barBox->h}, scale);

    for (size_t n = 0; n < SLOTS.size(); ++n) {
        const auto& SLOT   = SLOTS[n];
        auto&       button = g_pGlobalState->buttons[SLOT.index];

        bool        hovering = false;
        if (n < HITSLOTS.size()) {
            const auto& HIT = HITSLOTS[n].box;
            hovering        = VECINRECT(COORDS, HIT.x, HIT.y, HIT.x + HIT.w, HIT.y + HIT.h);
        }

        const auto trackHover = [&]() {
            bool currentBit = (m_iButtonHoverState & (1 << SLOT.index)) != 0;
            if (hovering != currentBit) {
                m_iButtonHoverState ^= (1 << SLOT.index);
                // damage to get rid of some artifacts when icons are "hidden"
                damageEntire();
            }
        };

        // A button drawn from art already has its glyph baked in.
        if (!button.image.empty() && button.imageTex) {
            trackHover();
            continue;
        }

        const auto scaledButtonSize = button.size * scale;

        if ((!button.iconTex || button.iconTex->m_texID == 0) && !button.icon.empty()) {
            auto fgcol     = button.userfg ? button.fgcol : (button.bgcol.r + button.bgcol.g + button.bgcol.b < 1) ? CHyprColor(0xFFFFFFFF) : CHyprColor(0xFF000000);
            button.iconTex = g_pHyprRenderer->renderText(button.icon, fgcol, std::round(button.size * 0.62 * scale), false, "sans", scaledButtonSize);
        }

        if (!button.iconTex || button.iconTex->m_texID == 0) {
            trackHover();
            continue;
        }

        const auto iconX = barBox->x + SLOT.box.x + SLOT.box.w / 2.0 - button.iconTex->m_size.x / 2.0;
        const auto iconY = barBox->y + barBox->height / 2.0 - button.iconTex->m_size.y / 2.0;
        CBox       pos   = {iconX, iconY, button.iconTex->m_size.x, button.iconTex->m_size.y};

        if (!ICONONHOVER || (ICONONHOVER && m_iButtonHoverState > 0))
            g_pHyprOpenGL->renderTexture(button.iconTex, pos, {.a = a});

        trackHover();
    }
}

void CHyprBar::draw(PHLMONITOR pMonitor, const float& a) {
    if (barsShuttingDown())
        return;

    const auto ENABLED = g_pGlobalState->config.enabled->value();

    if (m_bLastEnabledState != ENABLED) {
        m_bLastEnabledState = ENABLED;
        g_pDecorationPositioner->repositionDeco(this);
    }

    if (m_hidden || !validMapped(m_pWindow) || !ENABLED)
        return;

    const auto PWINDOW = m_pWindow.lock();

    if (!PWINDOW->m_ruleApplicator->decorate().valueOrDefault())
        return;

    auto data = CBarPassElement::SBarData{this, a};
    g_pHyprRenderer->m_renderPass.add(makeUnique<CBarPassElement>(data));
}

void CHyprBar::renderPass(PHLMONITOR pMonitor, const float& a) {
    // UNLOAD SAFETY. This runs inside Hyprland's render pass, and every line
    // below dereferences a config value owned by this plugin. When the plugin is
    // unloaded those values are destroyed, but a CBarPassElement can still be
    // queued and will draw one more frame -- calling ->value() on a dead value,
    // which THROWS. An exception escaping the render pass is std::terminate, and
    // that is the SIGABRT that has been taking the compositor down on every
    // plugin unload and every `hyprctl reload`: the trace is
    //     CIntValue::value() <- CHyprBar::renderPass <- CBarPassElement::draw
    // with the .so already unloaded.
    if (barsShuttingDown())
        return;

    const auto  PWINDOW = m_pWindow.lock();

    static auto PENABLEBLURGLOBAL = CConfigValue<Config::BOOL>("decoration:blur:enabled");
    const auto  BARCOLOR          = g_pGlobalState->config.barColor->value();
    const auto  HEIGHT            = g_pGlobalState->config.barHeight->value();
    const auto  PRECEDENCE        = g_pGlobalState->config.barPrecedenceOverBorder->value();
    const auto  ALIGNBUTTONS      = g_pGlobalState->config.barButtonsAlignment->value();
    const auto  ENABLETITLE       = g_pGlobalState->config.barTitleEnabled->value();
    const auto  ENABLEBLUR        = g_pGlobalState->config.barBlur->value();
    const auto  INACTIVECOLOR     = g_pGlobalState->config.inactiveButtonColor->value();

    if (INACTIVECOLOR > 0) {
        bool currentWindowFocus = PWINDOW == Desktop::focusState()->window();
        if (currentWindowFocus != m_bWindowHasFocus) {
            m_bWindowHasFocus = currentWindowFocus;
            m_bButtonsDirty   = true;
        }
    }

    const CHyprColor DEST_COLOR = m_bForcedBarColor.value_or(configColor(BARCOLOR));
    if (DEST_COLOR != m_cRealBarColor->goal())
        *m_cRealBarColor = DEST_COLOR;

    CHyprColor color = m_cRealBarColor->value();

    color.a *= a;
    const bool BUTTONSRIGHT = ALIGNBUTTONS != "left";
    const bool SHOULDBLUR   = ENABLEBLUR && *PENABLEBLURGLOBAL && color.a < 1.F;

    if (HEIGHT < 1) {
        m_iLastHeight = HEIGHT;
        return;
    }

    const auto PWORKSPACE      = PWINDOW->m_workspace;
    const auto WORKSPACEOFFSET = PWORKSPACE && !PWINDOW->m_pinned ? PWORKSPACE->m_renderOffset->value() : Vector2D();

    const auto ROUNDING = PWINDOW->rounding() + (PRECEDENCE ? 0 : PWINDOW->getRealBorderSize());

    const auto scaledRounding = ROUNDING > 0 ? ROUNDING * pMonitor->m_scale - 2 /* idk why but otherwise it looks bad due to the gaps */ : 0;

    const auto FRAME = frameMetrics();

    m_seExtents = FRAME.on ? SBoxExtents{{(double)FRAME.left, (double)HEIGHT}, {(double)FRAME.right, (double)FRAME.bottom}} : SBoxExtents{{0, HEIGHT}, {}};

    const auto DECOBOX = assignedBoxGlobal();

    // ABSOLUTE decorations are handed an empty assignedGeometry, so the usual
    // resize detection in onPositioningReply never fires. Watch the width here.
    if (FRAME.on && (int)DECOBOX.w != m_iLastFrameWidth) {
        m_iLastFrameWidth    = (int)DECOBOX.w;
        m_bWindowSizeChanged = true;
    }

    const auto BARBUF = DECOBOX.size() * pMonitor->m_scale;

    CBox       titleBarBox = {DECOBOX.x - pMonitor->m_position.x, DECOBOX.y - pMonitor->m_position.y, DECOBOX.w,
                              DECOBOX.h + ROUNDING * 3 /* to fill the bottom cuz we can't disable rounding there */};

    titleBarBox.translate(PWINDOW->m_floatingOffset).scale(pMonitor->m_scale).round();

    if (titleBarBox.w < 1 || titleBarBox.h < 1)
        return;

    const auto FRAMEBOX_GLOBAL = frameBoxGlobal();
    CBox       frameBox        = {FRAMEBOX_GLOBAL.x - pMonitor->m_position.x, FRAMEBOX_GLOBAL.y - pMonitor->m_position.y, FRAMEBOX_GLOBAL.w, FRAMEBOX_GLOBAL.h};
    frameBox.translate(PWINDOW->m_floatingOffset).scale(pMonitor->m_scale).round();

    // The bezel lives outside the bar strip, so the scissor has to open up to
    // the whole ring or the sides and bottom get clipped away.
    g_pHyprOpenGL->scissor(FRAME.on ? frameBox : titleBarBox);

    if (ROUNDING) {
        // the +1 is a shit garbage temp fix until renderRect supports an alpha matte
        CBox windowBox = {PWINDOW->position(Desktop::View::IGeometric::GEOMETRIC_CURRENT).x + PWINDOW->m_floatingOffset.x - pMonitor->m_position.x + 1,
                          PWINDOW->position(Desktop::View::IGeometric::GEOMETRIC_CURRENT).y + PWINDOW->m_floatingOffset.y - pMonitor->m_position.y + 1,
                          PWINDOW->size(Desktop::View::IGeometric::GEOMETRIC_CURRENT).x - 2, PWINDOW->size(Desktop::View::IGeometric::GEOMETRIC_CURRENT).y - 2};

        if (windowBox.w < 1 || windowBox.h < 1)
            return;

        glClearStencil(0);
        glClear(GL_STENCIL_BUFFER_BIT);

        g_pHyprOpenGL->setCapStatus(GL_STENCIL_TEST, true);

        glStencilFunc(GL_ALWAYS, 1, -1);
        glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);

        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

        windowBox.translate(WORKSPACEOFFSET).scale(pMonitor->m_scale).round();
        g_pHyprOpenGL->renderRect(windowBox, CHyprColor(0, 0, 0, 0), {.round = scaledRounding, .roundingPower = m_pWindow->roundingPower()});
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

        glStencilFunc(GL_NOTEQUAL, 1, -1);
        glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    }

    bool texturedBar = false;

    // Frame mode draws one nine-patch across the whole window and the bar
    // texture is not drawn at all -- the frame's top band IS the title bar, so
    // painting both would double the bezel down the sides of the bar.
    if (FRAME.on) {
        const std::string TEXPREFIX = g_pGlobalState->config.frameTexture->value();
        ensureFrameNinePatches(TEXPREFIX, g_pGlobalState->config.frameTextureBorder->value());

        const bool  FOCUSED = PWINDOW == Desktop::focusState()->window();
        const auto& PATCH   = (FOCUSED || !g_ninePatchFrameInactive.valid) ? g_ninePatchFrameActive : g_ninePatchFrameInactive;

        if (PATCH.valid) {
            // A nine-patch authored in LOGICAL pixels has to be upscaled by the
            // monitor scale, and at a fractional scale that resampling is what
            // produces moire in a fine pattern: at 1.25 a 2px period lands on
            // 2.5 device rows, so ridges alternate thick and thin. Art authored
            // at DEVICE resolution is blitted 1:1 instead and the pattern comes
            // out exact.
            const float PATCHSCALE = g_pGlobalState->config.frameTextureUnscaled->value() ? 1.F : pMonitor->m_scale;

            // Skip the centre cell: the client covers it, and painting it would
            // tint any window that is translucent.
            renderNinePatch(PATCH, frameBox, a, PATCHSCALE, true);
            texturedBar = true;
        }
    }

    if (!texturedBar) {
        const std::string TEXPREFIX = g_pGlobalState->config.barTexture->value();
        if (!TEXPREFIX.empty()) {
            const std::string TEXBORDER = g_pGlobalState->config.barTextureBorder->value();
            ensureNinePatches(TEXPREFIX, TEXBORDER);

            const bool  FOCUSED = PWINDOW == Desktop::focusState()->window();
            const auto& PATCH   = (FOCUSED || !g_ninePatchInactive.valid) ? g_ninePatchActive : g_ninePatchInactive;

            if (PATCH.valid) {
                renderNinePatch(PATCH, titleBarBox, a, pMonitor->m_scale);
                texturedBar = true;
            }
        }
    }

    // Fall back to the flat fill when no texture is configured or it failed to
    // load, so an unset/bad path degrades to upstream behaviour.
    if (!texturedBar) {
        if (SHOULDBLUR)
            g_pHyprOpenGL->renderRect(titleBarBox, color, {.round = scaledRounding, .roundingPower = m_pWindow->roundingPower(), .blur = true, .blurA = a});
        else
            g_pHyprOpenGL->renderRect(titleBarBox, color, {.round = scaledRounding, .roundingPower = m_pWindow->roundingPower()});
    }

    // render title
    if (ENABLETITLE && (m_szLastTitle != PWINDOW->m_title || m_bWindowSizeChanged || !m_pTextTex || m_pTextTex->m_texID == 0 || m_bTitleColorChanged)) {
        m_szLastTitle = PWINDOW->m_title;
        renderBarTitle(BARBUF, pMonitor->m_scale);
    }

    if (ROUNDING) {
        // cleanup stencil
        glClearStencil(0);
        glClear(GL_STENCIL_BUFFER_BIT);
        g_pHyprOpenGL->setCapStatus(GL_STENCIL_TEST, false);
        glStencilMask(-1);
        glStencilFunc(GL_ALWAYS, 1, 0xFF);
    }

    CBox textBox = {titleBarBox.x, titleBarBox.y, (int)BARBUF.x, (int)BARBUF.y};

    // A patterned bar (the OS 9 hatch) must not run behind the title or behind
    // the boxes -- on real Platinum the ridges stop dead at both. Painting the
    // face colour over those patches is what carves the plain areas out, and it
    // has to happen after the texture and before anything sits on top of it.
    const auto CLEARCOL = configColor(g_pGlobalState->config.barClearColor->value());
    if (CLEARCOL.a > 0.F) {
        const auto TEXTPAD   = g_pGlobalState->config.barTextClearPad->value() * pMonitor->m_scale;
        const auto BUTTONPAD = g_pGlobalState->config.barButtonClearPad->value() * pMonitor->m_scale;

        // The bar's own top rule + highlight and its bottom shadow are part of
        // the frame bezel, not part of the hatch. Clearing the full bar height
        // painted straight over them, which read as the button padding bleeding
        // into the frame. Inset so the patches only touch the interior.
        const auto INSET_T = g_pGlobalState->config.barClearInsetTop->value() * pMonitor->m_scale;
        const auto INSET_B = g_pGlobalState->config.barClearInsetBottom->value() * pMonitor->m_scale;
        const auto CLEAR_Y = titleBarBox.y + INSET_T;
        const auto CLEAR_H = std::max(0.0, (double)BARBUF.y - INSET_T - INSET_B);

        auto       clear = CLEARCOL;
        clear.a *= a;

        if (ENABLETITLE && m_pTextTex) {
            const auto REGION = titleRegion(BARBUF, pMonitor->m_scale);
            const auto WIDTH  = std::min<double>(m_pTextTex->m_size.x, REGION.w);
            const auto CENTRE = g_pGlobalState->config.barTextAlign->value() != "left" ? REGION.x + (REGION.w - WIDTH) / 2.0 : REGION.x;

            CBox patch = {textBox.x + CENTRE - TEXTPAD, CLEAR_Y, WIDTH + TEXTPAD * 2, CLEAR_H};
            patch.round();
            if (patch.w > 0 && patch.h > 0)
                g_pHyprOpenGL->renderRect(patch, clear, {});
        }

        bool clearedLeft = false, clearedRight = false;
        for (const auto& SLOT : buttonSlots(BARBUF, pMonitor->m_scale)) {
            double x0 = SLOT.box.x - BUTTONPAD;
            double x1 = SLOT.box.x + SLOT.box.w + BUTTONPAD;

            // The OUTERMOST box on each side clears all the way out to the end
            // of the bar. Otherwise a sliver of hatch survives between the box
            // and the frame, and on real Platinum the ridges stop at the boxes
            // rather than squeezing past them.
            bool& done = SLOT.right ? clearedRight : clearedLeft;
            if (!done) {
                done = true;
                if (SLOT.right)
                    x1 = BARBUF.x;
                else
                    x0 = 0;
            }

            CBox patch = {textBox.x + x0, CLEAR_Y, x1 - x0, CLEAR_H};
            patch.round();
            if (patch.w > 0 && patch.h > 0)
                g_pHyprOpenGL->renderRect(patch, clear, {});
        }
    }

    if (ENABLETITLE && m_pTextTex) {
        const auto REGION = titleRegion(BARBUF, pMonitor->m_scale);

        // The struct overload returns a texture as wide as the region it was
        // given, with the text already aligned inside it, so the box is placed
        // at the region and not centred a second time. A narrower texture (an
        // older renderer, or a short title) still gets centred here.
        const auto xOffset = m_pTextTex->m_size.x >= REGION.w - 1 ? REGION.x : REGION.x + std::round((REGION.w - m_pTextTex->m_size.x) / 2.0);
        const auto yOffset = std::round((BARBUF.y - m_pTextTex->m_size.y) / 2.0);

        CBox titleBox = {textBox.x + xOffset, textBox.y + yOffset, m_pTextTex->m_size.x, m_pTextTex->m_size.y};

        g_pHyprOpenGL->renderTexture(m_pTextTex, titleBox, {.a = a});
    }

    renderBarButtons(&textBox, pMonitor->m_scale, a);
    m_bButtonsDirty = false;

    g_pHyprOpenGL->scissor(nullptr);

    renderBarButtonsText(&textBox, pMonitor->m_scale, a);

    m_bWindowSizeChanged = false;
    m_bTitleColorChanged = false;

    // dynamic updates change the extents
    if (m_iLastHeight != HEIGHT) {
        PWINDOW->layoutTarget()->recalc();
        m_iLastHeight = HEIGHT;
    }
}

eDecorationType CHyprBar::getDecorationType() {
    return DECORATION_CUSTOM;
}

void CHyprBar::updateWindow(PHLWINDOW pWindow) {
    if (barsShuttingDown())
        return;

    syncFrameRounding();
    damageEntire();
}

void CHyprBar::onConfigReloaded() {
    // frame_texture may have only just been set (os9-window-bars applies it by
    // hyprctl eval, long after the bars themselves exist), so re-check here
    // rather than trusting what was true at construction.
    syncFrameRounding();

    m_bButtonsDirty      = true;
    m_bTitleColorChanged = true;
    m_pTextTex           = nullptr;

    g_pDecorationPositioner->repositionDeco(this);
    damageEntire();
}

void CHyprBar::damageEntire() {
    if (barsShuttingDown())
        return;

    // Must cover the side and bottom bezels too, or dragging a window leaves
    // the frame smeared behind it.
    g_pHyprRenderer->damageBox(frameBoxGlobal());
}

Vector2D CHyprBar::cursorRelativeToBar() {
    return g_pInputManager->getMouseCoordsInternal() - assignedBoxGlobal().pos();
}

eDecorationLayer CHyprBar::getDecorationLayer() {
    if (barsShuttingDown())
        return DECORATION_LAYER_UNDER;

    // UNDER puts the decoration beneath the client surface, which means the
    // innermost pixels of a frame ring get covered: the client overlaps the
    // reserved area by a pixel or two after rounding, and it is exactly the
    // content rule and its highlight that disappear. OVER draws the ring on top
    // instead. Safe here because the nine-patch skips its centre cell, so only
    // the ring is painted, never over the client's interior.
    return g_pGlobalState->config.frameOverWindow->value() ? DECORATION_LAYER_OVER : DECORATION_LAYER_UNDER;
}

uint64_t CHyprBar::getDecorationFlags() {
    if (barsShuttingDown())
        return 0;

    return DECORATION_ALLOWS_MOUSE_INPUT | (g_pGlobalState->config.barPartOfWindow->value() ? DECORATION_PART_OF_MAIN_WINDOW : 0);
}

CBox CHyprBar::assignedBoxGlobal() {
    if (!validMapped(m_pWindow))
        return {};

    const auto PWORKSPACE      = m_pWindow->m_workspace;
    const auto WORKSPACEOFFSET = PWORKSPACE && !m_pWindow->m_pinned ? PWORKSPACE->m_renderOffset->value() : Vector2D();
    const auto FRAME           = frameMetrics();

    // The contract of this box is "the title bar strip", and every hit test,
    // drag and button offset in this file is written against it. In frame mode
    // there is no assigned geometry to read -- ABSOLUTE decorations are told to
    // position themselves -- so derive the same strip from the client box,
    // which the reservation has already pushed down by exactly bar_height.
    if (FRAME.on) {
        const auto PWINDOW = m_pWindow.lock();
        const auto POS     = PWINDOW->position(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
        const auto SIZE    = PWINDOW->size(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
        const auto HEIGHT  = g_pGlobalState->config.barHeight->value();

        return CBox{POS.x, POS.y - HEIGHT, SIZE.x, (double)HEIGHT}.translate(WORKSPACEOFFSET);
    }

    CBox box = m_bAssignedBox;
    box.translate(g_pDecorationPositioner->getEdgeDefinedPoint(DECORATION_EDGE_TOP, m_pWindow.lock()));

    return box.translate(WORKSPACEOFFSET);
}

// The whole reserved ring: the bar strip, plus the side and bottom bezels.
// Falls back to the bar strip when no frame is configured, so callers that only
// need "everything this decoration paints" can use it unconditionally.
CBox CHyprBar::frameBoxGlobal() {
    const auto BAR   = assignedBoxGlobal();
    const auto FRAME = frameMetrics();

    if (!FRAME.on || !validMapped(m_pWindow))
        return BAR;

    const auto SIZE = m_pWindow->size(Desktop::View::IGeometric::GEOMETRIC_CURRENT);

    return CBox{BAR.x - FRAME.left, BAR.y, BAR.w + FRAME.left + FRAME.right, BAR.h + SIZE.y + FRAME.bottom};
}

PHLWINDOW CHyprBar::getOwner() {
    return m_pWindow.lock();
}

void CHyprBar::updateRules() {
    const auto PWINDOW              = m_pWindow.lock();
    auto       prevHidden           = m_hidden;
    auto       prevForcedTitleColor = m_bForcedTitleColor;

    m_bForcedBarColor   = std::nullopt;
    m_bForcedTitleColor = std::nullopt;
    m_hidden            = false;

    if (PWINDOW->m_ruleApplicator->m_otherProps.props.contains(g_pGlobalState->nobarRuleIdx))
        m_hidden = truthy(PWINDOW->m_ruleApplicator->m_otherProps.props.at(g_pGlobalState->nobarRuleIdx)->effect);
    if (PWINDOW->m_ruleApplicator->m_otherProps.props.contains(g_pGlobalState->barColorRuleIdx))
        m_bForcedBarColor = CHyprColor(Config::ParserUtils::parseColor(PWINDOW->m_ruleApplicator->m_otherProps.props.at(g_pGlobalState->barColorRuleIdx)->effect).value_or(0));
    if (PWINDOW->m_ruleApplicator->m_otherProps.props.contains(g_pGlobalState->titleColorRuleIdx))
        m_bForcedTitleColor = CHyprColor(Config::ParserUtils::parseColor(PWINDOW->m_ruleApplicator->m_otherProps.props.at(g_pGlobalState->titleColorRuleIdx)->effect).value_or(0));

    if (prevHidden != m_hidden)
        g_pDecorationPositioner->repositionDeco(this);
    if (prevForcedTitleColor != m_bForcedTitleColor)
        m_bTitleColorChanged = true;
}

void CHyprBar::damageOnButtonHover() {
    if (barsShuttingDown())
        return;

    const auto HEIGHT = g_pGlobalState->config.barHeight->value();
    const auto COORDS = cursorRelativeToBar();

    bool       hover = false;
    for (const auto& SLOT : buttonSlots(Vector2D{(double)(int)assignedBoxGlobal().w, (double)HEIGHT}, 1.F)) {
        const auto& BOX = SLOT.box;
        if (VECINRECT(COORDS, BOX.x, BOX.y, BOX.x + BOX.w, BOX.y + BOX.h)) {
            hover = true;
            break;
        }
    }

    if (hover != m_bButtonHovered) {
        m_bButtonHovered = hover;
        damageEntire();
    }
}
