#include "mqtt_client.h"

#include <QTcpSocket>
#include <QTimer>
#include <QDebug>

// MQTT 3.1.1 包类型（高 4 位）
enum MqttType {
    T_CONNECT = 1, T_CONNACK = 2, T_PUBLISH = 3, T_PUBACK = 4,
    T_SUBSCRIBE = 8, T_SUBACK = 9, T_UNSUBSCRIBE = 10, T_UNSUBACK = 11,
    T_PINGREQ = 12, T_PINGRESP = 13, T_DISCONNECT = 14
};

MqttClient::MqttClient(QObject* parent) : QObject(parent) {
    sock_ = new QTcpSocket(this);
    pingTimer_ = new QTimer(this);
    pingTimer_->setSingleShot(false);
    reconnectTimer_ = new QTimer(this);
    reconnectTimer_->setSingleShot(true);

    connect(sock_, &QTcpSocket::connected, this, &MqttClient::onSocketConnected);
    connect(sock_, &QTcpSocket::disconnected, this, &MqttClient::onSocketDisconnected);
    connect(sock_, &QTcpSocket::readyRead, this, &MqttClient::onSocketReadyRead);
    connect(sock_, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::error),
            this, [this](QAbstractSocket::SocketError) {
        qWarning() << "MQTT socket error:" << sock_->errorString();
    });
    connect(pingTimer_, &QTimer::timeout, this, &MqttClient::onPingTimer);
    connect(reconnectTimer_, &QTimer::timeout, this, &MqttClient::onReconnectTimer);
}

MqttClient::~MqttClient() {
    stop();
}

void MqttClient::configure(const QString& host, int port, const QString& clientId,
                           const QString& username, const QString& password,
                           const QString& topic, int qos, int keepalive, int reconnectDelay) {
    host_ = host; port_ = port; clientId_ = clientId;
    username_ = username; password_ = password;
    topic_ = topic; qos_ = qos; keepalive_ = keepalive; reconnectDelay_ = reconnectDelay;
}

void MqttClient::start() {
    started_ = true;
    tryConnect();
}

void MqttClient::stop() {
    started_ = false;
    pingTimer_->stop();
    reconnectTimer_->stop();
    if (sock_->state() == QAbstractSocket::ConnectedState) {
        sendDisconnect();
        sock_->flush();
    }
    sock_->abort();
    rxbuf_.clear();
    setState(Disconnected);
}

void MqttClient::tryConnect() {
    if (!started_) return;
    if (host_.isEmpty()) {
        qWarning() << "MQTT host empty, cannot connect";
        return;
    }
    setState(Connecting);
    qDebug() << "MQTT connecting to" << host_ << ":" << port_;
    sock_->abort();
    sock_->connectToHost(host_, port_);
}

void MqttClient::setState(State s) {
    if (state_ != s) {
        state_ = s;
        emit stateChanged((int)s);
    }
}

void MqttClient::scheduleReconnect() {
    if (!started_) return;
    qDebug() << "MQTT will retry in" << reconnectDelay_ << "s";
    reconnectTimer_->start(reconnectDelay_ * 1000);
}

void MqttClient::onReconnectTimer() {
    tryConnect();
}

// ---------- 剩余长度编解码 ----------

QByteArray MqttClient::encodeRemainingLength(quint32 len) {
    QByteArray out;
    do {
        quint8 b = len % 128;
        len /= 128;
        if (len > 0) b |= 128;
        out.append((char)b);
    } while (len > 0);
    return out;
}

quint32 MqttClient::decodeRemainingLength(const QByteArray& buf, int& consumed, bool& ok) {
    ok = false;
    quint32 multiplier = 1;
    quint32 value = 0;
    consumed = 0;
    for (int i = 0; i < 4 && i < buf.size(); ++i) {
        quint8 b = (quint8)buf[i];
        value += (b & 127) * multiplier;
        multiplier *= 128;
        ++consumed;
        if ((b & 128) == 0) {
            ok = true;
            return value;
        }
    }
    return 0; // 不完整或超长
}

// ---------- 发包 ----------

void MqttClient::sendConnect() {
    QByteArray varHeader;
    // Protocol Name "MQTT"
    varHeader.append((char)0x00); varHeader.append((char)0x04);
    varHeader.append("MQTT", 4);
    // Protocol Level (3.1.1 = 4)
    varHeader.append((char)0x04);
    // Connect Flags
    quint8 flags = 0x02; // Clean Session
    if (!username_.isEmpty()) flags |= 0x80;
    if (!password_.isEmpty()) flags |= 0x40;
    varHeader.append((char)flags);
    // Keep Alive
    varHeader.append((char)((keepalive_ >> 8) & 0xFF));
    varHeader.append((char)(keepalive_ & 0xFF));

    QByteArray payload;
    auto addStr = [&payload](const QString& s) {
        QByteArray u = s.toUtf8();
        payload.append((char)((u.size() >> 8) & 0xFF));
        payload.append((char)(u.size() & 0xFF));
        payload.append(u);
    };
    addStr(clientId_.isEmpty() ? QStringLiteral("face_client") : clientId_);
    if (!username_.isEmpty()) addStr(username_);
    if (!password_.isEmpty()) addStr(password_);

    QByteArray body = varHeader + payload;
    QByteArray packet;
    packet.append((char)0x10); // CONNECT
    packet.append(encodeRemainingLength((quint32)body.size()));
    packet.append(body);
    sock_->write(packet);
}

void MqttClient::sendSubscribe() {
    quint16 packetId = nextPacketId_++;
    QByteArray body;
    // Packet Identifier
    body.append((char)((packetId >> 8) & 0xFF));
    body.append((char)(packetId & 0xFF));
    // Topic Filter
    QByteArray tf = topic_.toUtf8();
    body.append((char)((tf.size() >> 8) & 0xFF));
    body.append((char)(tf.size() & 0xFF));
    body.append(tf);
    // QoS
    body.append((char)(qos_ & 0x03));

    QByteArray packet;
    packet.append((char)0x82); // SUBSCRIBE (bit 1 reserved = 1)
    packet.append(encodeRemainingLength((quint32)body.size()));
    packet.append(body);
    sock_->write(packet);
    qDebug() << "MQTT SUBSCRIBE sent, topic:" << topic_ << "qos:" << qos_;
}

void MqttClient::sendPing() {
    sock_->write(QByteArray("\xC0\x00", 2)); // PINGREQ
}

void MqttClient::sendPuback(quint16 packetId) {
    QByteArray packet;
    packet.append((char)0x40); // PUBACK
    packet.append((char)0x02);
    packet.append((char)((packetId >> 8) & 0xFF));
    packet.append((char)(packetId & 0xFF));
    sock_->write(packet);
}

void MqttClient::sendDisconnect() {
    sock_->write(QByteArray("\xE0\x00", 2)); // DISCONNECT
}

// ---------- 收包 ----------

void MqttClient::onSocketConnected() {
    qDebug() << "MQTT TCP connected, sending CONNECT";
    rxbuf_.clear();
    sendConnect();
}

void MqttClient::onSocketDisconnected() {
    qDebug() << "MQTT TCP disconnected";
    pingTimer_->stop();
    setState(Disconnected);
    if (started_) scheduleReconnect();
}

void MqttClient::onSocketReadyRead() {
    rxbuf_.append(sock_->readAll());
    processBuffer();
}

void MqttClient::onPingTimer() {
    if (state_ == Connected || state_ == Subscribed) {
        sendPing();
    }
}

void MqttClient::processBuffer() {
    while (!rxbuf_.isEmpty()) {
        if (rxbuf_.size() < 2) break; // 至少 2 字节才能开始解析
        quint8 firstByte = (quint8)rxbuf_[0];
        // 解码剩余长度（从第 2 字节开始）
        int rlConsumed = 0;
        bool rlOk = false;
        quint32 remainingLen = decodeRemainingLength(rxbuf_.mid(1), rlConsumed, rlOk);
        if (!rlOk) break; // 剩余长度字段不完整，等更多数据

        int totalLen = 1 + rlConsumed + (int)remainingLen;
        if (rxbuf_.size() < totalLen) break; // 包体不完整，等更多数据

        // 提取一个完整包
        QByteArray packet = rxbuf_.left(totalLen);
        rxbuf_.remove(0, totalLen);

        quint8 type = (firstByte >> 4) & 0x0F;
        QByteArray body = packet.mid(1 + rlConsumed); // 去掉固定头

        switch (type) {
        case T_CONNACK: {
            if (body.size() >= 2) {
                quint8 returnCode = (quint8)body[1];
                if (returnCode == 0) {
                    qDebug() << "MQTT CONNACK accepted";
                    setState(Connected);
                    sendSubscribe();
                    int pingInterval = keepalive_ > 0 ? keepalive_ : 60;
                    pingTimer_->start(pingInterval * 1000);
                } else {
                    qWarning() << "MQTT CONNACK rejected, code:" << returnCode;
                    sock_->abort();
                    scheduleReconnect();
                }
            }
            break;
        }
        case T_SUBACK: {
            qDebug() << "MQTT SUBACK received -> Subscribed";
            setState(Subscribed);
            break;
        }
        case T_PUBLISH: {
            quint8 qos = (firstByte >> 1) & 0x03;
            if (body.size() < 2) break;
            int topicLen = ((quint8)body[0] << 8) | (quint8)body[1];
            if (body.size() < 2 + topicLen) break;
            QString topic = QString::fromUtf8(body.mid(2, topicLen));
            int payloadStart = 2 + topicLen;
            quint16 packetId = 0;
            if (qos > 0) {
                if (body.size() < payloadStart + 2) break;
                packetId = ((quint8)body[payloadStart] << 8) | (quint8)body[payloadStart + 1];
                payloadStart += 2;
            }
            QByteArray payload = body.mid(payloadStart);
            emit messageReceived(topic, payload);
            if (qos == 1) sendPuback(packetId);
            break;
        }
        case T_PINGRESP:
            // 心跳响应，无需处理
            break;
        default:
            qDebug() << "MQTT unhandled packet type:" << type;
            break;
        }
    }
}
