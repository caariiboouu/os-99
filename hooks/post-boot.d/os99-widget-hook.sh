#!/bin/bash
# The shell has not necessarily started yet when post-boot hooks run, so this
# waits in the background rather than holding the boot up to find out.
setsid "$HOME/.local/bin/os99-widget-check" 90 >/dev/null 2>&1 &
exit 0
