#include <QApplication>
#include <QStyleFactory>

#include <clocale>

#include "mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    std::setlocale(LC_NUMERIC, "C");

    app.setApplicationName("NovaPlayer");
    app.setOrganizationName("NovaPlayer");
    app.setStyle(QStyleFactory::create("Fusion"));

    app.setStyleSheet(R"QSS(
        QMainWindow {
            background: #050609;
        }

        QWidget {
            background: transparent;
            color: #f7f8fb;
            font-family: "Segoe UI";
            font-size: 10pt;
        }

        QWidget#root {
            background: #050609;
        }

        QWidget#videoSurface {
            background: #000000;
        }

        QWidget#topBar,
        QWidget#sideRail,
        QWidget#controlDeck,
        QWidget#qualityBadge {
            background-color: rgba(15, 18, 26, 235);
            border: 1px solid rgba(255, 255, 255, 30);
            border-radius: 18px;
        }

        QWidget#controlDeck {
            border-radius: 22px;
        }

        QWidget#settingsPanel,
        QWidget#chapterPill {
            background-color: rgba(255, 255, 255, 10);
            border: 1px solid rgba(255, 255, 255, 18);
            border-radius: 13px;
        }

        QLabel#brandMark {
            color: #78f5e7;
            font-weight: 800;
            letter-spacing: 2px;
        }

        QLabel#eyebrow,
        QLabel#centerKicker,
        QLabel#chapterKicker {
            color: rgba(120, 245, 231, 180);
            font-size: 8pt;
            font-weight: 700;
        }

        QLabel#mediaTitle {
            color: #f7f8fb;
            font-size: 11pt;
            font-weight: 600;
        }

        QLabel#mutedLabel,
        QLabel#timelineLabel {
            color: rgba(235, 239, 249, 120);
            font-size: 8pt;
        }

        QLabel#timecode {
            color: rgba(255, 255, 255, 205);
            font-family: "Consolas";
            font-size: 9pt;
        }

        QLabel#qualityPrimary {
            color: #f7f8fb;
            font-size: 8pt;
            font-weight: 800;
        }

        QLabel#qualitySecondary {
            color: rgba(255, 255, 255, 155);
            font-size: 8pt;
            font-weight: 700;
        }

        QLabel#statusDot {
            color: #78f5e7;
            font-size: 11pt;
        }

        QLabel#chapterIndex {
            color: #a7fff6;
            background-color: rgba(120, 245, 231, 16);
            border: 1px solid rgba(120, 245, 231, 32);
            border-radius: 9px;
            padding: 5px 7px;
            font-size: 8pt;
            font-weight: 800;
        }

        QLabel#chapterTitle {
            color: rgba(255, 255, 255, 220);
            font-size: 9pt;
            font-weight: 600;
        }

        QLabel#wave {
            color: rgba(120, 245, 231, 135);
            font-family: "Consolas";
            font-size: 9pt;
        }

        QPushButton {
            outline: none;
        }

        QPushButton[uiRole="icon"],
        QPushButton[uiRole="rail"],
        QPushButton[uiRole="text"],
        QComboBox {
            color: rgba(255, 255, 255, 205);
            background-color: rgba(255, 255, 255, 9);
            border: 1px solid rgba(255, 255, 255, 19);
            border-radius: 11px;
            padding: 6px 9px;
        }

        QPushButton[uiRole="icon"] {
            min-width: 30px;
            min-height: 30px;
            font-weight: 700;
        }

        QPushButton[uiRole="rail"] {
            min-width: 38px;
            min-height: 38px;
            font-weight: 800;
        }

        QPushButton[uiRole="text"] {
            min-height: 30px;
            font-weight: 600;
        }

        QPushButton[uiRole="strong"] {
            color: #04110e;
            background-color: #8ff8e9;
            border: 1px solid #d4fff9;
            border-radius: 13px;
            min-width: 36px;
            min-height: 36px;
            font-size: 13pt;
            font-weight: 900;
        }

        QPushButton[uiRole="playCore"] {
            color: #04110e;
            background-color: #8ff8e9;
            border: 2px solid #d4fff9;
            border-radius: 34px;
            min-width: 64px;
            max-width: 64px;
            min-height: 64px;
            max-height: 64px;
            font-size: 20pt;
            font-weight: 900;
        }

        QPushButton[uiRole="icon"]:hover,
        QPushButton[uiRole="rail"]:hover,
        QPushButton[uiRole="text"]:hover,
        QComboBox:hover {
            color: #ffffff;
            background-color: rgba(255, 255, 255, 20);
            border-color: rgba(120, 245, 231, 75);
        }

        QPushButton[active="true"] {
            color: #a7fff6;
            background-color: rgba(120, 245, 231, 18);
            border-color: rgba(120, 245, 231, 48);
        }

        QPushButton:disabled {
            color: rgba(255, 255, 255, 55);
            background-color: rgba(255, 255, 255, 4);
            border-color: rgba(255, 255, 255, 10);
        }

        QComboBox {
            min-height: 28px;
            padding-right: 22px;
        }

        QComboBox::drop-down {
            border: none;
            width: 20px;
        }

        QComboBox QAbstractItemView {
            background: #11141b;
            color: #f7f8fb;
            border: 1px solid #303642;
            selection-background-color: #263e3c;
            selection-color: #a7fff6;
            padding: 4px;
        }

        QSlider#timelineSlider::groove:horizontal {
            height: 5px;
            background: rgba(255, 255, 255, 24);
            border-radius: 2px;
        }

        QSlider#timelineSlider::sub-page:horizontal {
            background: #78f5e7;
            border-radius: 2px;
        }

        QSlider#timelineSlider::handle:horizontal {
            width: 13px;
            margin: -5px 0;
            border-radius: 6px;
            background: #ddfff9;
            border: 2px solid #63cabe;
        }

        QSlider#volumeSlider::groove:horizontal {
            height: 3px;
            background: rgba(255, 255, 255, 25);
            border-radius: 2px;
        }

        QSlider#volumeSlider::sub-page:horizontal {
            background: #78f5e7;
            border-radius: 2px;
        }

        QSlider#volumeSlider::handle:horizontal {
            width: 10px;
            margin: -4px 0;
            border-radius: 5px;
            background: #d9fff9;
        }

        QToolTip {
            color: #f7f8fb;
            background: #11141b;
            border: 1px solid #353b46;
            padding: 5px;
        }
    )QSS");

    MainWindow window;
    window.resize(1180, 720);
    window.show();

    if (argc > 1) {
        window.openPath(QString::fromLocal8Bit(argv[1]));
    }

    return app.exec();
}
