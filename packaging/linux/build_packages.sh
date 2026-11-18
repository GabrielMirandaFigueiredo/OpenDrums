#!/bin/bash
# usage: build_packages.sh <version> <artefacts dir (…/OpenDrums_artefacts/Release)> <out dir>
# Builds a .deb and a .tar.gz with the same layout (VST3 in /usr/lib/vst3, app in /usr/bin, content in /usr/share/opendrums).
set -euo pipefail
VERSION="$1"; ART="$(cd "$2" && pwd)"; OUT="$3"
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
mkdir -p "$OUT"; OUT="$(cd "$OUT" && pwd)"
ROOT="$(mktemp -d)/opendrums_${VERSION}"
DEBVER="$(echo "$VERSION" | tr - "~")"

install -d "$ROOT/usr/lib/vst3" "$ROOT/usr/bin" "$ROOT/usr/share/opendrums" "$ROOT/usr/share/applications" "$ROOT/usr/share/doc/opendrums"
cp -R "$ART/VST3/OpenDrums.vst3" "$ROOT/usr/lib/vst3/"
install -m755 "$ART/Standalone/OpenDrums" "$ROOT/usr/bin/opendrums"
cp -R "$REPO/kits"    "$ROOT/usr/share/opendrums/Kits"
cp -R "$REPO/grooves" "$ROOT/usr/share/opendrums/Grooves"
cp "$REPO/README.md" "$ROOT/usr/share/doc/opendrums/"
cat > "$ROOT/usr/share/applications/opendrums.desktop" <<DESK
[Desktop Entry]
Type=Application
Name=OpenDrums
Comment=Drum VSTi with cartoon UI
Exec=opendrums
Categories=AudioVideo;Audio;Midi;
Terminal=false
DESK
find "$ROOT" -type d -exec chmod 755 {} +

tar -C "$(dirname "$ROOT")" -czf "$OUT/OpenDrums-$VERSION-Linux-x86_64.tar.gz" "$(basename "$ROOT")"

install -d "$ROOT/DEBIAN"
SIZE=$(du -sk "$ROOT/usr" | cut -f1)
cat > "$ROOT/DEBIAN/control" <<CTL
Package: opendrums
Version: $DEBVER
Section: sound
Priority: optional
Architecture: amd64
Installed-Size: $SIZE
Depends: libasound2 | libasound2t64, libfreetype6, libx11-6, libxext6, libxrandr2, libxinerama1, libxcursor1, libfontconfig1, libgl1
Maintainer: OpenDrums <noreply@example.com>
Description: OpenDrums - drum VSTi with a cartoon interface
 VST3 plugin and standalone app with kit view, mixer, groove browser and MIDI matrix.
CTL
dpkg-deb --build --root-owner-group "$ROOT" "$OUT/OpenDrums-$VERSION-Linux-amd64.deb"
ls -la "$OUT"
