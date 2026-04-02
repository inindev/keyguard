#include <CoreGraphics/CoreGraphics.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

// macOS virtual keycodes for common keys to disable
// Full list: Events.h in Carbon framework
// Some useful codes:
//   F1=122 F2=120 F3=99  F4=118 F5=96  F6=97
//   F7=98  F8=100 F9=101 F10=109 F11=103 F12=111
//   CapsLock=57

#define MAX_DISABLED_KEYS 32
#define DEFAULT_DEBOUNCE_MS 1000

typedef struct {
    int64_t keycode;
    int debounce;       // 0 = fully disabled, >0 = debounce in ms
    int pressed;        // currently held down
    int activated;      // debounce timer fired while held
    CFRunLoopTimerRef timer;
} key_entry_t;

static key_entry_t keys[MAX_DISABLED_KEYS];
static int num_keys = 0;

static key_entry_t *find_key(int64_t keycode) {
    for (int i = 0; i < num_keys; i++) {
        if (keys[i].keycode == keycode)
            return &keys[i];
    }
    return NULL;
}

static void debounce_timer_fired(CFRunLoopTimerRef timer __attribute__((unused)),
                                 void *info) {
    key_entry_t *k = (key_entry_t *)info;
    if (!k->pressed) return;

    k->activated = 1;

    // Inject a synthetic key event to activate the key
    CGEventRef down = CGEventCreateKeyboardEvent(NULL, (CGKeyCode)k->keycode, true);
    CGEventPost(kCGHIDEventTap, down);
    CFRelease(down);
}

static CGEventRef event_callback(CGEventTapProxy proxy __attribute__((unused)),
                                 CGEventType type,
                                 CGEventRef event, void *info) {
    // If the tap is disabled by the system, re-enable it
    if (type == kCGEventTapDisabledByTimeout ||
        type == kCGEventTapDisabledByUserInput) {
        CGEventTapEnable(*(CFMachPortRef *)info, true);
        return event;
    }

    int64_t keycode = CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode);
    key_entry_t *k = find_key(keycode);
    if (!k) return event;

    // Fully disabled key
    if (k->debounce == 0)
        return NULL;

    // Debounce key handling
    bool is_keydown;
    if (type == kCGEventFlagsChanged) {
        // For modifier keys like caps lock, check flags to determine up/down
        CGEventFlags flags = CGEventGetFlags(event);
        is_keydown = (flags & kCGEventFlagMaskAlphaShift) != 0;
    } else {
        is_keydown = (type == kCGEventKeyDown);
    }

    if (is_keydown && !k->pressed) {
        k->pressed = 1;
        k->activated = 0;

        // Start debounce timer
        CFRunLoopTimerContext ctx = { 0, k, NULL, NULL, NULL };
        k->timer = CFRunLoopTimerCreate(
            NULL,
            CFAbsoluteTimeGetCurrent() + (k->debounce / 1000.0),
            0, 0, 0,
            debounce_timer_fired,
            &ctx);
        CFRunLoopAddTimer(CFRunLoopGetMain(), k->timer, kCFRunLoopCommonModes);

        return NULL; // suppress the initial press
    }

    if (!is_keydown && k->pressed) {
        k->pressed = 0;

        // Cancel timer if it hasn't fired
        if (k->timer) {
            CFRunLoopTimerInvalidate(k->timer);
            CFRelease(k->timer);
            k->timer = NULL;
        }

        if (k->activated) {
            k->activated = 0;
            return event; // let the key-up through
        }

        return NULL; // short press, suppress key-up too
    }

    return NULL; // suppress any other events for this key while in debounce
}

static const struct {
    const char *name;
    int64_t code;
} named_keys[] = {
    {"f1", 122}, {"f2", 120}, {"f3", 99},  {"f4", 118},
    {"f5", 96},  {"f6", 97},  {"f7", 98},  {"f8", 100},
    {"f9", 101}, {"f10", 109}, {"f11", 103}, {"f12", 111},
    {"capslock", 57}, {"escape", 53}, {"dnd", 178}, {"focus", 178},
    {NULL, 0}
};

static int parse_key(const char *arg, int64_t *out_code, int *out_debounce) {
    // Check for :debounce or :debounce=N suffix
    char buf[256];
    strncpy(buf, arg, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    *out_debounce = 0;
    char *colon = strchr(buf, ':');
    if (colon) {
        *colon = '\0';
        char *suffix = colon + 1;
        char *eq = strchr(suffix, '=');

        if (eq) {
            *eq = '\0';
            if (strcasecmp(suffix, "debounce") != 0) {
                fprintf(stderr, "Error: unknown modifier '%s'\n", suffix);
                return 0;
            }
            char *end;
            long ms = strtol(eq + 1, &end, 10);
            if (*end != '\0' || ms <= 0) {
                fprintf(stderr, "Error: invalid debounce value '%s' (must be positive integer ms)\n", eq + 1);
                return 0;
            }
            *out_debounce = (int)ms;
        } else {
            if (strcasecmp(suffix, "debounce") != 0) {
                fprintf(stderr, "Error: unknown modifier '%s'\n", suffix);
                return 0;
            }
            *out_debounce = DEFAULT_DEBOUNCE_MS;
        }
    }

    // Try named key (case-insensitive)
    for (int i = 0; named_keys[i].name; i++) {
        if (strcasecmp(buf, named_keys[i].name) == 0) {
            *out_code = named_keys[i].code;
            return 1;
        }
    }
    // Try numeric keycode
    char *end;
    long val = strtol(buf, &end, 0);
    if (*end == '\0' && end != buf && val >= 0 && val <= 255) {
        *out_code = val;
        return 1;
    }
    return 0;
}

static volatile sig_atomic_t running = 1;

static void handle_signal(int sig) {
    (void)sig;
    running = 0;
    CFRunLoopStop(CFRunLoopGetMain());
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <key> [key ...]\n", argv[0]);
        fprintf(stderr, "\nDisable one or more keyboard keys system-wide.\n");
        fprintf(stderr, "Requires Accessibility permission in System Preferences.\n");
        fprintf(stderr, "\nKeys can be names or numeric keycodes:\n");
        fprintf(stderr, "  Named:   f1-f12, capslock, escape, dnd (focus mode)\n");
        fprintf(stderr, "  Numeric: 97 (raw macOS virtual keycode)\n");
        fprintf(stderr, "\nModifiers:\n");
        fprintf(stderr, "  :debounce      only activate on long press (default %dms)\n", DEFAULT_DEBOUNCE_MS);
        fprintf(stderr, "  :debounce=N    only activate on long press of N milliseconds\n");
        fprintf(stderr, "\nExamples:\n");
        fprintf(stderr, "  %s dnd                  # disable Focus/DND key\n", argv[0]);
        fprintf(stderr, "  %s dnd capslock         # disable DND and Caps Lock\n", argv[0]);
        fprintf(stderr, "  %s capslock:debounce    # Caps Lock only on long press (%dms)\n", argv[0], DEFAULT_DEBOUNCE_MS);
        fprintf(stderr, "  %s capslock:debounce=500  # Caps Lock only on 500ms press\n", argv[0]);
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        if (num_keys >= MAX_DISABLED_KEYS) {
            fprintf(stderr, "Error: too many keys (max %d)\n", MAX_DISABLED_KEYS);
            return 1;
        }
        int64_t code;
        int debounce;
        if (!parse_key(argv[i], &code, &debounce)) {
            fprintf(stderr, "Error: unknown key '%s'\n", argv[i]);
            return 1;
        }
        keys[num_keys].keycode = code;
        keys[num_keys].debounce = debounce;
        keys[num_keys].pressed = 0;
        keys[num_keys].activated = 0;
        keys[num_keys].timer = NULL;
        num_keys++;

        if (debounce > 0)
            printf("Debounce key: %s (keycode %lld, %dms)\n", argv[i], code, debounce);
        else
            printf("Disabling key: %s (keycode %lld)\n", argv[i], code);
    }

    CGEventMask mask = CGEventMaskBit(kCGEventKeyDown) |
                       CGEventMaskBit(kCGEventKeyUp) |
                       CGEventMaskBit(kCGEventFlagsChanged);

    CFMachPortRef tap = CGEventTapCreate(
        kCGHIDEventTap,
        kCGHeadInsertEventTap,
        kCGEventTapOptionDefault,
        mask,
        event_callback,
        &tap);

    if (!tap) {
        fprintf(stderr,
            "Error: failed to create event tap.\n"
            "Grant Accessibility permission:\n"
            "  System Settings > Privacy & Security > Accessibility\n"
            "Then add this program (or Terminal) to the list.\n");
        return 1;
    }

    CFRunLoopSourceRef source = CFMachPortCreateRunLoopSource(NULL, tap, 0);
    CFRunLoopAddSource(CFRunLoopGetMain(), source, kCFRunLoopCommonModes);
    CGEventTapEnable(tap, true);

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    printf("Running. Press Ctrl+C to stop.\n");

    while (running) {
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 1.0, true);
    }

    printf("\nStopping.\n");
    CGEventTapEnable(tap, false);

    // Clean up any active timers
    for (int i = 0; i < num_keys; i++) {
        if (keys[i].timer) {
            CFRunLoopTimerInvalidate(keys[i].timer);
            CFRelease(keys[i].timer);
        }
    }

    CFRelease(source);
    CFRelease(tap);

    return 0;
}
