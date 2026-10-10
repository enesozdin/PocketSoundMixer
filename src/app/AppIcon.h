#pragma once

#include <cstdint>
#include <vector>

namespace psm {

// The app icon, drawn in code so no image files ship: three mixer faders on a dark rounded tile.
// RGBA, `size` x `size`, rows top to bottom.
std::vector<std::uint8_t> makeAppIcon(int size);

} // namespace psm
