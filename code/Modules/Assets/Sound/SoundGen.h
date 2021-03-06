#pragma once
//------------------------------------------------------------------------------
/**
    Small sound generation helper classes.
*/
#include "Core/Types.h"
#include "Core/Assertion.h"
#include <cmath>
#include <cstdlib>

namespace Oryol {
namespace SoundGen {

//------------------------------------------------------------------------------
/**
    @class Oryol::SoundGen::Sample
    @ingroup Assets
    @brief general sample math helper functions
*/
class Sample {
public:
    /// return 

    /// clamp float value to -1.0..+1.0
    static float32 Float32(float32 val) {
        if (val < -1.0f) {
            return -1.0f;
        }
        else if (val > 1.0f) {
            return 1.0f;
        }
        else {
            return val;
        }
    };
    /// clamp float value to -1.0..+1.0 and convert to int16 sample value
    static int16 Int16(float32 val) {
        int32 ival = int32(val * 32768);
        if (ival < -32768) {
            ival = -32768;
        }
        else if (ival > 32767) {
            ival = 32767;
        }
        return (int16) ival;
    };
};

//------------------------------------------------------------------------------
/**
    @class Oryol::SoundGen::Range
    @ingroup Assets
    @brief define time range and test time against range
*/
class Range {
public:
    /// start time
    float32 Begin = 0.0f;
    /// stop time
    float32 End = 0.0f;

    /// test if t is in [t0, t1], if yes return true and init Start/Stop
    bool In(float32 t, float32 t0, float32 t1) {
        if ((t >= t0) && (t < t1)) {
            this->Begin = t0;
            this->End = t1;
            return true;
        }
        else {
            return false;
        }
    };
    /// test if t < t0, if yes, return true, if no, return false and set Begin to t0
    bool BeforeBegin(float32 t, float32 t0) {
        if (t < t0) {
            return true;
        }
        else {
            this->Begin = t0;
            return false;
        }
    };
    /// test if t >= t0, if yes, return true and set Begin to t0
    bool AfterBegin(float32 t, float32 t0) {
        if (t >= t0) {
            this->Begin = t0;
            return true;
        }
        else {
            return false;
        }
    };
};

//------------------------------------------------------------------------------
/**
    @class Oryol::SoundGen::Freq
    @ingroup Assets
    @brief maintain sample position for frequency
    
    The Freq object is initialized with a sample duration in seconds,
    and a frequency (which can optionally be dynamic). Each call to 
    the Step() method returns a sample position normalized to the
    0.0..1.0 range, and advanced the sample position. The resulting
    sample position can directly be fed into wave generators.
*/
class Freq {
public:
    /// construct with sample duration (default frequency is 440Hz)
    Freq(float32 dt_) {
        o_assert_dbg(dt_ > 0.0f);
        this->dt = dt_;
        this->step = this->dt * 440.0f;
        this->pos = 0.0f;
    };
    /// construct with sample duration and frequency
    Freq(float32 dt_, float32 freq) {
        o_assert_dbg(dt_ > 0.0f);
        o_assert_dbg(freq > 0.0f);
        this->dt = dt_;
        this->step = this->dt * freq;
        this->pos = 0.0f;
    };
    /// get next sample position, and advance
    float32 Step() {
        float32 p = this->pos;
        this->pos += this->step;
        if (this->pos >= 1.0f) {
            this->pos = std::fmod(this->pos, 1.0f);
        }
        return p;
    };
    /// get next sample position with variable frequency, and advance
    float32 Step(float32 freq) {
        o_assert_dbg(freq > 0.0f);
        this->step = this->dt * freq;
        float32 p = this->pos;
        this->pos += this->step;
        if (this->pos >= 1.0f) {
            this->pos = std::fmod(this->pos, 1.0f);
        }
        return p;
    };

private:
    float32 dt;             // duration of one sample in seconds
    float32 step;           // current step size
    float32 pos;            // current sample position
};

//------------------------------------------------------------------------------
/**
    @class Oryol::SoundGen::Wave
    @ingroup Assets
    @brief helper class to generate sample wave data
    
    The wave generator generates a sine, square, triangle and
    saw-tooth sample from a sample position usually provided 
    by a Freq object. The sample pos parameter is expected
    to be normalized to 1 Hz (so, pos is 0.0 at start, and 1.0 at
    a full second).
*/
class Wave {
private:
    static const float pi;
public:
    /// get next sine wave value at normalized sample position
    static float32 Sine(float32 p) {
        return std::sin(p * pi * 2.0f);
    };
    /// get next square wave value at normalized sample position
    static float32 Square(float32 p) {
        return (p > 0.5f) ? -1.0f : 1.0f;
    };
    /// get next triangle wave value at normalized sample position
    static float32 Triangle(float32 p) {
        // FIXME: this should probably return 2 cycles
        if (p < 0.5f) {
            return (p * 4.0f) - 1.0f;
        }
        else {
            return 1.0f - ((p - 0.5f) * 4.0f);
        }
    };
    /// get sawtooth wave value at normalized sample position
    static float32 SawTooth(float32 p) {
        // FIXME: this should probably return 2 cycles
        return 1.0f - (p * 2.0f);
    };
    /// get inverse-sawtooth wave value at normalized sample position
    static float32 InvSawTooth(float32 p) {
        // FIXME: this should probably return 2 cycles
        return (p * 2.0f) - 1.0f;
    };
    /// return random noise
    static float32 RandNoise() {
        return (float32(std::rand() & 0xffff) / 32768.0f) - 1.0f;
    };
};

//------------------------------------------------------------------------------
/**
    @class Oryol::SoundGen::Mod
    @ingroup Assets
    @brief volume and frequency modulation helper functions
*/
class Mod {
public:
    /// linear fade-in, return value between 0.0 and 1.0
    /// @param t current sample time
    /// @param t0 start time where result is 0.0
    /// @param t1 end time where result is 1.0
    static float32 FadeIn(float32 t, float32 t0, float32 t1) {
        o_assert_dbg(t1 > t0);
        if (t < t0) {
            return 0.0f;
        }
        else if (t > t1) {
            return 1.0f;
        }
        else {
            return (t - t0) / (t1 - t0);
        }
    };
    /// linear fade-out, return value between 1.0 and 0.0
    /// @param t current sample time
    /// @param t0 start time where result is 1.0
    /// @param t1 end time where result is 0.0
    static float32 FadeOut(float32 t, float32 t0, float32 t1) {
        o_assert_dbg(t1 > t0);
        if (t < t0) {
            return 1.0f;
        }
        else if (t > t1) {
            return 0.0f;
        }
        else {
            return 1.0f - ((t - t0) / (t1 - t0));
        }
    };
    /// fade-in squared (start slow and accelerate)
    /// @param t current sample time
    /// @param t0 start time where result is 0.0
    /// @param t1 end time where result is 1.0
    static float32 FadeInSq(float32 t, float32 t0, float32 t1) {
        float32 v = FadeIn(t, t0, t1);
        return v * v;
    };
    /// fade-out squared (fast decay at start)
    /// @param t current sample time
    /// @param t0 start time where result is 1.0
    /// @param t1 end time where result is 0.0
    static float32 FadeOutSq(float32 t, float32 t0, float32 t1) {
        float32 v = FadeOut(t, t0, t1);
        return v * v;
    };
    /// linear-interpolate from v0 at t0 to v1 at t1
    /// @param t current sample time
    /// @param t0 start time where result is v0
    /// @param t1 end time where result is v1
    /// @param v0 start value
    /// @param v1 end value
    static float32 Lerp(float32 t, float32 t0, float32 t1, float32 v0, float32 v1) {
        o_assert_dbg(t1 > t0);
        if (t < t0) {
            return v0;
        }
        else if (t > t1) {
            return v1;
        }
        else {
            return v0 + (v1 - v0) * ((t - t0) / (t1 - t0));
        }
    };

};

//------------------------------------------------------------------------------
/**
    @class Oryol::SoundGen::NamcoVoice
    @ingroup Assets
    @brief Namco WSG sound chip helper functions
    
    See here: http://www.lomont.org/Software/Games/PacMan/PacmanEmulation.pdf
    And here: http://www.vecoven.com/elec/pacman/code/pac_z80.html
    
    FIXME: rewrite the whole math to integer, and exactly to Namco spec!

*/
class NamcoVoice {
public:
    /// waveform Ids from the Pacman ROM:
    enum WaveForm {
        Pacman0 = 0,
        Pacman1,
        Pacman2,
        Pacman3,
        Pacman4,
        Pacman5,
        Pacman6,
        Pacman7,
        Pacman8,

        NumWaveForms
    };

    /// the number of samples in a wave form
    static const int NumWaveSamples = 32;
    /// the actual waveform data (see SoundGen.cc)
    static const char WaveData[NumWaveForms][NumWaveSamples];

    /// current wave
    WaveForm Wave = Pacman0;
    /// current volume
    float32 Volume = 1.0f;
    /// current frequency
    float32 Frequency = 440.0f;

    /// construct from WaveForm, sample duration, frequency and volume
    NamcoVoice(float32 dt_, WaveForm wave) {
        this->dt = dt_;
        this->pos = 0.0f;
        this->Wave = wave;
    };

    /// step the voice generator, return new sample value in range -1.0f..1.0f
    float32 Step() {
        o_assert_range_dbg(this->Wave, NumWaveForms);
        if (this->Volume > 0.0f) {
            int32 waveIndex = int32(32.0f * this->pos);
            float32 s = ((float32(WaveData[this->Wave][waveIndex]) / 8.0f) - 1.0f);
            s *= this->Volume;
            this->pos += this->dt * this->Frequency;
            if (this->pos >= 1.0f) {
                this->pos = std::fmod(this->pos, 1.0f);
            }
            return s;
        }
        else {
            return 0.0f;
        }
    };

private:
    float32 dt;
    float32 pos;
};

} // namespace Oryol
} // namespace SoundGen
