#include "app_icon.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

struct Pt { float x, y; };

// Stylized ECG heartbeat trace, normalized to the icon's [0,1] square.
constexpr Pt kPulse[] = {
    {0.05f, 0.55f}, {0.22f, 0.55f}, {0.30f, 0.62f}, {0.38f, 0.20f},
    {0.46f, 0.85f}, {0.54f, 0.55f}, {0.64f, 0.40f}, {0.72f, 0.55f}, {0.95f, 0.55f},
};
constexpr int kPulseSegs = (int)(sizeof(kPulse) / sizeof(kPulse[0])) - 1;

float DistToSegment(float px, float py, Pt a, Pt b) {
    float vx = b.x - a.x, vy = b.y - a.y;
    float wx = px - a.x, wy = py - a.y;
    float len2 = vx * vx + vy * vy;
    float t = len2 > 0.0f ? std::clamp((wx * vx + wy * vy) / len2, 0.0f, 1.0f) : 0.0f;
    float dx = px - (a.x + t * vx);
    float dy = py - (a.y + t * vy);
    return std::sqrt(dx * dx + dy * dy);
}

// Signed distance to a rounded square of side `size` and corner radius
// `radius`; negative inside, positive outside.
float RoundedSquareSDF(float px, float py, float size, float radius) {
    float half = size * 0.5f;
    float qx = std::fabs(px - half) - (half - radius);
    float qy = std::fabs(py - half) - (half - radius);
    float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
    return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - radius;
}

} // namespace

namespace AppIcon {

std::vector<unsigned char> Render(int size) {
    std::vector<unsigned char> out((size_t)size * size * 4, 0);

    const float bg[3]   = {11.0f, 12.0f, 16.0f};
    const float line[3] = {111.0f, 207.0f, 142.0f}; // Theme::kAccentCpu (RefinedDark)

    float radius = size * 0.20f;
    float thickness = std::max(1.5f, size * 0.09f);

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float fx = x + 0.5f, fy = y + 0.5f;

            float bgD = RoundedSquareSDF(fx, fy, (float)size, radius);
            float bgCoverage = std::clamp(0.5f - bgD, 0.0f, 1.0f);

            float lineDist = 1e9f;
            for (int i = 0; i < kPulseSegs; ++i) {
                Pt a{kPulse[i].x * size, kPulse[i].y * size};
                Pt b{kPulse[i + 1].x * size, kPulse[i + 1].y * size};
                lineDist = std::min(lineDist, DistToSegment(fx, fy, a, b));
            }
            float lineCoverage = std::clamp(thickness * 0.5f - lineDist + 0.5f, 0.0f, 1.0f);

            unsigned char* px = &out[((size_t)y * size + x) * 4];
            for (int c = 0; c < 3; ++c) {
                float v = bg[c] + (line[c] - bg[c]) * lineCoverage;
                px[c] = (unsigned char)std::clamp(v, 0.0f, 255.0f);
            }
            px[3] = (unsigned char)(bgCoverage * 255.0f);
        }
    }

    return out;
}

std::string RenderSVG() {
    std::string points;
    for (const Pt& p : kPulse) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%s%.0f,%.0f", points.empty() ? "" : " ", p.x * 100.0f, p.y * 100.0f);
        points += buf;
    }
    return "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 100\">"
           "<rect width=\"100\" height=\"100\" rx=\"20\" fill=\"rgb(11,12,16)\"/>"
           "<polyline points=\"" + points + "\" fill=\"none\" stroke=\"rgb(111,207,142)\" "
           "stroke-width=\"9\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>"
           "</svg>";
}

} // namespace AppIcon
