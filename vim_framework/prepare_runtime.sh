#!/bin/bash
# prepare_runtime.sh - Create VimRuntime.bundle from vim runtime files
#
# This script creates a minimal runtime bundle for the vim.framework
# containing only the essential files for syntax highlighting and basic operation.

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VIM_SRC="${SCRIPT_DIR}/../vim"
RUNTIME_SRC="${VIM_SRC}/runtime"
BUNDLE_DIR="${SCRIPT_DIR}/VimRuntime.bundle"

echo "Creating VimRuntime.bundle..."

# Clean up any existing bundle
rm -rf "${BUNDLE_DIR}"
mkdir -p "${BUNDLE_DIR}"

# Essential directories to include
echo "Copying syntax files..."
cp -R "${RUNTIME_SRC}/syntax" "${BUNDLE_DIR}/"

echo "Copying color schemes..."
cp -R "${RUNTIME_SRC}/colors" "${BUNDLE_DIR}/"

echo "Copying indent files..."
cp -R "${RUNTIME_SRC}/indent" "${BUNDLE_DIR}/"

echo "Copying filetype plugins..."
cp -R "${RUNTIME_SRC}/ftplugin" "${BUNDLE_DIR}/"

# Essential root-level files
echo "Copying essential runtime files..."
cp "${RUNTIME_SRC}/filetype.vim" "${BUNDLE_DIR}/" 2>/dev/null || true
cp "${RUNTIME_SRC}/ftoff.vim" "${BUNDLE_DIR}/" 2>/dev/null || true
cp "${RUNTIME_SRC}/ftplugin.vim" "${BUNDLE_DIR}/" 2>/dev/null || true
cp "${RUNTIME_SRC}/ftplugof.vim" "${BUNDLE_DIR}/" 2>/dev/null || true
cp "${RUNTIME_SRC}/indent.vim" "${BUNDLE_DIR}/" 2>/dev/null || true
cp "${RUNTIME_SRC}/indoff.vim" "${BUNDLE_DIR}/" 2>/dev/null || true
cp "${RUNTIME_SRC}/scripts.vim" "${BUNDLE_DIR}/" 2>/dev/null || true
cp "${RUNTIME_SRC}/defaults.vim" "${BUNDLE_DIR}/" 2>/dev/null || true
cp "${RUNTIME_SRC}/synmenu.vim" "${BUNDLE_DIR}/" 2>/dev/null || true

# Optional: autoload directory (needed for some plugins)
echo "Copying autoload files..."
mkdir -p "${BUNDLE_DIR}/autoload"
cp -R "${RUNTIME_SRC}/autoload/"*.vim "${BUNDLE_DIR}/autoload/" 2>/dev/null || true
# Copy subdirectories that are commonly needed
for dir in dist netrw; do
    if [ -d "${RUNTIME_SRC}/autoload/${dir}" ]; then
        cp -R "${RUNTIME_SRC}/autoload/${dir}" "${BUNDLE_DIR}/autoload/"
    fi
done

# Optional: plugin directory for standard plugins
echo "Copying plugin files..."
cp -R "${RUNTIME_SRC}/plugin" "${BUNDLE_DIR}/" 2>/dev/null || true

# Create Info.plist for the bundle
cat > "${BUNDLE_DIR}/Info.plist" << 'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleIdentifier</key>
    <string>com.holzschu.VimRuntime</string>
    <key>CFBundleName</key>
    <string>VimRuntime</string>
    <key>CFBundleVersion</key>
    <string>9.1</string>
</dict>
</plist>
EOF

# Calculate size
BUNDLE_SIZE=$(du -sh "${BUNDLE_DIR}" | cut -f1)
FILE_COUNT=$(find "${BUNDLE_DIR}" -type f | wc -l | tr -d ' ')

echo ""
echo "VimRuntime.bundle created successfully!"
echo "  Location: ${BUNDLE_DIR}"
echo "  Size: ${BUNDLE_SIZE}"
echo "  Files: ${FILE_COUNT}"
echo ""
echo "Add this bundle to your Xcode project's 'Copy Bundle Resources' build phase."
