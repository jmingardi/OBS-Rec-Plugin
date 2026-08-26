#include "replay-path-resolver.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include <QDirIterator>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

namespace replay_timeline {
namespace {

constexpr qint64 saveTimeToleranceMs = 15'000;
constexpr qint64 nameTimeToleranceMs = 2'000;
constexpr qint64 nameCandidateDateToleranceMs = 6 * 60 * 60 * 1000;

struct Candidate {
	QString path;
	qint64 score = std::numeric_limits<qint64>::max();
};

bool supportedMediaFile(const QFileInfo &info)
{
	static const QSet<QString> extensions{QStringLiteral("mkv"),  QStringLiteral("mp4"),
					      QStringLiteral("mov"),  QStringLiteral("flv"),
					      QStringLiteral("ts"),   QStringLiteral("m4v"),
					      QStringLiteral("webm")};
	return info.isFile() && extensions.contains(info.suffix().toLower());
}

int timeOfDaySeconds(const QString &name)
{
	static const QRegularExpression expression(
		QStringLiteral(R"((?:^|\D)([01]?\d|2[0-3])[-_.:]([0-5]\d)[-_.:]([0-5]\d)(?:\D|$))"));
	QRegularExpressionMatchIterator matches = expression.globalMatch(name);
	int result = -1;
	while (matches.hasNext()) {
		const QRegularExpressionMatch match = matches.next();
		result = match.captured(1).toInt() * 3600 + match.captured(2).toInt() * 60 +
			 match.captured(3).toInt();
	}
	return result;
}

qint64 circularTimeDifferenceMs(int left, int right)
{
	if (left < 0 || right < 0)
		return std::numeric_limits<qint64>::max();
	const int difference = std::abs(left - right);
	return static_cast<qint64>(std::min(difference, 86'400 - difference)) * 1000;
}

qint64 fileTimeDifferenceMs(const QFileInfo &info, const QDateTime &savedUtc)
{
	if (!savedUtc.isValid())
		return std::numeric_limits<qint64>::max();
	const QDateTime birth = info.birthTime();
	if (birth.isValid())
		return std::abs(birth.msecsTo(savedUtc));
	const QDateTime modified = info.lastModified();
	return modified.isValid() ? std::abs(modified.msecsTo(savedUtc)) : std::numeric_limits<qint64>::max();
}

} // namespace

ReplayPathResolution resolveReplayPath(const QString &reportedPath, const QDateTime &savedUtc)
{
	const QFileInfo reported(reportedPath);
	if (reported.exists() && reported.isFile())
		return {reported.absoluteFilePath(), {}, true, false};

	const QString root = reported.absolutePath();
	if (!QFileInfo(root).isDir())
		return {{}, QStringLiteral("reported replay directory does not exist: %1").arg(root), false, false};

	const int expectedNameTime = timeOfDaySeconds(reported.completeBaseName());
	std::vector<Candidate> candidates;
	QDirIterator iterator(root, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
	while (iterator.hasNext()) {
		iterator.next();
		const QFileInfo info = iterator.fileInfo();
		if (!supportedMediaFile(info))
			continue;

		const qint64 fileDifference = fileTimeDifferenceMs(info, savedUtc);
		const qint64 nameDifference = circularTimeDifferenceMs(
			expectedNameTime, timeOfDaySeconds(info.completeBaseName()));
		const bool saveTimeMatch = fileDifference <= saveTimeToleranceMs;
		const bool nameTimeMatch = nameDifference <= nameTimeToleranceMs &&
					   fileDifference <= nameCandidateDateToleranceMs;
		if (!saveTimeMatch && !nameTimeMatch)
			continue;

		// An exact filename time is strongest; filesystem creation time breaks ties.
		const qint64 nameScore = nameDifference == std::numeric_limits<qint64>::max()
					  ? 10'000'000
					  : nameDifference * 100;
		candidates.push_back({info.absoluteFilePath(), nameScore + std::min(fileDifference, 9'999'999LL)});
	}

	if (candidates.empty())
		return {{},
			QStringLiteral("reported replay was moved, but no unique time-matched media file was found beneath %1")
				.arg(root),
			false, false};

	std::sort(candidates.begin(), candidates.end(), [](const Candidate &left, const Candidate &right) {
		return left.score < right.score || (left.score == right.score && left.path < right.path);
	});
	if (candidates.size() > 1 && candidates[1].score - candidates[0].score < 1000) {
		return {{},
			QStringLiteral("reported replay was moved and multiple equally likely files were found beneath %1")
				.arg(root),
			false, false};
	}

	return {candidates.front().path,
		QStringLiteral("replay relocated from %1 to %2").arg(reportedPath, candidates.front().path), true, true};
}

ReplayPathResolution resolveRecordingPath(const QString &reportedPath, const QDateTime &expectedStartedUtc)
{
	const QFileInfo reported(reportedPath);
	if (reported.exists() && reported.isFile())
		return {reported.absoluteFilePath(), {}, true, false};
	if (!expectedStartedUtc.isValid())
		return {{}, QStringLiteral("recording start time is unavailable"), false, false};

	const QString root = reported.isDir() ? reported.absoluteFilePath() : reported.absolutePath();
	QDir directory(root);
	if (!directory.exists())
		return {{}, QStringLiteral("reported recording directory does not exist: %1").arg(root), false, false};

	const int expectedNameTime = expectedStartedUtc.time().hour() * 3600 +
				     expectedStartedUtc.time().minute() * 60 + expectedStartedUtc.time().second();
	std::vector<Candidate> candidates;
	const QFileInfoList entries = directory.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
	for (const QFileInfo &info : entries) {
		if (!supportedMediaFile(info))
			continue;
		const qint64 fileDifference = fileTimeDifferenceMs(info, expectedStartedUtc);
		const qint64 nameDifference = circularTimeDifferenceMs(
			expectedNameTime, timeOfDaySeconds(info.completeBaseName()));
		const bool creationMatch = fileDifference <= 10 * 60 * 1000;
		const bool nameMatch = nameDifference <= nameTimeToleranceMs &&
				       fileDifference <= nameCandidateDateToleranceMs;
		if (!creationMatch && !nameMatch)
			continue;
		// Filesystem creation time is timezone-aware and strongest for recordings;
		// the filename time is only a tie-breaker because OBS filenames use local time.
		const qint64 nameTieBreaker = nameDifference == std::numeric_limits<qint64>::max()
					       ? 9'999'999
					       : std::min(nameDifference, 9'999'999LL);
		candidates.push_back(
			{info.absoluteFilePath(), std::min(fileDifference, 100'000'000LL) * 100 + nameTieBreaker});
	}

	if (candidates.empty())
		return {{}, QStringLiteral("no recording matched the calculated start time in %1").arg(root), false,
			false};
	std::sort(candidates.begin(), candidates.end(), [](const Candidate &left, const Candidate &right) {
		return left.score < right.score || (left.score == right.score && left.path < right.path);
	});
	if (candidates.size() > 1 && candidates[1].score - candidates[0].score < 1000)
		return {{}, QStringLiteral("multiple recordings matched the calculated start time in %1").arg(root),
			false, false};

	return {candidates.front().path,
		QStringLiteral("recording path finalized from %1 to %2").arg(reportedPath, candidates.front().path),
		true, true};
}

} // namespace replay_timeline
