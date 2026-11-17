// Arachne - the painter (see engine.h); the model loading and the call are Firmament's.
#include "engine.h"

#include <stable-diffusion.h>

#include <SDL.h>

#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <random>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace Engine {

static std::function<void(const std::string&)> logFn;
void setLog(std::function<void(const std::string&)> f) { logFn = std::move(f); }
static std::string exeDir() { char* b = SDL_GetBasePath(); std::string s = b ? b : ""; if (b) SDL_free(b); return s; }

Models findModels() {
    Models m; std::error_code ec;
    std::vector<fs::path> roots = {fs::u8path(exeDir()) / "models", fs::path("F:/AIModels")};
    if (const char* la = std::getenv("LOCALAPPDATA")) roots.push_back(fs::u8path(la) / "Programs" / "Firmament" / "models");   // Firmament's: one download for both
    auto find = [&](const char* sub, std::initializer_list<const char*> files) {
        for (const auto& r : roots) for (const char* f : files) for (const fs::path& p : {r / sub / f, r / f}) if (fs::exists(p, ec)) return p.u8string();
        return (roots.back() / sub / *files.begin()).u8string();
    };
    m.diffusion = find("krea2/diffusion_models", {"Krea-2-Turbo-Q4_K_M.gguf", "krea2_turbo_bf16-Q4_0.gguf", "Krea-2-Turbo-Q3_K_M.gguf", "Krea-2-Turbo-Q5_K_S.gguf", "Krea-2-Turbo-Q6_K.gguf", "Krea-2-Turbo-Q8_0.gguf"});
    m.llm = find("krea2/text_encoders", {"qwen3vl_4b_fp8_scaled.safetensors", "qwen3vl_4b_int8_convrot.safetensors", "qwen3vl_4b_bf16.safetensors"});
    m.vae = find("krea2/vae", {"wan_2.1_vae.safetensors", "qwen_image_vae.safetensors"});
    return m;
}
std::vector<std::string> Models::missing() const {
    std::vector<std::string> v; std::error_code ec;
    for (const std::string* p : {&diffusion, &llm, &vae}) if (!fs::exists(fs::u8path(*p), ec)) v.push_back(fs::u8path(*p).filename().u8string());
    return v;
}
bool available(std::string& why) {
#ifdef _WIN32
    static int known = -1; static std::string reason;
    if (known < 0) {
        known = 0;
        if (!LoadLibraryW(L"nvcuda.dll")) reason = "Painting needs an NVIDIA graphics card (tracing works without one).";
        else if (!LoadLibraryW(L"stable-diffusion.dll")) reason = "stable-diffusion.dll (or the CUDA libraries beside it) is missing: please install Arachne again.";
        else known = 1;
    }
    why = reason; return known == 1;
#else
    why.clear(); return true;
#endif
}

static std::mutex mtx;
static sd_ctx_t* ctx = nullptr;
static std::string loadedKey;
static Progress* current = nullptr;
static void onLog(enum sd_log_level_t level, const char* text, void*) {
    if (level < SD_LOG_INFO || !logFn || !text) return;
    std::string s(text); while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    if (!s.empty()) logFn(s);
}
static void onProgress(int step, int steps, float, void*) { if (current) { current->step = step; current->steps = steps; } }

std::string generate(const Models& m, const Settings& s, Image& out, int64_t& seedUsed, Progress& p) {
    std::lock_guard<std::mutex> g(mtx);
    current = &p; p.running = true; p.cancel = false; p.step = 0; p.steps = s.steps;
    const std::string key = m.diffusion + "|" + m.llm + "|" + m.vae + (s.offload ? "|off" : "|on");
    if (!ctx || key != loadedKey) {
        if (ctx) { free_sd_ctx(ctx); ctx = nullptr; }
        sd_set_log_callback(onLog, nullptr); sd_set_progress_callback(onProgress, nullptr);
        sd_ctx_params_t c; sd_ctx_params_init(&c);
        c.diffusion_model_path = m.diffusion.c_str(); c.llm_path = m.llm.c_str(); c.vae_path = m.vae.c_str();
        c.n_threads = std::max(1, (int)sd_get_num_physical_cores());
        c.flash_attn = true; c.diffusion_flash_attn = true;
        if (s.offload) c.params_backend = "*=cpu";
        ctx = new_sd_ctx(&c);
        if (!ctx) { p.running = false; current = nullptr; return "The models didn't load (see the log): is there enough memory free?"; }
        loadedKey = key;
    }
    sd_img_gen_params_t q; sd_img_gen_params_init(&q);
    q.prompt = s.prompt.c_str(); q.negative_prompt = "";
    q.width = s.width; q.height = s.height;
    sd_sample_params_init(&q.sample_params);
    q.sample_params.sample_steps = s.steps; q.sample_params.guidance.txt_cfg = s.cfg;
    q.sample_params.sample_method = str_to_sample_method("er_sde");
    q.sample_params.scheduler = str_to_scheduler("simple");
    int64_t seed = s.seed;
    if (seed < 0) { seed = (int64_t)(std::random_device{}() & 0x7fffffff) << 20 ^ (int64_t)std::random_device{}(); if (seed < 0) seed = -seed; }
    q.seed = seed; q.batch_count = 1;
    q.vae_tiling_params.enabled = s.width * s.height > 1024 * 1024;
    sd_image_generation_result_t* r = nullptr;
    std::string err;
    if (!generate_image(ctx, &q, &r) || !r || r->count < 1 || !r->items[0].image.data) err = p.cancel ? "Stopped" : "The picture didn't come out (see the log)";
    else {
        const sd_image_t& im = r->items[0].image;
        out.w = (int)im.width; out.h = (int)im.height; out.px.resize((size_t)out.w * out.h * 3);
        for (size_t i = 0; i < (size_t)out.w * out.h; ++i) for (int c = 0; c < 3; ++c) out.px[i * 3 + c] = im.data[i * im.channel + c];
        seedUsed = seed;
    }
    if (r) free_sd_image_generation_result(r);
    p.running = false; current = nullptr;
    return err;
}
void cancel() { if (current) current->cancel = true; }
}
