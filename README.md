# disable-key

Disable specific keyboard keys on macOS using a CGEventTap.

## Build

    make

This builds both `disable-key` and `snoop-key`.

## Test it

    ./disable-key dnd

Press Ctrl+C to stop. Multiple keys can be disabled at once:

    ./disable-key dnd capslock

## Debounce mode

Instead of fully disabling a key, you can require a long press to
activate it. Short/accidental taps are suppressed; only a sustained hold
gets through.

    ./disable-key capslock:debounce          # default 1000ms
    ./disable-key capslock:debounce=500      # 500ms hold required
    ./disable-key capslock:debounce=2000     # 2s hold required

You can mix fully disabled and debounced keys:

    ./disable-key dnd capslock:debounce

## Identifying keys with snoop-key

If you need to find the keycode for a key, run:

    ./snoop-key

Then press the key you want to identify. It prints the event type and
keycode for every key press. For example, the Do Not Disturb (half-moon)
key on a MacBook shows:

    [KeyDown] keycode=178  flags=0x800100
    [KeyUp] keycode=178  flags=0x800100

Press Ctrl+C to stop. snoop-key is listen-only and won't interfere with
normal keyboard operation.

You can then use the keycode directly with disable-key:

    ./disable-key 178               # disable keycode 178 (DND)
    ./disable-key 178:debounce=500  # debounce keycode 178 at 500ms

## Install as a login service

This keeps the DND key disabled across reboots:

    sudo make install
    cp com.local.disable-key.plist ~/Library/LaunchAgents/
    launchctl load ~/Library/LaunchAgents/com.local.disable-key.plist

To change which keys are disabled, edit the `ProgramArguments` array in
the plist file, then:

    launchctl unload ~/Library/LaunchAgents/com.local.disable-key.plist
    launchctl load ~/Library/LaunchAgents/com.local.disable-key.plist

## Uninstall

    launchctl unload ~/Library/LaunchAgents/com.local.disable-key.plist
    rm ~/Library/LaunchAgents/com.local.disable-key.plist
    sudo rm /usr/local/bin/disable-key

## Accessibility permission

The program needs Accessibility access to intercept keyboard events.
On first run macOS will prompt you, or you can add it manually:

**System Settings > Privacy & Security > Accessibility**

Add either the `disable-key` binary or Terminal (if running from terminal).

## Supported key names

dnd (Focus/Do Not Disturb), f1-f12, capslock, escape -- or pass raw macOS
virtual keycodes as numbers (use `snoop-key` to find them).
