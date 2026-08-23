#include "ninePatch.hpp"

#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <cairo/cairo.h>
#include <sstream>
#include <vector>
#include <algorithm>

// Copies one rectangle out of `src` into a fresh surface and uploads it.
static SP<Render::ITexture> sliceToTexture(cairo_surface_t* src, int x, int y, int w, int h) {
    if (w <= 0 || h <= 0)
        return nullptr;

    auto sub = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    if (cairo_surface_status(sub) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(sub);
        return nullptr;
    }

    auto cr = cairo_create(sub);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_surface(cr, src, -x, -y);
    // Pixel art: never interpolate while slicing.
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_flush(sub);

    auto tex = g_pHyprRenderer->createTexture(sub);
    cairo_surface_destroy(sub);

    if (tex) {
        // Keep hard pixel edges when the cell is scaled.
        tex->magFilter = GL_NEAREST;
        tex->minFilter = GL_NEAREST;
    }
    return tex;
}

SP<Render::ITexture> loadPixelTexture(const std::string& path) {
    if (path.empty())
        return nullptr;

    auto surf = cairo_image_surface_create_from_png(path.c_str());
    if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surf);
        return nullptr;
    }

    auto tex = sliceToTexture(surf, 0, 0, cairo_image_surface_get_width(surf), cairo_image_surface_get_height(surf));
    cairo_surface_destroy(surf);
    return tex;
}

void parseBorders(const std::string& spec, int& t, int& r, int& b, int& l) {
    std::vector<int>  v;
    std::stringstream ss(spec);
    int               n;
    while (ss >> n)
        v.push_back(n);

    switch (v.size()) {
        case 1: t = r = b = l = v[0]; break;
        case 2: t = b = v[0]; r = l = v[1]; break;
        case 3: t = v[0]; r = l = v[1]; b = v[2]; break;
        case 4: t = v[0]; r = v[1]; b = v[2]; l = v[3]; break;
        default: t = r = b = l = 0; break;
    }
}

bool loadNinePatch(SNinePatch& out, const std::string& path, int t, int r, int b, int l) {
    out.valid = false;
    out.path  = path;

    if (path.empty())
        return false;

    auto surf = cairo_image_surface_create_from_png(path.c_str());
    if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surf);
        return false;
    }

    const int W = cairo_image_surface_get_width(surf);
    const int H = cairo_image_surface_get_height(surf);

    // Clamp borders so the middle band never goes negative on small art.
    l = std::clamp(l, 0, std::max(0, W - 1));
    r = std::clamp(r, 0, std::max(0, W - 1 - l));
    t = std::clamp(t, 0, std::max(0, H - 1));
    b = std::clamp(b, 0, std::max(0, H - 1 - t));

    out.left = l; out.right = r; out.top = t; out.bottom = b;
    out.size = {(double)W, (double)H};

    const int colX[3] = {0, l, W - r};
    const int colW[3] = {l, W - l - r, r};
    const int rowY[3] = {0, t, H - b};
    const int rowH[3] = {t, H - t - b, b};

    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            const int i  = row * 3 + col;
            out.cells[i] = sliceToTexture(surf, colX[col], rowY[row], colW[col], rowH[row]);
            out.cellSize[i] = {(double)colW[col], (double)rowH[row]};
        }
    }

    cairo_surface_destroy(surf);
    out.valid = true;
    return true;
}

void renderNinePatch(const SNinePatch& patch, const CBox& box, float alpha, float scale, bool skipCentre) {
    if (!patch.valid)
        return;

    double sl = patch.left * scale, sr = patch.right * scale;
    double st = patch.top * scale,  sb = patch.bottom * scale;

    // Never let the caps overrun the destination.
    if (sl + sr > box.w) {
        const double k = box.w / std::max(1.0, sl + sr);
        sl *= k; sr *= k;
    }
    if (st + sb > box.h) {
        const double k = box.h / std::max(1.0, st + sb);
        st *= k; sb *= k;
    }

    const double colX[3] = {box.x, box.x + sl, box.x + box.w - sr};
    const double colW[3] = {sl, box.w - sl - sr, sr};
    const double rowY[3] = {box.y, box.y + st, box.y + box.h - sb};
    const double rowH[3] = {st, box.h - st - sb, sb};

    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            const int i = row * 3 + col;
            if (skipCentre && row == 1 && col == 1)
                continue;
            if (!patch.cells[i] || colW[col] <= 0 || rowH[row] <= 0)
                continue;

            CBox dst = {colX[col], rowY[row], colW[col], rowH[row]};
            dst.round();
            if (dst.w < 1 || dst.h < 1)
                continue;

            Render::GL::g_pHyprOpenGL->renderTexture(patch.cells[i], dst, {.a = alpha});
        }
    }
}
