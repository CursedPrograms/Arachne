// Arachne - the tracer: a picture into an SVG of filled shapes. Written for Arachne (MIT):
//   1. colours: k-means in CIELAB on a sample of the pixels (k = colours), every pixel to its nearest
//   2. speckles: blobs under the speckle size merged into the neighbour they touch most
//   3. layers: the colours stacked largest first; each layer is its colour plus every colour above it, so shapes
//      overlap and there are never hairline gaps between them
//   4. outlines: each layer's exact pixel outline (holes too), its stair-steps flattened (Douglas-Peucker), corners
//      kept where it turns sharper than the corner angle, smooth cubic Beziers fitted between them (least squares,
//      Newton reparameterisation, split where the error is worst)
#pragma once
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace Trace {

struct Image {                         // 8-bit RGB
    int w = 0, h = 0;
    std::vector<uint8_t> px;
    bool empty() const { return px.empty(); }
};

struct Settings {
    int colours = 12;                  // 2..64
    int speckle = 16;                  // blobs smaller than this (pixels, at the working size) are merged away
    int workSize = 1024;               // the longest side traced at (the SVG keeps the picture's own size)
    float smoothInput = 0;             // 0..3: blur before the colours are found (noisy photos)
    float straighten = 1.0f;           // 0.3..4 px: how far an outline may move to become a straight line
    float corner = 100;                // degrees: turns sharper than this stay corners (180: everything smooth)
    float curveFit = 1.0f;             // 0.2..6 px: how far a curve may stray from the outline (bigger: fewer nodes)
    bool dropBackground = false;       // the biggest colour left out (transparent)
    bool outlines = false;             // shapes as strokes (no fill): line art
    float strokeWidth = 1.5f;
    int seed = 1;
};

struct Result {
    std::string svg;
    int shapes = 0, nodes = 0, colours = 0;
    Image quantised;                   // the picture in its colours (at the working size), for the view
};

// returns false if stopped (cancel set) or the picture is empty
bool trace(const Image& in, const Settings& s, Result& out, std::atomic<bool>* cancel = nullptr, std::atomic<int>* percent = nullptr);

}
