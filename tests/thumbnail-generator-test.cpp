#include "media/thumbnail-generator.hpp"

#include <cstdlib>
#include <iostream>

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>

using replay_timeline::ThumbnailResult;
using replay_timeline::generateReplayThumbnail;
using replay_timeline::replayThumbnailCachePath;

namespace {
void expect(bool condition, const char *message)
{
	if (!condition) {
		std::cerr << "FAILED: " << message << '\n';
		std::exit(EXIT_FAILURE);
	}
}
} // namespace

int main()
{
	QTemporaryDir directory;
	expect(directory.isValid(), "temporary thumbnail directory is available");
	const QString mediaPath = directory.filePath(QStringLiteral("frame.ppm"));
	QByteArray fixture("P6\n64 32\n255\n");
	for (int pixel = 0; pixel < 64 * 32; ++pixel)
		fixture.append(QByteArray::fromRawData("\x20\x80\xe0", 3));
	QFile media(mediaPath);
	expect(media.open(QIODevice::WriteOnly) && media.write(fixture) == fixture.size(),
	       "video-frame fixture is written");
	media.close();

	const QString cacheDirectory = directory.filePath(QStringLiteral("thumbnails"));
	const QString expectedPath = replayThumbnailCachePath(cacheDirectory, mediaPath);
	expect(!expectedPath.isEmpty(), "stable thumbnail cache path is available");
	const ThumbnailResult first = generateReplayThumbnail(mediaPath, cacheDirectory, -1);
	expect(first.succeeded() && !first.cacheHit && first.path == expectedPath,
	       "first thumbnail request decodes and caches a video frame");
	const QImage thumbnail(first.path);
	expect(!thumbnail.isNull() && thumbnail.size() == QSize(192, 96), "thumbnail scales within a 16:9 cache bound");

	const ThumbnailResult second = generateReplayThumbnail(mediaPath, cacheDirectory, -1);
	expect(second.succeeded() && second.cacheHit && second.path == first.path,
	       "second thumbnail request reuses the derived cache file");
	QFile corruptCache(first.path);
	expect(corruptCache.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
		       corruptCache.write("invalid thumbnail") > 0,
	       "cached thumbnail can be corrupted for recovery coverage");
	corruptCache.close();
	const ThumbnailResult repaired = generateReplayThumbnail(mediaPath, cacheDirectory, -1);
	expect(repaired.succeeded() && !repaired.cacheHit && !QImage(repaired.path).isNull(),
	       "an invalid cache artifact is regenerated");
	const ThumbnailResult missing =
		generateReplayThumbnail(directory.filePath(QStringLiteral("missing.mkv")), cacheDirectory, -1);
	expect(!missing.succeeded() && !missing.error.isEmpty(), "missing media fails without a cache artifact");
	expect(QFileInfo(first.path).size() > 0, "cached thumbnail is non-empty");
	std::cout << "Thumbnail generator tests passed\n";
	return EXIT_SUCCESS;
}
