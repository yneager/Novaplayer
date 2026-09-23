#pragma once

#include <QString>
#include <QWidget>

class QAbstractButton;
class QFrame;
class QScrollArea;
class QWidget;

class HomePage final : public QWidget
{
    Q_OBJECT

public:
    explicit HomePage(QWidget *parent = nullptr);

    void setCurrentMedia(const QString &displayName, bool available);

signals:
    void openVideoRequested();
    void resumeRequested();

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    QScrollArea *scroll_ = nullptr;
    QFrame *nav_ = nullptr;
    QWidget *navLinks_ = nullptr;
    QAbstractButton *resumeCard_ = nullptr;
    bool currentMediaAvailable_ = false;
    QString currentMediaName_;
};
