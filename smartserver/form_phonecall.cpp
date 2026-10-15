#include "form_phonecall.h"
#include "ui_form_phonecall.h"
#include <QDebug>
#include "slidehintwidget.h"
#include "widget.h"
#include <QUdpSocket>
#include <QAudioFormat>
#include <QAudioOutput>
#include <QAudioInput>
#include <QHostAddress>
#include <QImage>
#include <QCloseEvent>
#include <QPushButton>
#include <QMediaPlayer>
#include <QUrl>

Form_phonecall::Form_phonecall(QWidget *parent, bool playRing) :
    QWidget(parent),
    ui(new Ui::Form_phonecall)
{
    // 设置透明背景和无任务栏图标
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground, true);
    ui->setupUi(this);
    ui->label_slider->installEventFilter(this);
    ui->label_slider->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    ui->label_slider->setEnabled(true);
   // ui->label_slider->setStyleSheet("background: #FF0000; border-radius: 10px;"); // 调试用红色背景
    ui->label_slider->raise();
    qDebug() << "label_slider:" << ui->label_slider;
    qDebug() << "label_slider parent:" << ui->label_slider->parentWidget();
    // 默认隐藏提示label
    if (ui->label_image) ui->label_image->setVisible(false);
    // 添加流光引导控件
    hint = new SlideHintWidget(ui->widget_slider_area);
    hint->setGeometry(ui->widget_slider_area->rect());
    hint->show();
    ui->label_slider->raise(); // 确保滑块在流光之上

    // 动态创建挂断按钮
    btn_hangup = new QPushButton(ui->widget_slider_area->parentWidget());
    btn_hangup->setVisible(false);
    btn_hangup->setFixedSize(64, 64);
    btn_hangup->setStyleSheet(
        "QPushButton {"
        " border: none;"
        " background: transparent;"
        " border-image: url(:/pic/hangup.png) 0 0 0 0 stretch stretch;"
        "}"
        "QPushButton:pressed {"
        " opacity: 0.7;"
        "}"
    );
    btn_hangup->setCursor(Qt::PointingHandCursor);
    connect(btn_hangup, &QPushButton::clicked, this, [this](){
        qDebug() << "btn_hangup clicked, emitting hangupClicked";
        emit hangupClicked();
        this->close();
    });

    // 启动门铃音循环播放（Qt5写法）
    if (playRing) {
        ringPlayer = new QMediaPlayer(this);
        ringPlayer->setMedia(QUrl("qrc:/door.mp3"));
        ringPlayer->setVolume(100);
        ringPlayer->play();
        connect(ringPlayer, SIGNAL(stateChanged(QMediaPlayer::State)), this, SLOT(onRingStateChanged(QMediaPlayer::State)));
    }
}

Form_phonecall::~Form_phonecall()
{
    stopAV();
    if (ringPlayer) { ringPlayer->stop(); ringPlayer->deleteLater(); ringPlayer = nullptr; }
    delete ui;
}

void Form_phonecall::closeEvent(QCloseEvent *event)
{
    stopAV();
    QWidget::closeEvent(event);
}

bool Form_phonecall::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == ui->label_slider) {

        if (event->type() == QEvent::MouseButtonPress) {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
            dragging = true;
            dragOffset = mouseEvent->pos().x();

            return true;
        } else if (event->type() == QEvent::MouseMove) {
            if (dragging) {
                QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
                int newX = ui->label_slider->x() + mouseEvent->pos().x() - dragOffset;
                newX = qBound(0, newX, ui->widget_slider_area->width() - ui->label_slider->width());
                ui->label_slider->move(newX, ui->label_slider->y());

            }
            return true;
        } else if (event->type() == QEvent::MouseButtonRelease) {
            if (dragging) {
                dragging = false;
                // 在滑块滑到最右边时调用startAV
                if (ui->label_slider->x() > ui->widget_slider_area->width() - ui->label_slider->width() - 10) {
                    if (ui->label_image) ui->label_image->setVisible(true); // 显示label
                    // 停止门铃音
                    if (ringPlayer) { ringPlayer->stop(); ringPlayer->deleteLater(); ringPlayer = nullptr; }
                    startAV(); // 启动音视频
                    emit accepted();
                    emit answered();
                    // 隐藏滑块、滑块框、流光
                    ui->label_slider->setVisible(false);
                    ui->widget_slider_area->setVisible(false);
                    if (hint) hint->setVisible(false);
                    // 显示挂断按钮，横向居中于widget_slider_area
                    QSize btnSize(64, 64);
                    QRect areaRect = ui->widget_slider_area->geometry();
                    int btnX = areaRect.left() + (areaRect.width() - btnSize.width())/2;
                    int btnY = areaRect.top() + (areaRect.height() - btnSize.height())/2;
                    btn_hangup->move(btnX, btnY);
                    btn_hangup->setVisible(true);
                    btn_hangup->raise();
                } else {
                    ui->label_slider->move(0, ui->label_slider->y());
                    if (ui->label_image) ui->label_image->setVisible(false); // 保持隐藏
                }
                ui->label_slider->raise(); // 每次释放后再置顶
            }
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

// 音视频服务实现
void Form_phonecall::startAV()
{
    // 启动音频UDP
    if (!audioSocket) {
        audioSocket = new QUdpSocket(this);
        // 用本机实际IP替换下行IP
        audioSocket->bind(QHostAddress(SELF_IP), 12345, QUdpSocket::ShareAddress);
        connect(audioSocket, &QUdpSocket::readyRead, this, &Form_phonecall::onAudioDataReady);
    }

    // 启动视频UDP
    if (!videoSocket) {
        videoSocket = new QUdpSocket(this);
        if (!videoSocket->bind(QHostAddress::Any, 23456, QUdpSocket::ShareAddress)) {
            qDebug() << "视频UDP监听端口失败";
            delete videoSocket;
            videoSocket = nullptr;
        } else {
            connect(videoSocket, &QUdpSocket::readyRead, this, &Form_phonecall::onVideoDataReady);
        }
    }

    // 启动音频输出
    QAudioFormat format;
    format.setSampleRate(16000);
    format.setChannelCount(1);
    format.setSampleSize(16);
    format.setCodec("audio/pcm");
    format.setByteOrder(QAudioFormat::LittleEndian);
    format.setSampleType(QAudioFormat::SignedInt);
    int bytesPerMs = format.bytesPerFrame() * format.sampleRate() / 1000;
    if (audioOutput) {
        audioOutput->stop();
        delete audioOutput;
        audioOutput = nullptr;
    }
    audioOutput = new QAudioOutput(format, this);
    audioOutput->setBufferSize(bytesPerMs * 27);
    audioOutput->setNotifyInterval(1);
    // 不再connect notify信号
    audioDevice = audioOutput->start();
    audioBuffer.clear();

    // 启动音频输入采集
    if (audioInput) {
        audioInput->stop();
        delete audioInput;
        audioInput = nullptr;
    }
    audioInput = new QAudioInput(format, this);
    audioInput->setBufferSize(320);
    inputDevice = audioInput->start();
    connect(inputDevice, &QIODevice::readyRead, this, &Form_phonecall::onInputReady);
    inputBuffer.clear();
}

void Form_phonecall::onAudioDataReady()
{
    static const int sampleRate = 16000;
    static const int channelCount = 1;
    static const int sampleSize = 16;
    static const int maxBufferMs = 200; // 最大缓冲时长（毫秒）
    const int bytesPerMs = sampleRate * channelCount * (sampleSize / 8) / 1000;
    const int maxBufferBytes = maxBufferMs * bytesPerMs;
    while (audioSocket && audioSocket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(int(audioSocket->pendingDatagramSize()));
        QHostAddress sender;
        quint16 senderPort;
        audioSocket->readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);
        t113Address = sender;
        t113Port = senderPort;
        audioBuffer.append(datagram);
        // 限制缓冲区最大长度，丢弃最早的数据
        if (audioBuffer.size() > maxBufferBytes) {
            audioBuffer.remove(0, audioBuffer.size() - maxBufferBytes);
        }
        writeAudioData();
    }
}

void Form_phonecall::writeAudioData()
{
    if (!audioOutput || !audioDevice) return;
    while (!audioBuffer.isEmpty() && audioOutput->bytesFree() > 0) {
        int chunk = qMin(audioBuffer.size(), audioOutput->bytesFree());
        int written = audioDevice->write(audioBuffer.constData(), chunk);
        if (written > 0)
            audioBuffer.remove(0, written);
        else
            break;
    }
}

void Form_phonecall::onInputReady()
{
    if (!inputDevice || !audioSocket) return;
    inputBuffer.append(inputDevice->readAll());
    int packetSize = 320; // 10ms
    while (inputBuffer.size() >= packetSize) {
        if (!t113Address.isNull() && t113Port != 0) {
            audioSocket->writeDatagram(inputBuffer.constData(), packetSize, t113Address, t113Port);
        }
        inputBuffer.remove(0, packetSize);
    }
}

void Form_phonecall::onVideoDataReady()
{
   // qDebug()<<"audio"<<endl;
    while (videoSocket && videoSocket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(int(videoSocket->pendingDatagramSize()));
        videoSocket->readDatagram(datagram.data(), datagram.size());
        QImage img;
        img.loadFromData(datagram, "JPG");
        if (!img.isNull() && ui->label_image) {
            QPixmap pix = QPixmap::fromImage(img).scaled(ui->label_image->size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
            QRect targetRect = QRect(
                (pix.width() - ui->label_image->width()) / 2,
                (pix.height() - ui->label_image->height()) / 2,
                ui->label_image->width(),
                ui->label_image->height()
            );
            ui->label_image->setPixmap(pix.copy(targetRect));
        }
    }
}

void Form_phonecall::stopAV()
{
    if (audioSocket) {
        audioSocket->close();
        audioSocket->deleteLater();
        audioSocket = nullptr;
    }
    if (videoSocket) {
        videoSocket->close();
        videoSocket->deleteLater();
        videoSocket = nullptr;
    }
    if (audioOutput) {
        audioOutput->stop();
        delete audioOutput;
        audioOutput = nullptr;
    }
    if (audioInput) {
        audioInput->stop();
        delete audioInput;
        audioInput = nullptr;
    }
    audioDevice = nullptr;
    inputDevice = nullptr;
    audioBuffer.clear();
    inputBuffer.clear();
    if (ringPlayer) { ringPlayer->stop(); ringPlayer->deleteLater(); ringPlayer = nullptr; }
}

void Form_phonecall::onRingStateChanged(QMediaPlayer::State state)
{
    if (state == QMediaPlayer::StoppedState && ringPlayer) {
        ringPlayer->play();
    }
}
