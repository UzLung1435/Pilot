#!/usr/bin/env bash
# 
# A7C2 Offset Verification Helper
# 
# This script helps verify that appFw.so offsets in pilot_menu_ui.c
# are correctly updated for the a7c2 target.
#
# Usage: ./verify_offsets.sh [path/to/a7c2/appFw.so]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_FILE="$SCRIPT_DIR/pilot_menu_ui.c"

echo "=== A7C2 Menu UI Offset Verification ==="
echo

# Extract all APPFW_* offset definitions
echo "Extracting offset definitions from pilot_menu_ui.c..."
OFFSETS=$(grep -E '^#define APPFW_.*_OFF.*0x[0-9A-Fa-f]+ull' "$SOURCE_FILE" | wc -l)
echo "Found $OFFSETS offset definitions"
echo

# Check if offsets still match a6700 values (placeholder detection)
echo "Checking for a6700 placeholder values..."
A6700_MARKERS=$(grep -E 'ILCE-6700|a6700' "$SOURCE_FILE" | grep -v "ported from" | wc -l)
if [ "$A6700_MARKERS" -gt 0 ]; then
    echo "⚠️  WARNING: Found $A6700_MARKERS references to a6700"
    echo "   This suggests offsets may still be placeholders!"
    echo
fi

# List all offset constants
echo "Offset constants defined:"
grep -E '^#define APPFW_' "$SOURCE_FILE" | \
    grep -E '0x[0-9A-Fa-f]+ull' | \
    awk '{print "  " $2 " = " $3}' | \
    sed 's/ull//' | \
    sort
echo

# If appFw.so path provided, do basic analysis
if [ $# -ge 1 ] && [ -f "$1" ]; then
    APPFW_PATH="$1"
    echo "Analyzing provided appFw.so: $APPFW_PATH"
    echo
    
    # Check file type
    FILE_TYPE=$(file "$APPFW_PATH")
    echo "File type: $FILE_TYPE"
    
    if ! echo "$FILE_TYPE" | grep -q "ELF 64-bit.*aarch64"; then
        echo "❌ ERROR: Not a valid AArch64 ELF binary"
        exit 1
    fi
    
    # Get file size
    FILE_SIZE=$(stat -f%z "$APPFW_PATH" 2>/dev/null || stat -c%s "$APPFW_PATH" 2>/dev/null)
    echo "File size: $FILE_SIZE bytes"
    echo
    
    # Check for required symbols
    echo "Checking for key symbols..."
    if command -v readelf >/dev/null 2>&1; then
        SYMBOLS=$(readelf -sW "$APPFW_PATH" 2>/dev/null | grep -E 'TextSprite.*setString|Application_System' | wc -l)
        echo "Found $SYMBOLS key symbols"
    fi
    echo
    
    # Check GOT section
    echo "Checking GOT section..."
    if command -v readelf >/dev/null 2>&1; then
        GOT_INFO=$(readelf -S "$APPFW_PATH" 2>/dev/null | grep -E '\.got')
        if [ -n "$GOT_INFO" ]; then
            echo "$GOT_INFO"
        else
            echo "⚠️  Could not find .got section"
        fi
    fi
    echo
    
    echo "✓ Basic validation complete"
    echo
    echo "Next steps:"
    echo "1. Load $APPFW_PATH in IDA Pro or Ghidra"
    echo "2. Locate GOT entries for menu table, page table, etc."
    echo "3. Find screen factory functions (0x1C3, 0x293)"
    echo "4. Update offset constants in pilot_menu_ui.c"
    echo "5. Run this script again to verify changes"
else
    echo "No appFw.so provided for analysis"
    echo
    echo "Usage: $0 [path/to/a7c2/appFw.so]"
    echo
    echo "To verify offsets:"
    echo "1. Extract appFw.so from a7c2 firmware"
    echo "2. Run: $0 /path/to/appFw.so"
    echo "3. Use IDA/Ghidra to find correct offsets"
    echo "4. Update pilot_menu_ui.c with verified values"
fi

echo
echo "=== Verification Status ==="
if [ "$A6700_MARKERS" -gt 0 ]; then
    echo "Status: ❌ NOT READY - Contains a6700 placeholders"
    echo "Action: Update offsets for a7c2 before use"
else
    echo "Status: ⚠️  UNVERIFIED - Offsets updated but not tested"
    echo "Action: Test on actual a7c2 camera"
fi
echo
