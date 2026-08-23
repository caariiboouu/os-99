#pragma once

#include "globals.hpp"
#include <hyprland/src/helpers/math/Math.hpp>
#include <string>

// A nine-patch is sliced once at load time into nine separate textures. Slicing
// up front avoids any custom-UV work at draw time: each cell is just a texture
// rendered into its own box, and the renderer stretches it for us.
struct SNinePatch {
    SP<Render::ITexture> cells[9];
    Vector2D             cellSize[9];
    // Border widths in texture pixels, CSS order: top, right, bottom, left.
    int                  top = 0, right = 0, bottom = 0, left = 0;
    Vector2D             size;
    std::string          path;
    bool                 valid = false;
};

// Loads <prefix>_active.png / <prefix>_inactive.png. Returns false and leaves
// the patch invalid if the file is missing or unreadable.
bool loadNinePatch(SNinePatch& out, const std::string& path, int t, int r, int b, int l);

// Draws the patch into box. Corners keep their size (scaled by `scale`), edges
// and centre stretch to fill. `skipCentre` leaves the middle cell undrawn --
// a window frame only wants its ring, and painting a full-window quad under a
// translucent client would tint it.
void renderNinePatch(const SNinePatch& patch, const CBox& box, float alpha, float scale, bool skipCentre = false);

// Loads a plain PNG as one nearest-filtered texture. Returns nullptr if the
// file is missing or unreadable. Must be called with a GL context current,
// i.e. from the render thread.
SP<Render::ITexture> loadPixelTexture(const std::string& path);

// Parses "top right bottom left" (also accepts 1 or 2 values, CSS style).
void parseBorders(const std::string& spec, int& t, int& r, int& b, int& l);

// One copy shared by every bar -- the art is identical across windows, so
// loading it per-decoration would be pure waste.
inline SNinePatch g_ninePatchActive;
inline SNinePatch g_ninePatchInactive;

// The full window frame, when one is configured. Separate art from the bar:
// the bar is a strip, this is a ring.
inline SNinePatch g_ninePatchFrameActive;
inline SNinePatch g_ninePatchFrameInactive;
