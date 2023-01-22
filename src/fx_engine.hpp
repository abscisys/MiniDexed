#pragma once

#include "fx_components.h"

#include <algorithm>
#include <climits>
#include <cassert>

#define MAKE_INTEGRAL_FRACTIONAL(x) \
    int32_t x ## _integral = static_cast<int32_t>(x); \
    float32_t x ## _fractional = x - static_cast<float32_t>(x ## _integral);

enum Format
{
    FORMAT_FLOAT32,
    FORMAT_12_BIT,
    FORMAT_16_BIT,
    FORMAT_32_BIT
};

template <Format format>
struct DataType
{
};

inline int16_t clip16(int32_t x)
{
    if(x > INT16_MAX)
    {
        return INT16_MAX;
    }
    
    if(x < INT16_MIN)
    {
        return INT16_MIN;
    }

    return static_cast<int16_t>(x);
}

template <>
struct DataType<Format::FORMAT_FLOAT32>
{
    typedef float32_t T;

    static inline float32_t decompress(T value)
    {
        return value;
    }

    static inline T compress(float32_t value)
    {
        return constrain(value, -1.0f, 1.0f);
    }
};

template <>
struct DataType<Format::FORMAT_12_BIT>
{
    typedef uint16_t T;

    static inline float32_t decompress(T value)
    {
        return static_cast<float>(static_cast<int16_t>(value)) / 4096.0f;
    }

    static inline T compress(float32_t value)
    {
        return clip16(static_cast<int32_t>(value * 4096.0f));
    }
};

template <>
struct DataType<Format::FORMAT_16_BIT>
{
    typedef uint32_t T;

    static inline float32_t decompress(T value)
    {
        return static_cast<float32_t>(static_cast<int16_t>(value)) / 65536.0f;
    }

    static inline T compress(float32_t value)
    {
        return clip16(static_cast<int32_t>(value * 65536.0f));
    }
};

template <>
struct DataType<Format::FORMAT_32_BIT>
{
    typedef uint32_t T;

    static inline float32_t decompress(T value)
    {
        return static_cast<float32_t>(static_cast<int64_t>(value)) / static_cast<float32_t>(UINT32_MAX);
    }

    static inline T compress(float32_t value)
    {
        return value * static_cast<float32_t>(INT32_MAX);
    }
};

template <
    size_t size,
    Format format,
    bool enable_lfo = true>
class FxEngine : public FXBase
{
    DISALLOW_COPY_AND_ASSIGN(FxEngine);

public:
    typedef typename DataType<format>::T T;

    enum LFOIndex
    {
        LFO_1 = 0,
        LFO_2,
        kLFOCount
    };

    FxEngine(float32_t sampling_rate, float32_t max_lfo1_frequency = 1.0f, float32_t max_lfo2_frequency = 1.0f) :
        FXBase(sampling_rate)
    {
        this->buffer_ = new T[size];
        for(unsigned i = 0; i < LFOIndex::kLFOCount; ++i) this->lfo_[i] = enable_lfo ? new LFO(sampling_rate, 0.0f, max_lfo1_frequency) : nullptr;
        this->clear();
    }

    ~FxEngine()
    {
        delete[] this->buffer_;
        if(enable_lfo)
        {
            for(unsigned i = 0; i < LFOIndex::kLFOCount; ++i) delete this->lfo_[i];
        }
    }

    void clear()
    {
        memset(this->buffer_, 0, size * sizeof(T));
        this->write_ptr_ = 0;
    }

    virtual void reset() override
    {
        this->clear();
        if(enable_lfo)
        {
            for(unsigned i = 0; i < LFOIndex::kLFOCount; ++i) this->lfo_[i]->reset();
        }
    }

    struct Empty
    {
    };

    template <int32_t _length, typename T = Empty>
    struct Reserve
    {
        typedef T Tail;
        enum
        {
            length = _length
        };
    };

    template <typename Memory, int32_t index>
    struct DelayLine
    {
        enum
        {
            length = DelayLine<typename Memory::Tail, index - 1>::length,
            base = DelayLine<Memory, index - 1>::base + DelayLine<Memory, index - 1>::length + 1
        };
    };

    template <typename Memory>
    struct DelayLine<Memory, 0>
    {
        enum
        {
            length = Memory::length,
            base = 0
        };
    };

    class Context
    {
        DISALLOW_COPY_AND_ASSIGN(Context);
        friend class FxEngine;

    public:
        Context() :
            accumulator_(0.0f),
            previous_read_(0.0f),
            buffer_(nullptr),
            write_ptr_(0)
        {
            memset(this->lfo_value_, 0, LFOIndex::kLFOCount * sizeof(T));
        }

        ~Context()
        {
        }

        inline void load(float32_t value)
        {
            this->accumulator_ = value;
        }

        inline void read(float32_t value, float32_t scale)
        {
            this->accumulator_ += value * scale;
        }

        inline void read(float32_t value)
        {
            this->accumulator_ += value;
        }

        inline void write(float32_t& value)
        {
            value = this->accumulator_;
        }

        inline void write(float32_t& value, float32_t scale)
        {
            value = this->accumulator_;
            this->accumulator_ *= scale;
        }

        template <typename D>
        inline void write(D& d, int32_t offset, float32_t scale)
        {
            assert(D::base + D::length <= size);
            T w = DataType<format>::compress(this->accumulator_);
            if(offset == -1)
            {
                this->buffer_[(this->write_ptr_ + D::base + D::length - 1) & MASK] = w;
            }
            else
            {
                this->buffer_[(this->write_ptr_ + D::base + offset) & MASK] = w;
            }
            this->accumulator_ *= scale;
        }

        template <typename D>
        inline void write(D& d, float32_t scale)
        {
            this->write(d, 0, scale);
        }

        template <typename D>
        inline void writeAllPass(D& d, int32_t offset, float32_t scale)
        {
            this->write(d, offset, scale);
            this->accumulator_ += this->previous_read_;
        }

        template <typename D>
        inline void writeAllPass(D& d, float32_t scale)
        {
            this->writeAllPass(d, 0, scale);
        }

        template <typename D>
        inline void read(D& d, int32_t offset, float32_t scale)
        {
            assert(D::base + D::length <= size);
            T r;
            if(offset == -1)
            {
                r = this->buffer_[(this->write_ptr_ + D::base + D::length - 1) & MASK];
            }
            else
            {
                r = this->buffer_[(this->write_ptr_ + D::base + offset) & MASK];
            }
            float32_t r_f = DataType<format>::decompress(r);
            this->previous_read_ = r_f;
            this->accumulator_ += r_f * scale;
        }

        template <typename D>
        inline void read(D& d, float32_t scale)
        {
            this->read(d, 0, scale);
        }

        inline void lp(float32_t& state, float32_t coefficient)
        {
            state += coefficient * (this->accumulator_ - state);
            this->accumulator_ = state;
        }

        inline void hp(float32_t& state, float32_t coefficient)
        {
            state += coefficient * (this->accumulator_ - state);
            this->accumulator_ -= state;
        }

        template <typename D>
        inline void interpolate(D& d, float32_t offset, float32_t scale)
        {
            assert(D::base + D::length <= size);
            MAKE_INTEGRAL_FRACTIONAL(offset);
            float32_t a = DataType<format>::decompress(this->buffer_[(this->write_ptr_ + offset_integral + D::base) & MASK]);
            float32_t b = DataType<format>::decompress(this->buffer_[(this->write_ptr_ + offset_integral + D::base + 1) & MASK]);
            float32_t x = a + (b - a) * offset_fractional;
            this->previous_read_ = x;
            this->accumulator_ += x * scale;
        }

        template <typename D>
        inline void interpolate(D& d, float32_t offset, LFOIndex index, float32_t amplitude, float32_t scale)
        {
            assert(D::base + D::length <= size);
            assert(index < LFOIndex::kLFOCount);
            offset += amplitude * this->lfo_value_[index];
            MAKE_INTEGRAL_FRACTIONAL(offset);
            float32_t a = DataType<format>::decompress(this->buffer_[(this->write_ptr_ + offset_integral + D::base) & MASK]);
            float32_t b = DataType<format>::decompress(this->buffer_[(this->write_ptr_ + offset_integral + D::base + 1) & MASK]);
            float32_t x = a + (b - a) * offset_fractional;
            this->previous_read_ = x;
            this->accumulator_ += x * scale;
        }

    private:
        float32_t accumulator_;
        float32_t previous_read_;
        float32_t lfo_value_[LFOIndex::kLFOCount];
        T* buffer_;
        int32_t write_ptr_;
    };

    inline void setLFOFrequency(LFOIndex index, float32_t frequency)
    {
        assert(index < LFOIndex::kLFOCount);
        if(enable_lfo)
        {
            this->lfo_[index]->setFrequency(frequency);
        }
    }

    inline void setLFONormalizedFrequency(LFOIndex index, float32_t normalized_frequency)
    {
        assert(index < LFOIndex::kLFOCount);
        if(enable_lfo)
        {
            this->lfo_[index]->setNormalizedFrequency(normalized_frequency);
        }
    }

    inline void start(Context *c)
    {
        --this->write_ptr_;
        if(this->write_ptr_ < 0)
        {
            this->write_ptr_ += size;
        }
        c->accumulator_ = 0.0f;
        c->previous_read_ = 0.0f;
        c->buffer_ = buffer_;
        c->write_ptr_ = write_ptr_;
        if(enable_lfo)
        {
            for(unsigned i = 0; i < LFOIndex::kLFOCount; ++i) 
            {
                c->lfo_value_[i] = this->lfo_[i]->process();
            }
        }
    }

private:
    enum
    {
        MASK = size - 1
    };

    T* buffer_;
    int32_t write_ptr_;

    LFO* lfo_[LFOIndex::kLFOCount];
};
