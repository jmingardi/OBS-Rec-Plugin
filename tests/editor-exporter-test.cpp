#include "export/editor-exporter.hpp"

#include <cstdlib>
#include <iostream>
#include <utility>

#include <QFile>
#include <QTemporaryDir>
#include <QXmlStreamReader>

using replay_timeline::CsvRow;
using replay_timeline::EditorExportFormat;
using replay_timeline::EditorExportOptions;
using replay_timeline::EditorFrameRate;
using replay_timeline::createPremiereMarkerXml;
using replay_timeline::createResolveMarkerEdls;
using replay_timeline::writeEditorExport;

namespace {
void expect(bool condition, const char *message)
{
	if (!condition) {
		std::cerr << "FAILED: " << message << '\n';
		std::exit(EXIT_FAILURE);
	}
}

CsvRow row(std::int64_t replayId, std::int64_t run, std::int64_t start, std::int64_t end, std::int64_t segment,
	   QString tag = QStringLiteral("Bug"))
{
	CsvRow value;
	value.sessionId = 4;
	value.replayId = replayId;
	value.recordingRun = run;
	value.recordingSegment = segment;
	value.runStartNs = start;
	value.runEndNs = end;
	value.segmentStartNs = start;
	value.segmentEndNs = end;
	value.tag = std::move(tag);
	value.note = QStringLiteral("quote <tag> & newline\nkept");
	value.rating = 5;
	value.applicationName = QStringLiteral("game.exe");
	value.replayPath = QStringLiteral("C:/replays/ação & fun.mp4");
	value.confidence = QStringLiteral("approximate");
	return value;
}
} // namespace

int main()
{
	std::vector<CsvRow> rows;
	rows.push_back(row(7, 1, 1'234'000'000, 1'500'000'000, 1));
	rows.push_back(row(7, 1, 1'500'000'000, 2'000'000'000, 2));
	rows.push_back(row(8, 2, 0, 1'000'000'000, 1, QStringLiteral("Keep")));
	CsvRow unmapped = row(9, 0, -1, -1, 0, QStringLiteral("External"));
	rows.push_back(unmapped);

	const EditorExportOptions ntsc{{30'000, 1'001}, 1};
	expect(ntsc.frameRate.isValid() && ntsc.frameRate.nominalFramesPerSecond() == 30 &&
		       ntsc.frameRate.usesDropFrameTimecode(),
	       "29.97 fps selects nominal 30 fps drop-frame timecode");
	const auto resolve = createResolveMarkerEdls(rows, ntsc);
	expect(resolve.error.isEmpty() && resolve.documents.size() == 2, "Resolve creates one EDL per recording run");
	expect(resolve.markerCount == 2 && resolve.skippedReplayCount == 1,
	       "split spans collapse into one marker and unmapped replays are counted once");
	expect(resolve.documents[0].data.contains("FCM: DROP FRAME"), "Resolve declares drop-frame timecode");
	expect(resolve.documents[0].data.contains("01:00:01;06 01:00:01;07"),
	       "Resolve marker uses frame-accurate one-hour-offset drop-frame timecode");
	expect(resolve.documents[0].data.contains("|D:24"), "Resolve duration covers the aggregated split replay");
	expect(resolve.documents[0].data.contains("ResolveColorPurple"), "Resolve maps known tags to marker colors");
	expect(!resolve.documents[0].data.contains("\nkept"), "Resolve marker metadata is normalized to one line");
	const auto twentyThreeNinetyEight =
		createResolveMarkerEdls({row(10, 1, 0, 1'000'000'000, 1)}, {{24'000, 1'001}, 1});
	expect(twentyThreeNinetyEight.documents.front().data.contains("01:00:00:00 01:00:00:01"),
	       "23.976 fps uses nominal frame numbering for an exact one-hour start timecode");

	const EditorExportOptions sixty{{60, 1}, 0};
	const auto premiere = createPremiereMarkerXml(rows, sixty);
	expect(premiere.error.isEmpty() && premiere.documents.size() == 1 && premiere.markerCount == 2,
	       "Premiere combines recording runs into one XML project");
	const QByteArray xml = premiere.documents.front().data;
	expect(xml.contains("<!DOCTYPE xmeml>"), "Premiere output is Final Cut Pro 7 XML");
	expect(xml.count("<sequence id=") == 2, "Premiere XML contains one sequence per recording run");
	expect(xml.contains("<in>74</in>") && xml.contains("<out>120</out>"),
	       "Premiere marker frames aggregate split spans with floor/ceil coverage");
	expect(xml.contains("quote &lt;tag&gt; &amp; newline"), "Premiere XML escapes Unicode marker metadata");
	expect(xml.contains("<displayformat>NDF</displayformat>"), "integer frame rates use non-drop display");
	QXmlStreamReader xmlReader(xml);
	while (!xmlReader.atEnd())
		xmlReader.readNext();
	expect(!xmlReader.hasError(), "Premiere output is well-formed XML");

	QTemporaryDir directory;
	expect(directory.isValid(), "temporary export directory is available");
	QStringList writtenPaths;
	QString error;
	int markerCount = 0;
	int skippedReplayCount = 0;
	const QString resolvePath = directory.filePath(QStringLiteral("markers.edl"));
	expect(writeEditorExport(resolvePath, EditorExportFormat::DaVinciResolveEdl, rows, ntsc, writtenPaths,
				 markerCount, skippedReplayCount, error),
	       "multi-run Resolve export writes its document set");
	expect(writtenPaths.size() == 2 && QFile::exists(writtenPaths[0]) && QFile::exists(writtenPaths[1]),
	       "multi-run Resolve files receive deterministic run suffixes");
	expect(!writeEditorExport(resolvePath, EditorExportFormat::DaVinciResolveEdl, rows, ntsc, writtenPaths,
				  markerCount, skippedReplayCount, error) &&
		       error.contains(QStringLiteral("overwrite")),
	       "multi-run export refuses to overwrite an unconfirmed sibling file");
	const QString premierePath = directory.filePath(QStringLiteral("markers.xml"));
	expect(writeEditorExport(premierePath, EditorExportFormat::AdobePremiereXml, rows, sixty, writtenPaths,
				 markerCount, skippedReplayCount, error) &&
		       writtenPaths.size() == 1 && QFile::exists(premierePath),
	       "Premiere export writes one atomic XML project");

	const auto invalid = createResolveMarkerEdls(rows, {{0, 1}, 1});
	expect(!invalid.error.isEmpty() && invalid.documents.empty(), "invalid frame rates are rejected");
	const auto noMarkers = createPremiereMarkerXml({unmapped}, sixty);
	expect(!noMarkers.error.isEmpty() && noMarkers.documents.empty(), "unmapped-only exports fail clearly");

	std::cout << "Editor exporter tests passed\n";
	return EXIT_SUCCESS;
}
