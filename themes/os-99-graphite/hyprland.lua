-- Mac OS 9 Platinum window frames: a hard 1px rule, no gradient, no glow.
-- Rounding, blur and inactive-dim are forced off in ~/.config/hypr/looknfeel.lua,
-- which loads after this file and would otherwise win. It keys off the
-- os99.marker file shipped alongside this one.

local active_border_color = "rgb(000000)"
local inactive_border_color = "rgb(5A5A5A)"

hl.config({
  general = {
    border_size = 1,
    col = {
      active_border = active_border_color,
      inactive_border = inactive_border_color,
    },
  },

  decoration = {
    shadow = {
      enabled = false,
    },
  },

  group = {
    col = {
      border_active = active_border_color,
      border_inactive = inactive_border_color,
    },

    -- Grouped windows read as Platinum tab strips.
    groupbar = {
      -- The same face as the title bars and the menu bar. This is only the
      -- startup value: os99-window-bars overwrites it with whichever face the
      -- font chain actually resolved, so a machine without ChicagoFLF still
      -- gets a matching groupbar rather than a fontconfig substitution.
      font_family = "ChicagoFLF",
      font_size = 12,
      font_weight_active = "bold",
      font_weight_inactive = "normal",
      height = 20,
      indicator_height = 0,
      indicator_gap = 0,
      gaps_in = 0,
      gaps_out = 0,
      gradients = false,
      gradient_rounding = 0,
      text_color = "rgb(ECECEC)",
      text_color_inactive = "rgba(ECECECAA)",
      col = {
        active = "rgb(4A4A4A)",
        inactive = "rgb(343434)",
      },
    },
  },
})

-- ---------------------------------------------------------------------------
-- Re-apply the OS 9 window frame on EVERY config load.
--
-- generate-art.py writes bars.lua INTO each theme. It must be sourced
-- here rather than left to the hook alone, because Hyprland discards everything
-- set through `hyprctl eval` on a config reload -- and it auto-reloads whenever
-- anything under ~/.config/hypr is saved. Without this, editing monitors.lua
-- stripped the frame back to a plain title bar (bar_height 15, frame_texture
-- empty, buttons gone) until the next theme switch. Living in the THEME also
-- means a reload picks up the NEW theme's colours immediately, instead of the
-- previous theme's until the theme-set hook eventually runs.
--
-- pcall because the file will not exist until the hook has run once.
-- The art directory of whatever theme is currently applied. bars.lua reads it
-- as `os99_dir`, so the same file works wherever the theme is installed.
os99_dir = (os.getenv("HOME") or "") .. "/.local/state/omarchy/current/theme/decor"
pcall(dofile, os99_dir .. "/bars.lua")

-- ...and re-run the hook itself, which the dofile above cannot do.
--
-- The generated file re-applies whatever geometry was current when it was
-- written. It cannot notice that the DISPLAY changed. Change monitor scale in
-- monitors.lua and Hyprland reloads, so the settings come back -- but the art
-- is still baked for the old scale, and since it is blitted 1:1 the frame comes
-- back the wrong SIZE. os99-window-bars compares the scale stamped in bar.env
-- against the live display, redraws when they differ, and rewrites this file.
--
-- Idempotent and cheap when nothing changed; it is also what post-boot.d and
-- theme-set.d run, so this just adds "on config reload" to the same trigger set.
hl.exec_cmd((os.getenv("HOME") or "") .. "/.local/bin/os99-window-bars")
