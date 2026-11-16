// Arachne - the splash: a web spun of Bezier curves (its threads sag between the spokes, one curve shown with its
// handles), the name, the version, the credits and what's loading. Atelier greys and the Atelier blue, solid.
#include "splash.h"

#include <imgui.h>

#include <cfloat>
#include <cmath>

#ifndef ARACHNE_VERSION
#define ARACHNE_VERSION "0.1.0"
#endif

namespace Splash {

static ImFont *titleFont = nullptr, *smallFont = nullptr;
void setFonts(ImFont* title, ImFont* small) { titleFont = title; smallFont = small; }

static const ImU32 BG = IM_COL32(0x1a, 0x1a, 0x1a, 255), LINE = IM_COL32(0x33, 0x33, 0x33, 255),
                   TEXT = IM_COL32(0xd6, 0xd6, 0xd6, 255), DIM = IM_COL32(0x9a, 0x9a, 0x9a, 255),
                   BLUE = IM_COL32(0x00, 0x7b, 0xb7, 255), LIGHT = IM_COL32(0x34, 0xa9, 0xe3, 255), THREAD = IM_COL32(0x6a, 0x6a, 0x6a, 255);

static void text(ImDrawList* dl, ImFont* f, float size, ImVec2 p, ImU32 col, const char* s) { dl->AddText(f ? f : ImGui::GetFont(), size, p, col, s); }
static float textWidth(ImFont* f, float size, const char* s) { return (f ? f : ImGui::GetFont())->CalcTextSizeA(size, FLT_MAX, 0, s).x; }

void draw(ImDrawList* dl, float x, float y, const char* status, float progress) {
    const ImVec2 o(x, y), end(x + W, y + H);
    dl->PushClipRect(o, end, true);
    dl->AddRectFilled(o, end, BG);
    // the web: spokes from the centre, rings of threads sagging inwards between them (each a quadratic curve)
    const float cx = x + 480, cy = y + 190, R = 175; const int spokes = 11;
    auto at = [&](int k, float r) { const float a = -1.5708f + k * 6.28318f / spokes + 0.05f * std::sin(k * 1.7f); return ImVec2(cx + std::cos(a) * r, cy + std::sin(a) * r); };
    for (int k = 0; k < spokes; ++k) dl->AddLine(ImVec2(cx, cy), at(k, R), THREAD, 1.2f);
    for (int ring = 1; ring <= 7; ++ring) {
        const float r = R * (0.14f + 0.12f * ring);
        for (int k = 0; k < spokes; ++k) {
            const ImVec2 a = at(k, r), b = at(k + 1, r), m((a.x + b.x) / 2, (a.y + b.y) / 2);
            const ImVec2 c(m.x + (cx - m.x) * 0.16f, m.y + (cy - m.y) * 0.16f);   // the sag, towards the centre
            dl->AddBezierQuadratic(a, c, b, ring == 5 && k == 2 ? BLUE : THREAD, ring == 5 && k == 2 ? 3.0f : 1.2f, 16);
            if (ring == 5 && k == 2) {   // one thread shown as a curve being edited: its control point and handles
                dl->AddLine(a, c, LIGHT, 1.0f); dl->AddLine(c, b, LIGHT, 1.0f);
                dl->AddRectFilled(ImVec2(a.x - 4, a.y - 4), ImVec2(a.x + 4, a.y + 4), LIGHT);
                dl->AddRectFilled(ImVec2(b.x - 4, b.y - 4), ImVec2(b.x + 4, b.y + 4), LIGHT);
                dl->AddCircleFilled(c, 4.5f, BLUE);
            }
        }
    }
    // the name: "Arach" in the text colour, "ne" in blue
    const float ts = 50, tx = x + 40, ty = y + 92;
    text(dl, titleFont, ts, ImVec2(tx, ty), TEXT, "Arach");
    text(dl, titleFont, ts, ImVec2(tx + textWidth(titleFont, ts, "Arach"), ty), BLUE, "ne");
    text(dl, nullptr, 16, ImVec2(tx + 2, ty + 62), DIM, "Version " ARACHNE_VERSION);
    text(dl, nullptr, 16, ImVec2(tx + 2, ty + 84), DIM, "Vector art by Cursed Entertainment");
    const float cs = 13, cry = y + 306;
    text(dl, smallFont, cs, ImVec2(tx, cry), DIM, "Arachne's own tracer. Pictures from words: Krea 2 Turbo,");
    text(dl, smallFont, cs, ImVec2(tx, cry + 17), DIM, "run by stable-diffusion.cpp. nanosvg, Dear ImGui, SDL2, stb.");
    text(dl, smallFont, cs, ImVec2(tx, cry + 34), DIM, "Farica\xe2\x98\x86Kimora \xc2\xa9 Cursed Entertainment");
    if (status) text(dl, nullptr, 16, ImVec2(tx, y + 368), TEXT, status);
    if (progress >= 0) {
        const float p = progress > 1 ? 1 : progress;
        dl->AddRectFilled(ImVec2(x, end.y - 3), end, LINE);
        dl->AddRectFilled(ImVec2(x, end.y - 3), ImVec2(x + W * p, end.y), BLUE);
    }
    dl->AddRect(o, end, LINE);
    dl->PopClipRect();
}

}
