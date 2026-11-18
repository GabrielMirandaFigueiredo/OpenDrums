#!/bin/bash
# usage: build_pkg.sh <version> <artefacts dir (…/OpenDrums_artefacts/Release)> <out dir>
# Builds an unsigned installer .pkg (VST3, AU, standalone app, sample content) wrapped in a .dmg.
set -euo pipefail
VERSION="$1"; ART="$(cd "$2" && pwd)"; OUT="$3"
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
mkdir -p "$OUT"; OUT="$(cd "$OUT" && pwd)"
STAGE="$(mktemp -d)"; PKGVER="${VERSION%%-*}"

install -d "$STAGE/Library/Audio/Plug-Ins/VST3" "$STAGE/Library/Audio/Plug-Ins/Components" "$STAGE/Applications" \
           "$STAGE/Library/Application Support/OpenDrums"
cp -R "$ART/VST3/OpenDrums.vst3"      "$STAGE/Library/Audio/Plug-Ins/VST3/"
[ -d "$ART/AU/OpenDrums.component" ] && cp -R "$ART/AU/OpenDrums.component" "$STAGE/Library/Audio/Plug-Ins/Components/"
cp -R "$ART/Standalone/OpenDrums.app" "$STAGE/Applications/"
cp -R "$REPO/kits"    "$STAGE/Library/Application Support/OpenDrums/Kits"
cp -R "$REPO/grooves" "$STAGE/Library/Application Support/OpenDrums/Grooves"

# ad-hoc signature so Apple-silicon Macs will load the binaries (a real Developer ID signature is better, see README)
for b in "$STAGE/Library/Audio/Plug-Ins/VST3/OpenDrums.vst3" "$STAGE/Library/Audio/Plug-Ins/Components/OpenDrums.component" "$STAGE/Applications/OpenDrums.app"; do
  [ -e "$b" ] && codesign --force --deep --sign - "$b"
done

pkgbuild --root "$STAGE" --identifier org.opendrums.OpenDrums --version "$PKGVER" --install-location / "$OUT/OpenDrums.pkg"
DMGDIR="$(mktemp -d)"; cp "$OUT/OpenDrums.pkg" "$DMGDIR/"; cp "$REPO/README.md" "$DMGDIR/"
hdiutil create -volname "OpenDrums $VERSION" -srcfolder "$DMGDIR" -ov -format UDZO "$OUT/OpenDrums-$VERSION-macOS-universal.dmg"
rm -f "$OUT/OpenDrums.pkg"
ls -la "$OUT"
