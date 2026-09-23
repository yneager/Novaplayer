#pragma once

#include <QString>
#include <QWidget>

class QWebEngineView;

class HomePage final : public QWidget
{
    Q_OBJECT

public:
    explicit HomePage(QWidget *parent = nullptr);
    void setCurrentMedia(const QString &displayName, bool available);

signals:
    void openVideoRequested();
    void resumeRequested();
    void openPathRequested(const QString &path);

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void updateSessionCard();

    QWebEngineView *view_ = nullptr;
    QString currentMediaName_;
    bool currentMediaAvailable_ = false;
    bool pageLoaded_ = false;
};
