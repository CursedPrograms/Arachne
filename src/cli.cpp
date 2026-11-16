// arachne-cli: a picture into an SVG, from the command line (the same tracer as the window).
//   arachne-cli in.png out.svg [--colours 12] [--speckle 16] [--work 1024] [--smooth 0] [--straighten 1]
//                              [--corner 100] [--fit 1] [--drop-background] [--outlines] [--render out.png]
// --render: the SVG drawn back to a PNG (nanosvg), and how far it is from the picture (mean difference, 0-255).
#include "trace.h"

#include <stb_image.h>
#include <stb_image_write.h>
#define NANOSVG_IMPLEMENTATION
#include <nanosvg.h>
#define NANOSVGRAST_IMPLEMENTATION
#include <nanosvgrast.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc == 6 && std::string(argv[1]) == "--render-only") {   // arachne-cli --render-only in.svg w h out.png (tests)
        std::ifstream f(argv[2], std::ios::binary); std::string t((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        std::vector<char> text(t.begin(), t.end()); text.push_back(0);
        const int w = std::atoi(argv[3]), h = std::atoi(argv[4]);
        NSVGimage* svg = nsvgParse(text.data(), "px", 96); NSVGrasterizer* rast = nsvgCreateRasterizer();
        std::vector<unsigned char> rgba((size_t)w * h * 4, 0);
        nsvgRasterize(rast, svg, 0, 0, 1, rgba.data(), w, h, w * 4);
        stbi_write_png(argv[5], w, h, 4, rgba.data(), w * 4);
        nsvgDeleteRasterizer(rast); nsvgDelete(svg);
        return 0;
    }
    if (argc < 3) { std::printf("arachne-cli in.png out.svg [--colours N] [--speckle N] [--work N] [--smooth F] [--straighten F] [--corner F] [--fit F] [--drop-background] [--outlines] [--render out.png]\n"); return 1; }
    Trace::Settings s; std::string render, quant;
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i]; auto num = [&] { return i + 1 < argc ? std::atof(argv[++i]) : 0.0; };
        if (a == "--colours") s.colours = (int)num(); else if (a == "--speckle") s.speckle = (int)num(); else if (a == "--work") s.workSize = (int)num();
        else if (a == "--smooth") s.smoothInput = (float)num(); else if (a == "--straighten") s.straighten = (float)num(); else if (a == "--corner") s.corner = (float)num();
        else if (a == "--fit") s.curveFit = (float)num(); else if (a == "--drop-background") s.dropBackground = true; else if (a == "--outlines") s.outlines = true;
        else if (a == "--render" && i + 1 < argc) render = argv[++i];
        else if (a == "--quantised" && i + 1 < argc) quant = argv[++i];
    }
    int w, h, n; unsigned char* px = stbi_load(argv[1], &w, &h, &n, 3);
    if (!px) { std::printf("Couldn't read %s\n", argv[1]); return 1; }
    Trace::Image im; im.w = w; im.h = h; im.px.assign(px, px + (size_t)w * h * 3); stbi_image_free(px);
    const auto t0 = std::chrono::steady_clock::now();
    Trace::Result r;
    if (!Trace::trace(im, s, r)) { std::printf("Tracing failed\n"); return 1; }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::ofstream(argv[2], std::ios::binary) << r.svg;
    if (!quant.empty()) stbi_write_png(quant.c_str(), r.quantised.w, r.quantised.h, 3, r.quantised.px.data(), r.quantised.w * 3);   // the picture in its colours, before the outlines
    std::printf("%d colours, %d shapes, %d curves, %.1f KB, %.2f s\n", r.colours, r.shapes, r.nodes, r.svg.size() / 1024.0, secs);
    if (!render.empty()) {
        std::vector<char> text(r.svg.begin(), r.svg.end()); text.push_back(0);
        NSVGimage* svg = nsvgParse(text.data(), "px", 96);
        NSVGrasterizer* rast = nsvgCreateRasterizer();
        std::vector<unsigned char> rgba((size_t)w * h * 4, 0);
        nsvgRasterize(rast, svg, 0, 0, 1, rgba.data(), w, h, w * 4);
        stbi_write_png(render.c_str(), w, h, 4, rgba.data(), w * 4);
        double diff = 0; for (size_t i = 0; i < (size_t)w * h; ++i) for (int c = 0; c < 3; ++c) diff += std::fabs((double)rgba[i * 4 + c] - im.px[i * 3 + c]);
        std::printf("drawn back: mean difference %.1f of 255\n", diff / ((double)w * h * 3));
        nsvgDeleteRasterizer(rast); nsvgDelete(svg);
    }
    return 0;
}
