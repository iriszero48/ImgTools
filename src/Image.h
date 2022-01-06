#pragma once

#include <vector>

namespace Image
{
    template <typename T>
    struct PixivRGB
    {
        using Type = T;

        T R;
        T G;
        T B;

        PixivRGB() : R(0), G(0), B(0) {}

		explicit PixivRGB(const T v) : R(v), G(v), B(v) {}

		PixivRGB(const T& r, const T& g, const T& b) : R(r), G(g), B(b) {}

		explicit PixivRGB(const T (&arr) [3]) : R(arr[0]), G(arr[1]), B(arr[2]) {}

		static const PixivRGB<T>& Zero()
		{
			static PixivRGB<T> zero(0);
			return zero;
		}
    };

    template <typename T, typename TA = T, TA AValue = static_cast<TA>(-1)>
    struct PixivRGBA : PixivRGB<T>
    {
        using Base = PixivRGB<T>;
        using Type = T;

        TA A = AValue;

        using Base::PixivRGB;

        explicit PixivRGBA(const T v, const TA a) : Base(v), A(a) {}

		PixivRGBA(const T& r, const T& g, const T& b, const TA& a) : Base(r, g, b), A(a) {}

		explicit PixivRGBA(const T (&arr) [3], const TA& a) : Base(arr), A(a) {}

		static const PixivRGBA<T>& Zero()
		{
			static PixivRGBA<T> zero(0);
			return zero;
		}
    };
    
    template <typename T>
    struct Frame
    {
        using SizeType = std::vector<T>::size_type;

        std::vector<T> data{};
        SizeType rows;
        SizeType cols;

        Frame() : rows(0), cols(0) {}

        T& At(const SizeType row, const SizeType col)
        {
            return data[row * cols + col];
        }

        const T& At(const SizeType row, const SizeType col) const
        {
            return data[row * cols + col];
        }
    };

    class Processor
    {
    private:
        /* data */
    public:
        Processor(/* args */);
        ~Processor();
    };

    class Reader
    {

    };
}