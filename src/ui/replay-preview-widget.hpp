#pragma once

#include <QString>
#include <QWidget>

#include <obs.h>

class QLabel;
class QHideEvent;
class QPushButton;
class QSlider;
class QTimer;

namespace replay_timeline {

class ReplayVideoSurface;

class ReplayPreviewWidget final : public QWidget {
public:
	explicit ReplayPreviewWidget(QWidget *parent = nullptr);
	~ReplayPreviewWidget() override;

	ReplayPreviewWidget(const ReplayPreviewWidget &) = delete;
	ReplayPreviewWidget &operator=(const ReplayPreviewWidget &) = delete;

	void loadReplay(const QString &path);
	void clearReplay();

protected:
	void hideEvent(QHideEvent *event) override;

private:
	void releaseSource();
	void togglePlayback();
	void updatePlaybackState();
	void applyAudioState();
	void updateMuteButton();
	void setControlsEnabled(bool enabled);

	ReplayVideoSurface *videoSurface_ = nullptr;
	QLabel *pathLabel_ = nullptr;
	QLabel *statusLabel_ = nullptr;
	QLabel *timeLabel_ = nullptr;
	QPushButton *playPauseButton_ = nullptr;
	QPushButton *stopButton_ = nullptr;
	QPushButton *muteButton_ = nullptr;
	QSlider *seekSlider_ = nullptr;
	QTimer *playbackTimer_ = nullptr;
	obs_source_t *source_ = nullptr;
	QString replayPath_;
	bool sourceActive_ = false;
	bool sourceShowing_ = false;
	bool seeking_ = false;
	bool muted_ = true;
};

} // namespace replay_timeline
