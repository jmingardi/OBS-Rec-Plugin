#include "media-probe.hpp"

#include <algorithm>
#include <array>

#include <QByteArray>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/mathematics.h>
}

namespace replay_timeline {
namespace {
QString ffmpegError(int code)
{
	std::array<char, AV_ERROR_MAX_STRING_SIZE> buffer{};
	av_strerror(code, buffer.data(), buffer.size());
	return QString::fromUtf8(buffer.data());
}
} // namespace

MediaProbeResult probeMediaDuration(const QString &path)
{
	const QByteArray utf8 = path.toUtf8();
	AVFormatContext *context = nullptr;
	int result = avformat_open_input(&context, utf8.constData(), nullptr, nullptr);
	if (result < 0)
		return {-1, ffmpegError(result)};

	result = avformat_find_stream_info(context, nullptr);
	if (result < 0) {
		avformat_close_input(&context);
		return {-1, ffmpegError(result)};
	}

	std::int64_t durationNs = -1;
	if (context->duration != AV_NOPTS_VALUE && context->duration > 0) {
		durationNs = av_rescale_q(context->duration, AV_TIME_BASE_Q, AVRational{1, 1'000'000'000});
	} else {
		for (unsigned int index = 0; index < context->nb_streams; ++index) {
			const AVStream *stream = context->streams[index];
			if (stream->duration == AV_NOPTS_VALUE || stream->duration <= 0)
				continue;
			const std::int64_t streamDuration =
				av_rescale_q(stream->duration, stream->time_base, AVRational{1, 1'000'000'000});
			durationNs = std::max(durationNs, streamDuration);
		}
	}

	avformat_close_input(&context);
	return durationNs > 0 ? MediaProbeResult{durationNs, {}}
			      : MediaProbeResult{-1, QStringLiteral("media duration is unavailable")};
}

} // namespace replay_timeline
