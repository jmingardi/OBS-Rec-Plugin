#pragma once

#include "persistence/repository.hpp"

#include <cstdint>
#include <vector>

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace replay_timeline {

enum class EditorExportFormat {
	DaVinciResolveEdl,
	AdobePremiereXml,
};

struct EditorFrameRate {
	int numerator = 60;
	int denominator = 1;

	bool isValid() const;
	int nominalFramesPerSecond() const;
	bool usesDropFrameTimecode() const;
};

struct EditorExportOptions {
	EditorFrameRate frameRate;
	int timelineStartHours = 1;
};

struct EditorExportDocument {
	std::int64_t sessionId = 0;
	std::int64_t recordingRun = 0;
	QByteArray data;
};

struct EditorExportResult {
	std::vector<EditorExportDocument> documents;
	int markerCount = 0;
	int skippedReplayCount = 0;
	QString error;
};

EditorExportResult createResolveMarkerEdls(const std::vector<CsvRow> &rows, const EditorExportOptions &options);
EditorExportResult createPremiereMarkerXml(const std::vector<CsvRow> &rows, const EditorExportOptions &options);

bool writeEditorExport(const QString &path, EditorExportFormat format, const std::vector<CsvRow> &rows,
		       const EditorExportOptions &options, QStringList &writtenPaths, int &markerCount,
		       int &skippedReplayCount, QString &error);

} // namespace replay_timeline
