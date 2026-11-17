// Arachne - pictures into vector art. Open a picture (or paint one from words), tune the tracing with sliders on the
// left, see the picture and its SVG in the middle (drag the line between them), save the SVG or a PNG of it.
// Part of Atelier.
//
//   Arachne [picture]
//   Arachne --capture out.bmp [--colours N] [--view 0-3] [--lang de] [--paint "words"] [picture]   (a screenshot once traced, then quit)
#include "engine.h"
#include "lang.h"
#include "splash.h"
#include "trace.h"

#include <SDL.h>
#include <SDL_opengl.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>
#include <stb_image.h>
#include <stb_image_write.h>
#define NANOSVG_IMPLEMENTATION
#include <nanosvg.h>
#define NANOSVGRAST_IMPLEMENTATION
#include <nanosvgrast.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace fs = std::filesystem;
using Trace::Image;

// ---------- the Atelier look: Changeling's greys, the Atelier blue for the controls; solid ----------
static const ImU32 C_BG = IM_COL32(0x1a, 0x1a, 0x1a, 255), C_PANEL = IM_COL32(0x24, 0x24, 0x24, 255),
                   C_LINE = IM_COL32(0x3a, 0x3a, 0x3a, 255), C_TEXT = IM_COL32(0xe6, 0xe6, 0xe6, 255),
                   C_DIM = IM_COL32(0x9a, 0x9a, 0x9a, 255), C_BLUE = IM_COL32(0x00, 0x7b, 0xb7, 255),
                   C_DEEP = IM_COL32(0x00, 0x51, 0x84, 255), C_WELL = IM_COL32(0x14, 0x14, 0x14, 255);
static ImVec4 col4(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }
static void applyStyle() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 0; s.FrameRounding = 2; s.GrabRounding = 2; s.PopupRounding = 2; s.ScrollbarRounding = 2;
    s.WindowBorderSize = 0; s.FrameBorderSize = 0; s.PopupBorderSize = 1;
    s.WindowPadding = ImVec2(12, 10); s.FramePadding = ImVec2(7, 4); s.ItemSpacing = ImVec2(6, 7);
    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = col4(C_TEXT); c[ImGuiCol_TextDisabled] = col4(C_DIM);
    c[ImGuiCol_WindowBg] = col4(C_PANEL); c[ImGuiCol_ChildBg] = col4(C_PANEL); c[ImGuiCol_PopupBg] = col4(IM_COL32(0x21, 0x21, 0x21, 255));
    c[ImGuiCol_Border] = col4(C_LINE); c[ImGuiCol_Separator] = col4(C_LINE);
    c[ImGuiCol_FrameBg] = col4(C_WELL); c[ImGuiCol_FrameBgHovered] = col4(IM_COL32(0x2e, 0x2e, 0x2e, 255)); c[ImGuiCol_FrameBgActive] = col4(IM_COL32(0x33, 0x33, 0x33, 255));
    c[ImGuiCol_MenuBarBg] = col4(C_BG);
    c[ImGuiCol_CheckMark] = col4(C_BLUE); c[ImGuiCol_SliderGrab] = col4(C_BLUE); c[ImGuiCol_SliderGrabActive] = col4(C_DEEP);
    c[ImGuiCol_Button] = col4(IM_COL32(0x33, 0x33, 0x33, 255)); c[ImGuiCol_ButtonHovered] = col4(C_DEEP); c[ImGuiCol_ButtonActive] = col4(C_BLUE);
    c[ImGuiCol_Header] = col4(IM_COL32(0x33, 0x33, 0x33, 255)); c[ImGuiCol_HeaderHovered] = col4(C_DEEP); c[ImGuiCol_HeaderActive] = col4(C_BLUE);
    c[ImGuiCol_PlotHistogram] = col4(C_BLUE);
}
static bool fileExists(const std::string& f) { std::error_code ec; return fs::exists(fs::u8path(f), ec); }
static std::string exeDir() { char* b = SDL_GetBasePath(); std::string s = b ? b : ""; if (b) SDL_free(b); return s; }
static std::string prefsFile() { char* p = SDL_GetPrefPath("Cursed Entertainment", "Arachne"); std::string s = p ? std::string(p) + "arachne.ini" : ""; if (p) SDL_free(p); return s; }

static void loadFonts() {
    ImGuiIO& io = ImGui::GetIO();
    const char* ui = "C:/Windows/Fonts/segoeui.ttf";
    if (!fileExists(ui)) { io.Fonts->AddFontDefault(); return; }
    static const ImWchar ranges[] = {0x0020, 0x024F, 0x0400, 0x04FF, 0x2010, 0x2027, 0x2190, 0x21FF, 0x2600, 0x26FF, 0};
    auto add = [&](const char* file, float size) {
        ImFont* f = io.Fonts->AddFontFromFileTTF(file, size, nullptr, ranges);
        ImFontConfig m; m.MergeMode = true;
        if (Lang::needsCJK()) {
            const char* cjk = Lang::current() == "ko" ? "C:/Windows/Fonts/malgun.ttf" : Lang::current() == "zh" ? "C:/Windows/Fonts/msyh.ttc" : "C:/Windows/Fonts/YuGothM.ttc";
            const ImWchar* r = Lang::current() == "ko" ? io.Fonts->GetGlyphRangesKorean() : Lang::current() == "zh" ? io.Fonts->GetGlyphRangesChineseSimplifiedCommon() : io.Fonts->GetGlyphRangesJapanese();
            if (fileExists(cjk)) io.Fonts->AddFontFromFileTTF(cjk, size, &m, r);
        }
        return f;
    };
    add(ui, 16.0f);
    ImFont* title = add(fileExists("C:/Windows/Fonts/segoeuil.ttf") ? "C:/Windows/Fonts/segoeuil.ttf" : ui, 46.0f);
    ImFont* small = add(ui, 13.0f);
    Splash::setFonts(title, small);
}

static std::string fileDialog(bool save, const char* filter, const char* defExt, const std::string& name = "") {
#ifdef _WIN32
    std::wstring f;
    for (const char* p = filter; *p; ++p) f += (*p == '|') ? L'\0' : (wchar_t)*p;
    f += L'\0'; f += L'\0';
    std::vector<wchar_t> buf(32768, 0);
    if (!name.empty()) { const std::wstring w = fs::u8path(name).wstring(); std::copy(w.begin(), w.begin() + std::min(w.size(), buf.size() - 1), buf.begin()); }
    OPENFILENAMEW o{};
    o.lStructSize = sizeof(o); o.lpstrFilter = f.c_str(); o.lpstrFile = buf.data(); o.nMaxFile = (DWORD)buf.size();
    std::wstring ext = defExt ? std::wstring(defExt, defExt + strlen(defExt)) : L"";
    o.lpstrDefExt = defExt ? ext.c_str() : nullptr;
    o.Flags = save ? (OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR) : (OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR);
    if (!(save ? GetSaveFileNameW(&o) : GetOpenFileNameW(&o))) return "";
    return fs::path(buf.data()).u8string();
#else
    (void)filter; (void)defExt; (void)name;
    FILE* p = popen(save ? "zenity --file-selection --save --confirm-overwrite 2>/dev/null" : "zenity --file-selection 2>/dev/null", "r");
    if (!p) return {};
    char line[4096] = {0}; std::string r = fgets(line, sizeof line, p) ? line : ""; pclose(p);
    while (!r.empty() && (r.back() == '\n' || r.back() == '\r')) r.pop_back();
    return r;
#endif
}
static const char* PICTURES = "Pictures|*.png;*.jpg;*.jpeg;*.jfif;*.bmp;*.tga;*.gif|All files|*.*|";

static void upload(GLuint& tex, const Image& im) {
    if (im.empty()) return;
    if (!tex) glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, im.w, im.h, 0, GL_RGB, GL_UNSIGNED_BYTE, im.px.data());
}

// the SVG drawn to pixels (nanosvg), on a background (the checker shows what's transparent)
static bool rasterise(const std::string& svgText, int w, int h, float scale, std::vector<unsigned char>& rgba) {
    std::vector<char> text(svgText.begin(), svgText.end()); text.push_back(0);
    NSVGimage* svg = nsvgParse(text.data(), "px", 96);
    if (!svg) return false;
    NSVGrasterizer* r = nsvgCreateRasterizer();
    rgba.assign((size_t)w * h * 4, 0);
    nsvgRasterize(r, svg, 0, 0, scale, rgba.data(), w, h, w * 4);
    nsvgDeleteRasterizer(r); nsvgDelete(svg);
    return true;
}
static Image onChecker(const std::vector<unsigned char>& rgba, int w, int h) {
    Image o; o.w = w; o.h = h; o.px.resize((size_t)w * h * 3);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const unsigned char* p = &rgba[((size_t)y * w + x) * 4]; const float a = p[3] / 255.0f;
        const int bg = ((x / 12 + y / 12) & 1) ? 0x2c : 0x22;
        for (int c = 0; c < 3; ++c) o.px[((size_t)y * w + x) * 3 + c] = (uint8_t)std::lround(p[c] * a + bg * (1 - a));   // nanosvg's colours aren't premultiplied
    }
    return o;
}

// ---------- the state ----------
static SDL_Window* window = nullptr;
static bool quit = false;
static Trace::Settings ts;
static std::string picPath, picName;
static Image original;                 // the picture
static Trace::Result traced; static bool haveTrace = false;
static Image drawn;                    // the SVG drawn back (the picture's size, up to 2048)
static GLuint originalTex = 0, drawnTex = 0, quantTex = 0;
static float wipe = 0.5f; static int view = 0;   // 0 before | after, 1 after (the SVG), 2 before, 3 the colours
static bool traceWanted = false; static double changedAt = 0;
// the painter
static Engine::Models models; static Engine::Progress paintProgress;
static char prompt[1024] = "";
static int style = 0, paintSize = 1024, paintSteps = 8; static bool sameSeed = false; static int64_t lastSeed = -1;
static const char* STYLES[] = {"Flat illustration", "Logo", "Line art", "Sticker", "Icon"};
static std::string promptFor(int st, const std::string& what) {
    static const char* tail[] = {
        ", flat vector illustration, bold clean shapes, solid flat colours, no gradients, no texture, plain background",
        ", minimal flat vector logo, simple geometric shapes, two or three solid colours, centred on a plain white background",
        ", clean black ink line art, even line weight, no shading, no colour, white background",
        ", die-cut sticker, flat vector cartoon, thick white outline, solid colours, plain background",
        ", flat vector app icon, simple bold shapes, solid colours, centred"};
    return what + tail[std::clamp(st, 0, 4)];
}
// preferences (File > Preferences), with the language
static struct Prefs { bool reopenLast = false; std::string last, language; float panel = 330; } prefs;   // panel: the left panel's width
static bool showPrefs = false, showAbout = false;
static void savePrefs() {
    std::ofstream f(fs::u8path(prefsFile()));
    f << "reopenLast=" << (prefs.reopenLast ? 1 : 0) << "\nlast=" << prefs.last << "\nlanguage=" << Lang::current() << "\npanel=" << prefs.panel << "\n";
}
static void loadPrefs() {
    std::ifstream f(fs::u8path(prefsFile())); std::string line;
    while (std::getline(f, line)) {
        const size_t eq = line.find('='); if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "reopenLast") prefs.reopenLast = v == "1"; else if (k == "last") prefs.last = v; else if (k == "language") prefs.language = v; else if (k == "panel") prefs.panel = std::clamp((float)std::atof(v.c_str()), 260.0f, 700.0f);
    }
}

// the worker: one job at a time off the window's thread
static std::mutex mtx; static std::deque<std::function<void()>> jobs; static std::vector<std::function<void()>> onMain;
static std::string status; static std::atomic<bool> busy{false}, workerQuit{false}, traceCancel{false};
static std::atomic<int> tracePercent{0};
static std::thread worker;
static void say(const std::string& s) { std::lock_guard<std::mutex> g(mtx); status = s; }
static void post(std::function<void()> f) { std::lock_guard<std::mutex> g(mtx); onMain.push_back(std::move(f)); }
static void queue(std::function<void()> f) { std::lock_guard<std::mutex> g(mtx); jobs.push_back(std::move(f)); }
static void workerLoop() {
    while (!workerQuit) {
        std::function<void()> job;
        { std::lock_guard<std::mutex> g(mtx); if (!jobs.empty()) { job = std::move(jobs.front()); jobs.pop_front(); } }
        if (!job) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); continue; }
        busy = true; job(); busy = false;
    }
}

static bool loadPicture(const std::string& path, Image& im, std::string& err) {
    std::ifstream f(fs::u8path(path), std::ios::binary);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    int w, h, n; unsigned char* px = bytes.empty() ? nullptr : stbi_load_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &n, 4);
    if (!px) { err = T("That picture couldn't be read"); return false; }
    im.w = w; im.h = h; im.px.resize((size_t)w * h * 3);
    for (size_t i = 0; i < (size_t)w * h; ++i) {   // transparency onto white (what a traced picture usually sits on)
        const float a = px[i * 4 + 3] / 255.0f;
        for (int c = 0; c < 3; ++c) im.px[i * 3 + c] = (uint8_t)std::lround(px[i * 4 + c] * a + 255 * (1 - a));
    }
    stbi_image_free(px);
    return true;
}

// tracing: on the worker; a newer request stops an older one
static void requestTrace() { traceWanted = true; changedAt = ImGui::GetTime(); traceCancel = true; }
static void runTrace() {
    if (original.empty()) return;
    traceCancel = false;
    const Image im = original; const Trace::Settings s = ts;
    queue([im, s] {
        traceCancel = false;
        Trace::Result r;
        const auto t0 = std::chrono::steady_clock::now();
        if (!Trace::trace(im, s, r, &traceCancel, &tracePercent)) return;
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        // drawn back at the picture's size (at most 2048 on its longest side)
        const float k = std::min(1.0f, 2048.0f / std::max(im.w, im.h));
        const int w = std::max(1, (int)std::lround(im.w * k)), h = std::max(1, (int)std::lround(im.h * k));
        std::vector<unsigned char> rgba; rasterise(r.svg, w, h, k, rgba);
        Image d = onChecker(rgba, w, h);
        char m[200]; std::snprintf(m, sizeof m, T("%d colours, %d shapes, %d curves, %.0f KB, in %.2f s"), r.colours, r.shapes, r.nodes, r.svg.size() / 1024.0, secs);
        const std::string msg = m;
        post([r = std::move(r), d = std::move(d), msg]() mutable { traced = std::move(r); drawn = std::move(d); haveTrace = true; upload(drawnTex, drawn); upload(quantTex, traced.quantised); say(msg); });
    });
}
static void setPicture(Image im, const std::string& path, const std::string& name) {
    original = std::move(im); picPath = path; picName = name;
    upload(originalTex, original); haveTrace = false;
    SDL_SetWindowTitle(window, ("Arachne - " + name).c_str());
    traceWanted = true; changedAt = -10;
}
static void openPicture(const std::string& path) {
    Image im; std::string err;
    if (!loadPicture(path, im, err)) { say(err); return; }
    setPicture(std::move(im), path, fs::u8path(path).filename().u8string());
    prefs.last = path; savePrefs();
    say(T("Tracing..."));
}

static void paint() {
    std::string why;
    if (!Engine::available(why)) { say(T(why.c_str())); return; }
    if (!models.missing().empty()) { say(T("Painting needs Krea 2 Turbo: get it in Firmament (its Models tab: Arachne finds it there), or put it in the models folder beside Arachne")); return; }
    if (!prompt[0]) { say(T("Describe the picture first")); return; }
    Engine::Settings s; s.prompt = promptFor(style, prompt); s.width = s.height = paintSize; s.steps = paintSteps;
    s.seed = sameSeed ? lastSeed : -1;
    const std::string what = prompt;
    say(T("Painting..."));
    queue([s, what] {
        Image im; int64_t seed = -1;
        const std::string err = Engine::generate(models, s, im, seed, paintProgress);
        if (!err.empty()) { say(err); return; }
        post([im = std::move(im), what, seed]() mutable { lastSeed = seed; setPicture(std::move(im), "", what + ".png"); say(T("Painted: tracing...")); });
    });
}

static std::string baseName() { const std::string n = fs::u8path(picName.empty() ? "drawing" : picName).stem().u8string(); return n.empty() ? "drawing" : n; }
static void saveSvg(const std::string& to = "") {
    if (!haveTrace) return;
    const std::string f = to.empty() ? fileDialog(true, "SVG|*.svg|", "svg", baseName() + ".svg") : to;
    if (f.empty()) return;
    std::ofstream(fs::u8path(f), std::ios::binary) << traced.svg;
    say(std::string(T("Saved ")) + fs::u8path(f).filename().u8string());
}
static float pngScale = 1.0f;
static void savePng(const std::string& to = "") {
    if (!haveTrace) return;
    const std::string f = to.empty() ? fileDialog(true, "PNG|*.png|", "png", baseName() + ".png") : to;
    if (f.empty()) return;
    const int w = std::max(1, (int)std::lround(original.w * pngScale)), h = std::max(1, (int)std::lround(original.h * pngScale));
    std::vector<unsigned char> rgba;
    if (!rasterise(traced.svg, w, h, pngScale, rgba)) { say(T("The SVG couldn't be drawn")); return; }
    std::vector<unsigned char> out(rgba.size());
    for (size_t i = 0; i < (size_t)w * h; ++i) for (int c = 0; c < 4; ++c) out[i * 4 + c] = rgba[i * 4 + c];
    stbi_write_png(fs::u8path(f).string().c_str(), w, h, 4, out.data(), w * 4);
    say(std::string(T("Saved ")) + fs::u8path(f).filename().u8string());
}

// ---------- the panel ----------
static void tip(const char* t) { if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", T(t)); }
static void heading(const char* t) { ImGui::Spacing(); ImGui::PushStyleColor(ImGuiCol_Text, col4(C_BLUE)); ImGui::TextUnformatted(T(t)); ImGui::PopStyleColor(); }
static bool sliderI(const char* label, int* v, int lo, int hi, const char* fmt, const char* help) {
    ImGui::TextUnformatted(T(label)); ImGui::SetNextItemWidth(-1);
    const bool c = ImGui::SliderInt((std::string("##") + label).c_str(), v, lo, hi, fmt); tip(help); return c;
}
static bool sliderF(const char* label, float* v, float lo, float hi, const char* fmt, const char* help) {
    ImGui::TextUnformatted(T(label)); ImGui::SetNextItemWidth(-1);
    const bool c = ImGui::SliderFloat((std::string("##") + label).c_str(), v, lo, hi, fmt); tip(help); return c;
}
static void panel(ImVec2 pos, ImVec2 size) {
    ImGui::SetNextWindowPos(pos); ImGui::SetNextWindowSize(size);
    ImGui::Begin("##panel", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
    ImGui::BeginChild("scroll");
    ImGui::PushTextWrapPos(0);
    heading("Picture");
    if (ImGui::Button(T("Open a picture..."), ImVec2(-1, 0))) { const std::string f = fileDialog(false, PICTURES, nullptr); if (!f.empty()) openPicture(f); }
    ImGui::TextDisabled("%s", T("or paint one from words:"));
    ImGui::InputTextMultiline("##prompt", prompt, sizeof prompt, ImVec2(-1, 64)); tip("What to paint: a fox, a mountain at dawn, a coffee cup...");
    for (int i = 0; i < 5; ++i) {
        if (i % 3) ImGui::SameLine();
        const bool on = style == i;
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, col4(C_BLUE));
        if (ImGui::Button(T(STYLES[i]), ImVec2((size.x - 48) / 3, 0))) style = i;
        if (on) ImGui::PopStyleColor();
    }
    sliderI("Size", &paintSize, 512, 1536, "%d px", "The painted picture's side. 1024 is quick; bigger takes longer and more memory");
    paintSize = paintSize / 64 * 64;
    sliderI("Steps", &paintSteps, 4, 20, "%d", "More steps: more detail, slower. Krea 2 Turbo is good at 8");
    ImGui::Checkbox(T("Same seed every time"), &sameSeed); tip("Keep the last picture's seed: change the words, keep the look");
    if (paintProgress.running) {
        ImGui::ProgressBar(paintProgress.steps > 0 ? (float)paintProgress.step / paintProgress.steps : 0.0f, ImVec2(-1, 0));
        if (ImGui::Button(T("Stop"), ImVec2(-1, 0))) Engine::cancel();
    } else if (ImGui::Button(T("Paint the picture"), ImVec2(-1, 0))) paint();

    heading("Tracing");
    bool ch = false;
    ch |= sliderI("Colours", &ts.colours, 2, 64, "%d", "How many colours the drawing has. Few: a poster; many: close to the picture");
    ch |= sliderI("Speckle", &ts.speckle, 0, 400, "%d px", "Blobs smaller than this are merged into what's round them. Raise it for cleaner drawings");
    ch |= sliderF("Smooth the picture first", &ts.smoothInput, 0, 3, "%.1f", "Blurs the picture before the colours are found: photos and noisy pictures come out cleaner");
    ch |= sliderF("Straighten", &ts.straighten, 0.3f, 4, "%.1f px", "How far an outline may move to become straight. More: simpler, more geometric shapes");
    ch |= sliderF("Corners", &ts.corner, 30, 180, "%.0f\xc2\xb0", "Turns sharper than this stay sharp corners; at 180 everything is smooth");
    ch |= sliderF("Curve fit", &ts.curveFit, 0.2f, 6, "%.1f px", "How far a curve may stray from the outline. More: fewer, smoother curves and a smaller file");
    ch |= sliderI("Detail", &ts.workSize, 256, 3072, "%d px", "The size the picture is traced at. More: finer detail, slower, bigger files");
    ch |= ImGui::Checkbox(T("Leave out the background"), &ts.dropBackground); tip("The biggest colour is left out: the drawing on a transparent background");
    ch |= ImGui::Checkbox(T("Outlines only"), &ts.outlines); tip("Each colour's edges as lines, no fill: line art");
    if (ts.outlines) ch |= sliderF("Line width", &ts.strokeWidth, 0.25f, 8, "%.2f px", "The outlines' width");
    if (ch && !original.empty()) requestTrace();

    heading("Save");
    ImGui::BeginDisabled(!haveTrace);
    if (ImGui::Button(T("Save the SVG..."), ImVec2(-1, 0))) saveSvg();
    sliderF("PNG size", &pngScale, 0.25f, 8, "%.2fx", "The PNG's size against the picture's: vector art draws sharp at any size");
    if (ImGui::Button(T("Save a PNG..."), ImVec2(-1, 0))) savePng();
    if (ImGui::Button(T("Copy the SVG"), ImVec2(-1, 0))) { SDL_SetClipboardText(traced.svg.c_str()); say(T("The SVG is on the clipboard")); }
    ImGui::EndDisabled();
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
    ImGui::End();
}

static void canvas(ImVec2 pos, ImVec2 size) {
    ImGui::SetNextWindowPos(pos); ImGui::SetNextWindowSize(size);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, col4(C_BG));
    ImGui::Begin("##canvas", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (original.empty()) {
        const ImVec2 c(pos.x + size.x / 2, pos.y + size.y / 2);
        auto centred = [&](const char* t, float y, ImU32 col) { const ImVec2 tsz = ImGui::CalcTextSize(t); dl->AddText(ImVec2(c.x - tsz.x / 2, y), col, t); };
        centred(T("Drop a picture here, or paint one from words on the left"), c.y - 20, C_TEXT);
        centred(T("Arachne traces it into vector art: shapes and curves that stay sharp at any size."), c.y + 8, C_DIM);
        ImGui::End(); ImGui::PopStyleColor();
        return;
    }
    const float pad = 20, s = std::min((size.x - pad * 2) / original.w, (size.y - pad * 2) / original.h), dw = original.w * s, dh = original.h * s;
    const ImVec2 a(pos.x + (size.x - dw) / 2, pos.y + (size.y - dh) / 2), b(a.x + dw, a.y + dh);
    const bool after = haveTrace && drawnTex;
    auto label = [&](const char* t, float lx) {
        const ImVec2 tsz = ImGui::CalcTextSize(t);
        dl->AddRectFilled(ImVec2(lx - 6, a.y + 6), ImVec2(lx + tsz.x + 6, a.y + 10 + tsz.y), IM_COL32(0x1a, 0x1a, 0x1a, 210), 2);
        dl->AddText(ImVec2(lx, a.y + 8), C_TEXT, t);
    };
    if (view == 3 && after) { dl->AddImage((ImTextureID)(intptr_t)quantTex, a, b); label(T("The colours"), a.x + 14); }
    else if (view == 2 || !after) dl->AddImage((ImTextureID)(intptr_t)originalTex, a, b);
    else if (view == 1) dl->AddImage((ImTextureID)(intptr_t)drawnTex, a, b);
    else {
        const float x = a.x + dw * wipe;
        dl->AddImage((ImTextureID)(intptr_t)originalTex, a, ImVec2(x, b.y), ImVec2(0, 0), ImVec2(wipe, 1));
        dl->AddImage((ImTextureID)(intptr_t)drawnTex, ImVec2(x, a.y), b, ImVec2(wipe, 0), ImVec2(1, 1));
        dl->AddLine(ImVec2(x, a.y), ImVec2(x, b.y), C_BLUE, 2.0f);
        dl->AddCircleFilled(ImVec2(x, a.y + dh / 2), 9, C_BLUE);
        label(T("Before"), a.x + 14); label(T("After"), b.x - 14 - ImGui::CalcTextSize(T("After")).x);
    }
    ImGui::SetCursorScreenPos(a);
    ImGui::InvisibleButton("view", ImVec2(dw, dh));
    if (ImGui::IsItemActive() && view == 0) wipe = std::clamp((ImGui::GetIO().MousePos.x - a.x) / dw, 0.0f, 1.0f);
    if (ImGui::IsItemHovered() && view == 0) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (busy || traceWanted) {
        const float t = (float)ImGui::GetTime() * 6; const ImVec2 c(b.x - 22, b.y - 22);
        dl->AddCircleFilled(c, 16, IM_COL32(0x1a, 0x1a, 0x1a, 200));
        dl->PathArcTo(c, 10, t, t + 4.2f, 24); dl->PathStroke(C_BLUE, 0, 2.5f);
    }
    ImGui::End(); ImGui::PopStyleColor();
}

static void menuBar() {
    if (!ImGui::BeginMainMenuBar()) return;
    if (ImGui::BeginMenu(T("File"))) {
        if (ImGui::MenuItem(T("Open a picture..."), "Ctrl+O")) { const std::string f = fileDialog(false, PICTURES, nullptr); if (!f.empty()) openPicture(f); }
        const bool recent = !prefs.last.empty() && fileExists(prefs.last) && prefs.last != picPath;
        if (ImGui::MenuItem(recent ? (std::string(T("Open recent")) + ": " + fs::u8path(prefs.last).filename().u8string()).c_str() : T("Open recent"), nullptr, false, recent)) openPicture(prefs.last);
        ImGui::Separator();
        if (ImGui::MenuItem(T("Save the SVG..."), "Ctrl+S", false, haveTrace)) saveSvg();
        if (ImGui::MenuItem(T("Save a PNG..."), nullptr, false, haveTrace)) savePng();
        if (ImGui::MenuItem(T("Copy the SVG"), "Ctrl+C", false, haveTrace)) { SDL_SetClipboardText(traced.svg.c_str()); say(T("The SVG is on the clipboard")); }
        ImGui::Separator();
        if (ImGui::MenuItem(T("Preferences..."))) showPrefs = true;
        ImGui::Separator();
        if (ImGui::MenuItem(T("Quit"))) quit = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(T("View"))) {
        if (ImGui::MenuItem(T("Before | After"), "1", view == 0)) view = 0;
        if (ImGui::MenuItem(T("After"), "2", view == 1)) view = 1;
        if (ImGui::MenuItem(T("Before"), "3", view == 2)) view = 2;
        if (ImGui::MenuItem(T("The colours"), "4", view == 3)) view = 3;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(T("Language"))) {
        for (const auto& p : Lang::packs())
            if (ImGui::MenuItem(p.name.c_str(), nullptr, Lang::current() == p.code)) {
                const bool cjk = Lang::needsCJK();
                Lang::load(exeDir() + "lang", p.code); savePrefs();
                if (Lang::needsCJK() != cjk || Lang::needsCJK()) say(T("Restart Arachne for this language's letters"));
            }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(T("Help"))) { if (ImGui::MenuItem(T("About Arachne"))) showAbout = true; ImGui::EndMenu(); }
    ImGui::EndMainMenuBar();
}

static void frame() {
    { std::vector<std::function<void()>> fs_; { std::lock_guard<std::mutex> g(mtx); fs_.swap(onMain); } for (auto& f : fs_) f(); }
    if (traceWanted && ImGui::GetTime() - changedAt > 0.25) { traceWanted = false; runTrace(); }   // a moment after the last change
    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_1)) view = 0; if (ImGui::IsKeyPressed(ImGuiKey_2)) view = 1;
        if (ImGui::IsKeyPressed(ImGuiKey_3)) view = 2; if (ImGui::IsKeyPressed(ImGuiKey_4)) view = 3;
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O)) { const std::string f = fileDialog(false, PICTURES, nullptr); if (!f.empty()) openPicture(f); }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) saveSvg();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C) && haveTrace) { SDL_SetClipboardText(traced.svg.c_str()); say(T("The SVG is on the clipboard")); }
    }
    menuBar();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float top = vp->WorkPos.y, statusH = 26;
    const float PANEL = std::min(prefs.panel, vp->WorkSize.x - 300);
    panel(ImVec2(vp->WorkPos.x, top), ImVec2(PANEL, vp->WorkSize.y - statusH));
    canvas(ImVec2(vp->WorkPos.x + PANEL, top), ImVec2(vp->WorkSize.x - PANEL, vp->WorkSize.y - statusH));
    {   // the panel's edge: drag it to make the panel wider or narrower (remembered)
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + PANEL - 3, top)); ImGui::SetNextWindowSize(ImVec2(6, vp->WorkSize.y - statusH));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("##splitter", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground);
        ImGui::InvisibleButton("##edge", ImVec2(6, vp->WorkSize.y - statusH));
        const bool hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
        if (hot) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (ImGui::IsItemActive()) prefs.panel = std::clamp(prefs.panel + io.MouseDelta.x, 260.0f, 700.0f);
        if (ImGui::IsItemDeactivated()) savePrefs();
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(vp->WorkPos.x + PANEL - 1, top), ImVec2(vp->WorkPos.x + PANEL + 1, top + vp->WorkSize.y - statusH), hot ? C_BLUE : C_LINE);
        ImGui::End(); ImGui::PopStyleVar();
    }
    // the status line
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, top + vp->WorkSize.y - statusH)); ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, statusH));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 4));
    ImGui::Begin("##status", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
    std::string line; { std::lock_guard<std::mutex> g(mtx); line = status; }
    ImGui::TextDisabled("%s", line.c_str());
    ImGui::End(); ImGui::PopStyleVar();
    // Preferences
    if (showPrefs) {
        ImGui::SetNextWindowSize(ImVec2(460, 0), ImGuiCond_Appearing);
        ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::Begin(T("Preferences"), &showPrefs, ImGuiWindowFlags_NoCollapse)) {
            if (ImGui::Checkbox(T("Open the last picture at the start"), &prefs.reopenLast)) savePrefs();
            tip("Off: Arachne starts empty; the last picture is in File > Open recent");
        }
        ImGui::End();
    }
    // About: the splash
    if (showAbout) { ImGui::OpenPopup("about"); showAbout = false; }
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    if (ImGui::BeginPopupModal("about", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        Splash::draw(ImGui::GetWindowDrawList(), at.x, at.y, nullptr, -1);
        ImGui::InvisibleButton("##splash", ImVec2(Splash::W, Splash::H));
        if (ImGui::IsItemClicked() || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
}

int main(int argc, char** argv) {
    std::string capture, file, lang;
    bool paintAtStart = false;   // --paint "words": paint at the start (tests)
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--capture" && i + 1 < argc) capture = argv[++i];
        else if (a == "--colours" && i + 1 < argc) ts.colours = std::atoi(argv[++i]);
        else if (a == "--view" && i + 1 < argc) view = std::clamp(std::atoi(argv[++i]), 0, 3);
        else if (a == "--lang" && i + 1 < argc) lang = argv[++i];
        else if (a == "--paint" && i + 1 < argc) { std::snprintf(prompt, sizeof prompt, "%s", argv[++i]); paintAtStart = true; }
        else if (a.rfind("--", 0) != 0) file = a;
    }
    loadPrefs();
    Lang::load(exeDir() + "lang", !lang.empty() ? lang : prefs.language);
    models = Engine::findModels();
    {   // the painter's log: arachne.log beside the preferences (what "see the log" means)
        static FILE* logFile = nullptr;
        const std::string lp = fs::u8path(prefsFile()).parent_path().u8string() + "/arachne.log";
        logFile = std::fopen(lp.c_str(), "w");
        Engine::setLog([](const std::string& line) { if (logFile) { std::fprintf(logFile, "%s\n", line.c_str()); std::fflush(logFile); } });
    }
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) return 1;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE); SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    window = SDL_CreateWindow("Arachne", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1500, 900, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_HIDDEN);
    if (!window) return 1;
    SDL_SetWindowMinimumSize(window, 1000, 640);
    SDL_GLContext gl = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, gl); SDL_GL_SetSwapInterval(1);
    SDL_Surface* icon = SDL_LoadBMP((exeDir() + "assets/icon.bmp").c_str());
    if (icon) SDL_SetWindowIcon(window, icon);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    loadFonts();
    applyStyle();
    SDL_Window* splashWin = nullptr;
    if (capture.empty()) {
        splashWin = SDL_CreateWindow("Arachne", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, (int)Splash::W, (int)Splash::H, SDL_WINDOW_OPENGL | SDL_WINDOW_BORDERLESS);
        if (splashWin && SDL_GL_MakeCurrent(splashWin, gl) != 0) { SDL_DestroyWindow(splashWin); splashWin = nullptr; SDL_GL_MakeCurrent(window, gl); }
        if (splashWin && icon) SDL_SetWindowIcon(splashWin, icon);
    }
    if (icon) SDL_FreeSurface(icon);
    ImGui_ImplSDL2_InitForOpenGL(splashWin ? splashWin : window, gl);
    ImGui_ImplOpenGL3_Init("#version 330");
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
    worker = std::thread(workerLoop);
    {   // what's ready: the tracer always; painting when the card and the models are there
        std::string why;
        if (!Engine::available(why)) say(std::string(T("Ready to trace. ")) + T(why.c_str()));
        else if (!models.missing().empty()) say(T("Ready to trace. To paint from words, Arachne needs Krea 2 Turbo (Firmament's models)"));
        else say(T("Ready"));
    }
    if (splashWin) {
        const Uint32 start = SDL_GetTicks(); bool skip = false;
        for (;;) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) { ImGui_ImplSDL2_ProcessEvent(&e); if (e.type == SDL_QUIT) quit = true; if (e.type == SDL_MOUSEBUTTONDOWN || (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)) skip = true; }
            const Uint32 t = SDL_GetTicks() - start;
            if (skip || quit || t >= 1400) break;
            ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplSDL2_NewFrame(); ImGui::NewFrame();
            Splash::draw(ImGui::GetBackgroundDrawList(), 0, 0, T("Spinning the web..."), t / 1400.0f);
            ImGui::Render();
            int sw, sh; SDL_GL_GetDrawableSize(splashWin, &sw, &sh);
            glViewport(0, 0, sw, sh); glClearColor(0.1f, 0.1f, 0.1f, 1); glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            SDL_GL_SwapWindow(splashWin);
        }
        ImGui_ImplSDL2_Shutdown();
        SDL_DestroyWindow(splashWin);
        SDL_GL_MakeCurrent(window, gl);
        ImGui_ImplSDL2_InitForOpenGL(window, gl);
    }
    if (capture.empty()) { SDL_ShowWindow(window); SDL_MaximizeWindow(window); SDL_RaiseWindow(window); }
    // the start: empty, unless a picture was given (or Preferences say the last one)
    if (paintAtStart) paint();
    else if (!file.empty()) openPicture(file);
    else if (prefs.reopenLast && capture.empty() && !prefs.last.empty() && fileExists(prefs.last)) openPicture(prefs.last);
    int frames = 0, idle = 0;
    while (!quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL2_ProcessEvent(&e);
            if (e.type == SDL_QUIT) quit = true;
            if (e.type == SDL_DROPFILE) { const std::string f = e.drop.file; SDL_free(e.drop.file); openPicture(f); }
        }
        ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplSDL2_NewFrame(); ImGui::NewFrame();
        frame();
        ImGui::Render();
        int w, h; SDL_GL_GetDrawableSize(window, &w, &h);
        glViewport(0, 0, w, h); glClearColor(0.1f, 0.1f, 0.1f, 1); glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        ++frames;
        if (!capture.empty()) {   // tests: a screenshot once everything's quiet (and traced, when there's a picture)
            bool quiet; { std::lock_guard<std::mutex> g(mtx); quiet = jobs.empty() && onMain.empty(); }
            idle = (quiet && !busy && !traceWanted && (original.empty() ? !paintAtStart : haveTrace)) ? idle + 1 : 0;
            if (paintAtStart && !busy && quiet && original.empty() && frames > 100) paintAtStart = false;   // the painting failed: say so in the shot
            if ((frames >= 8 && idle >= 10) || (frames > 6000 && !busy && quiet && !traceWanted)) {
                std::vector<unsigned char> px((size_t)w * h * 4), flip(px.size());
                glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
                for (int y = 0; y < h; ++y) memcpy(&flip[(size_t)y * w * 4], &px[(size_t)(h - 1 - y) * w * 4], (size_t)w * 4);
                SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(flip.data(), w, h, 32, w * 4, SDL_PIXELFORMAT_ABGR8888);
                SDL_SaveBMP(s, capture.c_str()); SDL_FreeSurface(s);
                if (haveTrace) saveSvg(capture + ".svg");
                break;
            }
        }
        SDL_GL_SwapWindow(window);
    }
    traceCancel = true; workerQuit = true;
    if (worker.joinable()) worker.join();
    ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplSDL2_Shutdown(); ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl); SDL_DestroyWindow(window); SDL_Quit();
    return 0;
}
