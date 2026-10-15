#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>
#include <QNetworkReply>
#include <QTimer>
#include <QUdpSocket>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QTcpServer>
#include <QTcpSocket>
#include <QSerialPort>
#include <QSet>

#include "form_phonecall.h"
// 本机IP 当前运行在虚拟机上 所以为虚拟机的IP
#define SELF_IP   "192.168.1.4"
// 设备IP 为T113终端设备当前的IP
#define DEVICE_IP "192.168.1.18"

QT_BEGIN_NAMESPACE
namespace Ui { class Widget; }
QT_END_NAMESPACE

class Widget : public QWidget
{
    Q_OBJECT

public:
    Widget(QWidget *parent = nullptr);
    ~Widget();

private slots:
    void on_btn_power_clicked();
    void on_btn_offline_clicked();
    void readHttpRply();
    void requestWeather();
    void time_reflash();
    void onUdpDataReceived();
    void onNewTcpConnection();
    void onTcpDataReceived(QTcpSocket *client);


    void on_btngetframe_clicked();
    void on_btn_lock_clicked();
    void on_btn_home_clicked();

    void on_btn_tea_clicked();

    void on_btn_teab_clicked();

    void on_btn_white_clicked();

private:
    Ui::Widget *ui;
    bool offmark;
    QNetworkReply *reply = nullptr;
    QTimer *sysTimeTimer = nullptr;
    QString myDate;
    QString myTime;
    QUdpSocket *udpSocket = nullptr;
    bool camActive = false;
    bool firstShow = true;
    QTcpServer *tcpServer = nullptr;
    // QTcpSocket *tcpClientSocket = nullptr;
    QSet<QTcpSocket*> tcpClients;
    Form_phonecall *callForm = nullptr;
    QSerialPort *serialPort;
    // 茶桌灯
    unsigned char hexArrayDesk[7] = {0xFD, 0x03, 0x4C, 0x12, 0x04, 0x5D, 0xDF};
    // 工位灯
    unsigned char hexArrayWork[7] = {0xFD, 0x03, 0x4C, 0x12, 0x02, 0x5D, 0xDF};
    // 工位圆灯
    unsigned char hexArrayWorkRound[7] = {0xFD, 0x03, 0x97, 0x36, 0xF4, 0x4F, 0xDF};
    // 茶桌圆灯
    unsigned char hexArrayDeskRound[7] = {0xFD, 0x03, 0x97, 0x36, 0xF2, 0x50, 0xDF};
    // 墙面射灯
    unsigned char hexArrayWall[7] = {0xFD, 0x03, 0x97, 0x36, 0xF1, 0x50, 0xDF};
};
#endif // WIDGET_H
