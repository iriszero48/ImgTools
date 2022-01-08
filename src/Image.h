#pragma once

#include <vector>
#include <format>

extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/hwcontext.h>
#include <libswscale/swscale.h>
}

namespace Media
{
	namespace __Detail
	{
#define MacroToStringImpl(x) #x
#define MacroToString(x) MacroToStringImpl(x)

		template<typename...Args>
		std::string Append(Args&&... args)
		{
			std::string buf;
			(buf.append(args), ...);
			return buf;
		}

		template <typename T, size_t S>
		constexpr const char* GetFilename(const T(&str)[S], size_t i = S - 1)
		{
			for (; i > 0; --i) if (str[i] == '/') return &str[i + 1];
			return str;
		}
	}

	class Exception : public std::runtime_error
	{
	public:
		template<typename...Args>
		Exception(Args&&... str) : std::runtime_error(__Detail::Append(std::forward<Args>(str)...)) {}
	};
#define ThrowEx(ex, ...) throw ex("[", MacroToString(ex), "] [", __MediaFuncName__, "] [", MacroToString(__LINE__), "] ", std::format(__VA_ARGS__))
#define ThrowBaseEx(...) ThrowEx(Media::Exception, __VA_ARGS__)

	namespace __Detail
	{
		class MemoryStream
		{
		public:
			MemoryStream() = default;

			MemoryStream(uint8_t* data, const uint64_t size, const bool keep = false) : size(size), keep(keep)
			{
				if (keep)
				{
					this->data = new uint8_t[size];
					std::copy_n(data, size, this->data);
				}
				else
				{
					this->data = data;
				}
			}

			MemoryStream(const MemoryStream& ms)
			{
				this->size = ms.size;
				if (ms.keep)
				{
					this->data = new uint8_t[this->size];
					std::copy_n(ms.data, ms.size, this->data);
				}
				else
				{
					this->data = ms.data;
				}
				this->keep = ms.keep;
				this->index = ms.index;
			}

			MemoryStream(MemoryStream&& ms) noexcept
			{
				this->size = ms.size;
				this->data = ms.data;
				this->keep = ms.keep;
				this->index = ms.index;
				ms.Reset();
			}

			MemoryStream& operator=(const MemoryStream& ms)
			{
				if (this == &ms) return *this;

				this->size = ms.size;
				if (ms.keep)
				{
					this->data = new uint8_t[this->size];
					std::copy_n(ms.data, ms.size, this->data);
				}
				else
				{
					this->data = ms.data;
				}
				this->keep = ms.keep;
				this->index = ms.index;
				return *this;
			}

			MemoryStream& operator=(MemoryStream&& ms) noexcept
			{
				if (this == &ms) return *this;

				this->size = ms.size;
				this->data = ms.data;
				this->keep = ms.keep;
				this->index = ms.index;
				ms.Reset();
				return *this;
			}

			~MemoryStream()
			{
				if (keep) delete[] data;
				Reset();
			}

			[[nodiscard]] uint64_t Size() const { return size; }

			int Read(unsigned char* buf, const int bufSize)
			{
				if (bufSize < 0) return bufSize;
				if (index >= size) return AVERROR_EOF;

				if (index + bufSize >= size)
				{
					const auto n = size - index;
					std::copy_n(data + index, n, buf);
					index += n;
					return n;
				}

				std::copy_n(data + index, bufSize, buf);
				index += bufSize;
				return bufSize;
			}

			int64_t Seek(const int64_t offset, const int whence)
			{
				if (whence == SEEK_SET)
				{
					if (offset < 0) return -1;
					index = offset;
				}
				else if (whence == SEEK_CUR)
				{
					index += offset;
				}
				else if (whence == SEEK_END)
				{
					if (offset > 0) return -1;
					index = size + offset;
				}
				else
				{
					constexpr auto __MediaFuncName__ = "Media::__Detail::MemoryStream::Seek";
					ThrowBaseEx("seek fail: unknow whence: {}", whence);
				}
				return 0;
			}

			void Reset()
			{
				data = nullptr;
				size = 0;
				keep = false;
				index = 0;
			}

		private:
			uint8_t* data = nullptr;
			uint64_t size = 0;
			bool keep = false;
			uint64_t index = 0;
		};

		class MemoryStreamIoContext
		{
			static const auto BufferSize = 4096;

		public:
			MemoryStreamIoContext() = default;
			MemoryStreamIoContext(MemoryStreamIoContext const&) = delete;
			MemoryStreamIoContext& operator=(MemoryStreamIoContext const&) = delete;
			MemoryStreamIoContext(MemoryStreamIoContext&&) = delete;
			MemoryStreamIoContext& operator=(MemoryStreamIoContext&&) = delete;
			~MemoryStreamIoContext() = default;

			MemoryStreamIoContext(MemoryStream inputStream) :
				inputStream(std::move(inputStream)),
				buffer(static_cast<unsigned char*>(av_malloc(BufferSize)))
			{
				constexpr auto __MediaFuncName__ = "ImageDatabase::__Detail::MemoryStreamIoContext::MemoryStreamIoContext";
				if (buffer == nullptr) ThrowBaseEx("[av_malloc] the buffer cannot be allocated");

				ctx = avio_alloc_context(buffer, BufferSize, 0, this,
					&MemoryStreamIoContext::Read, nullptr, &MemoryStreamIoContext::Seek);
				if (ctx == nullptr) ThrowBaseEx("[avio_alloc_context] the AVIO cannot be allocated");
			}

			void ResetInnerContext()
			{
				ctx = nullptr;
				buffer = nullptr;
			}

			static int Read(void* opaque, unsigned char* buf, const int bufSize)
			{
				auto h = static_cast<MemoryStreamIoContext*>(opaque);
				return h->inputStream.Read(buf, bufSize);
			}

			static int64_t Seek(void* opaque, const int64_t offset, const int whence) {
				auto h = static_cast<MemoryStreamIoContext*>(opaque);

				if (0x10000 == whence)
					return h->inputStream.Size();

				return h->inputStream.Seek(offset, whence);
			}

			[[nodiscard]] AVIOContext* GetAvio() const
			{
				return ctx;
			}

		private:
			MemoryStream inputStream{};
			unsigned char* buffer = nullptr;
			AVIOContext* ctx = nullptr;
		};

		inline std::string FFmpegErrStr(const int err)
		{
			char buf[4096]{ 0 };
			if (const auto ret = av_strerror(err, buf, 4096); ret < 0)
				return std::format("errnum {} cannot be found", ret);
			return std::format("errnum {}: {}", err, buf);
		}
	}

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
    struct Image
    {
        using SizeType = std::vector<T>::size_type;

        std::vector<T> data{};
        SizeType rows;
        SizeType cols;

        Image() : rows(0), cols(0) {}

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
        AVFormatContext* fmtCtx = nullptr;
		__Detail::MemoryStream ms{};
		std::unique_ptr<__Detail::MemoryStreamIoContext> privCtx{};

    public:
		std::filesystem::path FilePath{};

        Processor(const std::string_view& data)
        {
			constexpr auto __MediaFuncName__ = "ImageDatabase::Processor::Processor(std::string_view)";

			ms = __Detail::MemoryStream((uint8_t*)data.data(), data.length(), false);

			privCtx = std::make_unique<__Detail::MemoryStreamIoContext>(ms);
			fmtCtx = avformat_alloc_context();
			fmtCtx->pb = privCtx->GetAvio();

			if (const int ret = avformat_open_input(&fmtCtx, nullptr, nullptr, nullptr); ret < 0)
				ThrowBaseEx("[avforamt_open_input] {}", __Detail::FFmpegErrStr(ret));
        }

        Processor(const std::filesystem::path& path): FilePath(path)
        {
			constexpr auto __MediaFuncName__ = "ImageDatabase::Processor::Processor(std::string_view)";

			if (const int ret = avformat_open_input(&fmtCtx, nullptr, nullptr, nullptr); ret < 0)
				ThrowBaseEx("[avforamt_open_input] {}", __Detail::FFmpegErrStr(ret));
        }
        ~Processor();
    };

    class Reader
    {

    };
}