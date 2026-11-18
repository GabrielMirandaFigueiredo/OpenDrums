// OpenDrums core - cartoon backdrops for the 8 reverb rooms of the kit view.
// Same Prim list as the kit hardware, so the plugin (juce::Graphics) and the preview tool (SVG) share it.
#pragma once

#include "KitGeometry.hpp"

namespace od::geo {

// Draws room `roomIndex` (index into the reverb room table: garage, studio-s/m/l, club-s/m/l, stadium)
// into a w x h canvas, back to front. `floorY` receives the y where the floor starts (the kit stands on it).
std::vector<Prim> buildScene(int roomIndex, float w, float h, float* floorY = nullptr);

} // namespace od::geo
