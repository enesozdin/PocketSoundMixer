#include "AppIcon.h"

#include <algorithm>
#include <cmath>

namespace psm {

std::vector<std::uint8_t> makeAppIcon(int size)
{
    std::vector<std::uint8_t> px(static_cast<size_t>(size) * size * 4, 0);
    const float s = static_cast<float>(size);
    const float radius = s * 0.2f;
    // Knob height of each fader, 0 = bottom, 1 = top.
    const float knobs[3] = {0.7f, 0.35f, 0.55f};

    const auto put = [&](int x, int y, std::uint8_t r, std::uint8_t g, std::uint8_t b, float a) {
        std::uint8_t* p = &px[(static_cast<size_t>(y) * size + x) * 4];
        const float src = std::clamp(a, 0.0f, 1.0f);
        const float dst = p[3] / 255.0f;
        const float out = src + dst * (1.0f - src);
        if (out <= 0.0f) return;
        const auto mix = [&](std::uint8_t c, std::uint8_t d) {
            return static_cast<std::uint8_t>((c * src + d * dst * (1.0f - src)) / out + 0.5f);
        };
        p[0] = mix(r, p[0]);
        p[1] = mix(g, p[1]);
        p[2] = mix(b, p[2]);
        p[3] = static_cast<std::uint8_t>(out * 255.0f + 0.5f);
    };

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float cx = x + 0.5f;
            const float cy = y + 0.5f;
            // Rounded tile, anti-aliased edge.
            const float dx = std::max({radius - cx, 0.0f, cx - (s - radius)});
            const float dy = std::max({radius - cy, 0.0f, cy - (s - radius)});
            const float tile = std::clamp(radius - std::sqrt(dx * dx + dy * dy) + 0.5f, 0.0f, 1.0f);
            put(x, y, 30, 26, 40, tile);

            for (int i = 0; i < 3; ++i) {
                const float fx = s * (0.27f + 0.23f * i);
                const float top = s * 0.18f;
                const float bottom = s * 0.82f;
                // Track: a thin vertical line.
                const float trackW = std::max(1.0f, s * 0.06f);
                if (cy > top && cy < bottom) {
                    const float cover = std::clamp(trackW * 0.5f - std::fabs(cx - fx) + 0.5f, 0.0f, 1.0f);
                    put(x, y, 120, 112, 140, cover * tile);
                }
                // Knob: a rounded bar across the track.
                const float ky = bottom - (bottom - top) * knobs[i];
                const float kw = s * 0.085f;
                const float kh = s * 0.055f;
                const float ex = std::max(std::fabs(cx - fx) - kw, 0.0f);
                const float ey = std::max(std::fabs(cy - ky) - kh, 0.0f);
                const float knob = std::clamp(s * 0.03f - std::sqrt(ex * ex + ey * ey) + 0.5f, 0.0f, 1.0f);
                put(x, y, 150, 100, 230, knob * tile);
            }
        }
    }
    return px;
}

} // namespace psm
