#pragma once

#include <array>
#include <vector>

namespace bbg::wt
{
// The Advanced engine's wavetables (Bass Architecture 2.0): HPDG 808's own tables, built from formulas at start-up
// (no third-party table is copied). Each table: 32 frames along its Position, each frame one cycle, band-limited
// per octave (mip levels: 512, 256, ... 2 harmonics), so a note reads the richest level whose top harmonic stays under
// 0.45 x the sample rate; each level keeps 8 samples per period of its top harmonic and is read with 4-point Hermite
// interpolation, so the interpolation's images stay far down too: no aliasing at any pitch (Tests: under -50 dB). Frames are DC-free and levelled to the same peak, so a Position
// sweep does not jump in level.
//
//   0 Basic Shapes   sine -> triangle -> saw -> square
//   1 Kick Pulse     a cycle that runs fast and slows (a kick's pitch drop inside one period), driven harder along
//                    the table: the digital, punchy body pulse basses are made of (Dimension / Sub Focus)
//   2 Harmonic Sweep 1 -> 64 saw harmonics, one by one
//   3 PWM            pulse width 50 % -> 5 %
//   4 Hard Sync      a saw synced to the note, ratio 1 -> 8
//   5 Fold           a sine folded harder and harder
//   6 Vowel          formants A -> E -> I -> O -> U over a C2 harmonic series
//   7 Growl          a sine phase-modulated by its own 3rd harmonic, deeper along the table
class Bank
{
public:
    static constexpr int kFrames = 32;
    static constexpr int kLevels = 9;  // level L: 512 >> L harmonics
    static constexpr int kTopHarmonics = 512;

    // Built once (the first call allocates: call it off the audio thread, e.g. in prepare()).
    static const Bank& get();
    static int count() noexcept;
    static const char* name(int table) noexcept;

    // one sample of `table` at Position pos (0 .. 1) and phase (0 .. 1), for a phase increment dt (cycles per sample)
    double read(int table, double pos, double phase, double dt) const noexcept;

    // for the tests: the level read at dt
    static int levelFor(double dt) noexcept;

private:
    Bank();
    struct Level
    {
        int size = 0;               // samples per cycle; a frame is stored as x[size - 1], x[0 .. size - 1], x[0], x[1]
        std::vector<float> frames;  // kFrames x (size + 3)
    };
    std::vector<std::array<Level, kLevels>> tables;
};
} // namespace bbg::wt
