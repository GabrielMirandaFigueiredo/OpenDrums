#!/bin/bash
# Downloads the open-source kits and converts them to OpenDrums kits (needs git, ffmpeg, python3 + numpy).
#   usage: tools/build_real_kits.sh <work dir for the downloads> <output kits dir> [jobs]
# Licenses: Big Rusty Drums, Unruly Drums, Gogodze Phu Vol I and VCSL are CC0; DRSKit and MuldjordKit are CC-BY-4.0
# (attribution is in NOTICE.md and in each kit.json).
set -euo pipefail
WORK="$1"; OUT="$2"; JOBS="${3:-4}"
HERE="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "$WORK" "$OUT"
clone() { [ -d "$WORK/$1" ] || git clone --depth 1 "https://github.com/$2" "$WORK/$1"; }

echo "== Big Rusty Drums (CC0)"
clone brd sfzinstruments/karoryfer.big-rusty-drums
python3 "$HERE/convert_kit.py" "$HERE/recipes/big-rusty-drums.json" "$WORK/brd" "$OUT" --jobs "$JOBS"

echo "== Unruly Drums (CC0)"
clone unruly sfzinstruments/karoryfer.unruly-drums
python3 "$HERE/convert_kit.py" "$HERE/recipes/unruly-drums.json" "$WORK/unruly" "$OUT" --jobs "$JOBS"

echo "== Percussao latina (Gogodze Phu Vol I + VCSL, CC0)"
clone gogodze sfzinstruments/karoryfer.gogodze-phu-vol-i
if [ ! -d "$WORK/vcsl" ]; then
  git clone --depth 1 --filter=blob:none --sparse https://github.com/sgossner/VCSL "$WORK/vcsl"
  git -C "$WORK/vcsl" sparse-checkout set \
    "Membranophones/Struck Membranophones/Bongos" "Membranophones/Struck Membranophones/Conga" \
    "Idiophones/Struck Idiophones/Shaker, Small" "Idiophones/Struck Idiophones/Cowbells" \
    "Idiophones/Struck Idiophones/Tambourine 1" "Idiophones/Struck Idiophones/Triangles" \
    "Idiophones/Struck Idiophones/Agogo Bells"
fi
python3 "$HERE/convert_kit.py" "$HERE/recipes/percussao-latina.json" "$WORK/gogodze" "$OUT" --extra-source "vcsl=$WORK/vcsl" --jobs "$JOBS"

echo "== MuldjordKit (CC-BY-4.0)"
clone muldjord sfzinstruments/DrumGizmo.MuldjordKit
python3 "$HERE/convert_drumgizmo.py" "$WORK/muldjord/DrumGizmo/MuldjordKit" "$OUT" --id muldjord-kit --name "MuldjordKit" \
  --license CC-BY-4.0 --author "Lars Muldjord (drumgizmo.org)" --source https://github.com/sfzinstruments/DrumGizmo.MuldjordKit \
  --map "KdrumR=-,Tom4=floor,RideL=-,RideLBell=-,CrashL=crash1,CrashR=crash2,SnareRest=-" --jobs "$JOBS"

echo "== DRSKit (CC-BY-4.0)"
clone drs sfzinstruments/DrumGizmo.DRSKit
python3 "$HERE/convert_drumgizmo.py" "$WORK/drs/DrumGizmo/DRSKit" "$OUT" --id drskit --name "DRSKit" \
  --license CC-BY-4.0 --author "Michael Wolff (drumgizmo.org)" --source https://github.com/sfzinstruments/DrumGizmo.DRSKit \
  --map "Crash_left_tip=crash1,Crash_right_tip=crash2,Ride_tip=ride,Ride_tip_bell=ride:bell,Snare_rim=snare:rimshot" --jobs "$JOBS"

# the generated recipes are only working files
rm -f "$OUT"/*.recipe.json
echo "== done:"; du -sh "$OUT"/*/
