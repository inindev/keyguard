#include <IOKit/IOKitLib.h>
#include <IOKit/hid/IOHIDManager.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <CoreFoundation/CoreFoundation.h>
#include <mach/mach_time.h>

// Snoop on keyboard HID events to identify USB HID usage IDs.
// Reports values in the hidutil format that UserKeyMapping expects.
// Tracks fn key state and reads FnFunctionUsageMap from the keyboard
// so that pressing e.g. the moon key (without fn) shows the DND code,
// not F6.

static int show_all = 0;   // -a flag: show UP events too
static int raw_mode = 0;   // -r flag: no filtering at all
static int single_mode = 0; // -1 flag: capture one key and exit
static volatile sig_atomic_t running = 1;

// --- Fn function key map (read from device) ---

#define MAX_FN_MAP 24

typedef struct {
    uint32_t src;   // packed 0xPPPPUUUU (e.g. 0x0007003F = keyboard F6)
    uint32_t dst;   // packed 0xPPPPUUUU (e.g. 0x0001009B = DND)
} fn_map_entry_t;

static fn_map_entry_t fn_map[MAX_FN_MAP];
static int fn_map_count = 0;
static volatile int fn_held = 0;

// Dedup: suppress duplicate events from multiple HID interfaces
static uint64_t last_hidutil_value = 0;
static int last_pressed = -1;
static uint64_t last_event_time = 0;
static struct termios orig_termios;
static int termios_saved = 0;

// Parse "0x0007003a,0x00ff0005,0x0007003b,..." into fn_map
static void parse_fn_map(const char *str) {
    fn_map_count = 0;
    const char *p = str;
    while (*p && fn_map_count < MAX_FN_MAP) {
        char *end;
        unsigned long src = strtoul(p, &end, 16);
        if (*end != ',') break;
        p = end + 1;
        unsigned long dst = strtoul(p, &end, 16);
        fn_map[fn_map_count].src = (uint32_t)src;
        fn_map[fn_map_count].dst = (uint32_t)dst;
        fn_map_count++;
        if (*end == ',') p = end + 1;
        else break;
    }
}

// Look up the alternate (fn-remapped) code for a key.
// Returns true if found, and sets *dst_page/*dst_usage.
static int fn_map_lookup(uint32_t src_page, uint32_t src_usage,
                         uint32_t *dst_page, uint32_t *dst_usage) {
    uint32_t src = (src_page << 16) | src_usage;
    for (int i = 0; i < fn_map_count; i++) {
        if (fn_map[i].src == src) {
            *dst_page = fn_map[i].dst >> 16;
            *dst_usage = fn_map[i].dst & 0xFFFF;
            return 1;
        }
    }
    return 0;
}

// --- Human-readable key names ---

static const char *key_name_0x07(uint32_t usage) {
    switch (usage) {
    case 0x04: return "A";
    case 0x05: return "B";
    case 0x06: return "C";
    case 0x07: return "D";
    case 0x08: return "E";
    case 0x09: return "F";
    case 0x0A: return "G";
    case 0x0B: return "H";
    case 0x0C: return "I";
    case 0x0D: return "J";
    case 0x0E: return "K";
    case 0x0F: return "L";
    case 0x10: return "M";
    case 0x11: return "N";
    case 0x12: return "O";
    case 0x13: return "P";
    case 0x14: return "Q";
    case 0x15: return "R";
    case 0x16: return "S";
    case 0x17: return "T";
    case 0x18: return "U";
    case 0x19: return "V";
    case 0x1A: return "W";
    case 0x1B: return "X";
    case 0x1C: return "Y";
    case 0x1D: return "Z";
    case 0x1E: return "1";
    case 0x1F: return "2";
    case 0x20: return "3";
    case 0x21: return "4";
    case 0x22: return "5";
    case 0x23: return "6";
    case 0x24: return "7";
    case 0x25: return "8";
    case 0x26: return "9";
    case 0x27: return "0";
    case 0x28: return "Return";
    case 0x29: return "Escape";
    case 0x2A: return "Backspace";
    case 0x2B: return "Tab";
    case 0x2C: return "Space";
    case 0x2D: return "Minus";
    case 0x2E: return "Equal";
    case 0x2F: return "LeftBracket";
    case 0x30: return "RightBracket";
    case 0x31: return "Backslash";
    case 0x33: return "Semicolon";
    case 0x34: return "Quote";
    case 0x35: return "Grave";
    case 0x36: return "Comma";
    case 0x37: return "Period";
    case 0x38: return "Slash";
    case 0x39: return "CapsLock";
    case 0x3A: return "F1";
    case 0x3B: return "F2";
    case 0x3C: return "F3";
    case 0x3D: return "F4";
    case 0x3E: return "F5";
    case 0x3F: return "F6";
    case 0x40: return "F7";
    case 0x41: return "F8";
    case 0x42: return "F9";
    case 0x43: return "F10";
    case 0x44: return "F11";
    case 0x45: return "F12";
    case 0x46: return "PrintScreen";
    case 0x47: return "ScrollLock";
    case 0x48: return "Pause";
    case 0x49: return "Insert";
    case 0x4A: return "Home";
    case 0x4B: return "PageUp";
    case 0x4C: return "Delete";
    case 0x4D: return "End";
    case 0x4E: return "PageDown";
    case 0x4F: return "RightArrow";
    case 0x50: return "LeftArrow";
    case 0x51: return "DownArrow";
    case 0x52: return "UpArrow";
    case 0x65: return "Application";
    case 0xE0: return "LeftControl";
    case 0xE1: return "LeftShift";
    case 0xE2: return "LeftAlt";
    case 0xE3: return "LeftGUI";
    case 0xE4: return "RightControl";
    case 0xE5: return "RightShift";
    case 0xE6: return "RightAlt";
    case 0xE7: return "RightGUI";
    default:   return NULL;
    }
}

static const char *key_name_0x0C(uint32_t usage) {
    switch (usage) {
    case 0x30:  return "Power";
    case 0xB4:  return "Rewind";
    case 0xB3:  return "FastForward";
    case 0xB5:  return "NextTrack";
    case 0xB6:  return "PrevTrack";
    case 0xB7:  return "StopMedia";
    case 0xCD:  return "PlayPause";
    case 0xCF:  return "Dictation";
    case 0xE2:  return "Mute";
    case 0xE9:  return "VolumeUp";
    case 0xEA:  return "VolumeDown";
    case 0x221: return "Spotlight";
    case 0x22F: return "DoNotDisturb";
    case 0x19E: return "Lock";
    case 0x29D: return "KeyboardBrightnessDown";
    case 0x29E: return "KeyboardBrightnessUp";
    case 0x70:  return "DisplayBrightness";
    default:    return NULL;
    }
}

static const char *key_name_0x01(uint32_t usage) {
    switch (usage) {
    case 0x9B: return "DoNotDisturb";
    default:   return NULL;
    }
}

static const char *key_name_0xFF(uint32_t usage) {
    switch (usage) {
    case 0x03: return "Fn";
    case 0x04: return "BrightnessUp";
    case 0x05: return "BrightnessDown";
    default:   return NULL;
    }
}

static const char *key_name_0xFF01(uint32_t usage) {
    switch (usage) {
    case 0x10: return "MissionControl";
    default:   return NULL;
    }
}

static const char *key_name(uint32_t page, uint32_t usage) {
    switch (page) {
    case 0x01:   return key_name_0x01(usage);
    case 0x07:   return key_name_0x07(usage);
    case 0x0C:   return key_name_0x0C(usage);
    case 0xFF:   return key_name_0xFF(usage);
    case 0xFF01: return key_name_0xFF01(usage);
    default:     return NULL;
    }
}

// --- Read FnFunctionUsageMap from IORegistry ---

static void load_fn_map(void) {
    // The map lives on AppleHIDKeyboardEventDriverV2 in the IORegistry,
    // not on the HID device itself.
    CFMutableDictionaryRef match = IOServiceMatching("AppleHIDKeyboardEventDriverV2");
    if (!match) return;

    io_iterator_t iter;
    if (IOServiceGetMatchingServices(kIOMainPortDefault, match, &iter) != KERN_SUCCESS)
        return;

    io_service_t service;
    while ((service = IOIteratorNext(iter)) != 0) {
        CFTypeRef prop = IORegistryEntryCreateCFProperty(
            service, CFSTR("FnFunctionUsageMap"),
            kCFAllocatorDefault, 0);
        IOObjectRelease(service);

        if (!prop) continue;
        if (CFGetTypeID(prop) != CFStringGetTypeID()) {
            CFRelease(prop);
            continue;
        }

        char buf[1024];
        if (CFStringGetCString(prop, buf, sizeof(buf), kCFStringEncodingUTF8)) {
            parse_fn_map(buf);
            // Map loaded silently; use -r to debug
        }
        CFRelease(prop);
        break;  // use first one found
    }
    IOObjectRelease(iter);
}

// --- Input callback ---

static void input_callback(void *context __attribute__((unused)),
                           IOReturn result __attribute__((unused)),
                           void *sender __attribute__((unused)),
                           IOHIDValueRef value) {
    IOHIDElementRef element = IOHIDValueGetElement(value);
    uint32_t usage_page = IOHIDElementGetUsagePage(element);
    uint32_t usage_id = IOHIDElementGetUsage(element);
    CFIndex pressed = IOHIDValueGetIntegerValue(value);

    // Track fn key state (Apple Vendor Top Case page 0x00FF, usage 0x0003)
    if (usage_page == 0xFF && usage_id == 0x03) {
        fn_held = pressed ? 1 : 0;
        if (!raw_mode) return;  // don't print fn itself in normal mode
    }

    if (!raw_mode) {
        // Only report keyboard/keypad keys and consumer (media) keys
        if (usage_page != 0x07 && usage_page != 0x0C)
            return;

        // Skip sentinel/rollover and "no event" usages
        if (usage_id == 0x00 || usage_id == 0x01 || usage_id >= 0xFFFF)
            return;

        // By default only show key-down events
        if (!show_all && !pressed)
            return;
    }

    // If fn is NOT held and this key has an fn remap, show the remapped code.
    // This is what hidutil's UserKeyMapping will see.
    uint32_t report_page = usage_page;
    uint32_t report_usage = usage_id;
    int remapped = 0;

    if (!fn_held && !raw_mode) {
        uint32_t dst_page, dst_usage;
        if (fn_map_lookup(usage_page, usage_id, &dst_page, &dst_usage)) {
            report_page = dst_page;
            report_usage = dst_usage;
            remapped = 1;
        }
    }

    uint64_t hidutil_value = ((uint64_t)report_page << 32) | report_usage;

    // Dedup: skip if same key/direction within 50ms (multiple HID interfaces)
    if (!raw_mode) {
        uint64_t now = mach_absolute_time();
        static mach_timebase_info_data_t timebase;
        if (timebase.denom == 0) mach_timebase_info(&timebase);
        uint64_t elapsed_ns = (now - last_event_time) * timebase.numer / timebase.denom;

        if (hidutil_value == last_hidutil_value &&
            (int)pressed == last_pressed &&
            elapsed_ns < 50000000ULL) {  // 50ms
            return;
        }
        last_hidutil_value = hidutil_value;
        last_pressed = (int)pressed;
        last_event_time = now;
    }

    const char *name = key_name(report_page, report_usage);
    const char *direction = pressed ? "DOWN" : "UP  ";

    if (raw_mode) {
        printf("[%s] page=0x%04X usage=0x%04X  0x%llX",
               direction, usage_page, usage_id,
               (unsigned long long)hidutil_value);
        if (name) printf("  (%s)", name);
        printf("\n");
    } else if (show_all) {
        if (name)
            printf("[%s] %-20s  0x%llX%s\n", direction, name,
                   (unsigned long long)hidutil_value,
                   remapped ? "  (fn-remapped)" : "");
        else
            printf("[%s] 0x%02X:0x%04X %13s  0x%llX%s\n", direction,
                   report_page, report_usage, "",
                   (unsigned long long)hidutil_value,
                   remapped ? "  (fn-remapped)" : "");
    } else {
        if (name)
            printf("%-20s  0x%llX\n", name,
                   (unsigned long long)hidutil_value);
        else
            printf("0x%02X:0x%04X %13s  0x%llX\n",
                   report_page, report_usage, "",
                   (unsigned long long)hidutil_value);
    }
    fflush(stdout);

    if (single_mode) {
        running = 0;
        CFRunLoopStop(CFRunLoopGetMain());
    }
}

static void restore_terminal(void) {
    if (termios_saved)
        tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
}

static void handle_signal(int sig) {
    (void)sig;
    running = 0;
    CFRunLoopStop(CFRunLoopGetMain());
}

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s [-a] [-r] [-1]\n", prog);
    fprintf(stderr, "  -a  Show all events (including key-up)\n");
    fprintf(stderr, "  -r  Raw mode: no filtering, show every HID event\n");
    fprintf(stderr, "  -1  Capture one key press and exit\n");
}

static CFMutableDictionaryRef make_match(int32_t page, int32_t usage) {
    CFMutableDictionaryRef match = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 0,
        &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);
    CFNumberRef page_num = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &page);
    CFNumberRef usage_num = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &usage);
    CFDictionarySetValue(match, CFSTR(kIOHIDDeviceUsagePageKey), page_num);
    CFDictionarySetValue(match, CFSTR(kIOHIDDeviceUsageKey), usage_num);
    CFRelease(page_num);
    CFRelease(usage_num);
    return match;
}

int main(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-a") == 0) {
            show_all = 1;
        } else if (strcmp(argv[i], "-r") == 0) {
            raw_mode = 1;
            show_all = 1;
        } else if (strcmp(argv[i], "-1") == 0) {
            single_mode = 1;
        } else {
            print_usage(argv[0]);
            return 1;
        }
    }

    // Put terminal in raw mode to suppress escape sequences from keys
    // that pass through (e.g. fn+F6 producing ^[[17~)
    if (isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &orig_termios) == 0) {
        termios_saved = 1;
        atexit(restore_terminal);
        struct termios raw = orig_termios;
        raw.c_lflag &= ~(ICANON | ECHO);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    }

    if (!single_mode) {
        printf("Snooping keyboard HID events. Press keys to see hidutil values.\n");
        printf("Ctrl+C to stop.%s\n\n",
               show_all ? "" : " Use -a to include key-up events.");
    }

    IOHIDManagerRef manager = IOHIDManagerCreate(kCFAllocatorDefault,
                                                 kIOHIDOptionsTypeNone);
    if (!manager) {
        fprintf(stderr, "Error: failed to create HID manager.\n");
        return 1;
    }

    if (raw_mode) {
        // Match all HID devices so we don't miss vendor-specific pages
        IOHIDManagerSetDeviceMatching(manager, NULL);
    } else {
        // Match keyboards, consumer devices, and Apple Vendor Top Case (for fn key)
        CFMutableDictionaryRef kb_match = make_match(0x01, 0x06);
        CFMutableDictionaryRef consumer_match = make_match(0x0C, 0x01);
        CFMutableDictionaryRef topcase_match = make_match(0xFF, 0x01);

        CFDictionaryRef matches[] = { kb_match, consumer_match, topcase_match };
        CFArrayRef match_array = CFArrayCreate(kCFAllocatorDefault,
                                               (const void **)matches, 3,
                                               &kCFTypeArrayCallBacks);
        IOHIDManagerSetDeviceMatchingMultiple(manager, match_array);
        CFRelease(kb_match);
        CFRelease(consumer_match);
        CFRelease(topcase_match);
        CFRelease(match_array);
    }

    load_fn_map();
    IOHIDManagerRegisterInputValueCallback(manager, input_callback, NULL);
    IOHIDManagerScheduleWithRunLoop(manager, CFRunLoopGetMain(), kCFRunLoopDefaultMode);

    IOReturn ret = IOHIDManagerOpen(manager, kIOHIDOptionsTypeNone);
    if (ret != kIOReturnSuccess) {
        fprintf(stderr,
            "Error: failed to open HID manager (0x%x).\n"
            "Grant Input Monitoring permission:\n"
            "  System Settings > Privacy & Security > Input Monitoring\n"
            "Then add this program (or Terminal) to the list.\n", ret);
        CFRelease(manager);
        return 1;
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    // Drain any events already queued (e.g. the Enter key from launching)
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, false);

    while (running) {
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 1.0, true);
    }

    IOHIDManagerClose(manager, kIOHIDOptionsTypeNone);
    CFRelease(manager);
    return 0;
}
