#!/bin/sh
set -eu
cd "$(dirname "$0")"
app="../.pio/macos/STS3032 Control.app"
mkdir -p "$app/Contents/MacOS" ../.pio/macos/module-cache
swiftc -swift-version 5 -parse-as-library -target "$(uname -m)-apple-macos13.0" \
    -module-cache-path "$PWD/../.pio/macos/module-cache" \
    -o "$app/Contents/MacOS/ServoControl" ServoControl.swift
cat > "$app/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "https://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key><string>ServoControl</string>
    <key>CFBundleIdentifier</key><string>local.sts3032.control</string>
    <key>CFBundleName</key><string>STS3032 Control</string>
    <key>CFBundlePackageType</key><string>APPL</string>
</dict>
</plist>
PLIST
if [ "${1:-}" != "--build-only" ]; then
    open "$app"
fi
