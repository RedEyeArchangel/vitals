#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace AppIcon {

// Renders the vitals mark (dark rounded tile + green ECG pulse line) at
// size x size, RGBA8 top-left origin. Procedural, not loaded from a file —
// there's no image-decoding dependency in this project for one small icon.
std::vector<unsigned char> Render(int size);

// Same mark as a scalable SVG (vector, not rasterized) — for installing into
// an icon theme directory (e.g. the tray icon), where any requested size
// must render crisply and a raster PNG would need a real encoder.
std::string RenderSVG();

} // namespace AppIcon
