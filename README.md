# mackeys

Disable keyboard keys on macOS using `hidutil`.

## How to disable a key

Map the key to an undefined usage so it does nothing:

    hidutil property --set '{"UserKeyMapping":[{
      "HIDKeyboardModifierMappingSrc": 0x<KEY_CODE>,
      "HIDKeyboardModifierMappingDst": 0xFF000001
    }]}'

To disable multiple keys, add more entries to the `UserKeyMapping` array.

## How to disable Do Not Disturb

The moon/DND key on MacBook keyboards is `0x10000009B`:

    hidutil property --set '{"UserKeyMapping":[{
      "HIDKeyboardModifierMappingSrc": 0x10000009B,
      "HIDKeyboardModifierMappingDst": 0xFF000001
    }]}'

This disables DND while leaving fn+F6 intact.

## How to disable Caps Lock

Caps Lock is `0x700000039`:

    hidutil property --set '{"UserKeyMapping":[{
      "HIDKeyboardModifierMappingSrc": 0x700000039,
      "HIDKeyboardModifierMappingDst": 0xFF000001
    }]}'

### How to debounce Caps Lock

If you still want Caps Lock but want to prevent accidental activation,
`hidutil` has a built-in debounce. This requires a 1-second hold:

    hidutil property --set '{"CapsLockDelayOverride":1000}'

Adjust the value in milliseconds.

## How to reset

Clear all disabled keys:

    hidutil property --set '{"UserKeyMapping":[]}'

Clear CapsLock debounce:

    hidutil property --set '{"CapsLockDelayOverride":0}'

## Persistence

These settings do not survive a reboot. Run the commands again after
restarting, or save them to a script.

## Finding key codes

If you don't know the key code for the key you want to disable, build and
run the helper:

    make
    ./snoop-key

Press any key and it prints the `hidutil` code. It is fn-aware, so
pressing the moon key without fn shows `DoNotDisturb 0x10000009B`, while
fn+moon shows `F6 0x70000003F`.

There is also a script that wraps `snoop-key` and outputs the full
`hidutil` command for you:

    ./disable-key.sh

Press the key when prompted and it prints the command:

    # Disable DoNotDisturb by mapping 0x10000009B to 0xFF000001 (undefined usage, silently dropped)
    hidutil property --set '{"UserKeyMapping":[{"HIDKeyboardModifierMappingSrc":0x10000009b,"HIDKeyboardModifierMappingDst":0xff000001}]}'

### Permissions

`snoop-key` requires Input Monitoring permission. Add your terminal app at:

**System Settings > Privacy & Security > Input Monitoring**
