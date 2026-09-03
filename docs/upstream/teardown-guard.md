# hyprbars: the compositor can stop when you unload the plugin

Written in ASD-STE100 (Simplified Technical English).

## Summary

hyprbars reads its configuration values while it draws a bar. Hyprland destroys
these values when the plugin unloads. If hyprbars draws a bar after this moment,
it reads a destroyed value. The read makes an exception. The exception leaves a
render pass. An exception that leaves a render pass stops the compositor.

## What happens

1. The user unloads the plugin. Use `hyprpm disable` or `hyprctl plugin unload`.
2. Hyprland calls `PLUGIN_EXIT()`.
3. Hyprland destroys the plugin configuration values.
4. A bar decoration draws one more frame, or Hyprland asks it for its geometry.
5. The decoration calls `config.<name>->value()`.
6. The value does not exist. The call makes an exception.
7. The exception leaves the render pass. The compositor stops.

The user sees the desktop stop. The user does not see an error message.

## Why the risk is large

`barDeco.cpp` calls `->value()` in 46 places. Each call is in a function that
Hyprland can call: `draw`, `damageEntire`, and the geometry functions. The
current `PLUGIN_EXIT()` removes the queued pass elements and the window-effect
rules. It does not stop these functions.

## Evidence

We found this problem in a fork of hyprbars. The fork adds more configuration
values. Three crashes occurred at three different functions. Two examples:

- `CIntValue::value()`, called from the render pass.
- `CStringValue::value()`, called from a geometry function.

Each crash occurred immediately after a plugin unload. A guard at one function
did not stop the next crash at a different function.

## Suggested correction

Add one flag. Set the flag at the start of `PLUGIN_EXIT()`. Test the flag at
the start of every function that Hyprland can call.

~~~cpp
// globals.hpp
struct SGlobalState {
    bool shuttingDown = false;
    // ...
};

inline bool barsShuttingDown() {
    return !g_pGlobalState || g_pGlobalState->shuttingDown;
}
~~~

~~~cpp
// main.cpp
APICALL EXPORT void PLUGIN_EXIT() {
    if (g_pGlobalState)
        g_pGlobalState->shuttingDown = true;
    // ... the existing work ...
}
~~~

~~~cpp
// barDeco.cpp -- at the start of each entry point
if (barsShuttingDown())
    return;
~~~

Do this for each function that Hyprland can call. A guard at only one function
is not sufficient. Our fork guards every entry point. After this change, two
consecutive theme changes and many unloads did not stop the compositor.

## Environment

- Hyprland 0.56.2, commit `efb50993780079460b0cbed1363e2166a2de1d9f`
- hyprbars from `hyprwm/hyprland-plugins`, branch `main`
- Arch Linux, package `hyprland 0.56.2-1`

## Related

We keep the fork at https://github.com/caariiboouu/os-99 in `plugin/hyprbars`.
It has the guard. We are pleased to send a pull request if you want one.
