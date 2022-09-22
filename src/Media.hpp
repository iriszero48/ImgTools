#pragma once

#include <format>
#include <variant>
#include <vector>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace Media
{
    namespace __Detail
    {
#define MacroToStringImpl(x) #x
#define MacroToString(x) MacroToStringImpl(x)

        template <typename... Args>
        std::string Append(Args &&...args)
        {
            std::string buf;
            (buf.append(args), ...);
            return buf;
        }

        template <typename T, size_t S>
        constexpr const char *GetFilename(const T (&str)[S], size_t i = S - 1)
        {
            for (; i > 0; --i)
                if (str[i] == '/')
                    return &str[i + 1];
            return str;
        }
    }

    class Exception : public std::runtime_error
    {
    public:
        template <typename... Args>
        Exception(Args &&...str) : std::runtime_error(__Detail::Append(std::forward<Args>(str)...)) {}
    };

#define Ex(ex, ...) ex("[", MacroToString(ex), "] [", __MediaFuncName__, "] [", MacroToString(__LINE__), "] ", std::format(__VA_ARGS__))
#define ThrowEx(ex, ...) throw Ex(ex, __VA_ARGS__)
#define ThrowBaseEx(...) ThrowEx(Media::Exception, __VA_ARGS__)

    namespace __Detail
    {
        class MemoryStream
        {
        public:
            MemoryStream() = default;

            MemoryStream(uint8_t *data, const uint64_t size, const bool keep = false) : size(size), keep(keep)
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

            MemoryStream(const MemoryStream &ms)
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

            MemoryStream(MemoryStream &&ms) noexcept
            {
                this->size = ms.size;
                this->data = ms.data;
                this->keep = ms.keep;
                this->index = ms.index;
                ms.Reset();
            }

            MemoryStream &operator=(const MemoryStream &ms)
            {
                if (this == &ms)
                    return *this;

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

            MemoryStream &operator=(MemoryStream &&ms) noexcept
            {
                if (this == &ms)
                    return *this;

                this->size = ms.size;
                this->data = ms.data;
                this->keep = ms.keep;
                this->index = ms.index;
                ms.Reset();
                return *this;
            }

            ~MemoryStream()
            {
                if (keep)
                    delete[] data;
                Reset();
            }

            [[nodiscard]] uint64_t Size() const { return size; }

            int Read(unsigned char *buf, const int bufSize)
            {
                if (bufSize < 0)
                    return bufSize;
                if (index >= size)
                    return AVERROR_EOF;

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
                    if (offset < 0)
                        return -1;
                    index = offset;
                }
                else if (whence == SEEK_CUR)
                {
                    index += offset;
                }
                else if (whence == SEEK_END)
                {
                    if (offset > 0)
                        return -1;
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
            uint8_t *data = nullptr;
            uint64_t size = 0;
            bool keep = false;
            uint64_t index = 0;
        };

        class MemoryStreamIoContext
        {
            static const auto BufferSize = 4096;

        public:
            MemoryStreamIoContext() = default;
            MemoryStreamIoContext(MemoryStreamIoContext const &) = delete;
            MemoryStreamIoContext &operator=(MemoryStreamIoContext const &) = delete;
            MemoryStreamIoContext(MemoryStreamIoContext &&) = delete;
            MemoryStreamIoContext &operator=(MemoryStreamIoContext &&) = delete;
            ~MemoryStreamIoContext() = default;

            MemoryStreamIoContext(MemoryStream inputStream) : inputStream(std::move(inputStream)),
                                                              buffer(static_cast<unsigned char *>(av_malloc(BufferSize)))
            {
                constexpr auto __MediaFuncName__ = "ImageDatabase::__Detail::MemoryStreamIoContext::MemoryStreamIoContext";
                if (buffer == nullptr)
                    ThrowBaseEx("[av_malloc] the buffer cannot be allocated");

                ctx = avio_alloc_context(buffer, BufferSize, 0, this,
                                         &MemoryStreamIoContext::Read, nullptr, &MemoryStreamIoContext::Seek);
                if (ctx == nullptr)
                    ThrowBaseEx("[avio_alloc_context] the AVIO cannot be allocated");
            }

            void ResetInnerContext()
            {
                ctx = nullptr;
                buffer = nullptr;
            }

            static int Read(void *opaque, unsigned char *buf, const int bufSize)
            {
                auto h = static_cast<MemoryStreamIoContext *>(opaque);
                return h->inputStream.Read(buf, bufSize);
            }

            static int64_t Seek(void *opaque, const int64_t offset, const int whence)
            {
                auto h = static_cast<MemoryStreamIoContext *>(opaque);

                if (0x10000 == whence)
                    return h->inputStream.Size();

                return h->inputStream.Seek(offset, whence);
            }

            [[nodiscard]] AVIOContext *GetAvio() const
            {
                return ctx;
            }

        private:
            MemoryStream inputStream{};
            unsigned char *buffer = nullptr;
            AVIOContext *ctx = nullptr;
        };

        inline std::string FFmpegErrStr(const int err)
        {
            char buf[4096]{0};
            if (const auto ret = av_strerror(err, buf, 4096); ret < 0)
                return std::format("errnum {} cannot be found", ret);
            return std::format("errnum {}: {}", err, buf);
        }
    }

    template <typename T>
    struct PixivARGB
    {
        using Type = T;
        static const int Size = sizeof(T) * 4;
        static const AVPixelFormat AvPixFmt = AV_PIX_FMT_ARGB;

        T *A;
        T *R;
        T *G;
        T *B;

        PixivARGB() : A(0), R(0), G(0), B(0) {}

        explicit PixivARGB(const T v) : A(v), R(v), G(v), B(v) {}

        PixivARGB(const T &r, const T &g, const T &b, const T &a) : A(a), R(r), G(g), B(b) {}

        explicit PixivARGB(const std::string_view &arr) : A(&arr[0]), R(&arr[1]), G(&arr[2]), B(&arr[3]) {}

        constexpr auto Sizeof() { return sizeof(T) * 4; }
    };

    template <typename TPix>
    struct Image
    {
        using Type = TPix;
        static const int PixivSize = Type::Size;
        static const AVPixelFormat AvPixFmt = Type::AvPixFmt;
        using SizeType = std::string::size_type;

        std::string Data{};
        SizeType Width;
        SizeType Height;

        Image() : Width(0), Height(0) {}
        Image(const SizeType width, const SizeType height) : Data(width * height * PixivSize, '\0'), Width(width), Height(height) {}

        TPix &At(const SizeType row, const SizeType col)
        {
            return Type(std::string_view(Data).substr(Pos(row, col), PixivSize));
        }

        const TPix &At(const SizeType row, const SizeType col) const
        {
            return Type(std::string_view(Data).substr(Pos(row, col), PixivSize));
        }

    private:
        const auto Pos(const SizeType row, const SizeType col) const
        {
            return (row * Width + col) * PixivSize;
        }
    };

    template <typename PixFmt = PixivARGB<uint8_t>>
    class Reader
    {
    private:
        using Params0 = std::filesystem::path;
        using Params1 = std::string_view;
        using ParamsType = std::variant<Params0, Params1>;
        ParamsType params;

    public:
        class iterator
        {
        public:
            using iterator_category = std::forward_iterator_tag;
            using value_type = Image<PixFmt>;
            using difference_type = value_type::SizeType;
            using pointer = value_type *;
            using reference = value_type &;

        private:
            AVFormatContext *fmtCtx = nullptr;

            __Detail::MemoryStream ms{};
            std::unique_ptr<__Detail::MemoryStreamIoContext> privCtx{};

            AVCodec *codec = nullptr;
            AVCodecContext *codecCtx = nullptr;
            AVFrame *frame = nullptr;
            AVFrame *decFrame = nullptr;
            SwsContext *swsCtx = nullptr;

            int streamId;
            AVCodecParameters *codecParams = nullptr;

            int dstWidth;
            int dstHeight;
            const AVPixelFormat dstPixFmt = value_type::AvPixFmt;
            int64_t framesNumber;

            bool eof = false;
            bool setRange = false;
            bool finished = false;

            AVPacket *pkt = nullptr;
            int lineSize;

            value_type img;

            void Init(AVFormatContext *fmtCtx)
            {
                constexpr auto __MediaFuncName__ = "Media::Reader::iterator::Init";

                try
                {
                    int ret = avformat_find_stream_info(fmtCtx, nullptr);
                    if (ret < 0)
                        ThrowBaseEx("[avformat_find_stream_info] {}", __Detail::FFmpegErrStr(ret));

                    ret = av_find_best_stream(fmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
                    if (ret < 0)
                        ThrowBaseEx("[av_find_best_stream] {}", __Detail::FFmpegErrStr(ret));

                    streamId = ret;
                    codecParams = fmtCtx->streams[streamId]->codecpar;
                    framesNumber = fmtCtx->streams[streamId]->nb_frames;

                    codecCtx = avcodec_alloc_context3(codec);
                    if (!codecCtx)
                        ThrowBaseEx("[avcodec_alloc_context3] NULL");

                    ret = avcodec_parameters_to_context(codecCtx, codecParams);
                    if (ret < 0)
                        ThrowBaseEx("[avcodec_parameters_to_context] {}", __Detail::FFmpegErrStr(ret));

                    ret = avcodec_open2(codecCtx, codec, nullptr);
                    if (ret < 0)
                        ThrowBaseEx("[avcodec_open2] {}", __Detail::FFmpegErrStr(ret));

                    dstWidth = codecParams->width;
                    dstHeight = codecParams->height;

                    setRange = false;

                    if (codecCtx->pix_fmt != AV_PIX_FMT_NONE)
                    {
                        swsCtx = sws_getCachedContext(
                            nullptr, codecParams->width, codecParams->height, codecCtx->pix_fmt,
                            dstWidth, dstHeight, dstPixFmt, 0,
                            nullptr, nullptr, nullptr);
                        if (!swsCtx)
                            ThrowBaseEx("[sws_getCachedContext] NULL");
                    }

                    frame = av_frame_alloc();
                    if (!frame)
                        ThrowBaseEx("[av_frame_alloc] NULL");

                    const auto frameSize = av_image_get_buffer_size(dstPixFmt, dstWidth, dstHeight, dstWidth);
                    if (frameSize < 0)
                        ThrowBaseEx("[av_image_get_buffer_size] {}", __Detail::FFmpegErrStr(frameSize));

                    img = value_type(dstWidth, dstHeight);
                    if (frameSize != img.Data.length())
                        ThrowBaseEx("[frameSize != img.Data.length()] {} != {}", frameSize, img.Data.length());

                    ret = av_image_fill_arrays(
                        frame->data, frame->linesize,
                        reinterpret_cast<uint8_t *>(img.Data.data()),
                        dstPixFmt, dstWidth, dstHeight, dstWidth);
                    if (ret < 0)
                        ThrowBaseEx("[av_image_fill_arrays] {}", __Detail::FFmpegErrStr(ret));

                    decFrame = av_frame_alloc();
                    if (!decFrame)
                        ThrowBaseEx("[av_frame_alloc] NULL");

                    eof = false;
                    pkt = av_packet_alloc();
                    if (!pkt)
                        ThrowBaseEx("[av_packet_alloc] NULL");

                    ret = avformat_seek_file(fmtCtx, streamId, 0, 0, 0, AVSEEK_FLAG_FRAME);
                    if (ret < 0)
                        ThrowBaseEx("[avformat_seek_file] {}", __Detail::FFmpegErrStr(ret));

                    avcodec_flush_buffers(codecCtx);
                }
                catch (...)
                {
                    std::throw_with_nested(Ex(Media::Exception, "init media failed"));
                }
            }

        public:
            uint64_t Index = 0;

            iterator() : fmtCtx(nullptr), finished(true) {}

            iterator(ParamsType &params)
            {
                std::visit([&]<typename TR>(TR &p)
                           {
					using T = std::decay_t<TR>;
					if constexpr (std::is_same_v<T, Params0>)
					{
						constexpr auto __MediaFuncName__ = "Media::Reader::iterator::iterator<std::string_view>";

						if (const int ret = avformat_open_input(&fmtCtx, (const char*)p.u8string().c_str(), nullptr, nullptr); ret < 0)
						ThrowBaseEx("[avforamt_open_input] {}", __Detail::FFmpegErrStr(ret));
					}
					else if constexpr (std::is_same_v<T, Params1>)
					{
						constexpr auto __MediaFuncName__ = "Media::Reader::iterator::iterator<std::filesystem::path>";

						ms = __Detail::MemoryStream((uint8_t*)p.data(), p.length(), false);

						privCtx = std::make_unique<__Detail::MemoryStreamIoContext>(ms);
						fmtCtx = avformat_alloc_context();
						fmtCtx->pb = privCtx->GetAvio();

						if (const int ret = avformat_open_input(&fmtCtx, nullptr, nullptr, nullptr); ret < 0)
							ThrowBaseEx("[avforamt_open_input] {}", __Detail::FFmpegErrStr(ret));
					}
					else
					{
						constexpr auto __MediaFuncName__ = "Media::Reader::iterator::iterator<>";
						ThrowBaseEx("non-exhaustive type");
					} },
                           params);
                Init(fmtCtx);
            }

            ~iterator()
            {
                av_frame_free(&decFrame);
                av_frame_free(&frame);
                avcodec_free_context(&codecCtx);
                sws_freeContext(swsCtx);
            }

            iterator &operator++()
            {
                constexpr auto __MediaFuncName__ = "Media::Reader::iterator::operator++";
                int ret;

                if (!eof)
                {
                    ret = av_read_frame(fmtCtx, pkt);
                    if (ret < 0 && ret != AVERROR_EOF)
                        ThrowBaseEx("[av_read_frame] {}", __Detail::FFmpegErrStr(ret));

                    if (ret == 0 && pkt->stream_index != streamId)
                    {
                        av_packet_unref(pkt);
                        operator++();
                    }
                    eof = (ret == AVERROR_EOF);
                }
                if (eof)
                {
                    av_packet_unref(pkt);
                    ret = 0;
                }
                else
                {
                    ++Index;

                    ret = avcodec_send_packet(codecCtx, pkt);
                    if (ret < 0)
                        ThrowBaseEx("[avcodec_send_packet] {}", __Detail::FFmpegErrStr(ret));
                }
                while (ret >= 0)
                {
                    ret = avcodec_receive_frame(codecCtx, decFrame);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                    {
                        av_packet_unref(pkt);
                        break;
                    }
                    if (ret < 0)
                        ThrowBaseEx("[avcodec_receive_frame] {}", __Detail::FFmpegErrStr(ret));

                    if (swsCtx == nullptr)
                    {
                        swsCtx = sws_getCachedContext(
                            nullptr, codecParams->width, codecParams->height, codecCtx->pix_fmt,
                            dstWidth, dstHeight, dstPixFmt, 0, nullptr, nullptr, nullptr);
                        if (!swsCtx)
                            ThrowBaseEx("[sws_getCachedContext] NULL");
                    }

                    if (!setRange)
                    {
                        ret = sws_setColorspaceDetails(swsCtx,
                                                       sws_getCoefficients(decFrame->colorspace), decFrame->color_range,
                                                       sws_getCoefficients(SWS_CS_BT2020), AVCOL_RANGE_JPEG,
                                                       0, 1 << 16, 1 << 16);
                        if (ret == -1)
                            if (!swsCtx)
                                ThrowBaseEx("[sws_setColorspaceDetails] not supported");
                        setRange = true;
                    }

                    ret = sws_scale(swsCtx, decFrame->data, decFrame->linesize, 0, decFrame->height, frame->data, frame->linesize);
                    if (ret != dstHeight)
                        ThrowBaseEx("[sws_scale] height != {}", dstHeight);

                    Index++;
                }
                av_packet_unref(pkt);

                return *this;
            }

            bool operator==(const iterator &other) const
            {
                return finished == other.finished;
            }
            bool operator!=(const iterator &other) const { return !(*this == other); }

            reference operator*() { return img; }
            difference_type operator-(iterator other) const
            {
                return framesNumber;
            }
        };

        Reader(const std::string_view &data) : params(data) {}
        Reader(const std::filesystem::path &path) : params(path) {}

        ~Reader() {}

    public:
        iterator begin() { return iterator(params); }
        iterator end() { return iterator(); }
    };

    class Writer
    {
    private:
        static void encode(AVCodecContext *enc_ctx, AVFrame *frame, AVPacket *pkt,
                           std::ofstream &outfile)
        {
            constexpr auto __MediaFuncName__ = "Media::Writer::encode";
            int ret;

            /* send the frame to the encoder */
            // if (frame) printf("Send frame %3"PRId64"\n", frame->pts);

            ret = avcodec_send_frame(enc_ctx, frame);
            if (ret < 0)
                ThrowBaseEx("Error sending a frame for encoding");

            while (ret >= 0)
            {
                ret = avcodec_receive_packet(enc_ctx, pkt);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                    return;
                else if (ret < 0)
                    ThrowBaseEx("Error during encoding");

                // printf("Write packet %3"PRId64" (size=%5d)\n", pkt->pts, pkt->size);
                outfile.write((const char *)pkt->data, pkt->size);
                av_packet_unref(pkt);
            }
        }

    public:
    };
}