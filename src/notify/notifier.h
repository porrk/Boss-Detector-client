#pragma once
#include <QObject>
#include <QByteArray>
#include <QString>
#include <vector>

class Recognizer;
struct Config;

// 识别结果 -> 弹窗映射器（V2）
// 收到 MQTT payload -> 解析 embedding -> 比对 -> 按级别弹窗
class Notifier : public QObject {
    Q_OBJECT
public:
    explicit Notifier(QObject* parent = nullptr);

    void setRecognizer(Recognizer* r) { recognizer_ = r; }
    void setConfig(const Config* c)   { config_ = c; }

    // 处理 MQTT payload（JSON，按 message_parse.md 格式）
    void handlePayload(const QByteArray& payload);

    // 最近一次未知检测的 embedding，供"从检测入库"使用
    bool hasLastUnknown() const { return !lastUnknownEmb_.empty(); }
    const std::vector<float>& lastUnknownEmbedding() const { return lastUnknownEmb_; }
    QString lastUnknownDeviceId() const { return lastDeviceId_; }
    int     lastUnknownTrackId() const { return lastTrackId_; }
    double  lastUnknownScore() const { return lastScore_; }
    void    clearLastUnknown() { lastUnknownEmb_.clear(); }

signals:
    // 每次处理后发出，供状态栏/最近检测列表显示
    void detectionProcessed(const QString& summary, bool matched, double score,
                            const QString& name);

private:
    Recognizer* recognizer_ = nullptr;
    const Config* config_ = nullptr;

    std::vector<float> lastUnknownEmb_;
    QString lastDeviceId_;
    int     lastTrackId_ = 0;
    double  lastScore_ = 0.0;
};
