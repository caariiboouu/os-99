# What runs, and what bounds it

OS 99 is a theme, but it is not only pixels: it puts native code inside the
compositor, a widget inside a long-lived shell process, and a handful of shell
scripts between them. This is what each of those is allowed to do.

## The shell widget and the right-click menu

The bar widget and the menu are QML, and everything they know comes from a
helper they start. That is a boundary worth being explicit about, because the
data crossing it is window titles — written by whatever clients happen to be
running, at whatever length those clients feel like.

Nothing in the QML composes a shell command. Helpers are named by an absolute
path derived from the QML file's own location and started as an argv list, so
there is no command line for a title or an address to be quoted into, and no
`$PATH` for a different `os99-minimize` to be found on.

Every helper runs under [`bin/os99-run`](../bin/os99-run), which owns its
lifetime:

- It makes itself a **process group leader** before forking, so one signal
  reaches the helper, its `python3`, its `hyprctl`, and anything else the tree
  grew. A QML `Process` cannot do this — its child lands in the shell's own
  group, where killing the group would be killing the shell.
- It holds a **wall-clock deadline** and, when that runs out, takes the group
  down with TERM, then KILL, then reaps it. A compositor that is busy or wedged
  cannot leave a helper behind to wake up later.
- It **caps stdout and stderr concurrently** — separate budgets, enforced as the
  bytes arrive rather than after — and keeps draining past the cap, so a capped
  child never deadlocks on a full pipe.
- It **refuses to run anything that is not one of the helpers beside it**,
  resolved through symlinks. A mistake in the QML cannot become a way to run an
  arbitrary program.

Reading gets a five-second deadline. Acting gets one sized to the act: twenty
seconds to restore windows, fifteen minutes for the setup button, which compiles
a Hyprland plugin.

## Window titles

Rows are bounded twice, because the two bounds protect different things:
`os99-minimize` strips control characters and cuts each field and the row count
before they reach the pipe, and the widget cuts them again before they reach a
model. Titles are rendered as `Text.PlainText`, which is not the default:
without it Qt sniffs the string and may decide a window title is rich text, in
which case markup in it is obeyed and any resource it names is fetched.

## Writing configuration

The two commands that publish configuration — `os99-buttons` writing
`os99-theme.toml`, and `os99-install` writing `shell.json` — never write in
place. Each follows a symlink once (people keep these files in a dotfiles
repository, and renaming over the link would detach it), writes to a private
randomly named file beside the target, forces it to disk, and renames it over
the original. A reader sees the whole old file or the whole new one; an
interrupted run leaves the original exactly as it was. `os99-install`'s
generator log goes in a private temporary directory rather than at a predictable
name in `/tmp`, and every `shell.json` edit is preceded by a timestamped backup.

## The compositor plugin

[`hyprpm.toml`](../hyprpm.toml) pins the build. A `commit_pins` entry binds a
tested Hyprland release to a fixed commit of this repository, and `hyprpm`
checks that commit out before compiling — so what ends up inside the compositor
is a reviewed tree rather than whatever the branch says today. The pin binds the
`.so` and nothing else: the bar widget and the helpers arrive through the
Omarchy plugin clone, which `hyprpm` never touches. A Hyprland version with no
pin falls back to `HEAD`, because `hyprpm` offers nothing else for one, and pins
are added as each release is actually built and tested here.

`since_hyprland` names the oldest Hyprland this compiles against, as Hyprland's
own commit count (`hyprctl version` prints it as `commits:`). The fork calls
APIs that do not exist further back, so on an older Hyprland it would not fail
to work — it would fail to build, and a wall of C++ errors is a poor way to
learn you are on the wrong version.

The setup button builds through `hyprpm` only when no hyprbars is loaded yet.
That is deliberate: unloading and reloading plugins under a running Hyprland is
the one operation here that can take a session with it, and on a machine where
the fork is already loaded there is nothing to build anyway. The uninstall
leaves the two `hyprpm` commands to you for the same reason.

## Where the helpers live

`omarchy plugin add` clones the whole repository into the plugins directory, so
the widget's helpers (`bin/os99-run`, `os99-minimize`, `os99-buttons`,
`os99-menubar-font`, `os99-theme-reload`, `os99-install`) arrive with the widget
and stay with it. The QML finds them by walking up from its own file rather than
by looking on `$PATH`, so the copy that answers the bar is always the copy that
shipped with the bar. When they are genuinely missing, the widget says so in the
status bar and names the directory it looked in — it does not go quiet and leave
you wondering where a minimized window went.

## Behaviour on the bar

One instance of the widget exists per monitor, because a bar surface does. They
elect one poller — the first in the host's own list of them, so every instance
names the same one — and it hands the result to the rest; otherwise three
monitors would mean three `hyprctl` calls a tick for one answer. The election is
made at each tick rather than once, so a monitor plugged in or pulled out
settles itself.

Omarchy enables a plugin that is both a bar widget and a panel through its
bar-layout entry alone, so **taking the widget off the bar also unmounts the
right-click menu**. The widget costs nothing while it is idle — it collapses to
zero width until something is minimized or something needs attention — so
leaving it in place is the cheap way to keep the menu. `os99-buttons` configures
the boxes from a terminal either way.
