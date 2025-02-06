//
// mixing_console.hpp
//
// MiniDexed - Dexed FM synthesizer for bare metal Raspberry Pi
// Author: Vincent Gauché
// Copyright (C) 2022  The MiniDexed Team
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

// Implementation of the MixingConsole class defined in mixing_console.h
#pragma once

#include "mixing_console_constants.h"
#include "fx_tube.h"
#include "fx_chorus.h"
#include "fx_flanger.h"
#include "fx_orbitone.h"
#include "fx_phaser.h"
#include "fx_delay.h"
#include "effect_platervbstereo.h"
#include "fx_reverberator.h"
#include "fx_dry.h"
#include "fx_unit2.hpp"

template<size_t nb_inputs>
class MixingConsole : public FXBase
{
    DISALLOW_COPY_AND_ASSIGN(MixingConsole);

public:
    MixingConsole(float32_t sampling_rate, size_t buffer_size, bool swapStereoImage = false);
    ~MixingConsole();

    inline size_t getChannelNumber() const;

    inline void bypass(bool bypass);
    inline bool bypass() const;

    // Send section
    inline void setChannelLevel(size_t in, float32_t lvl);
    inline void setPan(size_t in, float32_t pan);
    inline void swapStereoImage(bool swap);
    inline void setSendLevel(size_t in, MixerOutput fx, float32_t lvl);
    inline void setInputSample(size_t in, float32_t sampleL, float32_t sampleR);
    inline void setInputSampleBuffer(size_t in, float32_t* samples);

    // Return section
    inline void setFXSendLevel(MixerOutput fromFX, MixerOutput toFX, float32_t lvl);
    inline void setReturnSample(MixerOutput ret, float32_t sampleL, float32_t sampleR);

    // Get FX
    inline FXElement* getFX(size_t fx);
    inline FXUnit2<Tube>* getTube();
    inline FXUnit2<Chorus>* getChorus();
    inline FXUnit2<Flanger>* getFlanger();
    inline FXUnit2<Orbitone>* getOrbitone();
    inline FXUnit2<Phaser>* getPhaser();
    inline FXUnit2<Delay>* getDelay();
    inline FXUnit2<AudioEffectPlateReverb>* getPlateReverb();
    inline FXUnit2<Reverberator>* getReverberator();
    inline FXUnit2<Dry>* getDry();

    // Processing
    inline void init();
    inline void reset() override;
    inline void preProcessInputSampleBuffer(size_t in, size_t nSamples);
    inline void injectInputSamples(size_t in, float32_t* samplesL, float32_t* samplesR, size_t nSamples);
    inline void processSample(float32_t& outL, float32_t& outR);
    void process(float32_t* outL, float32_t* outR);
    void process(float32_t* outLR);

protected:
    inline void updatePan(size_t in);
    inline void setLevel(size_t in, MixerOutput fx, float32_t lvl);
    inline void setSample(size_t in, float32_t sampleL, float32_t sampleR);

private:
    static inline float32_t weighted_sum(const float32_t* data, const float32_t* weights, size_t size);

    const size_t BufferSize;

    bool m_bypass;

    float32_t m_channelLevel[nb_inputs];
    float32_t m_pan[StereoChannels::kNumChannels + 1][nb_inputs];
    bool m_swapStereoImage;
    float32_t* m_tgInputSampleBuffer[nb_inputs];
    float32_t* m_inputSampleBuffer[StereoChannels::kNumChannels][nb_inputs];
    float32_t m_inputSamples[StereoChannels::kNumChannels][nb_inputs + MixerOutput::kFXCount - 1];
    float32_t m_levels[MixerOutput::kFXCount][nb_inputs + MixerOutput::kFXCount - 1];
    volatile size_t m_nSamples;

    FXElement* m_fx[MixerOutput::kFXCount];
    FXUnit2<Tube>* m_tube;
    FXUnit2<Chorus>* m_chorus;
    FXUnit2<Flanger>* m_flanger;
    FXUnit2<Orbitone>* m_orbitone;
    FXUnit2<Phaser>* m_phaser;
    FXUnit2<Delay>* m_delay;
    FXUnit2<AudioEffectPlateReverb>* m_plate_reverb;
    FXUnit2<Reverberator>* m_reverberator;
    FXUnit2<Dry>* m_dry;

    IMPLEMENT_DUMP(
        const size_t space = 9;
        const size_t precision = 5;

        std::stringstream ss;

        out << "START " << tag << "(" << typeid(*this).name() << ") dump" << std::endl << std::endl;

        out << "\t" << "Input levels & Pan:" << std::endl;
        {
            SS_RESET(ss, precision, std::left);
            SS_SPACE(ss, ' ', space, std::left, '|');
            SS__TEXT(ss, ' ', space, std::left, '|', "Level");
            SS__TEXT(ss, ' ', space, std::left, '|', "Pan L");
            SS__TEXT(ss, ' ', space, std::left, '|', "Pan R");
            SS__TEXT(ss, ' ', space, std::left, '|', "Pan");
            out << "\t" << ss.str() << std::endl;

            SS_RESET(ss, precision, std::left);
            SS_SPACE(ss, '-', space, std::left, '+');
            SS_SPACE(ss, '-', space, std::left, '+');
            SS_SPACE(ss, '-', space, std::left, '+');
            SS_SPACE(ss, '-', space, std::left, '+');
            SS_SPACE(ss, '-', space, std::left, '+');
            out << "\t" << ss.str() << std::endl;

            for(size_t i = 0; i < nb_inputs; ++i)
            {
                std::stringstream s;
                s << "* Input ";
                s << (i + 1);

                SS_RESET(ss, precision, std::left);
                SS__TEXT(ss, ' ', space, std::left, '|', s.str());
                SS__TEXT(ss, ' ', space - 1, std::right, " |", this->m_channelLevel[i]);
                SS__TEXT(ss, ' ', space - 1, std::right, " |", this->m_pan[StereoChannels::Left][i]);
                SS__TEXT(ss, ' ', space - 1, std::right, " |", this->m_pan[StereoChannels::Right][i]);
                SS__TEXT(ss, ' ', space - 1, std::right, " |", this->pan_[StereoChannels::kNumChannels][i]);

                out << "\t" << ss.str() << std::endl;
            }
        }
        out << std::endl;

        out << "\t" << "Mixing Console input samples:" << std::endl;
        {
            SS_RESET(ss, precision, std::left);
            SS_SPACE(ss, ' ', space, std::left, '|');
            for(size_t i = 0; i < nb_inputs; ++i)
            {
                std::stringstream s;
                s << "Input ";
                s << (i + 1);

                SS__TEXT(ss, ' ', space, std::left, '|', s.str());
            }
            for(size_t i = 0; i < (MixerOutput::kFXCount - 1); ++i)
            {
                std::string s = toString(static_cast<MixerOutput>(i));
                s.resize(space);
                SS__TEXT(ss, ' ', space, std::left, '|', s.c_str());
            }
            out << "\t" << ss.str() << std::endl;

            SS_RESET(ss, precision, std::left);
            SS_SPACE(ss, '-', space, std::left, '+');
            for(size_t i = 0; i < nb_inputs; ++i)
            {
                SS_SPACE(ss, '-', space, std::left, '+');
            }
            for(size_t i = 0; i < (MixerOutput::kFXCount - 1); ++i)
            {
                SS_SPACE(ss, '-', space, std::left, '+');
            }
            out << "\t" << ss.str() << std::endl;

            const char* LR = "LR";
            for(size_t c = 0; c < StereoChannels::kNumChannels; ++c)
            {
                std::stringstream s;
                s << "* Input ";
                s << LR[c];

                SS_RESET(ss, precision, std::left);
                SS__TEXT(ss, ' ', space, std::left, '|', s.str());
                for(size_t i = 0; i < (nb_inputs + MixerOutput::kFXCount - 1); ++i)
                {
                    SS__TEXT(ss, ' ', space - 1, std::right, " |", this->m_inputSamples[c][i]);
                }
                out << "\t" << ss.str() << std::endl;
            }
        }
        out << std::endl;

        out << "\t" << "Mixing Console levels:" << std::endl;
        {
            SS_RESET(ss, precision, std::left);
            SS_SPACE(ss, ' ', space, std::left, '|');
            for(size_t i = 0; i < nb_inputs; ++i)
            {
                std::stringstream s;
                s << "Input ";
                s << (i + 1);

                SS__TEXT(ss, ' ', space, std::left, '|', s.str());
            }
            for(size_t i = 0; i < (MixerOutput::kFXCount - 1); ++i)
            {
                std::string s = toString(static_cast<MixerOutput>(i));
                s.resize(space);
                SS__TEXT(ss, ' ', space, std::left, '|', s.c_str());
            }
            out << "\t" << ss.str() << std::endl;

            SS_RESET(ss, precision, std::left);
            SS_SPACE(ss, '-', space, std::left, '+');
            for(size_t i = 0; i < nb_inputs; ++i)
            {
                SS_SPACE(ss, '-', space, std::left, '+');
            }
            for(size_t i = 0; i < (MixerOutput::kFXCount - 1); ++i)
            {
                SS_SPACE(ss, '-', space, std::left, '+');
            }
            out << "\t" << ss.str() << std::endl;

            for(size_t c = 0; c < MixerOutput::kFXCount; ++c)
            {
                SS_RESET(ss, precision, std::left);
                std::string s = toString(static_cast<MixerOutput>(c));
                s.resize(space);
                SS__TEXT(ss, ' ', space, std::left, '|', s.c_str());
                for(size_t i = 0; i < (nb_inputs + MixerOutput::kFXCount - 1); ++i)
                {
                    SS__TEXT(ss, ' ', space - 1, std::right, " |", this->m_levels[c][i]);
                }
                out << "\t" << ss.str() << std::endl;
            }
        }
        out << std::endl;

        if(deepInspection)
        {
            this->m_tube->dump(out, deepInspection, tag + ".m_tube");
            this->m_chorus->dump(out, deepInspection, tag + ".m_chorus");
            this->m_flanger->dump(out, deepInspection, tag + ".m_flanger");
            this->m_orbitone->dump(out, deepInspection, tag + ".m_orbitone");
            this->m_phaser->dump(out, deepInspection, tag + ".m_phaser");
            this->m_delay->dump(out, deepInspection, tag + ".m_delay");
            this->m_plate_reverb->dump(out, deepInspection, tag + ".m_plate_reverb");
            this->m_reverberator->dump(out, deepInspection, tag + ".m_reverberator");
            this->m_dry->dump(out, deepInspection, tag + ".m_dry");
        }

        out << "END " << tag << "(" << typeid(*this).name() << ") dump" << std::endl << std::endl;
    )

    IMPLEMENT_INSPECT(
        size_t nb_errors = 0;

        for(size_t i = 0; i < nb_inputs; ++i)
        {
            nb_errors += inspector(tag + ".level[ input #" + std::to_string(i) + " ]" , this->m_channelLevel[i], -1.0f, 1.0f, deepInspection);
            nb_errors += inspector(tag + ".pan[ L ][ input #" + std::to_string(i) + " ]", this->m_pan[StereoChannels::Left][i], -1.0f, 1.0f, deepInspection);
            nb_errors += inspector(tag + ".pan[ R ][ input #" + std::to_string(i) + " ]", this->m_pan[StereoChannels::Right][i], -1.0f, 1.0f, deepInspection);
            nb_errors += inspector(tag + ".pan[ input #" + std::to_string(i) + " ]", this->m_pan[StereoChannels::kNumChannels][i], -1.0f, 1.0f, deepInspection);
        }

        for(size_t i = 0; i < nb_inputs; ++i)
        {
            nb_errors += inspector(tag + ".input[ L ][ input #" + std::to_string(i) + " ]", this->m_inputSamples[StereoChannels::Left ][i], -1.0f, 1.0f, deepInspection);
            nb_errors += inspector(tag + ".input[ R ][ input #" + std::to_string(i) + " ]", this->m_inputSamples[StereoChannels::Right][i], -1.0f, 1.0f, deepInspection);
        }

        for(size_t i = nb_inputs; i < (nb_inputs + MixerOutput::kFXCount - 1); ++i)
        {
            nb_errors += inspector(tag + ".input[ L ][ input " + toString(static_cast<MixerOutput>(i - nb_inputs)) + " ]", this->m_inputSamples[StereoChannels::Left ][i], -1.0f, 1.0f, deepInspection);
            nb_errors += inspector(tag + ".input[ R ][ input " + toString(static_cast<MixerOutput>(i - nb_inputs)) + " ]", this->m_inputSamples[StereoChannels::Right][i], -1.0f, 1.0f, deepInspection);
        }

        for(size_t c = 0; c < MixerOutput::kFXCount; ++c)
        {
            for(size_t i = 0; i < (nb_inputs + MixerOutput::kFXCount - 1); ++i)
            {
                nb_errors += inspector(tag + ".levels[ " + std::to_string(c) + " ][ " + std::to_string(i) + " ]", this->m_levels[c][i], -1.0f, 1.0f, deepInspection);
            }
        }

        if(deepInspection)
        {
            for(size_t i = 0; i < nb_inputs; ++i)
            {
                for(size_t k = 0; k < this->m_nSamples; ++k)
                {
                    nb_errors += inspector(tag + ".m_inputSampleBuffer[ L ][ " + std::to_string(i) + " ][ " + std::to_string(k) +" ] ", this->m_inputSampleBuffer[StereoChannels::Left ][i][k], -1.0f, 1.0f, deepInspection);
                    nb_errors += inspector(tag + ".m_inputSampleBuffer[ R ][ " + std::to_string(i) + " ][ " + std::to_string(k) +" ] ", this->m_inputSampleBuffer[StereoChannels::Right][i][k], -1.0f, 1.0f, deepInspection);
                }
            }

            nb_errors += this->m_tube->inspect(inspector, deepInspection, tag + ".m_tube");
            nb_errors += this->m_chorus->inspect(inspector, deepInspection, tag + ".m_chorus");
            nb_errors += this->m_flanger->inspect(inspector, deepInspection, tag + ".m_flanger");
            nb_errors += this->m_orbitone->inspect(inspector, deepInspection, tag + ".m_orbitone");
            nb_errors += this->m_phaser->inspect(inspector, deepInspection, tag + ".m_phaser");
            nb_errors += this->m_delay->inspect(inspector, deepInspection, tag + ".m_delay");
            nb_errors += this->m_plate_reverb->inspect(inspector, deepInspection, tag + ".m_plate_reverb");
            nb_errors += this->m_reverberator->inspect(inspector, deepInspection, tag + ".m_reverberator");
            nb_errors += this->m_dry->inspect(inspector, deepInspection, tag + ".m_dry");
        }

        return nb_errors;
    )
};

template<size_t nb_inputs>
float32_t MixingConsole<nb_inputs>::weighted_sum(const float32_t* data, const float32_t* weights, size_t size)
{
    float32_t res = arm_weighted_sum_f32(data, weights, size);

    return std::isnan(res) ? 0.0f : res;
}

template<size_t nb_inputs>
MixingConsole<nb_inputs>::MixingConsole(float32_t sampling_rate, size_t buffer_size, bool swapStereoImage) :
    FXBase(sampling_rate),
    BufferSize(buffer_size),
    m_bypass(true),
    m_swapStereoImage(swapStereoImage),
    m_nSamples(0)
{
    for(size_t i = 0; i < nb_inputs; ++i)
    {
        this->m_tgInputSampleBuffer[i] = nullptr;
        this->m_inputSampleBuffer[StereoChannels::Left ][i] = new float32_t[this->BufferSize];
        this->m_inputSampleBuffer[StereoChannels::Right][i] = new float32_t[this->BufferSize];
        memset(this->m_inputSampleBuffer[StereoChannels::Left ][i], 0, sizeof(float32_t) * this->BufferSize);
        memset(this->m_inputSampleBuffer[StereoChannels::Right][i], 0, sizeof(float32_t) * this->BufferSize);
    }

    this->m_fx[MixerOutput::FX_Tube] = this->m_tube = new FXUnit2<Tube>(sampling_rate);
    this->m_fx[MixerOutput::FX_Chorus] = this->m_chorus = new FXUnit2<Chorus>(sampling_rate);
    this->m_fx[MixerOutput::FX_Flanger] = this->m_flanger = new FXUnit2<Flanger>(sampling_rate);
    this->m_fx[MixerOutput::FX_Orbitone] = this->m_orbitone = new FXUnit2<Orbitone>(sampling_rate);
    this->m_fx[MixerOutput::FX_Phaser] = this->m_phaser = new FXUnit2<Phaser>(sampling_rate);
    this->m_fx[MixerOutput::FX_Delay] = this->m_delay  = new FXUnit2<Delay>(sampling_rate);
    this->m_fx[MixerOutput::FX_PlateReverb] = this->m_plate_reverb = new FXUnit2<AudioEffectPlateReverb>(sampling_rate);
    this->m_fx[MixerOutput::FX_Reverberator] = this->m_reverberator = new FXUnit2<Reverberator>(sampling_rate);
    this->m_fx[MixerOutput::MainOutput] = this->m_dry = new FXUnit2<Dry>(sampling_rate);

    this->bypass(false);

    this->init();
}

template<size_t nb_inputs>
MixingConsole<nb_inputs>::~MixingConsole()
{
    for(size_t i = 0; i < MixerOutput::kFXCount; ++i)
    {
        delete this->m_fx[i];
    }

    for(size_t i = 0; i < nb_inputs; ++i)
    {
        delete[] this->m_inputSampleBuffer[StereoChannels::Left ][i];
        delete[] this->m_inputSampleBuffer[StereoChannels::Right][i];

        // The m_tgInputSampleBuffer buffers are not freed as MixingConsole is not the creator
        // They must be freed by the creator of the buffers
        this->m_tgInputSampleBuffer[i] = nullptr;
    }
}

template<size_t nb_inputs>
void MixingConsole<nb_inputs>::bypass(bool bypass)
{
    if(this->m_bypass != bypass)
    {
        this->m_bypass = bypass;

        for(size_t fx = MixerOutput::FX_Tube; fx < MixerOutput::kFXCount; ++fx)
        {
            this->getFX(fx)->bypassFXProcess(bypass);
        }

        if(!bypass)
        {
            this->reset();
        }
    }
}

template<size_t nb_inputs>
bool MixingConsole<nb_inputs>::bypass() const
{
    return this->m_bypass;
}

template<size_t nb_inputs>
size_t MixingConsole<nb_inputs>::getChannelNumber() const
{
    return nb_inputs;
}

// Send section
template<size_t nb_inputs>
void MixingConsole<nb_inputs>::setChannelLevel(size_t in, float32_t lvl)
{
    assert(in < nb_inputs);

    lvl = constrain(lvl, 0.0f, 1.0f);
    if(lvl == this->m_channelLevel[in]) return;

    this->m_channelLevel[in] = lvl;
    this->updatePan(in);
}

template<size_t nb_inputs>
void MixingConsole<nb_inputs>::setPan(size_t in, float32_t pan)
{
    assert(in < nb_inputs);

    pan = constrain(pan, 0.0f, 1.0f);
    
    if(pan == this->m_pan[StereoChannels::kNumChannels][in]) return;

    this->m_pan[StereoChannels::kNumChannels][in] = pan;
    this->updatePan(in);
}

template<size_t nb_inputs>
void MixingConsole<nb_inputs>::swapStereoImage(bool swap)
{
    this->m_swapStereoImage = swap;
}

template<size_t nb_inputs>
void MixingConsole<nb_inputs>::setSendLevel(size_t in, MixerOutput fx, float32_t lvl)
{
    assert(in < nb_inputs);
    assert(fx < MixerOutput::kFXCount);

    this->setLevel(in, fx, lvl);
}

template<size_t nb_inputs>
void MixingConsole<nb_inputs>::setInputSample(size_t in, float32_t sampleL, float32_t sampleR)
{
    assert(in < nb_inputs);

    this->setSample(in, sampleL, sampleR);
}

template<size_t nb_inputs>
void MixingConsole<nb_inputs>::setInputSampleBuffer(size_t in, float32_t* samples)
{
    assert(in < nb_inputs);

    this->m_tgInputSampleBuffer[in] = samples;
}

// Return section
template<size_t nb_inputs>
void MixingConsole<nb_inputs>::setFXSendLevel(MixerOutput fromFX, MixerOutput toFX, float32_t lvl)
{
    assert(fromFX < (MixerOutput::kFXCount - 1));
    assert(toFX < MixerOutput::kFXCount);

    if(fromFX == toFX)
    {
        // An FX cannot feedback on itself
        return;
    }

    this->setLevel(nb_inputs + fromFX, toFX, lvl);
}

template<size_t nb_inputs>
void MixingConsole<nb_inputs>::setReturnSample(MixerOutput ret, float32_t sampleL, float32_t sampleR)
{
    assert(ret < (MixerOutput::kFXCount - 1));

    this->setSample(nb_inputs + ret, sampleL, sampleR);
}

// Get FX
template<size_t nb_inputs>
FXElement* MixingConsole<nb_inputs>::getFX(size_t fx)
{
    assert(fx < MixerOutput::kFXCount);
    return this->m_fx[fx];
}

template<size_t nb_inputs>
FXUnit2<Tube>* MixingConsole<nb_inputs>::getTube()
{
    return this->m_tube;
}

template<size_t nb_inputs>
FXUnit2<Chorus>* MixingConsole<nb_inputs>::getChorus()
{
    return this->m_chorus;
}

template<size_t nb_inputs>
FXUnit2<Flanger>* MixingConsole<nb_inputs>::getFlanger()
{
    return this->m_flanger;
}

template<size_t nb_inputs>
FXUnit2<Orbitone>* MixingConsole<nb_inputs>::getOrbitone()
{
    return this->m_orbitone;
}

template<size_t nb_inputs>
FXUnit2<Phaser>* MixingConsole<nb_inputs>::getPhaser()
{
    return this->m_phaser;
}

template<size_t nb_inputs>
FXUnit2<Delay>* MixingConsole<nb_inputs>::getDelay()
{
    return this->m_delay;
}

template<size_t nb_inputs>
FXUnit2<AudioEffectPlateReverb>* MixingConsole<nb_inputs>::getPlateReverb()
{
    return this->m_plate_reverb;
}

template<size_t nb_inputs>
FXUnit2<Reverberator>* MixingConsole<nb_inputs>::getReverberator()
{
    return this->m_reverberator;
}

template<size_t nb_inputs>
FXUnit2<Dry>* MixingConsole<nb_inputs>::getDry()
{
    return this->m_dry;
}

// Processing
template<size_t nb_inputs>
void MixingConsole<nb_inputs>::init()
{
    memset(this->m_channelLevel, 0, nb_inputs * sizeof(float32_t));
    for(size_t i = 0; i <= StereoChannels::kNumChannels; ++i) memset(this->m_pan[i], 0, nb_inputs * sizeof(float32_t));

    for(size_t i = 0; i < MixerOutput::kFXCount; ++i)
        memset(this->m_levels[i], 0, (nb_inputs + MixerOutput::kFXCount - 1) * sizeof(float32_t));
    
    for(size_t i = 0; i < StereoChannels::kNumChannels; ++i) 
        memset(this->m_inputSamples[i], 0, (nb_inputs + MixerOutput::kFXCount - 1) * sizeof(float32_t));

    this->reset();
}

template<size_t nb_inputs>
void MixingConsole<nb_inputs>::reset()
{
    for(size_t i = 0; i < nb_inputs; ++i)
    {
        memset(this->m_inputSampleBuffer[StereoChannels::Left ][i], 0, this->BufferSize * sizeof(float32_t));
        memset(this->m_inputSampleBuffer[StereoChannels::Right][i], 0, this->BufferSize * sizeof(float32_t));
    }

    for(size_t i = 0; i < MixerOutput::kFXCount; ++i)
    {
        this->m_fx[i]->reset();

        if(i != MixerOutput::MainOutput)
        {
            this->setReturnSample(static_cast<MixerOutput>(i), 0.0f, 0.0f);
        }
    }
}

template<size_t nb_inputs>
void MixingConsole<nb_inputs>::injectInputSamples(size_t in, float32_t* samplesL, float32_t* samplesR, size_t nSamples)
{
    // Only used to input stereo samples
    assert(in < nb_inputs);
    this->m_nSamples = std::min(nSamples, this->BufferSize);
    if(samplesL != nullptr)
    {
        memcpy(this->m_inputSampleBuffer[StereoChannels::Left ][in], samplesL, this->m_nSamples * sizeof(float32_t));
    }
    else
    {
        memset(this->m_inputSampleBuffer[StereoChannels::Left ][in], 0, this->m_nSamples * sizeof(float32_t));
    }

    if(samplesR != nullptr)
    {
        memcpy(this->m_inputSampleBuffer[StereoChannels::Right][in], samplesR, this->m_nSamples * sizeof(float32_t));
    }
    else
    {
        memset(this->m_inputSampleBuffer[StereoChannels::Right][in], 0, this->m_nSamples * sizeof(float32_t));
    }
}

template<size_t nb_inputs>
void MixingConsole<nb_inputs>::preProcessInputSampleBuffer(size_t in, size_t nSamples)
{
    assert(in < nb_inputs);
    assert(nSamples <= this->BufferSize);

    float32_t* samples = this->m_tgInputSampleBuffer[in];
    if(samples == nullptr) return;

    this->m_nSamples = nSamples;
    if(nSamples > 0)
    {
        if(this->m_pan[StereoChannels::Left ][in] != 0.0f)
        {
            arm_scale_f32(samples, this->m_pan[StereoChannels::Left ][in], this->m_inputSampleBuffer[StereoChannels::Left ][in], nSamples);
        }
        else
        {
            memset(this->m_inputSampleBuffer[StereoChannels::Left ][in], 0, nSamples * sizeof(float32_t));
        }
        
        if(this->m_pan[StereoChannels::Right][in] != 0.0f)
        {
            arm_scale_f32(samples, this->m_pan[StereoChannels::Right][in], this->m_inputSampleBuffer[StereoChannels::Right][in], nSamples);
        }
        else
        {
            memset(this->m_inputSampleBuffer[StereoChannels::Right][in], 0, nSamples * sizeof(float32_t));
        }
    }
    else
    {
        memset(this->m_inputSampleBuffer[StereoChannels::Left ][in], 0, this->BufferSize * sizeof(float32_t));
        memset(this->m_inputSampleBuffer[StereoChannels::Right][in], 0, this->BufferSize * sizeof(float32_t));
    }
}

template<size_t nb_inputs>
void MixingConsole<nb_inputs>::processSample(float32_t& outL, float32_t& outR)
{
    const size_t nBuffers = nb_inputs + MixerOutput::kFXCount - 1;

    float32_t fx_input_[StereoChannels::kNumChannels];
    float32_t fx_output_[StereoChannels::kNumChannels];
    for(size_t fxId = 0; fxId < MixerOutput::kFXCount; ++fxId)
    {
        // Compute the samples that will feed the FX
        fx_input_[StereoChannels::Left ] = MixingConsole<nb_inputs>::weighted_sum(this->m_inputSamples[StereoChannels::Left ], this->m_levels[fxId], nBuffers);
        fx_input_[StereoChannels::Right] = MixingConsole<nb_inputs>::weighted_sum(this->m_inputSamples[StereoChannels::Right], this->m_levels[fxId], nBuffers);

        // Process the FX
        this->m_fx[fxId]->processSample(
            fx_input_[StereoChannels::Left ],
            fx_input_[StereoChannels::Right],
            fx_output_[StereoChannels::Left ],
            fx_output_[StereoChannels::Right]
        );

        if(fxId != MixerOutput::MainOutput)
        {
            // Feedback the processed samples except for the main output
            this->setReturnSample(
                static_cast<MixerOutput>(fxId), 
                fx_output_[StereoChannels::Left],
                fx_output_[StereoChannels::Right]
            );
        }
        else
        {
            // Returns the main output sample
            outL = fx_output_[StereoChannels::Left];
            outR = fx_output_[StereoChannels::Right];
        }
    }
}

template<size_t nb_inputs>
void MixingConsole<nb_inputs>::process(float32_t* outL, float32_t* outR)
{
    size_t nSamples = this->m_nSamples;
    for(size_t s = 0; s < nSamples; ++s)
    {
        for(size_t in = 0; in < nb_inputs; ++in)
        {
            this->setSample(
                in, 
                this->m_inputSampleBuffer[StereoChannels::Left ][in][s], 
                this->m_inputSampleBuffer[StereoChannels::Right][in][s]
            );
        }

        if(this->m_swapStereoImage)
        {
            this->processSample(*outR, *outL);
        }
        else
        {
            this->processSample(*outL, *outR);
        }
        
        ++outL;
        ++outR;
    }

    this->m_nSamples = 0;
}

template<size_t nb_inputs>
void MixingConsole<nb_inputs>::process(float32_t* outLR)
{
    size_t nSamples = this->m_nSamples;
    for(size_t s = 0; s < nSamples; ++s)
    {
        for(size_t in = 0; in < nb_inputs; ++in)
        {
            this->setSample(
                in, 
                this->m_inputSampleBuffer[StereoChannels::Left ][in][s], 
                this->m_inputSampleBuffer[StereoChannels::Right][in][s]
            );
        }

        if(this->m_swapStereoImage)
        {
            this->processSample(*(outLR + 1), *outLR);
        }
        else
        {
            this->processSample(*outLR, *(outLR + 1));

        }
        
        outLR += 2;
    }

    this->m_nSamples = 0;
}

template<size_t nb_inputs>
void MixingConsole<nb_inputs>::updatePan(size_t in)
{
    float32_t pan = this->m_pan[StereoChannels::kNumChannels][in] * Constants::MPI_2;
    if(this->m_channelLevel[in] != 0.0f)
    {
        this->m_pan[StereoChannels::Left ][in] = InterpolatedSineOscillator::Cos(pan) * this->m_channelLevel[in];
        this->m_pan[StereoChannels::Right][in] = InterpolatedSineOscillator::Sin(pan) * this->m_channelLevel[in];
    }
    else
    {
        this->m_pan[StereoChannels::Left ][in] = 
        this->m_pan[StereoChannels::Right][in] = 0.0f;
    }
}

template<size_t nb_inputs>
void MixingConsole<nb_inputs>::setLevel(size_t in, MixerOutput fx, float32_t lvl)
{
    assert(in < (nb_inputs + MixerOutput::kFXCount - 1));
    assert(fx < MixerOutput::kFXCount);

    this->m_levels[fx][in] = constrain(lvl, 0.0f, 1.0f);
}

template<size_t nb_inputs>
void MixingConsole<nb_inputs>::setSample(size_t in, float32_t sampleL, float32_t sampleR)
{
    assert(in < (nb_inputs + MixerOutput::kFXCount - 1));
    this->m_inputSamples[StereoChannels::Left ][in] = sampleL;
    this->m_inputSamples[StereoChannels::Right][in] = sampleR;
}
