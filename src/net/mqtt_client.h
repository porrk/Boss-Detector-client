#pragma once
#include <QObject>
#include <QByteArray>
#include <QString>

class QTcpSocket;
class QTimer;

// MQTT 3.1.1 订阅客户端（用 QTcpSocket 手写，零外部依赖）
// 只做订阅 + 接收 PUBLISH，不做发布
class MqttClient : public QObject {
    Q_OBJECT
public:
    enum State { Disconnected = 0, Connecting = 1, Connected = 2, Subscribed = 3 };

    explicit MqttClient(QObject* parent = nullptr);
    ~MqttClient() override;

    void configure(const QString& host, int port, const QString& clientId,
                   const QString& username, const QString& password,
                   const QString& topic, int qos, int keepalive, int reconnectDelay);
    void start();
    void stop();
    State state() const { return state_; }

signals:
    void messageReceived(const QString& topic, const QByteArray& payload);
    void stateChanged(int state);

private slots:
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketReadyRead();
    void onPingTimer();
    void onReconnectTimer();

private:
    void tryConnect();
    void setState(State s);
    void scheduleReconnect();
    void sendConnect();
    void sendSubscribe();
    void sendPing();
    void sendPuback(quint16 packetId);
    void sendDisconnect();
    void processBuffer();

    static QByteArray encodeRemainingLength(quint32 len);
    // 解码剩余长度；返回值，consumed=消耗字节数，ok=是否完整
    static quint32 decodeRemainingLength(const QByteArray& buf, int& consumed, bool& ok);

    QTcpSocket* sock_ = nullptr;
    QTimer*     pingTimer_ = nullptr;
    QTimer*     reconnectTimer_ = nullptr;

    QString host_, clientId_, username_, password_, topic_;
    int port_ = 1883;
    int qos_ = 1;
    int keepalive_ = 60;
    int reconnectDelay_ = 5;
    bool started_ = false;

    State       state_ = Disconnected;
    QByteArray  rxbuf_;          // 接收缓冲（处理粘包/拆包）
    quint16     nextPacketId_ = 1;
};
