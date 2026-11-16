// Arachne - the tracer (see trace.h).
#include "trace.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <random>
#include <sstream>

namespace Trace {

// ---------------------------------------------------------------- colour
struct Lab { float L, a, b; };
static float lin(float c) { c /= 255.0f; return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
static Lab toLab(const uint8_t* p) {
    static float tab[256]; static bool init = false;
    if (!init) { for (int i = 0; i < 256; ++i) tab[i] = lin((float)i); init = true; }
    const float r = tab[p[0]], g = tab[p[1]], b = tab[p[2]];
    float x = (0.4124f * r + 0.3576f * g + 0.1805f * b) / 0.95047f, y = 0.2126f * r + 0.7152f * g + 0.0722f * b, z = (0.0193f * r + 0.1192f * g + 0.9505f * b) / 1.08883f;
    auto f = [](float t) { return t > 0.008856f ? std::cbrt(t) : 7.787f * t + 16.0f / 116.0f; };
    x = f(x); y = f(y); z = f(z);
    return {116 * y - 16, 500 * (x - y), 200 * (y - z)};
}
static float d2(const Lab& u, const Lab& v) { const float a = u.L - v.L, b = u.a - v.a, c = u.b - v.b; return a * a + b * b + c * c; }

static Image resize(const Image& in, int w, int h) {   // area average (down) / bilinear (up)
    Image o; o.w = w; o.h = h; o.px.resize((size_t)w * h * 3);
    const float sx = (float)in.w / w, sy = (float)in.h / h;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const int x0 = (int)(x * sx), x1 = std::max(x0 + 1, (int)((x + 1) * sx)), y0 = (int)(y * sy), y1 = std::max(y0 + 1, (int)((y + 1) * sy));
            int acc[3] = {0, 0, 0}, n = 0;
            for (int yy = y0; yy < std::min(y1, in.h); ++yy) for (int xx = x0; xx < std::min(x1, in.w); ++xx) { const uint8_t* p = &in.px[((size_t)yy * in.w + xx) * 3]; acc[0] += p[0]; acc[1] += p[1]; acc[2] += p[2]; ++n; }
            for (int c = 0; c < 3; ++c) o.px[((size_t)y * w + x) * 3 + c] = (uint8_t)(n ? acc[c] / n : 0);
        }
    return o;
}
static void blur(Image& im, float r) {   // box blur, three passes (close to a Gaussian)
    const int k = (int)std::lround(r);
    if (k < 1) return;
    std::vector<uint8_t> t(im.px.size());
    for (int pass = 0; pass < 3; ++pass) {
        for (int y = 0; y < im.h; ++y) for (int x = 0; x < im.w; ++x) for (int c = 0; c < 3; ++c) {
            int s = 0, n = 0; for (int d = -k; d <= k; ++d) { const int xx = std::clamp(x + d, 0, im.w - 1); s += im.px[((size_t)y * im.w + xx) * 3 + c]; ++n; }
            t[((size_t)y * im.w + x) * 3 + c] = (uint8_t)(s / n);
        }
        for (int y = 0; y < im.h; ++y) for (int x = 0; x < im.w; ++x) for (int c = 0; c < 3; ++c) {
            int s = 0, n = 0; for (int d = -k; d <= k; ++d) { const int yy = std::clamp(y + d, 0, im.h - 1); s += t[((size_t)yy * im.w + x) * 3 + c]; ++n; }
            im.px[((size_t)y * im.w + x) * 3 + c] = (uint8_t)(s / n);
        }
    }
}

// ---------------------------------------------------------------- geometry
struct P { double x, y; };
static P operator+(P a, P b) { return {a.x + b.x, a.y + b.y}; }
static P operator-(P a, P b) { return {a.x - b.x, a.y - b.y}; }
static P operator*(P a, double s) { return {a.x * s, a.y * s}; }
static double dot(P a, P b) { return a.x * b.x + a.y * b.y; }
static double len(P a) { return std::sqrt(dot(a, a)); }
static P unit(P a) { const double l = len(a); return l > 1e-12 ? a * (1 / l) : P{0, 0}; }

// Douglas-Peucker on an open run (keeps indices)
static void dp(const std::vector<P>& pts, int i0, int i1, double eps, std::vector<char>& keep) {
    if (i1 <= i0 + 1) return;
    const P a = pts[i0], b = pts[i1], ab = b - a; const double L = len(ab);
    double worst = -1; int at = -1;
    for (int i = i0 + 1; i < i1; ++i) {
        const P ap = pts[i] - a;
        const double d = L > 1e-9 ? std::fabs(ab.x * ap.y - ab.y * ap.x) / L : len(ap);
        if (d > worst) { worst = d; at = i; }
    }
    if (worst > eps) { keep[at] = 1; dp(pts, i0, at, eps, keep); dp(pts, at, i1, eps, keep); }
}

// cubic Bezier fitting (least squares with fixed end tangents, Newton reparameterisation, split at the worst point)
struct Cubic { P p0, p1, p2, p3; };
static P bez(const Cubic& c, double t) { const double u = 1 - t; return c.p0 * (u * u * u) + c.p1 * (3 * u * u * t) + c.p2 * (3 * u * t * t) + c.p3 * (t * t * t); }
static P bez1(const Cubic& c, double t) { const double u = 1 - t; return (c.p1 - c.p0) * (3 * u * u) + (c.p2 - c.p1) * (6 * u * t) + (c.p3 - c.p2) * (3 * t * t); }
static P bez2(const Cubic& c, double t) { return (c.p2 - c.p1 * 2 + c.p0) * (6 * (1 - t)) + (c.p3 - c.p2 * 2 + c.p1) * (6 * t); }

static Cubic leastSquares(const std::vector<P>& d, int f, int l, const std::vector<double>& u, P t1, P t2) {
    double C[2][2] = {{0, 0}, {0, 0}}, X[2] = {0, 0};
    const P p0 = d[f], p3 = d[l];
    for (int i = 0; i <= l - f; ++i) {
        const double t = u[i], s = 1 - t;
        const P a1 = t1 * (3 * s * s * t), a2 = t2 * (3 * s * t * t);
        C[0][0] += dot(a1, a1); C[0][1] += dot(a1, a2); C[1][1] += dot(a2, a2);
        const P tmp = d[f + i] - (p0 * (s * s * s) + p0 * (3 * s * s * t) + p3 * (3 * s * t * t) + p3 * (t * t * t));
        X[0] += dot(a1, tmp); X[1] += dot(a2, tmp);
    }
    C[1][0] = C[0][1];
    const double det = C[0][0] * C[1][1] - C[1][0] * C[0][1];
    double al = 0, ar = 0;
    if (std::fabs(det) > 1e-12) { al = (X[0] * C[1][1] - X[1] * C[0][1]) / det; ar = (C[0][0] * X[1] - C[1][0] * X[0]) / det; }
    const double seg = len(p3 - p0), eps = 1e-6 * seg;
    if (al < eps || ar < eps) al = ar = seg / 3;   // the fallback: a third of the chord (Wu/Barsky)
    return {p0, p0 + t1 * al, p3 + t2 * ar, p3};
}
static void fitCubic(const std::vector<P>& d, int f, int l, P t1, P t2, double err, std::vector<Cubic>& out, int depth = 0) {
    if (l - f == 1) { const double s = len(d[l] - d[f]) / 3; out.push_back({d[f], d[f] + t1 * s, d[l] + t2 * s, d[l]}); return; }
    std::vector<double> u(l - f + 1, 0.0);
    for (int i = f + 1; i <= l; ++i) u[i - f] = u[i - f - 1] + len(d[i] - d[i - 1]);
    for (double& v : u) v /= std::max(1e-12, u.back());
    Cubic c = leastSquares(d, f, l, u, t1, t2);
    auto maxErr = [&](int& split) {
        double worst = -1; split = (f + l) / 2;
        for (int i = f + 1; i < l; ++i) { const P e = bez(c, u[i - f]) - d[i]; const double q = dot(e, e); if (q >= worst) { worst = q; split = i; } }
        return worst;
    };
    int split; double e = maxErr(split);
    if (e < err) { out.push_back(c); return; }
    if (e < err * 4) for (int it = 0; it < 4; ++it) {   // Newton: better parameters, fitted again
        for (int i = 0; i <= l - f; ++i) {
            const P q = bez(c, u[i]) - d[f + i], q1 = bez1(c, u[i]), q2 = bez2(c, u[i]);
            const double den = dot(q1, q1) + dot(q, q2);
            if (std::fabs(den) > 1e-12) u[i] = std::clamp(u[i] - dot(q, q1) / den, 0.0, 1.0);
        }
        c = leastSquares(d, f, l, u, t1, t2);
        e = maxErr(split);
        if (e < err) { out.push_back(c); return; }
    }
    if (depth > 40) { out.push_back(c); return; }
    split = std::clamp(split, f + 1, l - 1);
    const P tc = unit(d[split - 1] - d[split + 1]);
    fitCubic(d, f, split, t1, tc, err, out, depth + 1);
    fitCubic(d, split, l, tc * -1, t2, err, out, depth + 1);
}

// ---------------------------------------------------------------- outlines of a mask (pixel edges, holes too)
// every boundary edge, inside on its right (y down); joined into loops, turning right where two pixels touch at a corner
static void outlines(const std::vector<uint8_t>& m, int W, int H, std::vector<std::vector<P>>& loops) {
    const int VW = W + 1;
    struct E { int from, to; bool used; };
    std::vector<E> edges;
    std::vector<std::array<int, 2>> out((size_t)VW * (H + 1), {-1, -1});
    auto at = [&](int x, int y) { return x >= 0 && y >= 0 && x < W && y < H && m[(size_t)y * W + x]; };
    auto add = [&](int x0, int y0, int x1, int y1) {
        const int a = y0 * VW + x0, b = y1 * VW + x1;
        auto& o = out[a]; (o[0] < 0 ? o[0] : o[1]) = (int)edges.size();
        edges.push_back({a, b, false});
    };
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            if (!m[(size_t)y * W + x]) continue;
            if (!at(x, y - 1)) add(x, y, x + 1, y);
            if (!at(x + 1, y)) add(x + 1, y, x + 1, y + 1);
            if (!at(x, y + 1)) add(x + 1, y + 1, x, y + 1);
            if (!at(x - 1, y)) add(x, y + 1, x, y);
        }
    for (size_t e0 = 0; e0 < edges.size(); ++e0) {
        if (edges[e0].used) continue;
        std::vector<P> loop;
        int e = (int)e0;
        while (e >= 0 && !edges[e].used) {
            edges[e].used = true;
            const int a = edges[e].from, b = edges[e].to;
            loop.push_back({(double)(a % VW), (double)(a / VW)});
            const auto& o = out[b];
            // the way on: the only edge out, or (two ways on, where pixels touch at a corner) the right turn, which keeps
            // diagonal neighbours apart. Chosen before looking at "used": back at the start, the way on is the first
            // edge again (used: the loop is closed), never the other loop's edge
            int next = o[0];
            if (o[1] >= 0) {
                const int dx = b % VW - a % VW, dy = b / VW - a / VW;
                const int c = o[1], ex = edges[c].to % VW - b % VW, ey = edges[c].to / VW - b / VW;
                if (dx * ey - dy * ex > 0) next = c;
            }
            e = next;
        }
        if (loop.size() >= 4) loops.push_back(std::move(loop));
    }
}

// one loop -> SVG path data (scaled to the picture), its node count
static int loopToPath(const std::vector<P>& raw, const Settings& s, double scale, std::ostringstream& d) {
    // the stair-step corners only (straight runs of unit edges collapse)
    std::vector<P> v;
    const size_t n = raw.size();
    for (size_t i = 0; i < n; ++i) {
        const P a = raw[(i + n - 1) % n], b = raw[i], c = raw[(i + 1) % n];
        if ((b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x) != 0) v.push_back(b);
    }
    if (v.size() < 3) return 0;
    // stair-steps flattened: Douglas-Peucker on the corners, closed (split at the point furthest from the first)
    const size_t m = v.size();
    size_t far = 0; double fd = -1; for (size_t i = 1; i < m; ++i) { const double q = len(v[i] - v[0]); if (q > fd) { fd = q; far = i; } }
    std::vector<P> ring(v); ring.push_back(v[0]);
    std::vector<char> keep(ring.size(), 0); keep[0] = keep[far] = keep[ring.size() - 1] = 1;
    dp(ring, 0, (int)far, s.straighten, keep); dp(ring, (int)far, (int)ring.size() - 1, s.straighten, keep);
    std::vector<P> poly; for (size_t i = 0; i + 1 < ring.size(); ++i) if (keep[i]) poly.push_back(ring[i]);
    if (poly.size() < 3) return 0;
    // densify (the curve fitter wants points along the way): each straightened side in steps of about a pixel
    const size_t k = poly.size();
    std::vector<P> dense; std::vector<int> cornerAt;   // index in dense of each poly vertex
    for (size_t i = 0; i < k; ++i) {
        const P a = poly[i], b = poly[(i + 1) % k];
        cornerAt.push_back((int)dense.size());
        const int steps = std::max(1, (int)std::ceil(len(b - a)));
        for (int j = 0; j < steps; ++j) dense.push_back(a + (b - a) * ((double)j / steps));
    }
    // the corners: where it turns sharper than the corner angle
    std::vector<int> corners;
    for (size_t i = 0; i < k; ++i) {
        const P a = poly[(i + k - 1) % k], b = poly[i], c = poly[(i + 1) % k];
        const double cosA = dot(unit(a - b), unit(c - b));
        const double ang = std::acos(std::clamp(cosA, -1.0, 1.0)) * 180 / 3.14159265358979;   // 180: straight on
        // a turn between short sides is a pixel step, not a corner (unless it's very sharp: a spike)
        const double shortest = std::min(len(a - b), len(c - b));
        if (ang < s.corner && (shortest >= 2.5 * std::max(1.0f, s.straighten) || ang < 40)) corners.push_back(cornerAt[i]);
    }
    const double err = (double)s.curveFit * s.curveFit;
    std::vector<Cubic> cubics;
    const int N = (int)dense.size();
    auto tangentAt = [&](int i, int dir) {   // along the outline, a few points' worth
        P t{0, 0}; for (int j = 1; j <= 3; ++j) t = t + (dense[((i + dir * j) % N + N) % N] - dense[i]);
        return unit(t);
    };
    // the break points: the corners, and (a loop with fewer than two) smooth ones across from them, so no run
    // starts and ends at the same point. At a smooth break the curve keeps its direction (a centred tangent).
    struct Break { int at; bool smooth; };
    std::vector<Break> br;
    for (int c : corners) br.push_back({c, false});
    if (br.empty()) { br.push_back({0, true}); br.push_back({N / 2, true}); }
    else if (br.size() == 1) br.push_back({(br[0].at + N / 2) % N, true});
    std::sort(br.begin(), br.end(), [](const Break& x, const Break& y) { return x.at < y.at; });
    auto centred = [&](int i) { return unit(dense[(i + 1) % N] - dense[(i + N - 1) % N]); };
    for (size_t c = 0; c < br.size(); ++c) {
        const Break A = br[c], B = br[(c + 1) % br.size()];
        std::vector<P> run; for (int i = A.at;; i = (i + 1) % N) { run.push_back(dense[i]); if (i == B.at && run.size() > 1) break; if (run.size() > (size_t)N + 1) break; }
        if (run.size() < 2) continue;
        const int L = (int)run.size() - 1;
        const P ab = run[L] - run[0]; const double AB = len(ab);
        // straight sides stay lines (between two corners, nothing strays from the chord)
        bool straight = !A.smooth && !B.smooth && AB > 1e-6;
        for (int i = 1; i < L && straight; ++i) { const P ap = run[i] - run[0]; if (std::fabs(ab.x * ap.y - ab.y * ap.x) / AB > s.curveFit * 0.5) straight = false; }
        if (straight) { cubics.push_back({run[0], run[0], run[L], run[L]}); continue; }
        const P t1 = A.smooth ? centred(A.at) : unit(run[1] - run[0]);
        const P t2 = B.smooth ? centred(B.at) * -1 : unit(run[L - 1] - run[L]);
        fitCubic(run, 0, L, t1, t2, err, cubics);
    }
    (void)tangentAt;
    if (cubics.empty()) return 0;
    char b[200];
    auto fmt = [&](double v) { return v * scale; };
    std::snprintf(b, sizeof b, "M%.2f %.2f", fmt(cubics[0].p0.x), fmt(cubics[0].p0.y)); d << b;
    for (const auto& c : cubics) {
        if (c.p1.x == c.p0.x && c.p1.y == c.p0.y && c.p2.x == c.p3.x && c.p2.y == c.p3.y) { std::snprintf(b, sizeof b, "L%.2f %.2f", fmt(c.p3.x), fmt(c.p3.y)); }
        else std::snprintf(b, sizeof b, "C%.2f %.2f %.2f %.2f %.2f %.2f", fmt(c.p1.x), fmt(c.p1.y), fmt(c.p2.x), fmt(c.p2.y), fmt(c.p3.x), fmt(c.p3.y));
        d << b;
    }
    d << "Z";
    return (int)cubics.size();
}

// ---------------------------------------------------------------- the whole picture
bool trace(const Image& in, const Settings& s, Result& out, std::atomic<bool>* cancel, std::atomic<int>* percent) {
    if (in.empty()) return false;
    auto stopped = [&] { return cancel && cancel->load(); };
    auto pct = [&](int v) { if (percent) *percent = v; };
    // the working size
    const double f = std::min(1.0, (double)std::max(64, s.workSize) / std::max(in.w, in.h));
    Image im = f < 1 ? resize(in, std::max(1, (int)std::lround(in.w * f)), std::max(1, (int)std::lround(in.h * f))) : in;
    if (s.smoothInput > 0) blur(im, s.smoothInput);
    const int W = im.w, H = im.h; const size_t NP = (size_t)W * H;
    pct(5);
    // 1. colours: k-means++ in Lab
    std::vector<Lab> lab(NP); for (size_t i = 0; i < NP; ++i) lab[i] = toLab(&im.px[i * 3]);
    std::mt19937 rng((unsigned)s.seed);
    std::vector<Lab> sample; { const size_t step = std::max<size_t>(1, NP / 40000); for (size_t i = 0; i < NP; i += step) sample.push_back(lab[i]); }
    const int K = std::clamp(s.colours, 2, 64);
    std::vector<Lab> cent; cent.push_back(sample[std::uniform_int_distribution<size_t>(0, sample.size() - 1)(rng)]);
    std::vector<float> best(sample.size(), 1e30f);
    while ((int)cent.size() < K) {
        double sum = 0; for (size_t i = 0; i < sample.size(); ++i) { best[i] = std::min(best[i], d2(sample[i], cent.back())); sum += best[i]; }
        if (sum <= 0) break;
        double r = std::uniform_real_distribution<double>(0, sum)(rng); size_t pick = 0;
        for (; pick < sample.size() - 1; ++pick) { r -= best[pick]; if (r <= 0) break; }
        cent.push_back(sample[pick]);
    }
    std::vector<int> as(sample.size());
    for (int it = 0; it < 20; ++it) {
        for (size_t i = 0; i < sample.size(); ++i) { int b = 0; float bd = 1e30f; for (size_t c = 0; c < cent.size(); ++c) { const float q = d2(sample[i], cent[c]); if (q < bd) { bd = q; b = (int)c; } } as[i] = b; }
        std::vector<double> acc(cent.size() * 4, 0.0);
        for (size_t i = 0; i < sample.size(); ++i) { double* a = &acc[as[i] * 4]; a[0] += sample[i].L; a[1] += sample[i].a; a[2] += sample[i].b; a[3] += 1; }
        for (size_t c = 0; c < cent.size(); ++c) if (acc[c * 4 + 3] > 0) cent[c] = {(float)(acc[c * 4] / acc[c * 4 + 3]), (float)(acc[c * 4 + 1] / acc[c * 4 + 3]), (float)(acc[c * 4 + 2] / acc[c * 4 + 3])};
    }
    const int KC = (int)cent.size();
    std::vector<int> label(NP);
    for (size_t i = 0; i < NP; ++i) { int b = 0; float bd = 1e30f; for (int c = 0; c < KC; ++c) { const float q = d2(lab[i], cent[c]); if (q < bd) { bd = q; b = c; } } label[i] = b; }
    if (stopped()) return false;
    pct(25);
    // 2. speckles merged into the neighbour they touch most (twice: merging can leave new small ones)
    for (int pass = 0; pass < 2 && s.speckle > 1; ++pass) {
        std::vector<int> comp(NP, -1); std::vector<int> stack, members;
        for (size_t i0 = 0; i0 < NP; ++i0) {
            if (comp[i0] >= 0) continue;
            const int lb = label[i0]; members.clear(); stack.assign(1, (int)i0); comp[i0] = (int)i0;
            std::vector<int> touch(KC, 0);
            while (!stack.empty()) {
                const int p = stack.back(); stack.pop_back(); members.push_back(p);
                const int x = p % W, y = p / W;
                const int nb[4] = {x > 0 ? p - 1 : -1, x < W - 1 ? p + 1 : -1, y > 0 ? p - W : -1, y < H - 1 ? p + W : -1};
                for (int q : nb) { if (q < 0) continue; if (label[q] == lb) { if (comp[q] < 0) { comp[q] = (int)i0; stack.push_back(q); } } else ++touch[label[q]]; }
            }
            if ((int)members.size() < s.speckle) {
                int to = -1, most = 0; for (int c = 0; c < KC; ++c) if (touch[c] > most) { most = touch[c]; to = c; }
                if (to >= 0) for (int p : members) label[p] = to;
            }
        }
        if (stopped()) return false;
    }
    pct(40);
    // the colours themselves: the mean of their pixels (the picture's own colours, not the Lab centres)
    std::vector<double> rgb(KC * 3, 0.0); std::vector<size_t> area(KC, 0);
    for (size_t i = 0; i < NP; ++i) { const int c = label[i]; for (int k = 0; k < 3; ++k) rgb[c * 3 + k] += im.px[i * 3 + k]; ++area[c]; }
    std::vector<int> order; for (int c = 0; c < KC; ++c) if (area[c]) order.push_back(c);
    std::sort(order.begin(), order.end(), [&](int a, int b) { return area[a] > area[b]; });
    std::vector<int> rank(KC, -1); for (size_t r = 0; r < order.size(); ++r) rank[order[r]] = (int)r;
    out.quantised.w = W; out.quantised.h = H; out.quantised.px.resize(NP * 3);
    for (size_t i = 0; i < NP; ++i) for (int k = 0; k < 3; ++k) out.quantised.px[i * 3 + k] = (uint8_t)(rgb[label[i] * 3 + k] / std::max<size_t>(1, area[label[i]]));
    // 3 + 4. layers, largest first; each one its colour and every colour above it
    const double scale = (double)in.w / W;
    std::ostringstream svg;
    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 " << in.w << " " << in.h << "\" width=\"" << in.w << "\" height=\"" << in.h << "\">\n";
    svg << "<!-- traced by Arachne -->\n";
    out.shapes = out.nodes = 0; out.colours = (int)order.size();
    std::vector<uint8_t> mask(NP);
    for (size_t r = 0; r < order.size(); ++r) {
        if (stopped()) return false;
        pct(40 + (int)(60.0 * r / order.size()));
        const int c = order[r];
        char col[16]; std::snprintf(col, sizeof col, "#%02x%02x%02x", (int)(rgb[c * 3] / area[c]), (int)(rgb[c * 3 + 1] / area[c]), (int)(rgb[c * 3 + 2] / area[c]));
        if (r == 0) {   // the biggest: the whole picture under everything (or left out)
            if (!s.dropBackground && !s.outlines) { svg << "<rect width=\"" << in.w << "\" height=\"" << in.h << "\" fill=\"" << col << "\"/>\n"; ++out.shapes; }
            if (!s.outlines) continue;
        }
        // filled: this colour and all above it (no gaps); outlines: this colour alone
        for (size_t i = 0; i < NP; ++i) mask[i] = s.outlines ? label[i] == c : rank[label[i]] >= (int)r;
        std::vector<std::vector<P>> loops; outlines(mask, W, H, loops);
        std::ostringstream d; int nodes = 0;
        for (const auto& l : loops) nodes += loopToPath(l, s, scale, d);
        if (!nodes) continue;
        out.nodes += nodes; ++out.shapes;
        if (s.outlines) svg << "<path fill=\"none\" stroke=\"" << col << "\" stroke-width=\"" << s.strokeWidth * scale << "\" stroke-linejoin=\"round\" d=\"" << d.str() << "\"/>\n";
        else svg << "<path fill=\"" << col << "\" fill-rule=\"evenodd\" d=\"" << d.str() << "\"/>\n";
    }
    svg << "</svg>\n";
    out.svg = svg.str();
    pct(100);
    return true;
}

}
