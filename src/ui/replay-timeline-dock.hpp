#pragma once

#include "persistence/repository.hpp"

#include <cstdint>
#include <functional>
#include <vector>

#include <QWidget>

class QComboBox;
class QLabel;
class QPlainTextEdit;
class QSortFilterProxyModel;
class QStandardItem;
class QStandardItemModel;
class QTableView;

namespace replay_timeline {

class ReplayTimelineDock final : public QWidget {
public:
	struct Callbacks {
		std::function<void(std::int64_t)> sessionSelected;
		std::function<void(std::int64_t, const QString &, const QString &)> replayEdited;
		std::function<void(const QString &)> exportCsv;
		std::function<void(const QStringList &)> tagsConfigured;
		std::function<void(std::int64_t)> retryProbe;
		std::function<void()> refreshRequested;
	};

	explicit ReplayTimelineDock(QWidget *parent = nullptr);

	void appendDiagnostic(const QString &eventName, const QString &detail, quint64 monotonicNanoseconds);
	void setOutputState(bool recordingActive, bool recordingPaused, bool replayBufferActive);
	void setCallbacks(Callbacks callbacks);
	void setSessions(const std::vector<SessionSummary> &sessions, std::int64_t selectedSessionId);
	void setReplayRows(const std::vector<ReplayRow> &rows);
	void setTagNames(const QStringList &tags);
	void showMessage(const QString &message, bool error = false);

private:
	void handleItemChanged(QStandardItem *changedItem);

	QLabel *recordingStatus_ = nullptr;
	QLabel *replayStatus_ = nullptr;
	QLabel *message_ = nullptr;
	QComboBox *sessionSelector_ = nullptr;
	QStandardItemModel *replayModel_ = nullptr;
	QSortFilterProxyModel *replayProxy_ = nullptr;
	QTableView *replayTable_ = nullptr;
	QPlainTextEdit *diagnostics_ = nullptr;
	Callbacks callbacks_;
	QStringList tagNames_;
	bool loadingRows_ = false;
};

} // namespace replay_timeline
