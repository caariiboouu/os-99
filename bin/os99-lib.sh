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
