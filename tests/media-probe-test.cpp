#include "media/media-probe.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>

#include <QByteArray>
#include <QFile>
#include <QTemporaryDir>

using replay_timeline::MediaProbeResult;
using replay_timeline::probeMediaDuration;

namespace {
void expect(bool condition, const char *message)
{
	if (!condition) {
		std::cerr << "FAILED: " << message << '\n';
		std::exit(EXIT_FAILURE);
	}
}

void writeLe16(char *target, unsigned int value)
{
	target[0] = static_cast<char>(value & 0xffU);
	target[1] = static_cast<char>((value >> 8U) & 0xffU);
}

void writeLe32(char *target, unsigned int value)
{
	target[0] = static_cast<char>(value & 0xffU);
	target[1] = static_cast<char>((value >> 8U) & 0xffU);
	target[2] = static_cast<char>((value >> 16U) & 0xffU);
	target[3] = static_cast<char>((value >> 24U) & 0xffU);
}
} // namespace

int main()
{
	QTemporaryDir directory;
	expect(directory.isValid(), "temporary media directory is available");
	constexpr unsigned int sampleRate = 8'000;
	constexpr unsigned int dataSize = sampleRate * 2;
	QByteArray wave(44 + static_cast<qsizetype>(dataSize), '\0');
	std::memcpy(wave.data(), "RIFF", 4);
	writeLe32(wave.data() + 4, 36 + dataSize);
	std::memcpy(wave.data() + 8, "WAVEfmt ", 8);
	writeLe32(wave.data() + 16, 16);
	writeLe16(wave.data() + 20, 1);
	writeLe16(wave.data() + 22, 1);
	writeLe32(wave.data() + 24, sampleRate);
	writeLe32(wave.data() + 28, sampleRate * 2);
	writeLe16(wave.data() + 32, 2);
	writeLe16(wave.data() + 34, 16);
	std::memcpy(wave.data() + 36, "data", 4);
	writeLe32(wave.data() + 40, dataSize);

	const QString path = directory.filePath(QStringLiteral("audio.wav"));
	QFile file(path);
	expect(file.open(QIODevice::WriteOnly) && file.write(wave) == wave.size(), "WAV fixture is written");
	file.close();
	const MediaProbeResult result = probeMediaDuration(path);
	expect(result.succeeded() && result.durationNs >= 990'000'000 && result.durationNs <= 1'010'000'000,
	       "real media duration is probed");
	expect(result.audioTracks == 1 && result.audioStatus() == QStringLiteral("ok"),
	       "one audio stream validates successfully");
	expect(MediaProbeResult{1, 0, {}}.audioStatus() == QStringLiteral("missing"),
	       "zero audio streams produces a warning");
	expect(MediaProbeResult{-1, -1, QStringLiteral("failed")}.audioStatus() == QStringLiteral("unknown"),
	       "failed probes keep audio status unknown");
	std::cout << "Media probe tests passed\n";
	return EXIT_SUCCESS;
}
