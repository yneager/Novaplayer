#pragma once
#include <QMainWindow>
#include <QStringList>
#include <mpv/client.h>
class QWidget;class QPushButton;class QSlider;class QLabel;class QComboBox;class QDragEnterEvent;class QDropEvent;class QKeyEvent;
class MainWindow final:public QMainWindow{Q_OBJECT
public:explicit MainWindow(QWidget *parent=nullptr);~MainWindow() override;void openPath(const QString &path);
signals:void mpvWakeup();
protected:void dragEnterEvent(QDragEnterEvent *event) override;void dropEvent(QDropEvent *event) override;void keyPressEvent(QKeyEvent *event) override;
private slots:void openFile();void togglePause();void toggleMute();void toggleFullscreen();void processMpvEvents();void seekReleased();void volumeChanged(int value);void speedChanged(int index);
private:static void wakeup(void *ctx);void buildUi();void initMpv();void handleEvent(mpv_event *event);void setMpvPropertyFlag(const char *name,bool value);void setMpvPropertyDouble(const char *name,double value);void command(const QStringList &args);void updateTimeLabel();static QString formatTime(double seconds);
mpv_handle *mpv_=nullptr;QWidget *video_=nullptr;QPushButton *playButton_=nullptr,*muteButton_=nullptr,*fullscreenButton_=nullptr;QSlider *seek_=nullptr,*volume_=nullptr;QLabel *timeLabel_=nullptr;QComboBox *speed_=nullptr;double position_=0.0,duration_=0.0;bool paused_=false,muted_=false,seeking_=false;};
