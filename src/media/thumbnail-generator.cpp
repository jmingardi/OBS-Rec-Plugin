#include "thumbnail-generator.hpp"

#include <algorithm>
#include <array>

#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QSaveFile>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/mathematics.h>
#include <libswscale/swscale.h>
}

namespace replay_timeline {
namespace {
constexpr int kThumbnailWidth = 192;
constexpr int kThumbnailHeight = 108;

QString ffmpegError(int code)
{
	std::array<char, AV_ERROR_MAX_STRING_SIZE> buffer{};
	av_strerror(code, buffer.data(), buffer.size());
	return QString::fromUtf8(buffer.data());
}

QString normalizedMediaIdentity(const QFileInfo &media)
{
	QString path = media.canonicalFilePath();
	if (path.isEmpty())
		path = media.absoluteFilePath();
#ifdef _WIN32
	path = path.toLower();
#endif
	return QStringLiteral("%1\n%2\n%3")
		.arg(QDir::cleanPath(path))
		.arg(media.size())
		.arg(media.lastModified().toMSecsSinceEpoch());
}

bool decodeFrame(AVFormatContext *format, int streamIndex, AVCodecContext *decoder, AVFrame *frame, QString &error)
{
	AVPacket *packet = av_packet_alloc();
	if (!packet) {
		error = QStringLiteral("could not allocate a media packet");
		return false;
	}

	bool decoded = false;
	int result = 0;
	while (!decoded && (result = av_read_frame(format, packet)) >= 0) {
		if (packet->stream_index == streamIndex) {
			result = avcodec_send_packet(decoder, packet);
			if (result >= 0 || result == AVERROR(EAGAIN)) {
				while ((result = avcodec_receive_frame(decoder, frame)) >= 0) {
					decoded = true;
					break;
				}
			}
		}
		av_packet_unref(packet);
	}

	if (!decoded) {
		result = avcodec_send_packet(decoder, nullptr);
		if (result >= 0 || result == AVERROR_EOF)
			decoded = avcodec_receive_frame(decoder, frame) >= 0;
	}
	av_packet_free(&packet);
	if (!decoded)
		error = result < 0 && result != AVERROR_EOF ? ffmpegError(result)
							 : QStringLiteral("the media has no decodable video frame");
	return decoded;
}

QImage convertFrame(const AVFrame *frame, QString &error)
{
	if (frame->width <= 0 || frame->height <= 0) {
		error = QStringLiteral("the decoded video frame has invalid dimensions");
		return {};
	}
	const QSize outputSize = QSize(frame->width, frame->height)
					 .scaled(kThumbnailWidth, kThumbnailHeight, Qt::KeepAspectRatio);
	QImage image(outputSize, QImage::Format_RGB888);
	if (image.isNull()) {
		error = QStringLiteral("could not allocate the thumbnail image");
		return {};
	}

	SwsContext *scaler = sws_getContext(frame->width, frame->height, static_cast<AVPixelFormat>(frame->format),
					outputSize.width(), outputSize.height(), AV_PIX_FMT_RGB24, SWS_BILINEAR, nullptr,
					nullptr, nullptr);
	if (!scaler) {
		error = QStringLiteral("could not create the thumbnail color converter");
		return {};
	}
	std::array<std::uint8_t *, 4> destination{image.bits(), nullptr, nullptr, nullptr};
	std::array<int, 4> destinationLines{static_cast<int>(image.bytesPerLine()), 0, 0, 0};
	const int outputRows = sws_scale(scaler, frame->data, frame->linesize, 0, frame->height, destination.data(),
					 destinationLines.data());
	sws_freeContext(scaler);
	if (outputRows != outputSize.height()) {
		error = QStringLiteral("could not convert the decoded video frame");
		return {};
	}
	return image;
}
} // namespace

QString replayThumbnailCachePath(const QString &cacheDirectory, const QString &mediaPath)
{
	const QFileInfo media(mediaPath);
	if (cacheDirectory.isEmpty() || mediaPath.isEmpty())
		return {};
	const QByteArray digest = QCryptographicHash::hash(normalizedMediaIdentity(media).toUtf8(),
							 QCryptographicHash::Sha256)
				  .toHex();
	return QDir(cacheDirectory).filePath(QString::fromLatin1(digest) + QStringLiteral(".bmp"));
}

ThumbnailResult generateReplayThumbnail(const QString &mediaPath, const QString &cacheDirectory,
					std::int64_t durationNs)
{
	const QFileInfo media(mediaPath);
	if (!media.exists() || !media.isFile())
		return {{}, QStringLiteral("replay media is missing"), false};
	if (!QDir().mkpath(cacheDirectory))
		return {{}, QStringLiteral("could not create the thumbnail cache directory"), false};
	const QString cachePath = replayThumbnailCachePath(cacheDirectory, mediaPath);
	if (QFileInfo::exists(cachePath)) {
		if (!QImage(cachePath).isNull())
			return {cachePath, {}, true};
		QFile::remove(cachePath);
	}

	const QByteArray utf8 = mediaPath.toUtf8();
	AVFormatContext *format = nullptr;
	int result = avformat_open_input(&format, utf8.constData(), nullptr, nullptr);
	if (result < 0)
		return {{}, ffmpegError(result), false};
	result = avformat_find_stream_info(format, nullptr);
	if (result < 0) {
		avformat_close_input(&format);
		return {{}, ffmpegError(result), false};
	}

	const AVCodec *codec = nullptr;
	const int streamIndex = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
	if (streamIndex < 0 || !codec) {
		avformat_close_input(&format);
		return {{}, QStringLiteral("the replay has no decodable video stream"), false};
	}
	AVCodecContext *decoder = avcodec_alloc_context3(codec);
	if (!decoder) {
		avformat_close_input(&format);
		return {{}, QStringLiteral("could not allocate the video decoder"), false};
	}
	result = avcodec_parameters_to_context(decoder, format->streams[streamIndex]->codecpar);
	if (result >= 0)
		result = avcodec_open2(decoder, codec, nullptr);
	if (result < 0) {
		avcodec_free_context(&decoder);
		avformat_close_input(&format);
		return {{}, ffmpegError(result), false};
	}

	if (durationNs > 0) {
		const AVStream *stream = format->streams[streamIndex];
		std::int64_t target = av_rescale_q((durationNs / 3) * 2, AVRational{1, 1'000'000'000}, stream->time_base);
		if (stream->start_time != AV_NOPTS_VALUE)
			target += stream->start_time;
		if (av_seek_frame(format, streamIndex, target, AVSEEK_FLAG_BACKWARD) >= 0)
			avcodec_flush_buffers(decoder);
	}

	AVFrame *frame = av_frame_alloc();
	QString error;
	if (!frame || !decodeFrame(format, streamIndex, decoder, frame, error)) {
		if (!frame && error.isEmpty())
			error = QStringLiteral("could not allocate a decoded video frame");
		av_frame_free(&frame);
		avcodec_free_context(&decoder);
		avformat_close_input(&format);
		return {{}, error, false};
	}
	const QImage image = convertFrame(frame, error);
	av_frame_free(&frame);
	avcodec_free_context(&decoder);
	avformat_close_input(&format);
	if (image.isNull())
		return {{}, error, false};

	QSaveFile output(cachePath);
	if (!output.open(QIODevice::WriteOnly) || !image.save(&output, "BMP") || !output.commit())
		return {{}, QStringLiteral("could not write the thumbnail cache file"), false};
	return {cachePath, {}, false};
}

} // namespace replay_timeline
