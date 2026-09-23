#include <QApplication>
#include <QStyleFactory>
#include <clocale>
#include "mainwindow.h"
int main(int argc,char *argv[]){QApplication app(argc,argv);std::setlocale(LC_NUMERIC,"C");app.setApplicationName("NovaPlayer");app.setOrganizationName("NovaPlayer");app.setStyle(QStyleFactory::create("Fusion"));app.setStyleSheet(R"(QMainWindow,QWidget{background:#15171b;color:#e9edf2}QPushButton,QComboBox{background:#252932;border:1px solid #353b46;border-radius:6px;padding:6px 10px;color:#e9edf2}QPushButton:hover,QComboBox:hover{background:#303642}QSlider::groove:horizontal{height:5px;background:#303642;border-radius:2px}QSlider::handle:horizontal{width:14px;margin:-5px 0;border-radius:7px;background:#e9edf2}QSlider::sub-page:horizontal{background:#8b9cff;border-radius:2px}QLabel{color:#d7dce3})");MainWindow w;w.resize(1100,700);w.show();if(argc>1)w.openPath(QString::fromLocal8Bit(argv[1]));return app.exec();}
