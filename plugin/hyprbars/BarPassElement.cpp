#include "BarPassElement.hpp"
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include "barDeco.hpp"

using namespace Render::GL;

CBarPassElement::CBarPassElement(const CBarPassElement::SBarData& data_) : data(data_) {
    ;
}

std::vector<UP<IPassElement>> CBarPassElement::draw() {
    // See the note in CHyprBar::renderPass -- a queued element can outlive the
    // plugin's config values by a frame.
    if (barsShuttingDown())
        return {};

    data.deco->renderPass(g_pHyprRenderer->m_renderData.pMonitor.lock(), data.a);
    return {};
}

bool CBarPassElement::needsLiveBlur() {
    static auto PENABLEBLURGLOBAL = CConfigValue<Config::BOOL>("decoration:blur:enabled");

    CHyprColor  color = data.deco->m_bForcedBarColor.value_or(CHyprColor{static_cast<uint64_t>(g_pGlobalState->config.barColor->value())});
    color.a *= data.a;
    const bool SHOULDBLUR = g_pGlobalState->config.barBlur->value() && *PENABLEBLURGLOBAL && color.a < 1.F;

    return SHOULDBLUR;
}

std::optional<CBox> CBarPassElement::boundingBox() {
    // Temporary fix: expand the bar bb a bit, otherwise occlusion gets too aggressive.
    // frameBoxGlobal() is the bar strip unless a frame is configured, in which
    // case it is the whole ring -- which must be inside the bb or the sides and
    // bottom get occluded away.
    auto       box     = data.deco->frameBoxGlobal();
    const auto TOOLTIP = data.deco->tooltipBoxGlobal();

    // A tooltip hangs below the frame, so it has to be inside the bb or it is
    // occluded away and never appears.
    if (TOOLTIP.w > 0 && TOOLTIP.h > 0) {
        const double X0 = std::min(box.x, TOOLTIP.x), Y0 = std::min(box.y, TOOLTIP.y);
        const double X1 = std::max(box.x + box.w, TOOLTIP.x + TOOLTIP.w), Y1 = std::max(box.y + box.h, TOOLTIP.y + TOOLTIP.h);
        box = CBox{X0, Y0, X1 - X0, Y1 - Y0};
    }

    return box.translate(-g_pHyprRenderer->m_renderData.pMonitor->m_position).expand(10);
}

bool CBarPassElement::needsPrecomputeBlur() {
    return false;
}
