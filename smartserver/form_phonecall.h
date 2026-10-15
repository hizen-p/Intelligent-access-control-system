#ifndef FORM_PHONECALL_H
#define FORM_PHONECALL_H

#include <QWidget>
#include <QMouseEvent>
#include <QUdpSocket>
#include <QAudioInput>
#include <QAudioOutput>
#include <QAudioFormat>
#include <QIODevice>
#include <QHostAddress>
#include <QPushButton>
#include "slidehintwidget.h"
#include <QMediaPlayer>
#include <QAudioOutput>


namespace Ui {
class Form_phonecall;
}

class Form_phonecall : public QWidget
{
    Q_OBJECT

public:
    explicit Form_phonecall(QWidget *parent = nullptr, bool playRing = true);
    ~Form_phonecall();

private:
    Ui::Form_phonecall *ui;
    bool dragging = false;
    int dragOffset = 0;
    QUdpSocket *audioSocket = nullptr;
    QUdpSocket *videoSocket = nullptr;
    QAudioOutput *audioOutput = nullptr;
    QIODevice *audioDevice = nullptr;
    QAudioInput *audioInput = nullptr;
    QIODevice *inputDevice = nullptr;
    QByteArray audioBuffer;
    QByteArray inputBuffer;
    QHostAddress t113Address;
    quint16 t113Port = 0;
    QPushButton *btn_hangup = nullptr;
    SlideHintWidget *hint = nullptr;
    QMediaPlayer *ringPlayer = nullptr;
    QAudioOutput *ringAudio = nullptr;

    void startAV();
    void stopAV();

private slots:
    void onAudioDataReady();
    void onInputReady();
    void onVideoDataReady();
    void writeAudioData();
    void onRingStateChanged(QMediaPlayer::State state);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
//    void mousePressEvent(QMouseEvent *event) override;
//    void mouseMoveEvent(QMouseEvent *event) override;
//    void mouseReleaseEvent(QMouseEvent *event) override;

signals:
    void answered();
    void accepted();
    void hangupClicked();
};

#endif // FORM_PHONECALL_H
