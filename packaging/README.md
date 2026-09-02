# Packaging the frame plugin

[`PKGBUILD`](PKGBUILD) builds `plugin/hyprbars` against the Hyprland headers
your distribution already ships, and installs it as
`/usr/lib/libhyprbars-os99.so`.

```
cd packaging
makepkg -si
```

`os99-window-bars` looks for a hand build in `~/.local/lib` first and falls back
to `/usr/lib/libhyprbars-os99.so`, so a package and a development build can sit
on the same machine without arguing: whoever built by hand last meant it.

## Why this rather than hyprpm

hyprpm is Hyprland's own plugin manager and it works. But its first act on a new
machine is to clone and build **Hyprland itself** to produce headers — which on
Arch are already installed, in the `hyprland` package, 595 files under
`/usr/include/hyprland/`, with a `pkg-config` entry naming the version. That
cost seven and a half minutes here. `make` against the packaged headers takes a
hundred seconds.

hyprpm remains the right answer where those headers are not packaged, and
`os99-install --auto` still uses it as the fallback.

## Why pkgver tracks Hyprland

Hyprland plugins are ABI-pinned. A plugin built against 0.56 does not load into
0.57, and the failure is silent — the frames simply stop appearing. `depends`
carries a version range so pacman says that out loud instead, which is the same
convention the upstream `hyprland-plugin-hyprbars` package uses. A Hyprland
release is what obsoletes the binary, so a Hyprland release is what bumps
`pkgver`; this theme's own version lives in `_os99ver` and moves `pkgrel`.
