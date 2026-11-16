// Arachne - the painter: a picture from words, to trace. stable-diffusion.cpp (MIT) running Krea 2 Turbo, as in
// Firmament (the same model files: F:\AIModels\krea2, else models\ beside Arachne), kept loaded between pictures.
// stable-diffusion.dll is delay-loaded: Arachne traces on any PC, and only painting needs the NVIDIA card.
#pragma once
#include "trace.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Engine {
using Image = Trace::Image;

struct Models {
    std::string diffusion, llm, vae;
    std::vector<std::string> missing() const;
};
Models findModels();
bool available(std::string& why);   // an NVIDIA card and stable-diffusion.dll

struct Settings {
    std::string prompt;
    int width = 1024, height = 1024;
    int steps = 8;
    float cfg = 1.0f;
    int64_t seed = -1;              // -1: random
    bool offload = true;            // weights in system memory (6-8 GB cards)
};
struct Progress {
    std::atomic<int> step{0}, steps{0};
    std::atomic<bool> running{false}, cancel{false};
};
std::string generate(const Models& m, const Settings& s, Image& out, int64_t& seedUsed, Progress& p);
void cancel();
void setLog(std::function<void(const std::string&)> f);
}
