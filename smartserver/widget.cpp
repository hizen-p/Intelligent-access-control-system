#include "widget.h"
#include "ui_widget.h"
#include <QRegion>
#include <QPainterPath>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QDateTime>
#include <QDate>
#include <QTime>
#include <QPixmap>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>


#include <QParallelAnimationGroup>

#include <QParallelAnimationGroup>
#include "form_phonecall.h"

Widget::Widget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Widget)
{
    ui->setupUi(this);
    offmark = false;

    // 设置窗口为无边框（可选）
    setWindowFlags(Qt::FramelessWindowHint);

    // 设置圆角
    int radius = 10; // 圆角半径
    QPainterPath path;
    path.addRoundedRect(rect(), radius, radius);
    setMask(QRegion(path.toFillPolygon().toPolygon()));

    // label_off 渐显渐隐动画初始化
    QGraphicsOpacityEffect *opacityEffect = new QGraphicsOpacityEffect(this);
    opacityEffect->setOpacity(0.0);
    ui->label_off->setGraphicsEffect(opacityEffect);
    ui->label_off->setVisible(false);

    // 天气功能集成
    requestWeather();

    // 系统时间定时器
    sysTimeTimer = new QTimer(this);
    connect(sysTimeTimer, &QTimer::timeout, this, &Widget::time_reflash);
    sysTimeTimer->start(1000); // 每秒刷新一次
    time_reflash(); // 启动时立即刷新一次

    // 启动TCP服务端监听8887
    tcpServer = new QTcpServer(this);
    connect(tcpServer, &QTcpServer::newConnection, this, &Widget::onNewTcpConnection);
    if (!tcpServer->listen(QHostAddress::Any, 8887)) {
        qDebug() << "TCP server failed to listen on port 8887:" << tcpServer->errorString();
    } else {
        qDebug() << "TCP server listening on port 8887";
    }


    ui->widget_light->setVisible(false);
    // 连接btn_home点击信号到槽函数

    serialPort = new QSerialPort(this);
    // 设置串口名称
    serialPort->setPortName("COM24");  // Linux串口设备
    // 设置波特率
    serialPort->setBaudRate(QSerialPort::Baud9600);
    // 设置数据位
    serialPort->setDataBits(QSerialPort::Data8);
    // 设置奇偶校验
    serialPort->setParity(QSerialPort::NoParity);
    // 设置停止位
    serialPort->setStopBits(QSerialPort::OneStop);
    // 设置流控制
    serialPort->setFlowControl(QSerialPort::NoFlowControl);
    // 打开串口
    if (serialPort->open(QIODevice::ReadWrite)) {
        qDebug() << "串口打开成功";
    } else {
        qDebug() << "串口打开失败:" << serialPort->errorString();
    }

}

Widget::~Widget()
{
    delete ui;
}

void Widget::on_btn_power_clicked()
{
    if(!offmark){
        ui->widget_main->setStyleSheet(
                "#widget_main {"
                "background-color: black;"
                "border-image: none;"
                "}"
            );
    }else{
        ui->widget_main->setStyleSheet(
            "#widget_main {"
            "background-color: rgb(255, 255, 255);"
            "border-image: url(:/pic/smartbg.png);"
            "}"
        );
    }
    offmark = !offmark;
}

void Widget::on_btn_offline_clicked()
{
    auto *effect = qobject_cast<QGraphicsOpacityEffect *>(ui->label_off->graphicsEffect());
    if (!effect) return;

    QPropertyAnimation *animation = new QPropertyAnimation(effect, "opacity", this);
    animation->setDuration(500); // 500ms 动画

    if (!ui->label_off->isVisible()) {
        ui->label_off->setVisible(true);
        animation->setStartValue(0.0);
        animation->setEndValue(1.0);
    } else {
        animation->setStartValue(1.0);
        animation->setEndValue(0.0);
        connect(animation, &QPropertyAnimation::finished, [this]() {
            ui->label_off->setVisible(false);
        });
    }
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}

void Widget::requestWeather()
{
    QNetworkAccessManager *manager = new QNetworkAccessManager(this);
    QUrl url("http://t.weather.itboy.net/api/weather/city/101230101"); // 替换为你需要的城市代码
    QNetworkRequest req(url);
    reply = manager->get(req);
    connect(reply, &QNetworkReply::finished, this, &Widget::readHttpRply);
}

void Widget::readHttpRply()
{
    int resCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() == QNetworkReply::NoError && resCode == 200) {
        QByteArray data = reply->readAll();
        QJsonDocument jsonResponse = QJsonDocument::fromJson(data);
        QJsonObject jsonObject = jsonResponse.object();

        QString city = jsonObject["cityInfo"].toObject()["city"].toString();
        QJsonObject today = jsonObject["data"].toObject()["forecast"].toArray()[0].toObject();
        QString weatherType = today["type"].toString();
        QString high = today["high"].toString().replace("高温 ", "");
        QString low = today["low"].toString().replace("低温 ", "");
        QString wind = today["fx"].toString();
        QString notice = today["notice"].toString();

        QString weatherText = QString("%1，今日天气%2，\n高温%3，低温%4，\n%5\n每日提醒：%6")
            .arg(city)
            .arg(weatherType)
            .arg(high)
            .arg(low)
            .arg(wind)
            .arg(notice);

        if (this->ui->label_weather)
            this->ui->label_weather->setText(weatherText);
    }
}

void Widget::time_reflash()
{
    QDateTime currentTime = QDateTime::currentDateTime();
    QDate date = currentTime.date();
    int year = date.year();
    int month = date.month();
    int day = date.day();
    QTime time = currentTime.time();
    int hour = time.hour();
    int minute = time.minute();
    int second = time.second();
    myDate = QString("%1-%2-%3").arg(year, 4, 10, QChar('0')).arg(month, 2, 10, QChar('0')).arg(day, 2, 10, QChar('0'));
    myTime = QString("%1:%2:%3").arg(hour, 2, 10, QChar('0')).arg(minute, 2, 10, QChar('0')).arg(second, 2, 10, QChar('0'));
    if (ui->labelDate)
        ui->labelDate->setText(myDate);
    if (ui->labelTime_2)
        ui->labelTime_2->setText(myTime);
}

void Widget::onUdpDataReceived()
{
    while (udpSocket && udpSocket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(int(udpSocket->pendingDatagramSize()));
        QHostAddress sender;
        quint16 senderPort;
        udpSocket->readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);
        QPixmap pix;
        pix.loadFromData(datagram, "JPG");
        if (!pix.isNull() && camActive) {
            if (firstShow || !ui->labelcam->isVisible()) {
                QRect endRect = ui->labelcam->geometry();
                QRect startRect = endRect;
                startRect.setWidth(endRect.width() * 0.2);
                startRect.setHeight(endRect.height() * 0.2);
                startRect.moveCenter(endRect.center());
                ui->labelcam->setGeometry(startRect);
                ui->labelcam->setVisible(true);
                QGraphicsOpacityEffect *effect = new QGraphicsOpacityEffect(ui->labelcam);
                ui->labelcam->setGraphicsEffect(effect);
                effect->setOpacity(0.0);
                QPropertyAnimation *opacityAnim = new QPropertyAnimation(effect, "opacity");
                opacityAnim->setDuration(1000);
                opacityAnim->setStartValue(0.0);
                opacityAnim->setEndValue(1.0);
                QPropertyAnimation *scaleAnim = new QPropertyAnimation(ui->labelcam, "geometry");
                scaleAnim->setDuration(1000);
                scaleAnim->setStartValue(startRect);
                scaleAnim->setEndValue(endRect);
                scaleAnim->setEasingCurve(QEasingCurve::OutElastic);
                QParallelAnimationGroup *group = new QParallelAnimationGroup(this);
                group->addAnimation(opacityAnim);
                group->addAnimation(scaleAnim);
                group->start(QAbstractAnimation::DeleteWhenStopped);
                firstShow = false;
            }
            ui->labelcam->setPixmap(pix.scaled(ui->labelcam->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
    }
}

// 新增btn_doorcam点击槽函数实现
void Widget::on_btngetframe_clicked()
{
    if (!udpSocket) {
        udpSocket = new QUdpSocket(this);
        quint16 localPort = 50000; // 本地端口，和服务端推送端口一致
        if (!udpSocket->bind(QHostAddress::Any, localPort, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
            qDebug() << "UDP bind failed:" << udpSocket->errorString();
            delete udpSocket;
            udpSocket = nullptr;
            return;
        }
        connect(udpSocket, &QUdpSocket::readyRead, this, &Widget::onUdpDataReceived);
        qDebug() << "UDP socket started on port" << localPort;
    }

    QHostAddress cameraAddr(DEVICE_IP);
    quint16 cameraPort = 50000;

    if (!camActive) {
        // 开始接收
        QByteArray requestData = "getframe";
        udpSocket->writeDatagram(requestData, cameraAddr, cameraPort);
        qDebug() << "Sent UDP 'getframe' to camera";
        firstShow = true; // 开启时重置动画标志
        camActive = true;
    } else {
        // 停止接收
        QByteArray requestData = "stop";
        udpSocket->writeDatagram(requestData, cameraAddr, cameraPort);
        qDebug() << "Sent UDP 'stop' to camera";
        ui->labelcam->setVisible(false); // 直接隐藏
        camActive = false;
        firstShow = true; // 下次再开时重新动画
    }
}

void Widget::onNewTcpConnection()
{
    while (tcpServer->hasPendingConnections()) {
        QTcpSocket *client = tcpServer->nextPendingConnection();
        tcpClients.insert(client);
        connect(client, &QTcpSocket::readyRead, this, [this, client]() { onTcpDataReceived(client); });
        connect(client, &QTcpSocket::disconnected, this, [this, client]() {
            tcpClients.remove(client);
            client->deleteLater();
            qDebug() << "TCP client disconnected:" << client;
        });
        qDebug() << "New TCP client connected from" << client->peerAddress().toString();
    }
}

void Widget::onTcpDataReceived(QTcpSocket *client)
{
    QByteArray data = client->readAll();
    qDebug() << "Received TCP data from" << client << ":" << data;
    if (data.trimmed() == "call") {
        bool playRing = !ui->label_off->isVisible();
        if (!callForm) {
            callForm = new Form_phonecall(nullptr, playRing); // 不设parent，顶层窗口
            connect(callForm, &Form_phonecall::accepted, this, [this, client](){
                if (client) {
                    client->write("accept\n");
                    client->flush();
                    qDebug() << "Sent TCP: accept";
                }
            });
            connect(callForm, &Form_phonecall::hangupClicked, this, [this, client](){
                if (client) {
                    client->write("recstop");
                    qDebug() << "send recstop";
                    client->flush();
                }
            });
            connect(callForm, &QObject::destroyed, this, [this]() { callForm = nullptr; });
            callForm->setAttribute(Qt::WA_DeleteOnClose);
            callForm->show();
            // 居中显示在widget_main范围内
            QRect mainRect = ui->widget_main->geometry();
            QPoint globalTopLeft = ui->widget_main->mapToGlobal(mainRect.topLeft());
            QRect globalRect(globalTopLeft, mainRect.size());
            QSize callSize = callForm->size();
            QPoint center = globalRect.center();
            QPoint topLeft(center.x() - callSize.width()/2, center.y() - callSize.height()/2);
            callForm->move(topLeft);
        }
    } else if (data.trimmed() == "stopsend") {
        if (callForm) {
            callForm->close();
            callForm = nullptr;
        }
    } else if (data.trimmed() == "unlock") {
        // 这里可以处理unlock指令
    }
}

void Widget::on_btn_lock_clicked()
{
    // 群发unlock到所有客户端
    for (QTcpSocket *client : tcpClients) {
        if (client && client->state() == QAbstractSocket::ConnectedState) {
            client->write("unlock\n");
            client->flush();
            qDebug() << "Sent TCP: unlock to client" << client;
        }
    }
}

void Widget::on_btn_home_clicked()
{
    if(ui->widget_light->isVisible()){
        qDebug()<<"novisabel";
        ui->widget_light->setVisible(false);
    }else{
        ui->widget_light->setVisible(true);
        qDebug()<<"sabel";
    }
}


void Widget::on_btn_tea_clicked()
{
    // 茶桌圆灯
    if (serialPort && serialPort->isOpen()) {
        serialPort->write((char*)hexArrayDeskRound, sizeof(hexArrayDeskRound));
        qDebug() << "发送茶桌圆灯控制命令";
    } else {
        qDebug() << "串口未打开，无法发送命令";
    }
}

void Widget::on_btn_teab_clicked()
{
    // 工位圆灯
    if (serialPort && serialPort->isOpen()) {
        serialPort->write((char*)hexArrayWorkRound, sizeof(hexArrayWorkRound));
        qDebug() << "发送工位圆灯控制命令";
    } else {
        qDebug() << "串口未打开，无法发送命令";
    }
}

void Widget::on_btn_white_clicked()
{
    // 茶桌灯
    if (serialPort && serialPort->isOpen()) {
        serialPort->write((char*)hexArrayDesk, sizeof(hexArrayDesk));
        qDebug() << "发送茶桌灯控制命令";
    } else {
        qDebug() << "串口未打开，无法发送命令";
    }
}
