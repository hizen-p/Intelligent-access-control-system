#include "widget.h"
#include "ui_widget.h"
#include <QDebug>
#include <QMessageBox>
#include <atomic>
#include <thread>
#include <chrono>
#include <cstring>
#include <arpa/inet.h>
#include "audiosenderthread.h"
#include "audioreceiverthread.h"
#include <QTimer>
#include <QImage>
#include <QPixmap>
#include <QFile>
#include <pthread.h>
#include <QDateTime>
#include <QPainter>
#include <QPainterPath>
#include <QElapsedTimer>
#include <opencv2/opencv.hpp>
#include <opencv2/objdetect.hpp>
#include <opencv2/face.hpp>
#include <QBuffer>
#include <QImageReader>
#include <QPainter>
#include <QElapsedTimer>
#include <QResizeEvent>
#include <QUdpSocket>
#include <QHostAddress>
#include <QTcpSocket>
#include <unistd.h>
#include <QtConcurrent/QtConcurrent>
#include "HailinFingerprint.h"
#include "form_passwd.h"
#include <QMutex>
#include <memory>
#include "camera/v4l2camera.h"
#include "camera/camera_udp_server.h"

#include <QInputMethod>


extern "C" {
     unsigned char* g_last_jpeg = nullptr;
     size_t g_last_jpeg_size = 0;
     pthread_mutex_t g_jpeg_mutex = PTHREAD_MUTEX_INITIALIZER;
}

// ========== UI按键相关实现 ==========
// Widget::Widget, on_btncall_clicked, on_btnadd_clicked, on_btnpsswd_clicked, 构造/析构等
Widget::Widget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Widget)
{
    // 删除fillLightAutoOffTimer和fillLightIsOn相关代码
    ui->setupUi(this);
    QList<QPushButton*> btns = {
        ui->btnadd, ui->btncall, ui->btnface, ui->btnpower,ui->btnpsswd,ui->btnfinger_step/* ... */
    };
    for (QPushButton* btn : btns) {
        btn->setFocusPolicy(Qt::NoFocus);
    }
    // 添加定时器刷新日期和时间
    QTimer *dateTimeTimer = new QTimer(this);
    connect(dateTimeTimer, &QTimer::timeout, this, [this]() {
        QDateTime current = QDateTime::currentDateTime();
        ui->labelDate->setText(current.date().toString("yyyy-MM-dd"));
        ui->labelTime->setText(current.time().toString("HH:mm:ss"));
    });
    dateTimeTimer->start(1000); // 每秒刷新一次

    // 初始化显示
    QDateTime current = QDateTime::currentDateTime();
    ui->labelDate->setText(current.date().toString("yyyy-MM-dd"));
    ui->labelTime->setText(current.time().toString("HH:mm:ss"));
    qDebug() << "[界面] 时间显示初始化完成";

    // 隐藏鼠标指针
    this->setCursor(Qt::BlankCursor);

    // 设置labelcam和父控件为透明，并设置四角圆角样式表
    ui->labelcam->setAttribute(Qt::WA_TranslucentBackground, true);
    if (ui->labelcam->parentWidget())
        ui->labelcam->parentWidget()->setAttribute(Qt::WA_TranslucentBackground, true);
    ui->labelcam->setStyleSheet(
        "background-color: rgb(85, 87, 83);"
        "border: none;"
    );
  /***********************************UIINIT****************************************************/


    struct gpiod_chip *chip = gpiod_chip_open_by_name("gpiochip0");
    line5 = gpiod_chip_get_line(chip, 133); // PE5
    gpiod_line_request_output(line5, "my_app", 0);

    //指纹功能
    fingerprint = new HailinFingerprint("/dev/ttyS4", this);
    if (fingerprint->open()) {
        qDebug() << "[指纹] 模块串口打开成功";
    } else {
       // qDebug() << "[指纹] 模块串口打开失败";
    }
    fingerprintManager = new FingerprintManager(this);
    fingerprintManager->setDevice(fingerprint); // fingerprint 为 HailinFingerprint*
    connect(fingerprintManager, &FingerprintManager::identifyResult,
            this, [this](bool success, int id, int score, const QString& msg){
        if (msg == "请按下指纹进行识别") {
            // 不再自动播报采集提示音
            return;
        }
        if (success) {
            qDebug() << "指纹识别成功";
            audioManager->playAsync("/tmp/pcm/fingerdoor.pcm");
        } else {
            qDebug() << "指纹识别失败";
            audioManager->playAsync("/tmp/pcm/fingererr.pcm");
        }
        // 识别完成后，延时再次启动识别，实现循环
        QTimer::singleShot(500, this, [this](){
            fingerprintManager->startIdentify();
        });
    });
    // 移除按钮触发
    // connect(ui->btnfinger_step, &QPushButton::clicked, fingerprintManager, &FingerprintManager::startIdentify);
    // 程序启动后自动开始指纹识别
    QTimer::singleShot(0, this, [this](){
        fingerprintManager->startIdentify();
    });

    connect(fingerprintManager, &FingerprintManager::enrollResult, this, [this](bool success, int id, const QString& msg){
    if (msg.contains("请按下手指进行")) {
        audioManager->playAsync("/tmp/pcm/caijifinger.pcm");
        return;
    }
    if (msg.contains("采集成功")) {
        // 可选：每次采集成功提示音
        audioManager->playAsync("/tmp/pcm/fingerone.pcm"); // 可根据第几次切换音频
        return;
    }
    if (success && msg == "注册成功") {
        audioManager->playAsync("/tmp/pcm/opendoor.pcm");
        // nextId++;
    } else if (!success) {
        audioManager->playAsync("/tmp/pcm/fingererr.pcm");
    }
    // 可在UI显示msg
    });
    //指纹功能结束


//音频功能
    audioManager = new AudioManager(this);
    connAudioManager = new AudioManager(this); // 初始化专用 conn.pcm 播放器
//人脸功能
   faceManager = new FaceManager(this);


    qputenv("QT_IM_MODULE", QByteArray("qtvirtualkeyboard"));
    qDebug() << "\n========== [系统启动] 智能门禁系统初始化 ==========";
   // playPcmFileAsync("/tmp/pcm/welcome.pcm");
    // 加载人脸分类器
    bool loaded = loadFaceCascade("/root/haarcascade_frontalface_default.xml");
    qDebug() << "[人脸检测] 分类器加载" << (loaded ? "成功" : "失败");

    // 初始化LBPH人脸识别器
    faceRecognizer = cv::face::LBPHFaceRecognizer::create();
    qDebug() << "[人脸识别] LBPH识别器初始化完成";

    // 去除所有QPushButton的焦点虚框
    this->setStyleSheet("QPushButton:focus { outline: none; }");



    // 打开本地V4L2摄像头
    camera = new V4L2Camera("/dev/video0");
    if (!camera->open()) {
        qDebug() << "[错误] 无法打开摄像头设备";
        QMessageBox::critical(this, "错误", "无法打开摄像头");
    } else {
        qDebug() << "[摄像头] V4L2设备打开成功";
    }
    // 启动UDP摄像头服务器
    cameraUdpServer = new CameraUdpServer(camera, 50000, this);

    // 定时器定时采集+检测+显示
    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &Widget::updateFrame);
    timer->start(50); // 20fps，配合跳帧优化实际显示约10fps，更流畅
    qDebug() << "[视频显示] 定时器启动，采集帧率: 20fps，显示帧率: 10fps";



    // 初始化TCP信令socket
    tcpSignalSocket = new QTcpSocket(this);
    connect(tcpSignalSocket, &QTcpSocket::readyRead, this, &Widget::onTcpReadyRead);
    // 断线自动重连
    connect(tcpSignalSocket, &QTcpSocket::disconnected, this, [this]() {
        qDebug() << "[信令] TCP连接断开，3秒后自动重连...";
        QTimer::singleShot(3000, this, [this]() {
            qDebug() << "[信令] 正在尝试重连...";
            tcpSignalSocket->connectToHost("192.168.1.4", 8887);
        });
    });
    // 程序启动时即连接
    tcpSignalSocket->connectToHost("192.168.1.4", 8887);

    // 移除音频UDP socket的创建和bind（延后到accept后）

    qDebug() << "[完成] 系统初始化完成，等待用户操作...";
    qDebug() << "===============================================\n";

    pe4Thread = new GpioPe4MonitorThread(this);
    // 修改GpioPe4MonitorThread，增加pe4Low信号
    connect(pe4Thread, &GpioPe4MonitorThread::pe4Low, this, [this](){
        hasPerson = false;
    });
    connect(pe4Thread, &GpioPe4MonitorThread::pe4High, this, [this](){
        hasPerson = true;
    });
    pe4Thread->start();

    // 指纹功能初始化与信号连接
    connect(fingerprint, &HailinFingerprint::identifyResult, this, &Widget::onFingerprintIdentify);
    connect(fingerprint, &HailinFingerprint::enrollResult, this, &Widget::onFingerprintEnroll);
    connect(fingerprint, &HailinFingerprint::debugInfo, this, &Widget::onFingerprintDebug);

    connect(ui->btnpsswd, &QPushButton::clicked, this, &Widget::on_btnpsswd_clicked);
    connect(fingerprint, &HailinFingerprint::stepRegisterStatus, this, &Widget::onStepRegisterStatus);
    connect(fingerprint, &HailinFingerprint::stepIdentifyStatus, this, &Widget::onStepIdentifyStatus);
    // 连接音频线程断开信号
    if(audioThread) connect(audioThread, &AudioSenderThread::connectionLost, this, &Widget::hangupCall);
    if(audioRecvThread) connect(audioRecvThread, &AudioReceiverThread::connectionLost, this, &Widget::hangupCall);

    // Widget 构造函数中添加 faceManager 信号连接
    connect(faceManager, &FaceManager::faceSampled, this, [this](int current, int total){
        if (current == 1) {
            audioManager->playAsync("/tmp/pcm/faceone.pcm");
            gpiod_line_set_value(line5, 1);
        }
        else if (current == 2) audioManager->playAsync("/tmp/pcm/facetwo.pcm");
        else if (current == 3) {
            audioManager->playAsync("/tmp/pcm/facethree.pcm");
            gpiod_line_set_value(line5, 0);
        }
    });
    // 在人脸注册采集样本时点亮补光灯
    connect(faceManager, &FaceManager::trainFinished, this, [this](bool success){
        if (success) {
            qDebug() << "[人脸] 训练完成";
        } else {
            qDebug() << "[人脸] 训练失败，样本不足";
        }
    });
    // faceManager::recognizeResult信号槽只播放识别结果音频
    connect(faceManager, &FaceManager::recognizeResult, this, [this](bool success, double confidence){
        qDebug() << "[人脸识别] 结果:" << (success ? "成功" : "失败") << ", confidence=" << confidence;
         gpiod_line_set_value(line5, 0);
        if (success) {
            audioManager->playAsync("/tmp/pcm/opendoor.pcm");


        } else {
            audioManager->playAsync("/tmp/pcm/faceerr.pcm");
        }
    });

    // Widget 构造函数中添加 passwordManager 初始化和信号连接
    passwordManager = new PasswordManager(this);
    connect(passwordManager, &PasswordManager::passwordChecked, this, [this](bool success){
        if (success) {
            audioManager->playAsync("/tmp/pcm/opendoor.pcm");
            // 关闭密码输入窗口
            if (passwdForm) {
                passwdForm->close();
                passwdForm = nullptr;
            }
        } else {
            audioManager->playAsync("/tmp/pcm/passwderr.pcm");
        }
    });
}

Widget::~Widget()
{
    if(cameraPushThread) {
        cameraPushThread->stop();
        cameraPushThread->wait();
        delete cameraPushThread;
        cameraPushThread = nullptr;
    }
    if(audioThread) {
        audioThread->stop();
        audioThread->wait();
        delete audioThread;
    }
    if(audioRecvThread) {
        audioRecvThread->stop();
        audioRecvThread->wait();
        delete audioRecvThread;
    }
    if (audioSocketFd >= 0) {
        ::close(audioSocketFd);
        audioSocketFd = -1;
    }
    if(timer) {
        timer->stop();
        delete timer;
        timer = nullptr;
    }
    if (pe4Thread) {
        pe4Thread->stop();
        pe4Thread->wait();
        delete pe4Thread;
        pe4Thread = nullptr;
    }
    if (fingerDetectThread) {
        fingerDetectThread->stop();
        fingerDetectThread->wait();
        delete fingerDetectThread;
        fingerDetectThread = nullptr;
    }
    if (fingerprint) {
        fingerprint->close();
        delete fingerprint;
        fingerprint = nullptr;
    }
    if (camera) {
        camera->close();
        delete camera;
        camera = nullptr;
    }
    delete ui;

}

// ========== 音频/网络/通话相关实现 ==========
// hangupCall, onServerAccept, onTcpReadyRead, AudioSenderThread/AudioReceiverThread相关
void Widget::hangupCall()
{
    if (connAudioManager) connAudioManager->stop(); // 挂断时停止 conn.pcm 播放
    // 1. 立即关闭窗口
    if (callForm) {
        callForm->stopCallTimer();
        callForm->close();
    }
    // 2. 安全结束音视频线程
    qDebug() << "[hangupCall] Stopping cameraPushThread...";
    if (cameraPushThread) {
        cameraPushThread->stop();
        if (!cameraPushThread->wait(3000)) {
            qWarning() << "[hangupCall] cameraPushThread wait timeout!";
        }
        delete cameraPushThread;
        cameraPushThread = nullptr;
        qDebug() << "[hangupCall] cameraPushThread stopped.";
    }
    qDebug() << "[hangupCall] Stopping audioThread...";
    if (audioThread) {
        audioThread->stop();
        if (!audioThread->wait(3000)) {
            qWarning() << "[hangupCall] audioThread wait timeout!";
        }
        delete audioThread;
        audioThread = nullptr;
        qDebug() << "[hangupCall] audioThread stopped.";
    }
    qDebug() << "[hangupCall] Stopping audioRecvThread...";
    if (audioRecvThread) {
        audioRecvThread->stop();
        if (!audioRecvThread->wait(3000)) {
            qWarning() << "[hangupCall] audioRecvThread wait timeout!";
        }
        delete audioRecvThread;
        audioRecvThread = nullptr;
        qDebug() << "[hangupCall] audioRecvThread stopped.";
    }
    // 3. 释放窗口对象
    if (callForm) {
        delete callForm;
        callForm = nullptr;
    }
    // 4. 断开TCP信令（已移除，保持连接）
    // if (tcpSignalSocket && tcpSignalSocket->state() == QAbstractSocket::ConnectedState) {
    //     tcpSignalSocket->disconnectFromHost();
    // }
    // 5. 关闭音频socket
    if (audioSocketFd >= 0) {
        ::close(audioSocketFd);
        audioSocketFd = -1;
        qDebug() << "[hangupCall] audioSocketFd closed.";
    }
    if (pe4Thread) {
        pe4Thread->resume();
    }
}

void Widget::on_btncall_clicked()
{
    // 先播放呼叫音
    audioManager->playAsync("/tmp/pcm/calling.pcm");
    // 串联播放等待音，conn.pcm 用独立 AudioManager
    QMetaObject::Connection* conn = new QMetaObject::Connection;
    *conn = connect(audioManager, &AudioManager::playbackFinished, this, [this, conn](const QString& file){
        if (file == "/tmp/pcm/calling.pcm") {
            connAudioManager->playAsync("/tmp/pcm/conn.pcm");
            QObject::disconnect(*conn);
            delete conn;
        }
    });
    // 弹出form_call窗口，左上角与widget_3左上角对齐
    if (callForm) {
        callForm->close();
        delete callForm;
        callForm = nullptr;
    }
    callForm = new Form_CALL(this);
    QPoint globalPos = ui->widget_3->mapToGlobal(QPoint(0, 0));
    callForm->move(globalPos);
    callForm->setStatusText("正在呼叫...");
    callForm->show();
    callForm->raise();
    // 连接挂断信号到hangupCall
    connect(callForm, &Form_CALL::hangupRequested, this, [this](){
        if (audioManager) audioManager->stop();
        if (connAudioManager) connAudioManager->stop();
        hangupCall();
    });

    // 只负责发数据，不再负责连接
    if (tcpSignalSocket->state() == QAbstractSocket::ConnectedState) {
        tcpSignalSocket->write("call\n");
        tcpSignalSocket->flush();
    } else {
        QMessageBox::warning(this, "错误", "信令服务器未连接");
        return;
    }
    // 3. 不再创建任何音视频线程，全部延后到accept后
    qDebug() << "[完成] 信令已发送，等待服务器accept...";
    qDebug() << "=============================================";
    if (pe4Thread) {
        pe4Thread->pause();
    }
}

// 服务端accept时调用
void Widget::onServerAccept()
{
    // 只打断 conn.pcm，不影响主 audioManager
    if (connAudioManager) connAudioManager->stop();
    qDebug() << "[信令] 收到服务器accept，开始创建音视频线程...";
    // 1. 创建音频UDP socket并bind 12345
    if (audioSocketFd < 0) {
        audioSocketFd = socket(AF_INET, SOCK_DGRAM, 0);
        if (audioSocketFd < 0) {
            qDebug() << "[音频] 创建UDP socket失败, errno=" << errno;
        } else {
            struct sockaddr_in local_addr;
            memset(&local_addr, 0, sizeof(local_addr));
            local_addr.sin_family = AF_INET;
            local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
            local_addr.sin_port = htons(12345);
            if (bind(audioSocketFd, (struct sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
                qDebug() << "[音频] 绑定端口12345失败, errno=" << errno;
                ::close(audioSocketFd);
                audioSocketFd = -1;
            } else {
                qDebug() << "[音频] 成功绑定端口12345, sockfd=" << audioSocketFd;
            }
        }
    }
    if (audioSocketFd < 0) {
        qDebug() << "[音频] 无法创建/绑定socket，音频线程不启动";
        return;
    }
    // 2. 创建并启动视频推流线程
    if (cameraPushThread) {
        cameraPushThread->stop();
        cameraPushThread->wait();
        delete cameraPushThread;
        cameraPushThread = nullptr;
    }
    // 注释掉视频推流相关调试信息
    cameraPushThread = new CameraPushThread(camera, "192.168.1.4", 23456, this);
    cameraPushThread->start();
    // 3. 创建并启动音频采集线程
    if (audioThread) {
        audioThread->stop();
        audioThread->wait();
        delete audioThread;
        audioThread = nullptr;
    }
    qDebug() << "[音频] 创建AudioSenderThread...";
    audioThread = new AudioSenderThread(audioSocketFd, "192.168.1.4", 12345, this);
    connect(audioThread, &AudioSenderThread::errorMsg, this, [this](const QString &msg){
        qDebug() << "[音频错误]" << msg;
        QMessageBox::critical(this, "错误", msg);
    });
    connect(audioThread, &AudioSenderThread::finishedMsg, this, [this](){
        qDebug() << "[音频完成] 音频采集线程已结束";
    });
    audioThread->start();
    qDebug() << "[音频] AudioSenderThread已启动";


    // 4. 创建并启动音频播放线程（延迟启动，避免资源冲突）
    QTimer::singleShot(400, this, [this]() {
        if (audioRecvThread) {
            audioRecvThread->stop();
            audioRecvThread->wait();
            delete audioRecvThread;
            audioRecvThread = nullptr;
        }
        qDebug() << "[音频] 创建AudioReceiverThread...";
        audioRecvThread = new AudioReceiverThread(audioSocketFd, this);
        audioRecvThread->setVolume(30); // 默认30%音量
        if (audioRecvThread) {
            qDebug() << "[音频] AudioReceiverThread 准备 start";
            audioRecvThread->start();
            qDebug() << "[音频] AudioReceiverThread 已调用 start";
            connect(audioRecvThread, &AudioReceiverThread::errorMsg, this, [this](const QString &msg){
                qWarning() << "[音频接收] 错误:" << msg;
            });
            connect(audioRecvThread, &AudioReceiverThread::finishedMsg, this, [this](){
                qDebug() << "[音频接收] 线程已结束";
            });
            connect(audioRecvThread, &AudioReceiverThread::connectionLost, this, [this](){
                qWarning() << "[音频接收] 连接丢失";
            });
        }
    });
    // 5. 启动通话计时
    if (callForm) callForm->startCallTimer();
    qDebug() << "[完成] 音频线程全部启动";
}

void Widget::onTcpReadyRead()
{
    QByteArray datagram = tcpSignalSocket->readAll();
    QStringList messages = QString::fromUtf8(datagram).split('\n', QString::SkipEmptyParts);
    for (const QString& serverMsgRaw : messages) {
        QString serverMsg = serverMsgRaw.trimmed();
        qDebug() << "get server: " << serverMsg;
        if (serverMsg == "accept") {
            onServerAccept();
        }
        // 新增：处理recstop信令
        else if (serverMsg == "recstop") {
            qDebug() << "[信令] 收到recstop，自动挂断通话，当前callForm指针:" << callForm
                     << ", cameraPushThread:" << cameraPushThread
                     << ", audioThread:" << audioThread
                     << ", audioRecvThread:" << audioRecvThread
                     << ", audioSocketFd:" << audioSocketFd;
            hangupCall();
        }
        // 新增：处理unlock信令
        else if (serverMsg == "unlock") {
            qDebug() << "[信令] 收到unlock，播放开门音";
            if (audioManager) audioManager->playAsync("/tmp/pcm/opendoor.pcm");
        }
        // 可在此处理其它信令
    }
}

// ========== 指纹相关实现 ==========
// onFingerprintIdentify, onFingerprintEnroll, onFingerprintDebug, onStepRegisterStatus, onStepIdentifyStatus, onAddFingerFromFingerForm
void Widget::onFingerprintIdentify(bool success, int id, int score, const QString& msg)
{
    if (success) {
       // qDebug() << "[指纹] 识别成功, ID=" << id << ", score=" << score << ", msg=" << msg;
        QMessageBox::information(this, "指纹识别", QString("识别成功，ID=%1，score=%2").arg(id).arg(score));
    } else {
        qDebug() << "[指纹] 识别失败, msg=" << msg;
        // 可选：识别失败时不弹窗，避免频繁打扰
    }
}

void Widget::onFingerprintEnroll(bool success, int id, const QString& msg)
{
    if (success) {
       // qDebug() << "[指纹] 注册成功, ID=" << id << ", msg=" << msg;
        nextFingerId++;
        QMessageBox::information(this, "指纹注册", QString("注册成功，ID=%1").arg(id));
    } else {
        qDebug() << "[指纹] 注册失败, ID=" << id << ", msg=" << msg;
        QMessageBox::warning(this, "指纹注册", QString("注册失败，ID=%1\n原因：%2").arg(id).arg(msg));
    }
}

void Widget::onFingerprintDebug(const QString& info)
{
  //  qDebug() << "[指纹调试]" << info;
}

void Widget::on_btnpsswd_clicked()
{
    Form_passwd* passwdForm = new Form_passwd(this);
    passwdForm->setAttribute(Qt::WA_DeleteOnClose); // 关闭时自动销毁
    connect(passwdForm, &Form_passwd::passwordEntered, passwordManager, &PasswordManager::checkPassword);
    QPoint globalPos = ui->widget_3->mapToGlobal(QPoint(0, 0));
    passwdForm->move(globalPos);
    passwdForm->show();
    passwdForm->raise();
    audioManager->playAsync("/tmp/pcm/inputpasswd.pcm");
}
void Widget::onStepRegisterStatus(const QString& msg, int step, bool ok) {}
void Widget::onStepIdentifyStatus(const QString& msg, bool ok, int id, int score) {}

// ========== 人脸相关实现 ==========
// onAddFaceFromFingerForm, updateFrame（含人脸检测/识别）
void Widget::updateFrame()
{
    static int detect_counter = 0;
    const int detect_interval = 10; // 每10帧检测一次
    QImage img = camera->getFrame();
    if (img.isNull()) {
        return;
    }
    // 摄像头画面始终显示
    QPainter painter(&img);
    painter.setPen(QPen(Qt::red, 3));
    QFont font = painter.font();
    font.setPointSize(18);
    painter.setFont(font);
    for (const auto &face : faces) {
        painter.drawRect(face.x, face.y, face.width, face.height);
        // 在左上角画采集次数
        QString text = QString::number(collectCount);
        // 黑色描边
        painter.setPen(QPen(Qt::black, 5));
        painter.drawText(face.x + 6, face.y + 28, text);
        // 白色字体
        painter.setPen(QPen(Qt::white, 1));
        painter.drawText(face.x + 6, face.y + 28, text);
        painter.setPen(QPen(Qt::red, 3)); // 恢复红色画框
    }
    painter.end();
    // 圆角显示，先扩展再裁剪
    QSize labelSize(430, 475); // 固定为430x475
    QImage scaledImg = img.scaled(labelSize, Qt::KeepAspectRatioByExpanding, Qt::FastTransformation);
    QRect cropRect((scaledImg.width() - labelSize.width())/2, (scaledImg.height() - labelSize.height())/2, labelSize.width(), labelSize.height());
    QPixmap pix = QPixmap::fromImage(scaledImg.copy(cropRect));
    ui->labelcam->setPixmap(pix);
    // 异步人脸检测
    bool doDetect = (++detect_counter % detect_interval == 0);
    if (doDetect && !face_cascade.empty()) {
        QImage smallImg = img.scaled(320, 240, Qt::KeepAspectRatio);
        QtConcurrent::run([this, smallImg]() {
            cv::Mat mat_small(smallImg.height(), smallImg.width(), CV_8UC4, (void*)smallImg.bits(), smallImg.bytesPerLine());
            cv::Mat mat_gray;
            cv::cvtColor(mat_small, mat_gray, cv::COLOR_BGRA2GRAY);
            std::vector<cv::Rect> faceRects;
            face_cascade.detectMultiScale(mat_gray, faceRects, 1.1, 3, 0, cv::Size(30, 30));
            QVector<cv::Rect> qfaces = QVector<cv::Rect>::fromStdVector(faceRects);
            emit QMetaObject::invokeMethod(this, [this, qfaces, w=smallImg.width(), h=smallImg.height()](){
                onFaceDetectResult(qfaces, w, h);
            }, Qt::QueuedConnection);
        });
    }
    static QElapsedTimer faceRecognizeCooldown;
    static int faceRecognizeIntervalMs = 2000;
    if (hasPerson) {
        if (!faces.empty()) {
            // 有人且有人脸，自动识别
            if (!faceRecognizeCooldown.isValid() || faceRecognizeCooldown.elapsed() > faceRecognizeIntervalMs) {
                qDebug() << "[人脸识别] 开始识别";
                auto maxFace = std::max_element(faces.begin(), faces.end(),
                    [](const cv::Rect& a, const cv::Rect& b) { return a.area() < b.area(); });
                if (maxFace != faces.end()) {
                    QRect qface(maxFace->x, maxFace->y, maxFace->width, maxFace->height);
                    QImage faceImg = img.copy(qface).scaled(100, 100, Qt::KeepAspectRatio);
                    faceManager->recognize(faceImg);
                    faceRecognizeCooldown.restart();
                }
            }
        } else {
            // 有人但无脸，每2秒只播放oncam.pcm
            if (!faceRecognizeCooldown.isValid() || faceRecognizeCooldown.elapsed() > faceRecognizeIntervalMs) {
                audioManager->playAsync("/tmp/pcm/oncam.pcm");
                faceRecognizeCooldown.restart();
                gpiod_line_set_value(line5, 1);
            }
        }
    }
    // 无人时不识别也不播报
}

void Widget::onFaceDetectResult(const QVector<cv::Rect>& detected, int imgW, int imgH)
{
    // 将检测到的人脸坐标缩放回原图尺寸
    QImage img = camera->getFrame();
    if (img.isNull()) return;
    double scaleX = double(img.width()) / imgW;
    double scaleY = double(img.height()) / imgH;
    faces.clear();
    for (const auto& face : detected) {
        cv::Rect f(face.x * scaleX, face.y * scaleY, face.width * scaleX, face.height * scaleY);
        faces.append(f);
    }
}

void Widget::on_btnadd_clicked()
{
    audioManager->playAsync("/tmp/pcm/regnew.pcm");
    if (!fingerForm) {
        fingerForm = new Form_finger(this);
        connect(fingerForm, &Form_finger::addFaceRequested, this, &Widget::onAddFaceFromFingerForm);
        connect(fingerForm, &Form_finger::addFingerRequested, this, &Widget::onAddFingerFromFingerForm);
    }
    // 使form_finger左上角与widget_3左上角对齐
    QPoint globalPos = ui->widget_3->mapToGlobal(QPoint(0, 0));
    fingerForm->move(globalPos);
    fingerForm->show();
    fingerForm->raise();
}

void Widget::onAddFaceFromFingerForm()
{
    if (faces.empty()) return;
    auto maxFace = std::max_element(faces.begin(), faces.end(),
        [](const cv::Rect& a, const cv::Rect& b) { return a.area() < b.area(); });
    if (maxFace == faces.end()) return;
    QImage img = camera->getFrame();
    if (img.isNull()) return;
    QRect qface(maxFace->x, maxFace->y, maxFace->width, maxFace->height);
    QImage faceImg = img.copy(qface).scaled(100, 100, Qt::KeepAspectRatio);
    faceManager->addFaceSample(faceImg);
    if (faceManager->sampleCount() == 3) {
        faceManager->train();
    }
}

void Widget::onAddFingerFromFingerForm()
{
    qDebug() << "[分步注册] 开始分步注册, ID=" << nextFingerId;
    QMessageBox::information(this, "分步注册", "请将手指按在指纹模块上，按提示操作，每次采集后抬手再按。");
    QtConcurrent::run(fingerprint, &HailinFingerprint::stepRegister, nextFingerId, 3); // 3次采集
}


void Widget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);

}

bool Widget::loadFaceCascade(const QString& path)
{
    return face_cascade.load(path.toStdString());
}



void Widget::on_btnpower_clicked()
{
    gpiod_line_set_value(line5, 0);
}
