#!/bin/bash
set -euo pipefail

# keyguard — disable keyboard keys via hidutil

SELF_DIR="$(cd "$(dirname "$0")" && pwd)"
SNOOP_KEY="$SELF_DIR/snoop-key"

die() { echo "Error: $*" >&2; exit 1; }

# Capture one key press via snoop-key, output "name hex"
capture_key() {
    [ -x "$SNOOP_KEY" ] || die "snoop-key not found (run 'make' first)"
    echo "Press the key to disable..." >&2
    "$SNOOP_KEY" -1
}

cmd_disable() {
    local output name hex
    output="$(capture_key)"
    [ -z "$output" ] && die "no key captured"
    name="$(echo "$output" | awk '{print $1}')"
    hex="$(echo "$output" | awk '{print $2}')"

    # Build new mapping list: existing mappings (minus this key) + new one
    # hidutil outputs decimal values; snoop-key gives hex.
    # hidutil --set accepts hex directly in its JSON-like input.
    local mappings
    mappings="$(python3 -c "
import subprocess, json, re

# Get current mappings
out = subprocess.run(['hidutil', 'property', '--get', 'UserKeyMapping'],
                     capture_output=True, text=True).stdout

# Parse plist-style output into list of (src, dst) decimal pairs
entries = []
src = None
for line in out.splitlines():
    m = re.search(r'HIDKeyboardModifierMappingSrc\s*=\s*(\d+)', line)
    if m:
        src = int(m.group(1))
    m = re.search(r'HIDKeyboardModifierMappingDst\s*=\s*(\d+)', line)
    if m and src is not None:
        entries.append((src, int(m.group(1))))
        src = None

# New key to disable (convert hex string to int)
new_src = int('$hex', 16)
null_dst = int('0xFF000001', 16)

# Filter out existing mapping for this key, then add new
entries = [(s, d) for s, d in entries if s != new_src]
entries.append((new_src, null_dst))

# Output as JSON with decimal values (hidutil accepts both)
# Format with hex values for readability
parts = []
for s, d in entries:
    parts.append('{\"HIDKeyboardModifierMappingSrc\":' + hex(s) + ',\"HIDKeyboardModifierMappingDst\":' + hex(d) + '}')
print('[' + ','.join(parts) + ']')
")"

    echo
    echo "# Disable $name by mapping $hex to 0xFF000001 (undefined usage, silently dropped)"
    echo "hidutil property --set '{\"UserKeyMapping\":$mappings}'"
    echo
    hidutil property --set "{\"UserKeyMapping\":$mappings}" >/dev/null
}

cmd_disable
