#pragma once

#include "persistence/repository.hpp"

#include <cstdint>
#include <functional>
#include <vector>

#include <QList>
#include <QWidget>

class QComboBox;
class QDialog;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSettings;
class QSplitter;
class QSortFilterProxyModel;
class QStandardItem;
class QStandardItemModel;
class QTableView;
class QTimer;

namespace replay_timeline {

class ReplayPreviewWidget;

class ReplayTimelineDock final : public QWidget {
public:
	struct Callbacks {
		std::function<void(std::int64_t)> sessionSelected;
		std::function<void(std::int64_t, const QString &, const QString &, int)> replayEdited;
		std::function<void(const QString &, bool)> exportCsv;
		std::function<void(const QStringList &)> tagsConfigured;
		std::function<void(std::int64_t)> retryProbe;
		std::function<void()> refreshRequested;
		std::function<void()> clearSessionsRequested;
	};

	explicit ReplayTimelineDock(QWidget *parent = nullptr);

	void appendDiagnostic(const QString &eventName, const QString &detail, quint64 monotonicNanoseconds);
	void setOutputState(bool recordingActive, bool recordingPaused, bool replayBufferActive);
	void setCallbacks(Callbacks callbacks);
	void setSessions(const std::vector<SessionSummary> &sessions, std::int64_t selectedSessionId);
	void setReplayRows(const std::vector<ReplayRow> &rows);
	void setReplayThumbnail(std::int64_t replayId, const QString &replayPath, const QString &thumbnailPath,
				const QString &error);
	void clearPreview();
	void setTagNames(const QStringList &tags);
	void showDiskRefreshActivity();
	void setDiskStatus(const QString &message, bool warning);
	void showMessage(const QString &message, bool error = false);

private:
	void handleItemChanged(QStandardItem *changedItem);
	void setPreviewFocusMode(bool enabled);

	QWidget *statusBar_ = nullptr;
	QWidget *toolbar_ = nullptr;
	QWidget *searchBar_ = nullptr;
	QLabel *recordingStatus_ = nullptr;
	QLabel *replayStatus_ = nullptr;
	QLabel *diskStatus_ = nullptr;
	QLabel *diskRefreshActivity_ = nullptr;
	QTimer *diskRefreshActivityTimer_ = nullptr;
	QLabel *message_ = nullptr;
	QComboBox *sessionSelector_ = nullptr;
	QPushButton *clearSessionsButton_ = nullptr;
	QStandardItemModel *replayModel_ = nullptr;
	QSortFilterProxyModel *replayProxy_ = nullptr;
	QTableView *replayTable_ = nullptr;
	QSplitter *contentSplitter_ = nullptr;
	ReplayPreviewWidget *preview_ = nullptr;
	QPushButton *previewFocusButton_ = nullptr;
	QDialog *diagnosticsDialog_ = nullptr;
	QPlainTextEdit *diagnostics_ = nullptr;
	QSettings *uiSettings_ = nullptr;
	Callbacks callbacks_;
	QStringList tagNames_;
	QList<int> normalSplitterSizes_;
	bool loadingRows_ = false;
	bool previewFocusMode_ = false;
};

} // namespace replay_timeline
