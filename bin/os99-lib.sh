# The few things every OS 99 script needs, in one place. Sourced, never run.
#
#   source "$(dirname -- "$(readlink -f "$0")")/os99-lib.sh"
#
# Kept deliberately small. This is not a framework; it is the two or three
# answers that were being written out separately in every script, and were
# therefore wrong in every script at once.

# ---------------------------------------------------------------- Hyprland
#
# WHICH HYPRLAND. Every one of these scripts used to do this:
#
#     HYPRLAND_INSTANCE_SIGNATURE=$(ls -t "$XDG_RUNTIME_DIR/hypr" | head -1)
#
# -- the newest instance DIRECTORY. Those directories outlive the compositors
# that made them, and their mtimes do not order themselves the way you would
# hope, so on a machine that has logged in and out a few times this reliably
# picks a corpse. Everything downstream then talks to a socket nobody is
# listening on and reports, quite calmly, that nothing is wrong.
#
# So: ask, do not guess. `hyprctl instances` enumerates them, an explicit
# signature in the environment is tried first because it may be right and older,
# and every candidate is PROVED with a call that has to reach the compositor.
# `hyprctl version` exits 4 when it cannot connect, which is the whole test.
os99_hypr_candidates() {
  local dir
  [[ -n ${HYPRLAND_INSTANCE_SIGNATURE:-} ]] && printf '%s\n' "$HYPRLAND_INSTANCE_SIGNATURE"
  hyprctl instances -j 2>/dev/null |
    sed -n 's/.*"instance": *"\([^"]*\)".*/\1/p'
  # Last resort: the directories themselves, newest first. Only ever reached
  # when hyprctl cannot enumerate, and still proved before it is used.
  dir="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/hypr"
  [[ -d $dir ]] && ls -t "$dir" 2>/dev/null
  return 0
}

# Print the signature of a Hyprland that answers, or nothing.
os99_hypr_instance() {
  local sig seen=""
  while read -r sig; do
    [[ -n $sig ]] || continue
    case " $seen " in *" $sig "*) continue ;; esac
    seen="$seen $sig"
    if HYPRLAND_INSTANCE_SIGNATURE="$sig" hyprctl version >/dev/null 2>&1; then
      printf '%s\n' "$sig"
      return 0
    fi
  done < <(os99_hypr_candidates)
  return 1
}

# Export it, or leave the environment alone and say so. Callers that can work
# without a compositor (os99-install --status) check the return; callers that
# cannot should say what they are missing rather than talk to a dead socket.
os99_use_hyprland() {
  local sig
  sig=$(os99_hypr_instance) || return 1
  export HYPRLAND_INSTANCE_SIGNATURE="$sig"
  return 0
}

# ------------------------------------------------------------------- state
#
# One directory for everything OS 99 knows that is not configuration: whether
# the frame plugin loaded, whether the widget did, whether an install was
# interrupted. Nothing in here is precious -- deleting it costs a notification,
# not a setting.
os99_state_dir() {
  local d="${XDG_STATE_HOME:-$HOME/.local/state}/os99"
  mkdir -p "$d" 2>/dev/null
  printf '%s\n' "$d"
}

# ------------------------------------------------------------------- liveness
#
# Did OS 99's QML actually load into the running shell? A widget that failed to
# load cannot report that it failed to load, so the question has to be asked
# from outside -- and the plugin's panel registers an IPC target the moment it
# is constructed, which makes "does that target answer" exactly the right
# question. No pids, no heartbeat files, no clock skew.
#
#   0  loaded          1  not loaded          2  cannot tell
#
# NOTE for anyone tempted by `omarchy-shell -q`: quiet mode is best-effort and
# exits 0 even when the shell, the target or the method is missing. It is
# useless as a probe. This deliberately does not use it.
os99_widget_loaded() {
  local try
  command -v omarchy-shell >/dev/null 2>&1 || return 2
  [[ -n $(pgrep -x quickshell 2>/dev/null) ]] || return 2
  # Three tries, because one silence is not an answer. A shell in the middle of
  # applying a theme has better things to do than answer an IPC call, and a
  # single timeout there would report the widget as broken in the middle of the
  # very run that installs it.
  for try in 1 2 3; do
    [[ $(timeout 5 omarchy-shell os99 ping 2>/dev/null) == ok ]] && return 0
    sleep 1
  done
  return 1
}

# ----------------------------------------------------------------- helpers
#
# A companion script, found BESIDE this one first. The bar widget runs the copy
# in the installed plugin directory, and the shell process's PATH is not a login
# shell's -- it does not necessarily carry ~/.local/bin. Sibling, then the usual
# install location, then PATH.
os99_sibling() {
  local here candidate
  here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd) || here=""
  for candidate in "$here/$1" "$HOME/.local/bin/$1"; do
    [[ -n $candidate && -x $candidate ]] && { printf '%s\n' "$candidate"; return 0; }
  done
  command -v "$1" 2>/dev/null
}

# ----------------------------------------------------------------------- art
#
# Copy freshly drawn art into the LIVE theme.
#
# `omarchy theme set` COPIES a theme into ~/.local/state/omarchy/current/theme;
# it does not symlink it. So regenerating art under ~/.config/omarchy/themes
# changes the SOURCE and not what is on screen, and everything looks like it
# worked. bars.lua rides along because it carries the BUTTON LIST and
# os99-window-bars evals the live copy -- without this, a box toggled in the
# config applies on the next theme switch rather than now.
#
# Returns non-zero when the live theme is not one of ours, which is not a
# failure: there is simply nothing of ours on screen to update.
os99_publish_art() {
  local live cur src
  live="$HOME/.local/state/omarchy/current/theme"
  cur=$(cat "$HOME/.local/state/omarchy/current/theme.name" 2>/dev/null || echo)
  [[ $cur == os-99-* ]] || return 1
  src="$HOME/.config/omarchy/themes/$cur"
  [[ -d $src/decor && -d $live/decor ]] || return 1
  cp -f "$src/decor/"*.png "$src/decor/bar.env" "$src/decor/bars.lua" \
        "$live/decor/" 2>/dev/null
}

# ------------------------------------------------------------------ sessions
#
# Setting OS 99 up is not one action. It copies a theme in, draws art at this
# display's scale, compiles a plugin into the compositor and switches the theme
# -- and somebody can log out in the middle of that. What makes this survivable
# is not a transaction log: it is that every one of those steps is idempotent,
# so recovery is simply running it again.
#
# What a record buys, then, is the one thing re-running cannot supply: the
# knowledge that something WAS interrupted, and what the theme was before it
# started. Written before the first change, removed only on success.
os99_session_begin() {
  local dir; dir=$(os99_state_dir)
  {
    echo "OP=$1"
    echo "PID=$$"
    echo "STARTED=$(date -Is)"
    echo "THEME_BEFORE=$(cat "$HOME/.local/state/omarchy/current/theme.name" 2>/dev/null || echo)"
  } > "$dir/session.tmp" && mv -f "$dir/session.tmp" "$dir/session"
}

os99_session_end() {
  rm -f "$(os99_state_dir)/session"
}

# Print the record if one was left behind, or nothing.
#
# A record whose process is still running is not an interruption, it is a run in
# progress -- checking that is what keeps `os99-install --status`, which the bar
# widget calls every minute, from announcing a crash every time somebody presses
# the setup button.
os99_session_interrupted() {
  local f pid
  f="$(os99_state_dir)/session"
  [[ -f $f ]] || return 1
  pid=$(sed -n 's/^PID=//p' "$f" | head -1)
  [[ -n $pid && -d /proc/$pid ]] && return 1
  cat "$f"
}

# --------------------------------------------------------------- publishing
#
# Replace a file's contents, or leave it exactly as it was. Used for every
# configuration file OS 99 writes.
#
# Follows a symlink once and then works on what it names: people keep these in
# dotfiles repositories, and renaming over the link would detach it. Writes to a
# private randomly named file beside the target (mkstemp: O_EXCL, mode 0600, a
# name nothing can predict and so nothing can pre-plant), forces it to disk, and
# renames it into place, which is atomic. A reader sees all of the old file or
# all of the new one. If anything fails, the temporary file goes and the
# original is untouched.
# The program lives in a variable rather than a heredoc, because a heredoc IS
# stdin -- `python3 - <<EOF` hands the interpreter its script on stdin and
# leaves nothing there for the data. That mistake writes an empty file, which is
# a very quiet way to lose somebody's bar layout.
read -r -d '' _os99_publish_py <<'PYEOF' || true
import os, stat, sys, tempfile

target = os.path.realpath(sys.argv[1])
data = sys.stdin.buffer.read()
if not data:
    sys.exit("os99_publish: refusing to write an empty file to %s" % target)

dirname = os.path.dirname(target) or "."
mode = stat.S_IMODE(os.stat(target).st_mode) if os.path.exists(target) else 0o644
fd, tmp = tempfile.mkstemp(prefix=".os99-", suffix=".tmp", dir=dirname)
try:
    os.fchmod(fd, mode)
    with os.fdopen(fd, "wb") as fh:
        fh.write(data)
        fh.flush()
        os.fsync(fh.fileno())
    os.replace(tmp, target)
    tmp = None
finally:
    if tmp is not None:
        try:
            os.unlink(tmp)
        except OSError:
            pass
dfd = os.open(dirname, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
try:
    os.fsync(dfd)
finally:
    os.close(dfd)
PYEOF

os99_publish() {
  python3 -c "$_os99_publish_py" "$1"
}
