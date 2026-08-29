#include "editor-exporter.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <tuple>

#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QXmlStreamWriter>

namespace replay_timeline {
namespace {
constexpr std::int64_t NanosecondsPerSecond = 1'000'000'000;

struct GroupKey {
	std::int64_t sessionId = 0;
	std::int64_t recordingRun = 0;

	bool operator<(const GroupKey &other) const
	{
		return std::tie(sessionId, recordingRun) < std::tie(other.sessionId, other.recordingRun);
	}
};

struct ReplayKey {
	GroupKey group;
	std::int64_t replayId = 0;

	bool operator<(const ReplayKey &other) const
	{
		return std::tie(group.sessionId, group.recordingRun, replayId) <
		       std::tie(other.group.sessionId, other.group.recordingRun, other.replayId);
	}
};

struct Marker {
	std::int64_t replayId = 0;
	std::int64_t startNs = -1;
	std::int64_t endNs = -1;
	QString tag;
	QString note;
	int rating = 0;
	QString applicationName;
	QString windowTitle;
	QString captureSource;
	QString replayPath;
	QString confidence;
};

using MarkerGroups = std::map<GroupKey, std::vector<Marker>>;

std::int64_t floorFrame(std::int64_t nanoseconds, const EditorFrameRate &rate)
{
	if (nanoseconds <= 0)
		return 0;
	const long double frames = static_cast<long double>(nanoseconds) * rate.numerator /
				   (static_cast<long double>(NanosecondsPerSecond) * rate.denominator);
	return static_cast<std::int64_t>(frames);
}

std::int64_t ceilFrame(std::int64_t nanoseconds, const EditorFrameRate &rate)
{
	if (nanoseconds <= 0)
		return 0;
	const std::int64_t floor = floorFrame(nanoseconds, rate);
	const long double represented = static_cast<long double>(floor) * NanosecondsPerSecond * rate.denominator;
	const long double requested = static_cast<long double>(nanoseconds) * rate.numerator;
	return represented < requested ? floor + 1 : floor;
}

std::int64_t timelineStartFrame(const EditorExportOptions &options)
{
	const int nominal = options.frameRate.nominalFramesPerSecond();
	if (!options.frameRate.usesDropFrameTimecode())
		return static_cast<std::int64_t>(options.timelineStartHours) * nominal * 3'600;
	const int droppedPerMinute = nominal == 60 ? 4 : 2;
	const std::int64_t framesPerHour = nominal * 3'600 - droppedPerMinute * 54;
	return static_cast<std::int64_t>(options.timelineStartHours) * framesPerHour;
}

QString timecodeFromFrame(std::int64_t frame, const EditorFrameRate &rate)
{
	const int nominal = rate.nominalFramesPerSecond();
	const bool dropFrame = rate.usesDropFrameTimecode();
	frame = std::max<std::int64_t>(0, frame);
	if (dropFrame) {
		const int droppedPerMinute = nominal == 60 ? 4 : 2;
		const std::int64_t framesPerMinute = nominal * 60 - droppedPerMinute;
		const std::int64_t framesPerTenMinutes = nominal * 600 - droppedPerMinute * 9;
		const std::int64_t framesPer24Hours = (nominal * 3'600 - droppedPerMinute * 54) * 24LL;
		frame %= framesPer24Hours;
		const std::int64_t tenMinuteBlocks = frame / framesPerTenMinutes;
		const std::int64_t remaining = frame % framesPerTenMinutes;
		frame += droppedPerMinute * 9 * tenMinuteBlocks;
		if (remaining >= droppedPerMinute)
			frame += droppedPerMinute * ((remaining - droppedPerMinute) / framesPerMinute);
	}

	const std::int64_t frames = frame % nominal;
	const std::int64_t totalSeconds = frame / nominal;
	const std::int64_t seconds = totalSeconds % 60;
	const std::int64_t minutes = (totalSeconds / 60) % 60;
	const std::int64_t hours = (totalSeconds / 3'600) % 24;
	const QChar separator = dropFrame ? QLatin1Char(';') : QLatin1Char(':');
	return QStringLiteral("%1:%2:%3%4%5")
		.arg(hours, 2, 10, QLatin1Char('0'))
		.arg(minutes, 2, 10, QLatin1Char('0'))
		.arg(seconds, 2, 10, QLatin1Char('0'))
		.arg(separator)
		.arg(frames, 2, 10, QLatin1Char('0'));
}

QString oneLine(QString value)
{
	value.replace(QLatin1Char('\r'), QLatin1Char(' '));
	value.replace(QLatin1Char('\n'), QLatin1Char(' '));
	value.replace(QLatin1Char('|'), QLatin1Char('/'));
	return value.simplified();
}

QString markerName(const Marker &marker)
{
	QString name = marker.tag.trimmed().isEmpty() ? QStringLiteral("Replay") : marker.tag.trimmed();
	if (marker.rating > 0)
		name += QStringLiteral(" %1/5").arg(marker.rating);
	return name;
}

QString markerComment(const Marker &marker)
{
	QStringList parts;
	if (!marker.note.trimmed().isEmpty())
		parts.push_back(marker.note.trimmed());
	if (!marker.applicationName.trimmed().isEmpty())
		parts.push_back(QStringLiteral("Application: %1").arg(marker.applicationName.trimmed()));
	if (!marker.windowTitle.trimmed().isEmpty())
		parts.push_back(QStringLiteral("Window: %1").arg(marker.windowTitle.trimmed()));
	if (!marker.captureSource.trimmed().isEmpty())
		parts.push_back(QStringLiteral("Source: %1").arg(marker.captureSource.trimmed()));
	if (!marker.replayPath.trimmed().isEmpty())
		parts.push_back(QStringLiteral("Replay: %1").arg(marker.replayPath.trimmed()));
	if (!marker.confidence.trimmed().isEmpty())
		parts.push_back(QStringLiteral("Mapping: %1").arg(marker.confidence.trimmed()));
	parts.push_back(QStringLiteral("Replay ID: %1").arg(marker.replayId));
	return parts.join(QLatin1Char('\n'));
}

QString resolveColor(const QString &tag)
{
	if (tag.compare(QStringLiteral("Kill"), Qt::CaseInsensitive) == 0)
		return QStringLiteral("Red");
	if (tag.compare(QStringLiteral("Bug"), Qt::CaseInsensitive) == 0)
		return QStringLiteral("Purple");
	if (tag.compare(QStringLiteral("Funny"), Qt::CaseInsensitive) == 0)
		return QStringLiteral("Yellow");
	if (tag.compare(QStringLiteral("Keep"), Qt::CaseInsensitive) == 0)
		return QStringLiteral("Green");
	if (tag.compare(QStringLiteral("External"), Qt::CaseInsensitive) == 0)
		return QStringLiteral("Blue");
	return QStringLiteral("Cyan");
}

MarkerGroups collectMarkers(const std::vector<CsvRow> &rows, int &skippedReplayCount)
{
	std::map<ReplayKey, Marker> aggregated;
	std::set<std::pair<std::int64_t, std::int64_t>> skipped;
	for (const CsvRow &row : rows) {
		if (row.recordingRun <= 0 || row.runStartNs < 0 || row.runEndNs <= row.runStartNs) {
			skipped.insert({row.sessionId, row.replayId});
			continue;
		}
		const ReplayKey key{{row.sessionId, row.recordingRun}, row.replayId};
		auto [position, inserted] =
			aggregated.try_emplace(key, Marker{row.replayId, row.runStartNs, row.runEndNs, row.tag,
							   row.note, row.rating, row.applicationName, row.windowTitle,
							   row.captureSource, row.replayPath, row.confidence});
		if (!inserted) {
			position->second.startNs = std::min(position->second.startNs, row.runStartNs);
			position->second.endNs = std::max(position->second.endNs, row.runEndNs);
		}
	}

	MarkerGroups groups;
	for (const auto &[key, marker] : aggregated)
		groups[key.group].push_back(marker);
	for (auto &[key, markers] : groups) {
		(void)key;
		std::sort(markers.begin(), markers.end(), [](const Marker &left, const Marker &right) {
			return std::tie(left.startNs, left.replayId) < std::tie(right.startNs, right.replayId);
		});
	}
	skippedReplayCount = static_cast<int>(skipped.size());
	return groups;
}

void writeRate(QXmlStreamWriter &writer, const EditorFrameRate &rate)
{
	writer.writeStartElement(QStringLiteral("rate"));
	writer.writeTextElement(QStringLiteral("timebase"), QString::number(rate.nominalFramesPerSecond()));
	writer.writeTextElement(QStringLiteral("ntsc"),
				rate.denominator == 1001 ? QStringLiteral("TRUE") : QStringLiteral("FALSE"));
	writer.writeEndElement();
}

void writePremiereSequence(QXmlStreamWriter &writer, const GroupKey &key, const std::vector<Marker> &markers,
			   const EditorExportOptions &options)
{
	const std::int64_t duration =
		std::max<std::int64_t>(1, ceilFrame(std::max_element(markers.begin(), markers.end(),
								     [](const Marker &left, const Marker &right) {
									     return left.endNs < right.endNs;
								     })
							    ->endNs,
						    options.frameRate));
	writer.writeStartElement(QStringLiteral("sequence"));
	writer.writeAttribute(QStringLiteral("id"),
			      QStringLiteral("sequence-s%1-r%2").arg(key.sessionId).arg(key.recordingRun));
	writer.writeTextElement(
		QStringLiteral("name"),
		QStringLiteral("Replay Timeline - Session %1 Run %2").arg(key.sessionId).arg(key.recordingRun));
	writer.writeTextElement(QStringLiteral("duration"), QString::number(duration));
	writeRate(writer, options.frameRate);

	writer.writeStartElement(QStringLiteral("timecode"));
	writeRate(writer, options.frameRate);
	const std::int64_t startFrame = timelineStartFrame(options);
	writer.writeTextElement(QStringLiteral("string"), timecodeFromFrame(startFrame, options.frameRate));
	writer.writeTextElement(QStringLiteral("frame"), QString::number(startFrame));
	writer.writeTextElement(QStringLiteral("displayformat"), options.frameRate.usesDropFrameTimecode()
									 ? QStringLiteral("DF")
									 : QStringLiteral("NDF"));
	writer.writeEndElement();

	for (const Marker &marker : markers) {
		const std::int64_t start = floorFrame(marker.startNs, options.frameRate);
		const std::int64_t end = std::max(start + 1, ceilFrame(marker.endNs, options.frameRate));
		writer.writeStartElement(QStringLiteral("marker"));
		writer.writeTextElement(QStringLiteral("name"), markerName(marker));
		writer.writeTextElement(QStringLiteral("comment"), markerComment(marker));
		writer.writeTextElement(QStringLiteral("in"), QString::number(start));
		writer.writeTextElement(QStringLiteral("out"), QString::number(end));
		writer.writeEndElement();
	}

	writer.writeStartElement(QStringLiteral("media"));
	writer.writeStartElement(QStringLiteral("video"));
	writer.writeStartElement(QStringLiteral("format"));
	writer.writeStartElement(QStringLiteral("samplecharacteristics"));
	writeRate(writer, options.frameRate);
	writer.writeTextElement(QStringLiteral("width"), QStringLiteral("1920"));
	writer.writeTextElement(QStringLiteral("height"), QStringLiteral("1080"));
	writer.writeTextElement(QStringLiteral("anamorphic"), QStringLiteral("FALSE"));
	writer.writeTextElement(QStringLiteral("pixelaspectratio"), QStringLiteral("square"));
	writer.writeTextElement(QStringLiteral("fielddominance"), QStringLiteral("none"));
	writer.writeEndElement();
	writer.writeEndElement();
	writer.writeStartElement(QStringLiteral("track"));
	writer.writeEndElement();
	writer.writeEndElement();
	writer.writeStartElement(QStringLiteral("audio"));
	writer.writeStartElement(QStringLiteral("track"));
	writer.writeEndElement();
	writer.writeEndElement();
	writer.writeEndElement();
	writer.writeEndElement();
}

QString documentPath(const QString &requestedPath, const EditorExportDocument &document, bool multiple)
{
	if (!multiple)
		return requestedPath;
	const QFileInfo info(requestedPath);
	const QString suffix = info.suffix().isEmpty() ? QStringLiteral("edl") : info.suffix();
	const QString stem = info.completeBaseName().isEmpty() ? QStringLiteral("replay-markers")
							       : info.completeBaseName();
	return info.dir().filePath(QStringLiteral("%1-session-%2-run-%3.%4")
					   .arg(stem)
					   .arg(document.sessionId)
					   .arg(document.recordingRun)
					   .arg(suffix));
}

bool writeAtomically(const QString &path, const QByteArray &data, QString &error)
{
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly)) {
		error = file.errorString();
		return false;
	}
	if (file.write(data) != data.size()) {
		error = file.errorString();
		file.cancelWriting();
		return false;
	}
	if (!file.commit()) {
		error = file.errorString();
		return false;
	}
	return true;
}
} // namespace

bool EditorFrameRate::isValid() const
{
	return numerator > 0 && denominator > 0 && numerator <= 240'000 && denominator <= 10'000;
}

int EditorFrameRate::nominalFramesPerSecond() const
{
	if (!isValid())
		return 0;
	return static_cast<int>((static_cast<std::int64_t>(numerator) + denominator / 2) / denominator);
}

bool EditorFrameRate::usesDropFrameTimecode() const
{
	return (numerator == 30'000 || numerator == 60'000) && denominator == 1'001;
}

EditorExportResult createResolveMarkerEdls(const std::vector<CsvRow> &rows, const EditorExportOptions &options)
{
	EditorExportResult result;
	if (!options.frameRate.isValid() || options.timelineStartHours < 0 || options.timelineStartHours > 23) {
		result.error = QStringLiteral("Invalid editor frame rate or timeline start time.");
		return result;
	}
	const MarkerGroups groups = collectMarkers(rows, result.skippedReplayCount);
	if (groups.empty()) {
		result.error = QStringLiteral("No replay markers are mapped to a recording in the selected session.");
		return result;
	}

	for (const auto &[key, markers] : groups) {
		if (markers.size() > 999) {
			result.error =
				QStringLiteral("Resolve EDL export supports at most 999 markers per recording run.");
			result.documents.clear();
			result.markerCount = 0;
			return result;
		}
		QString output = QStringLiteral("TITLE: OBS Replay Timeline - Session %1 Run %2\r\nFCM: %3\r\n\r\n")
					 .arg(key.sessionId)
					 .arg(key.recordingRun)
					 .arg(options.frameRate.usesDropFrameTimecode()
						      ? QStringLiteral("DROP FRAME")
						      : QStringLiteral("NON-DROP FRAME"));
		int event = 1;
		for (const Marker &marker : markers) {
			const std::int64_t markerStart = floorFrame(marker.startNs, options.frameRate);
			const std::int64_t markerEnd =
				std::max(markerStart + 1, ceilFrame(marker.endNs, options.frameRate));
			const QString in =
				timecodeFromFrame(timelineStartFrame(options) + markerStart, options.frameRate);
			const QString out =
				timecodeFromFrame(timelineStartFrame(options) + markerStart + 1, options.frameRate);
			output += QStringLiteral("%1  001      V     C        %2 %3 %2 %3\r\n")
					  .arg(event++, 3, 10, QLatin1Char('0'))
					  .arg(in, out);
			QString label = markerName(marker);
			const QString comment = oneLine(markerComment(marker));
			if (!comment.isEmpty())
				label += QStringLiteral(" - ") + comment;
			output += QStringLiteral(" |C:ResolveColor%1 |M:%2 |D:%3\r\n\r\n")
					  .arg(resolveColor(marker.tag), oneLine(label))
					  .arg(markerEnd - markerStart);
		}
		result.markerCount += static_cast<int>(markers.size());
		result.documents.push_back({key.sessionId, key.recordingRun, output.toUtf8()});
	}
	return result;
}

EditorExportResult createPremiereMarkerXml(const std::vector<CsvRow> &rows, const EditorExportOptions &options)
{
	EditorExportResult result;
	if (!options.frameRate.isValid() || options.timelineStartHours < 0 || options.timelineStartHours > 23) {
		result.error = QStringLiteral("Invalid editor frame rate or timeline start time.");
		return result;
	}
	const MarkerGroups groups = collectMarkers(rows, result.skippedReplayCount);
	if (groups.empty()) {
		result.error = QStringLiteral("No replay markers are mapped to a recording in the selected session.");
		return result;
	}

	QByteArray data;
	QXmlStreamWriter writer(&data);
	writer.setAutoFormatting(true);
	writer.writeStartDocument(QStringLiteral("1.0"));
	writer.writeDTD(QStringLiteral("<!DOCTYPE xmeml>"));
	writer.writeStartElement(QStringLiteral("xmeml"));
	writer.writeAttribute(QStringLiteral("version"), QStringLiteral("5"));
	writer.writeStartElement(QStringLiteral("project"));
	writer.writeTextElement(QStringLiteral("name"), QStringLiteral("OBS Replay Timeline Markers"));
	writer.writeStartElement(QStringLiteral("children"));
	for (const auto &[key, markers] : groups) {
		writePremiereSequence(writer, key, markers, options);
		result.markerCount += static_cast<int>(markers.size());
	}
	writer.writeEndElement();
	writer.writeEndElement();
	writer.writeEndElement();
	writer.writeEndDocument();
	result.documents.push_back({0, 0, data});
	return result;
}

bool writeEditorExport(const QString &path, EditorExportFormat format, const std::vector<CsvRow> &rows,
		       const EditorExportOptions &options, QStringList &writtenPaths, int &markerCount,
		       int &skippedReplayCount, QString &error)
{
	writtenPaths.clear();
	markerCount = 0;
	skippedReplayCount = 0;
	const EditorExportResult result = format == EditorExportFormat::DaVinciResolveEdl
						  ? createResolveMarkerEdls(rows, options)
						  : createPremiereMarkerXml(rows, options);
	if (!result.error.isEmpty()) {
		error = result.error;
		return false;
	}

	const bool multiple = result.documents.size() > 1;
	QStringList paths;
	for (const EditorExportDocument &document : result.documents) {
		const QString outputPath = documentPath(path, document, multiple);
		if (multiple && QFileInfo::exists(outputPath)) {
			error = QStringLiteral("Resolve export would overwrite an existing file: %1").arg(outputPath);
			return false;
		}
		paths.push_back(outputPath);
	}
	for (std::size_t index = 0; index < result.documents.size(); ++index) {
		if (!writeAtomically(paths[static_cast<int>(index)], result.documents[index].data, error))
			return false;
		writtenPaths.push_back(paths[static_cast<int>(index)]);
	}
	markerCount = result.markerCount;
	skippedReplayCount = result.skippedReplayCount;
	return true;
}

} // namespace replay_timeline
