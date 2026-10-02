// Writes AppIcon::RenderSVG() to a file — used at build time to produce the
// .desktop icon asset without hand-drawing or checking in a binary image.
#include "app_icon.h"

#include <cstdio>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <output.svg>\n", argv[0]);
        return 1;
    }
    std::string svg = AppIcon::RenderSVG();
    FILE* f = std::fopen(argv[1], "w");
    if (!f) {
        std::perror("fopen");
        return 1;
    }
    std::fwrite(svg.data(), 1, svg.size(), f);
    std::fclose(f);
    return 0;
}
