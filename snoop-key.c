#include <CoreGraphics/CoreGraphics.h>
#include <stdio.h>
#include <signal.h>

// Snoop on all keyboard and system-defined events to identify keycodes.
// Press the key you want to identify and look at the output.

static CGEventRef event_callback(CGEventTapProxy proxy __attribute__((unused)),
                                 CGEventType type,
                                 CGEventRef event,
                                 void *info) {
    if (type == kCGEventTapDisabledByTimeout ||
        type == kCGEventTapDisabledByUserInput) {
        CGEventTapEnable(*(CFMachPortRef *)info, true);
        return event;
    }

    const char *type_name;
    switch (type) {
        case kCGEventKeyDown:       type_name = "KeyDown";       break;
        case kCGEventKeyUp:         type_name = "KeyUp";         break;
        case kCGEventFlagsChanged:  type_name = "FlagsChanged";  break;
        default:                    type_name = "SystemDefined";  break;
    }

    int64_t keycode = CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode);

    if (type == NX_SYSDEFINED) {
        // Media/special keys come as NX_SYSDEFINED events.
        // The key data is packed into an integer field:
        //   bits 16-31: NX key code
        //   bit 8: 0 = key down, 1 = key up (inverted)
        int64_t data1 = CGEventGetIntegerValueField(event, 84); // NSEventData1
        int nx_keycode = (int)((data1 >> 16) & 0xFF);
        int flags = (int)(data1 & 0xFFFF);
        int is_down = !(flags & 0x100);

        printf("[%s] NX keycode=%d %s  (raw data1=0x%llx)\n",
               type_name, nx_keycode, is_down ? "DOWN" : "UP",
               (unsigned long long)data1);
    } else {
        int64_t flags = CGEventGetFlags(event);
        printf("[%s] keycode=%lld  flags=0x%llx\n",
               type_name, keycode, (unsigned long long)flags);
    }

    fflush(stdout);
    return event; // pass through everything
}

static volatile sig_atomic_t running = 1;

static void handle_signal(int sig) {
    (void)sig;
    running = 0;
    CFRunLoopStop(CFRunLoopGetMain());
}

int main(void) {
    printf("Snooping all keyboard and system-defined events.\n");
    printf("Press keys to see their codes. Ctrl+C to stop.\n\n");

    CGEventMask mask = CGEventMaskBit(kCGEventKeyDown) |
                       CGEventMaskBit(kCGEventKeyUp) |
                       CGEventMaskBit(kCGEventFlagsChanged) |
                       CGEventMaskBit(NX_SYSDEFINED);

    CFMachPortRef tap = CGEventTapCreate(
        kCGHIDEventTap,
        kCGHeadInsertEventTap,
        kCGEventTapOptionListenOnly, // listen only, don't suppress
        mask,
        event_callback,
        &tap);

    if (!tap) {
        fprintf(stderr,
            "Error: failed to create event tap.\n"
            "Grant Accessibility permission:\n"
            "  System Settings > Privacy & Security > Accessibility\n");
        return 1;
    }

    CFRunLoopSourceRef source = CFMachPortCreateRunLoopSource(NULL, tap, 0);
    CFRunLoopAddSource(CFRunLoopGetMain(), source, kCFRunLoopCommonModes);
    CGEventTapEnable(tap, true);

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    while (running) {
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 1.0, true);
    }

    printf("\nDone.\n");
    CFRelease(source);
    CFRelease(tap);
    return 0;
}
