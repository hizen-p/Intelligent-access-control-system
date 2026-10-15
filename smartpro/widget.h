/*
 * @Author: '嵌入式王子' 'donminedm@163.com'
 * @Date: 2026-04-14 17:38:44
 * @LastEditors: '嵌入式王子' 'donminedm@163.com'
 * @LastEditTime: 2026-04-14 18:08:38
 * @FilePath: \smartpronew项目最终源码\widget.h
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>
#include <QThread>
#include <QLabel>
#include <QTimer>
#include <QImage>
#include <QPixmap>
#include <QUdpSocket>
#include <QTcpSocket>
#include <QFuture>
#include <atomic>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <sys/mman.h>
#include <opencv2/opencv.hpp>
#include <opencv2/objdetect.hpp>
#include <opencv2/face.hpp>
#include <QVector>
#include <QtConcurrent/QtConcurrent>
#include <memory>
#include <gpiod.h>
#include <stdio.h>
#include <unistd.h>
#include <QElapsedTimer>

#include "audiosenderthread.h"
#include "audioreceiverthread.h"
#include "camera_push_thread.h"
#include "form_call.h"
#include "HailinFingerprint.h"
#include "form_finger.h"
#include "form_passwd.h"
#include "managers/fingerprint_manager.h"
#include "managers/audio_manager.h"
#include "managers/face_manager.h"
#include "managers/password_manager.h"
#include "camera/v4l2camera.h"
#include "camera/camera_udp_server.h"

extern "C" {
#include <alsa/asoundlib.h>
}

QT_BEGIN_NAMESPACE
namespace Ui { class Widget; }
QT_END_NAMESPACE

// 服务器 IP    虚拟机运行服务器时 为虚拟机ip
#define SERVER_IP  "192.168.1.4"
// 当前本机 IP   T113终端设备的当前IP
#define SELF_IP  "192.168.1.8"

#include <QThread>
#include <atomic>

#include <gpiod.h>
#include <stdio.h>
#include <unistd.h>
#include "managers/audio_manager.h"
#include "managers/face_manager.h"

class Widget;

class GpioPe4MonitorThread : public QThread {
    Q_OBJECT
public:
    GpioPe4MonitorThread(QObject* parent = nullptr)
        : QThread(parent), running(true), paused(false) {}
    void stop() { running = false; }
    void pause() { paused = true; }
    void resume() { paused = false; }
signals:
    void pe4High();
    void pe4Low();
protected:
    void run() override {
        struct gpiod_chip *chip = gpiod_chip_open_by_name("gpiochip0");
        if (!chip) return;
        struct gpiod_line *line = gpiod_chip_get_line(chip, 132); // PE4
        if (!line) { gpiod_chip_close(chip); return; }
        gpiod_line_request_input(line, "pe4_monitor");
        int highCount = 0;
        while (running) {
            if (paused) {
                msleep(100); // 暂停时短暂休眠，避免CPU占用
                continue;
            }
            int value = gpiod_line_get_value(line);
            if (value == 1) {
                highCount++;
                if (highCount >= 1) {
                    emit pe4High();
                    highCount = 0;
                }
            } else {
                highCount = 0;
                emit pe4Low(); // 检测到低电平时emit pe4Low()
            }
            msleep(400); // 每分钟检测一次
        }
        gpiod_line_release(line);
        gpiod_chip_close(chip);
    }
private:
    std::atomic<bool> running;
    std::atomic<bool> paused;
};


class FingerDetectThread : public QThread {
    Q_OBJECT
public:
    explicit FingerDetectThread(QObject* parent = nullptr)
        : QThread(parent), running(true) {}
    void stop() { running = false; }
signals:
    void detectRequest();
protected:
    void run() override {
        while (running) {
            emit detectRequest();
            msleep(1000);
        }
    }
private:
    std::atomic<bool> running;
};



class Widget : public QWidget
{
    Q_OBJECT

public:
    Widget(QWidget *parent = nullptr);
    ~Widget();

    // ================== 业务管理器 ==================
    FingerprintManager* fingerprintManager = nullptr; // 指纹注册/识别
    AudioManager* audioManager = nullptr;             // 音频播放
    FaceManager* faceManager = nullptr;               // 人脸采集/识别
    PasswordManager* passwordManager = nullptr;       // 密码校验/管理
    AudioManager* connAudioManager = nullptr; // 专用于 conn.pcm 播放

    HailinFingerprint* getFingerprint() const { return fingerprint; }
    int getNextFingerId() const { return nextFingerId; }
    void incrementNextFingerId() { ++nextFingerId; }
    QTcpSocket* tcpSignalSocket = nullptr;
private slots:
    // ================== UI按键相关槽 ==================
    void on_btncall_clicked();
    void on_btnadd_clicked();
    void on_btnpsswd_clicked();


    // ================== 指纹相关槽 ==================
    void onFingerprintIdentify(bool success, int id, int score, const QString& msg);
    void onFingerprintEnroll(bool success, int id, const QString& msg);
    void onFingerprintDebug(const QString& info);
    void onStepRegisterStatus(const QString& msg, int step, bool ok);
    void onStepIdentifyStatus(const QString& msg, bool ok, int id, int score);
    void onAddFingerFromFingerForm();

    // ================== 人脸相关槽 ==================
    void onAddFaceFromFingerForm();
    void onFaceDetectResult(const QVector<cv::Rect>& faces, int imgWidth, int imgHeight);

    void on_btnpower_clicked();

private:
    // ================== UI相关成员 ==================
    Ui::Widget *ui;
    QTimer* timer = nullptr;
    Form_finger* fingerForm = nullptr;
    Form_passwd* passwdForm = nullptr;
    Form_CALL* callForm = nullptr;

    // ================== 指纹相关成员 ==================
    HailinFingerprint* fingerprint = nullptr;
    int nextFingerId = 1;

    // ================== 摄像头相关成员 ==================
    V4L2Camera* camera = nullptr;
    cv::CascadeClassifier face_cascade;
    QVector<cv::Rect> faces;
    int collectCount = 0;
    cv::Ptr<cv::face::LBPHFaceRecognizer> faceRecognizer;
    bool loadFaceCascade(const QString &path);
    void updateFrame();
    void resizeEvent(QResizeEvent* event) override;

    // ================== 音频/网络/通话相关成员 ==================
    AudioSenderThread *audioThread = nullptr;
    AudioReceiverThread *audioRecvThread = nullptr;
    int audioSocketFd = -1; // 新增：音频UDP socket
    CameraPushThread* cameraPushThread = nullptr;

    void onServerAccept();
    void onTcpReadyRead();
    void hangupCall();

    // ================== GPIO/硬件相关成员 ==================
    GpioPe4MonitorThread* pe4Thread = nullptr;
    FingerDetectThread* fingerDetectThread = nullptr;
    struct gpiod_line *line5 = nullptr;

    // ================== 其它辅助成员 ==================
    QElapsedTimer faceRecognizeCooldown;
    int faceRecognizeIntervalMs = 2000;
    bool hasPerson = false;
    CameraUdpServer* cameraUdpServer = nullptr;
};
#endif // WIDGET_H
