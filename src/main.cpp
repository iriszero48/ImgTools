#include <filesystem>
#include <iostream>
#include <ranges>
#include <execution>

#include <eigen3/Eigen/Eigen>
#include <opencv2/opencv.hpp>

#include "CubeLUT.h"

template <typename T>
T blerp(T c00, T c10, T c01, T c11, T tx, T ty)
{
    return std::lerp(std::lerp(c00, c10, tx), std::lerp(c01, c11, tx), ty);
}

template <typename T>
T clerp(T c000, T c010, T c100, T c110, T c001, T c011, T c101, T c111, T tx, T ty, T tz)
{
    return std::lerp(blerp(c000, c100, c010, c110, tx, ty), blerp(c001, c101, c011, c111, tx, ty), tz);
}

const Lut::CubeLut::Row& GetPix(const Lut::Table3D& tab, const int64_t b, const int64_t g, const int64_t r)
{
    if (std::max(b, std::max(g, r)) >= tab.Length() ||
        std::min(b, std::min(g, r)) < 0.)
    {
        return { 0, 0, 0 };
    }
    return tab.At(r, g, b);
}

template <typename T>
const Lut::ColorRgb<T>& Lookup1D(const Lut::Table1D& tab, const T b, const T g, const T r)
{
    int64_t bi = static_cast<int64_t>(b), gi = static_cast<int64_t>(g), ri = static_cast<int64_t>(r);
    //const Lut::CubeLut::Row& c0 = tab.At()
    return Lut::ColorRgb<T>(0, 0, 0);
}

template <typename T>
const Lut::ColorRgb<T>& Lookup3D(const Lut::Table3D& tab, const T b, const T g, const T r)
{
    int64_t bi = static_cast<int64_t>(b), gi = static_cast<int64_t>(g), ri = static_cast<int64_t>(r);
    const Lut::CubeLut::Row&
        c000 = GetPix(tab, bi,     gi,     ri),
        c010 = GetPix(tab, bi,     gi + 1, ri),
        c100 = GetPix(tab, bi + 1, gi,     ri),
        c110 = GetPix(tab, bi + 1, gi + 1, ri),
        c001 = GetPix(tab, bi,     gi,     ri + 1),
        c011 = GetPix(tab, bi,     gi + 1, ri + 1),
        c101 = GetPix(tab, bi + 1, gi,     ri + 1),
        c111 = GetPix(tab, bi + 1, gi + 1, ri + 1);
    T tx = b - bi, ty = g - gi, tz = r - ri;
    const auto nr = clerp<T>(c000.R, c010.R, c100.R, c110.R, c001.R, c011.R, c101.R, c111.R, tx, ty, tz);
    const auto ng = clerp<T>(c000.G, c010.G, c100.G, c110.G, c001.G, c011.G, c101.G, c111.G, tx, ty, tz);
    const auto nb = clerp<T>(c000.B, c010.B, c100.B, c110.B, c001.B, c011.B, c101.B, c111.B, tx, ty, tz);
    return Lut::ColorRgb<T>(nr, ng, nb);
}

void ApplyLut(const std::filesystem::path& in, const std::filesystem::path& out, const Lut::CubeLut& lut)
{
    //using FloatType = double;
    //using VecType = cv::Vec3d;
    //constexpr auto RType = CV_64FC4;
    using FloatType = float;
    using VecType = cv::Vec3b;
    constexpr auto RType = CV_32FC4;

	auto img = cv::imread(in.string(), cv::IMREAD_COLOR);

    const int64_t rngSize = img.rows * img.cols;
    std::vector<uint64_t> rng(rngSize);
    std::iota(rng.begin(), rng.end(), 0);

    const auto dmin = lut.DomainMin;
    const auto dmax = lut.DomainMax;

    const auto dim = lut.GetDim();
    const auto& tab = lut.GetTable();
    const auto size = static_cast<FloatType>(dim == Lut::CubeLut::Dim::_1D
		? std::get<Lut::Table1D>(tab).Length()
		: std::get<Lut::Table3D>(tab).Length());

#define UseExecution

    std::for_each(
#ifdef UseExecution
        std::execution::par_unseq,
#endif
        rng.begin(), rng.end(), [&](const auto i)
        {
            const auto x = i / img.rows;
            const auto y = i % img.cols;

            auto [biR, giR, riR] = img.at<VecType>(x, y).val;
            FloatType bi = biR / 255., gi = giR / 255., ri = riR / 255.;

            ri = (ri - dmin.R) / (dmax.R - dmin.R);
            gi = (gi - dmin.G) / (dmax.G - dmin.G);
            bi = (bi - dmin.B) / (dmax.B - dmin.B);

            ri = ri * (size - 1.);
            gi = gi * (size - 1.);
            bi = bi * (size - 1.);

            ri = std::clamp<FloatType>(ri, 0., size - 1.);
            gi = std::clamp<FloatType>(gi, 0., size - 1.);
            bi = std::clamp<FloatType>(bi, 0., size - 1.);

            const auto nBgr = [](const auto& tab, const auto dim, const auto bi, const auto gi, const auto ri)
                {
                    if (dim == Lut::CubeLut::Dim::_3D) return Lookup3D<FloatType>(std::get<Lut::Table3D>(tab), bi, gi, ri);
                    if (dim == Lut::CubeLut::Dim::_1D) return Lookup1D<FloatType>(std::get<Lut::Table1D>(tab), bi, gi, ri);
            		throw std::runtime_error("...");
                }(tab, dim, bi, gi, ri);
            img.at<VecType>(x, y)[0] = nBgr.B * 255.;
            img.at<VecType>(x, y)[1] = nBgr.G * 255.;
            img.at<VecType>(x, y)[2] = nBgr.R * 255.;
        });

    imwrite(out.string(), img);
}

void Test()
{
    const std::filesystem::path& f1 = R"(F:\Cloth.std.png)";
	//const std::filesystem::path& f2 = R"(F:\Cloth.ff.png)";
    const std::filesystem::path& f2 = R"(F:\test.out.png)";

    const auto img1 = cv::imread(f1.string());
    const auto img2 = cv::imread(f2.string());

    std::cout << (img1.cols == img2.cols && img1.rows == img2.rows) << std::endl;

	std::vector<int> devs{};
	for (const auto i: std::views::iota(0, img1.cols * img2.rows))
	{
		for (int64_t x = i / img1.rows, y = i % img1.cols; const auto ii : std::views::iota(0, 3))
		{
            devs.push_back(std::abs(img1.at<cv::Vec3b>(x, y)[ii] - img2.at<cv::Vec3b>(x, y)[ii]));
		}
	}

    const auto mean = std::accumulate(devs.begin(), devs.end(), 0.) / devs.size();
    const auto stdDev = std::pow(std::accumulate(devs.begin(), devs.end(), 0., [&](const double s, const auto x)
        {
            return s + std::pow(x - mean, 2);
        }) / devs.size(), 0.5);

    std::cout << std::format("mean: {}\nstd dev: {}", mean, stdDev) << std::endl;

    for (int i = 0; i < 10; ++i)
    {
	    for (int j = 0; j < 10; ++j)
	    {
            std::cout << img1.at<cv::Vec3b>(i, j) << std::endl;
            std::cout << img2.at<cv::Vec3b>(i, j) << std::endl;
	    }
    }
}

int main(int argc, char* argv[])
{
    const auto in = R"(F:\Cloth.png)";
    const auto out = R"(F:\test.out.png)";
    const auto lut = Lut::CubeLut::FromCubeFile(R"(F:\test.cube)");

    ApplyLut(in, out, lut);

    Test();
}

