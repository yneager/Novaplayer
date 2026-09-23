#include "mainwindow.h"
#include <QComboBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMimeData>
#include <QPushButton>
#include <QSlider>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <stdexcept>
#include <vector>
MainWindow::MainWindow(QWidget *p):QMainWindow(p){setWindowTitle("NovaPlayer");setAcceptDrops(true);buildUi();initMpv();connect(this,&MainWindow::mpvWakeup,this,&MainWindow::processMpvEvents,Qt::QueuedConnection);}
MainWindow::~MainWindow(){if(mpv_){mpv_set_wakeup_callback(mpv_,nullptr,nullptr);mpv_terminate_destroy(mpv_);}}
void MainWindow::buildUi(){auto*r=new QWidget(this);auto*l=new QVBoxLayout(r);l->setContentsMargins(0,0,0,10);l->setSpacing(8);video_=new QWidget(r);video_->setAttribute(Qt::WA_NativeWindow);video_->setAttribute(Qt::WA_DontCreateNativeAncestors);video_->setStyleSheet("background:black;");video_->setMinimumSize(640,360);l->addWidget(video_,1);seek_=new QSlider(Qt::Horizontal,r);seek_->setRange(0,1000);connect(seek_,&QSlider::sliderPressed,this,[this]{seeking_=true;});connect(seek_,&QSlider::sliderReleased,this,&MainWindow::seekReleased);l->addWidget(seek_);auto*c=new QHBoxLayout;c->setContentsMargins(10,0,10,0);auto*o=new QPushButton("Open",r);playButton_=new QPushButton("Play",r);muteButton_=new QPushButton("Mute",r);fullscreenButton_=new QPushButton("Fullscreen",r);timeLabel_=new QLabel("00:00 / 00:00",r);volume_=new QSlider(Qt::Horizontal,r);volume_->setRange(0,100);volume_->setValue(80);volume_->setMaximumWidth(140);speed_=new QComboBox(r);speed_->addItems({"0.50x","0.75x","1.00x","1.25x","1.50x","2.00x"});speed_->setCurrentIndex(2);c->addWidget(o);c->addWidget(playButton_);c->addWidget(timeLabel_);c->addStretch();c->addWidget(new QLabel("Speed",r));c->addWidget(speed_);c->addWidget(muteButton_);c->addWidget(volume_);c->addWidget(fullscreenButton_);l->addLayout(c);setCentralWidget(r);connect(o,&QPushButton::clicked,this,&MainWindow::openFile);connect(playButton_,&QPushButton::clicked,this,&MainWindow::togglePause);connect(muteButton_,&QPushButton::clicked,this,&MainWindow::toggleMute);connect(fullscreenButton_,&QPushButton::clicked,this,&MainWindow::toggleFullscreen);connect(volume_,&QSlider::valueChanged,this,&MainWindow::volumeChanged);connect(speed_,&QComboBox::currentIndexChanged,this,&MainWindow::speedChanged);}
void MainWindow::initMpv(){mpv_=mpv_create();if(!mpv_)throw std::runtime_error("Could not create libmpv context.");int64_t wid=static_cast<int64_t>(video_->winId());mpv_set_option(mpv_,"wid",MPV_FORMAT_INT64,&wid);mpv_set_option_string(mpv_,"hwdec","auto-safe");mpv_set_option_string(mpv_,"keep-open","yes");mpv_set_option_string(mpv_,"osc","no");mpv_set_option_string(mpv_,"input-default-bindings","no");if(mpv_initialize(mpv_)<0)throw std::runtime_error("Could not initialize libmpv.");mpv_observe_property(mpv_,1,"time-pos",MPV_FORMAT_DOUBLE);mpv_observe_property(mpv_,2,"duration",MPV_FORMAT_DOUBLE);mpv_observe_property(mpv_,3,"pause",MPV_FORMAT_FLAG);mpv_observe_property(mpv_,4,"mute",MPV_FORMAT_FLAG);mpv_set_wakeup_callback(mpv_,&MainWindow::wakeup,this);setMpvPropertyDouble("volume",80);}
void MainWindow::wakeup(void*c){emit static_cast<MainWindow*>(c)->mpvWakeup();}
void MainWindow::processMpvEvents(){if(!mpv_)return;for(;;){auto*e=mpv_wait_event(mpv_,0);if(!e||e->event_id==MPV_EVENT_NONE)break;handleEvent(e);}}
void MainWindow::handleEvent(mpv_event*e){if(e->event_id==MPV_EVENT_PROPERTY_CHANGE){auto*p=static_cast<mpv_event_property*>(e->data);if(!p||!p->data)return;QString n=QString::fromUtf8(p->name);if(n=="time-pos"&&p->format==MPV_FORMAT_DOUBLE){position_=*static_cast<double*>(p->data);if(!seeking_&&duration_>0)seek_->setValue(int(position_/duration_*1000));updateTimeLabel();}else if(n=="duration"&&p->format==MPV_FORMAT_DOUBLE){duration_=*static_cast<double*>(p->data);updateTimeLabel();}else if(n=="pause"&&p->format==MPV_FORMAT_FLAG){paused_=*static_cast<int*>(p->data)!=0;playButton_->setText(paused_?"Play":"Pause");}else if(n=="mute"&&p->format==MPV_FORMAT_FLAG){muted_=*static_cast<int*>(p->data)!=0;muteButton_->setText(muted_?"Unmute":"Mute");}}else if(e->event_id==MPV_EVENT_FILE_LOADED){paused_=false;playButton_->setText("Pause");}else if(e->event_id==MPV_EVENT_END_FILE)playButton_->setText("Play");}
void MainWindow::command(const QStringList&a){if(!mpv_)return;std::vector<QByteArray>u;std::vector<const char*>v;for(const auto&x:a)u.push_back(x.toUtf8());for(auto&x:u)v.push_back(x.constData());v.push_back(nullptr);mpv_command_async(mpv_,0,v.data());}
void MainWindow::openFile(){QString p=QFileDialog::getOpenFileName(this,"Open video",{},"Video files (*.mkv *.mp4 *.avi *.mov *.webm *.m4v *.ts *.mts *.wmv);;All files (*.*)");if(!p.isEmpty())openPath(p);}
void MainWindow::openPath(const QString&p){command({"loadfile",p,"replace"});setWindowTitle(QString("NovaPlayer — %1").arg(QFileInfo(p).fileName()));}
void MainWindow::togglePause(){setMpvPropertyFlag("pause",!paused_);}void MainWindow::toggleMute(){setMpvPropertyFlag("mute",!muted_);}
void MainWindow::toggleFullscreen(){if(isFullScreen()){showNormal();fullscreenButton_->setText("Fullscreen");}else{showFullScreen();fullscreenButton_->setText("Window");}}
void MainWindow::seekReleased(){seeking_=false;if(duration_<=0)return;double t=duration_*double(seek_->value())/1000;command({"seek",QString::number(t,'f',3),"absolute","exact"});}
void MainWindow::volumeChanged(int v){setMpvPropertyDouble("volume",v);}void MainWindow::speedChanged(int i){static double s[]={.5,.75,1,1.25,1.5,2};if(i>=0&&i<6)setMpvPropertyDouble("speed",s[i]);}
void MainWindow::setMpvPropertyFlag(const char*n,bool v){if(!mpv_)return;int x=v;mpv_set_property_async(mpv_,0,n,MPV_FORMAT_FLAG,&x);}void MainWindow::setMpvPropertyDouble(const char*n,double v){if(mpv_)mpv_set_property_async(mpv_,0,n,MPV_FORMAT_DOUBLE,&v);}
void MainWindow::updateTimeLabel(){timeLabel_->setText(QString("%1 / %2").arg(formatTime(position_),formatTime(duration_)));}
QString MainWindow::formatTime(double x){if(x<0)x=0;int t=int(x),h=t/3600,m=t%3600/60,s=t%60;return h?QString("%1:%2:%3").arg(h).arg(m,2,10,QChar('0')).arg(s,2,10,QChar('0')):QString("%1:%2").arg(m,2,10,QChar('0')).arg(s,2,10,QChar('0'));}
void MainWindow::dragEnterEvent(QDragEnterEvent*e){if(e->mimeData()->hasUrls())e->acceptProposedAction();}void MainWindow::dropEvent(QDropEvent*e){auto u=e->mimeData()->urls();if(!u.isEmpty()&&u.first().isLocalFile())openPath(u.first().toLocalFile());}
void MainWindow::keyPressEvent(QKeyEvent*e){switch(e->key()){case Qt::Key_Space:togglePause();break;case Qt::Key_F:toggleFullscreen();break;case Qt::Key_M:toggleMute();break;case Qt::Key_Right:command({"seek","5","relative"});break;case Qt::Key_Left:command({"seek","-5","relative"});break;default:QMainWindow::keyPressEvent(e);}}
