#pragma once

#define WLR_USE_UNSTABLE

#include <hyprland/src/render/decorations/IHyprWindowDecoration.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/gl/GLTexture.hpp>
#include <hyprland/src/devices/IPointer.hpp>
#include <hyprland/src/devices/ITouch.hpp>
#include <hyprland/src/desktop/rule/windowRule/WindowRule.hpp>
#include <hyprland/src/helpers/AnimatedVariable.hpp>
#include <hyprland/src/helpers/time/Time.hpp>
#include <hyprland/src/helpers/signal/Signal.hpp>
#include "globals.hpp"

#define private public
#include <hyprland/src/managers/input/InputManager.hpp>
#undef private

namespace Event {
    struct SCallbackInfo;
}

class CHyprBar : public IHyprWindowDecoration {
  public:
    CHyprBar(PHLWINDOW);
    virtual ~CHyprBar();

    virtual SDecorationPositioningInfo getPositioningInfo();

    virtual void                       onPositioningReply(const SDecorationPositioningReply& reply);

    virtual void                       draw(PHLMONITOR, float const& a);

    virtual eDecorationType            getDecorationType();

    virtual void                       updateWindow(PHLWINDOW);
    void                               syncFrameRounding();
    void                               runNativeDispatch(const std::string& what);
    void                               toggleShade();
    void                               minimizeWindow();
    void                               keepShadeOnScreen();
    void                               keepBarBelowPanels();

    virtual void                       damageEntire();

    virtual eDecorationLayer           getDecorationLayer();

    virtual uint64_t                   getDecorationFlags();

    bool                               m_bButtonsDirty = true;

    virtual std::string                getDisplayName();

    PHLWINDOW                          getOwner();

    void                               updateRules();
    void                               onConfigReloaded();

    WP<CHyprBar>                       m_self;

  private:
    SBoxExtents                m_seExtents;

    PHLWINDOWREF               m_pWindow;

    CBox                       m_bAssignedBox;

    SP<Render::ITexture>       m_pTextTex;

    bool                       m_bWindowSizeChanged = false;
    bool                       m_hidden             = false;
    bool                       m_bTitleColorChanged = false;
    bool                       m_bButtonHovered     = false;
    bool                       m_bLastEnabledState  = false;
    bool                       m_bForcedSquare      = false;
    bool                       m_bForcedFlat        = false;

    // WINDOWSHADE. The OS 9 collapse box rolls a window up into just its title
    // bar, and rolls it back down on a second click. Hyprland has no such
    // dispatcher, so it is built here out of float + resize, remembering enough
    // to put the window back exactly as it was.
    bool                       m_bShaded            = false;
    int                        m_shadeFixTries      = 0;
    // keepBarBelowPanels: attempts spent on the current out-of-bounds episode.
    int                        m_clampTries         = 0;
    bool                       m_bShadeWasTiled     = false;
    Vector2D                   m_shadeRestoreSize;
    Vector2D                   m_shadeRestorePos;
    bool                       m_bWindowHasFocus    = false;
    std::optional<CHyprColor>  m_bForcedBarColor;
    std::optional<CHyprColor>  m_bForcedTitleColor;

    Time::steady_tp            m_lastMouseDown = Time::steadyNow();

    // TOOLTIPS. Which box the cursor is on -- an index into
    // g_pGlobalState->buttons, or -1 for none -- and whether one is up.
    //
    // The rendered text is cached on the bar rather than on the button because
    // only one tooltip can be up at a time, and its content depends on state
    // the BUTTON does not know: a latched box says the way back out.
    int                        m_hoveredButton   = -1;
    bool                       m_tooltipShown    = false;
    // When the cursor arrived on the current box. The tooltip is owed a delay
    // from this instant -- see renderTooltipInner for how it is counted down
    // without a timer.
    Time::steady_tp            m_hoverSince      = Time::steadyNow();
    SP<Render::ITexture>       m_tooltipTex;
    std::string                m_tooltipTexFor;
    float                      m_tooltipTexScale = 0;
    // The box the tooltip last occupied, in global logical coordinates. Kept
    // after it is hidden: the area it used has to be damaged on the way out as
    // well as on the way in, or it stays painted on screen.
    CBox                       m_lastTooltipBox;

    PHLANIMVAR<CHyprColor>     m_cRealBarColor;

    Vector2D                   cursorRelativeToBar();

    void                       renderPass(PHLMONITOR, float const& a);
    void                       renderBarTitle(const Vector2D& bufferSize, const float scale);

    struct STitleRegion {
        float x = 0, w = 0;
    };
    STitleRegion titleRegion(const Vector2D& barSize, const float scale);
    void renderBarButtons(CBox* barBox, const float scale, const float a);
    void renderBarButtonsText(CBox* barBox, const float scale, const float a);
    void renderTooltip(PHLMONITOR pMonitor, const float a);
    void renderTooltipInner(PHLMONITOR pMonitor, const float a);
    void damageOnButtonHover();

    // Hover moved to another box, or off every box.
    void onButtonHoverChanged(int index);
    // Whether the window is currently IN the state this box toggles, read live
    // from the window so a change made by any other route still shows here.
    bool buttonLatched(const SHyprButton& button);
    // Where the tooltip sits, in global logical coordinates. Empty when none
    // is showing.
    CBox tooltipBoxGlobal();

    bool inputIsValid();
    void onMouseButton(Event::SCallbackInfo& info, IPointer::SButtonEvent e);
    void onTouchDown(Event::SCallbackInfo& info, ITouch::SDownEvent e);
    void onTouchUp(Event::SCallbackInfo& info, ITouch::SUpEvent e);
    void onMouseMove(Vector2D coords);
    void onTouchMove(Event::SCallbackInfo& info, ITouch::SMotionEvent e);

    void handleDownEvent(Event::SCallbackInfo& info, std::optional<ITouch::SDownEvent> touchEvent);
    void handleContextDown(Event::SCallbackInfo& info);
    void handleUpEvent(Event::SCallbackInfo& info);
    void handleMovement();
    bool doButtonPress(Config::INTEGER barPadding, Config::INTEGER barButtonPadding, Config::INTEGER barHeight, Vector2D COORDS, bool BUTTONSRIGHT);

    CBox assignedBoxGlobal();
    CBox frameBoxGlobal();

    CHyprSignalListener m_pMouseButtonCallback;
    CHyprSignalListener m_pTouchDownCallback;
    CHyprSignalListener m_pTouchUpCallback;

    CHyprSignalListener m_pTouchMoveCallback;
    CHyprSignalListener m_pMouseMoveCallback;

    std::string         m_szLastTitle;

    bool                m_bDraggingThis  = false;
    bool                m_bTouchEv       = false;
    bool                m_bDragPending   = false;
    bool                m_bCancelledDown = false;
    int                 m_touchId        = 0;

    // store hover state for buttons as a bitfield
    unsigned int m_iButtonHoverState = 0;

    // for dynamic updates
    int    m_iLastHeight = 0;

    // Frame mode gets no positioning reply to compare against, so track the
    // bar width here instead -- the title texture is laid out to it.
    int    m_iLastFrameWidth = -1;

    size_t getVisibleButtonCount(Config::INTEGER barButtonPadding, Config::INTEGER barPadding, const Vector2D& bufferSize, const float scale);

    // Where a button sits, in bar-local coordinates multiplied by `scale`.
    struct SButtonSlot {
        size_t index = 0;
        CBox   box;
        bool   right = false;
    };

    // The single source of truth for button placement. Drawing, hover and hit
    // testing all read from this, so they cannot disagree -- which matters now
    // that buttons can sit on either side and each side has its own cursor.
    std::vector<SButtonSlot> buttonSlots(const Vector2D& barSize, const float scale);

    // Space the buttons claim on each side, used to keep a centred title
    // centred in the WINDOW rather than in the leftovers.
    void buttonExtents(const Vector2D& barSize, const float scale, float& leftOut, float& rightOut);

    friend class CBarPassElement;
};
