// Tes sambungan antar sub di tab A Mic - Komp (v3.4):
//  [1] Multiband yang dijeda Skala tidak lupa hasil belajarnya (level langsung kembali).
//  [2] EQ vokal sebelum limiter: puncak keluar strip tidak melewati ceiling walau EQ +12 dB.
//  [3] Skala -> rantai tanpa NaN/Inf, pada input senyap dan sangat keras.
// g++ -O2 -std=c++17 -o test_link test_link.cpp && ./test_link
#include <cstdio>
#include <vector>
#include "../../app/src/main/cpp/compressor.h"
#include "../../app/src/main/cpp/vocal_scale.h"

static float voice(int i, float sr, float amp) {
    const float t = i / sr;
    const float env = 0.6f + 0.4f * std::sin(6.2831853f * 1.3f * t);
    return amp * env * (std::sin(6.2831853f * 220.f * t) + 0.4f * std::sin(6.2831853f * 1800.f * t));
}

// Jalankan rantai; held aktif pada detik [hOn, hOff). Kembalikan RMS keluar (dB) 0..300 ms
// setelah held dilepas.
static float runHold(bool keepMemory) {
    const float SR = 48000.f;
    StereoChain ch;
    ch.reset();
    ProcParams p;
    p.mb.on = true;
    p.mb.autoOn = true;
    double acc = 0;
    int n = 0;
    for (int i = 0; i < (int)(SR * 9); ++i) {
        const float t = i / SR;
        if (i % 256 == 0) {
            const bool hold = t >= 4.f && t < 6.f;
            if (keepMemory) {
                p.mb.held = hold;
                p.mb.on = true;
            } else {  // perilaku lama v3.3: multiband dimatikan
                p.mb.held = false;
                p.mb.on = !hold;
            }
            ch.prepare(SR, p);
        }
        float yl, yr, red;
        ch.process(voice(i, SR, 0.05f), 0.f, false, p, yl, yr, red);
        if (t >= 6.f && t < 6.3f) {
            acc += yl * yl;
            ++n;
        }
    }
    return 10.f * std::log10((float)(acc / n) + 1e-12f);
}

int main() {
    const float SR = 48000.f;
    // ---- [1]
    // Rujukan: RMS 300 ms dari multiband yang sudah lama belajar (tanpa jeda).
    float ref;
    {
        StereoChain ch; ch.reset(); ProcParams p; p.mb.on = true;
        double acc = 0; int n = 0;
        for (int i = 0; i < (int)(SR * 9); ++i) {
            if (i % 256 == 0) ch.prepare(SR, p);
            float yl, yr, red;
            ch.process(voice(i, SR, 0.05f), 0.f, false, p, yl, yr, red);
            const float t = i / SR;
            if (t >= 6.f && t < 6.3f) { acc += yl * yl; ++n; }
        }
        ref = 10.f * std::log10((float)(acc / n) + 1e-12f);
    }
    const float lama = runHold(false), baru = runHold(true);
    printf("[1] Level 300 ms pertama setelah Skala OFF (rujukan multiband stabil %.1f dB)\n", ref);
    printf("    v3.3 (multiband lupa)   : %.1f dB  -> selisih %+.1f dB\n", lama, lama - ref);
    printf("    v3.4 (multiband ingat)  : %.1f dB  -> selisih %+.1f dB\n", baru, baru - ref);

    // ---- [2]
    {
        StereoChain ch; ch.reset(); VocalEq veq; veq.reset();
        ch.veq = &veq; ch.veqOn = true;
        float g[VocalEq::kBands];
        for (float& x : g) x = 12.f;
        veq.setup(SR, g);
        ProcParams p; p.ceilDb = -1.f; p.limOn = true; p.comp.ratio = 1.f;
        float pk = 0;
        for (int i = 0; i < (int)(SR * 3); ++i) {
            if (i % 256 == 0) ch.prepare(SR, p);
            float yl, yr, red;
            ch.process(voice(i, SR, 0.5f), 0.f, false, p, yl, yr, red);
            pk = std::max(pk, std::fabs(yl));
        }
        printf("[2] EQ vokal +12 dB semua pita, ceiling -1 dBFS (0.891): puncak keluar strip %.3f %s\n",
               pk, pk <= 0.8913f ? "OK" : "LEWAT");
    }

    // ---- [3]
    {
        VocalScale sc; sc.reset();
        ScaleParams sp; sp.on = true; sp.ratio = 3.06f; sp.pivotDb = -25.9f; sp.smoothMs = 14.f;
        sp.floorDb = -76.f; sp.maxBoostDb = 34.f;
        sc.prepare(SR, sp);
        StereoChain ch; ch.reset(); ProcParams p; p.mb.on = true; p.mb.held = true;
        p.comp.ratio = 1.f; p.comp.makeupDb = 0.f;
        bool bad = false; float pk = 0, gMax = -999, gMin = 999;
        for (int i = 0; i < (int)(SR * 6); ++i) {
            if (i % 256 == 0) ch.prepare(SR, p);
            const float t = i / SR;
            float x = t < 2.f ? 0.f : (t < 4.f ? voice(i, SR, 0.002f) : voice(i, SR, 1.5f));
            float r = 0.f;
            sc.process(x, r, false, sp);
            gMax = std::max(gMax, sc.gainDb); gMin = std::min(gMin, sc.gainDb);
            float yl, yr, red;
            ch.process(x, x, false, p, yl, yr, red);
            if (!std::isfinite(yl)) bad = true;
            pk = std::max(pk, std::fabs(yl));
        }
        printf("[3] Setelan acuan (3.06 / -25.9 / 14 ms / -76 / 34): NaN %s | puncak %.3f | "
               "kerja skala %+.1f .. %+.1f dB\n", bad ? "ADA" : "tidak", pk, gMin, gMax);
    }
}
