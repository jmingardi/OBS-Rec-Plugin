#include "csv-exporter.hpp"

#include <QFile>

namespace replay_timeline {
namespace {
QString quote(const QString &value)
{
	QString escaped = value;
	escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
	return QStringLiteral("\"") + escaped + QLatin1Char('"');
}

QString timecode(std::int64_t nanoseconds)
{
	if (nanoseconds < 0)
		return {};
	const std::int64_t totalMilliseconds = nanoseconds / 1'000'000;
	return QStringLiteral("%1:%2:%3.%4")
		.arg(totalMilliseconds / 3'600'000, 2, 10, QLatin1Char('0'))
		.arg((totalMilliseconds / 60'000) % 60, 2, 10, QLatin1Char('0'))
		.arg((totalMilliseconds / 1000) % 60, 2, 10, QLatin1Char('0'))
		.arg(totalMilliseconds % 1000, 3, 10, QLatin1Char('0'));
}
} // namespace

QByteArray createCsv(const std::vector<CsvRow> &rows)
{
	QString output =
		QStringLiteral("session_id,replay_id,saved_utc,recording_run,recording_segment,recording_start,"
			       "recording_end,tag,note,replay_duration,replay_path,recording_path,mapping_confidence,"
			       "probe_status,mapping_reason\r\n");
	for (const CsvRow &row : rows) {
		output += QString::number(row.sessionId) + QLatin1Char(',') + QString::number(row.replayId) +
			  QLatin1Char(',') + quote(row.savedUtc) + QLatin1Char(',');
		if (row.recordingRun > 0)
			output += QString::number(row.recordingRun);
		output += QLatin1Char(',');
		if (row.recordingSegment > 0)
			output += QString::number(row.recordingSegment);
		output += QLatin1Char(',') + timecode(row.recordingStartNs) + QLatin1Char(',') +
			  timecode(row.recordingEndNs) + QLatin1Char(',') + quote(row.tag) + QLatin1Char(',') +
			  quote(row.note) + QLatin1Char(',') + timecode(row.durationNs) + QLatin1Char(',') +
			  quote(row.replayPath) + QLatin1Char(',') + quote(row.recordingPath) + QLatin1Char(',') +
			  quote(row.confidence) + QLatin1Char(',') + quote(row.probeStatus) + QLatin1Char(',') +
			  quote(row.reason) + QStringLiteral("\r\n");
	}
	return output.toUtf8();
}

bool writeCsv(const QString &path, const std::vector<CsvRow> &rows, QString &error)
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		error = file.errorString();
		return false;
	}
	const QByteArray data = createCsv(rows);
	if (file.write(data) != data.size()) {
		error = file.errorString();
		return false;
	}
	return true;
}

} // namespace replay_timeline
