#include "notifier.h"
#include "face/recognition.h"
#include "config.h"
#include "ui/toast_window.h"

#include <cJSON.h>
#include <QDateTime>
#include <QDebug>
#include <cmath>

Notifier::Notifier(QObject* parent) : QObject(parent) {}

void Notifier::handlePayload(const QByteArray& payload) {
    if (!recognizer_ || !config_) return;

    // 解析 JSON
    cJSON* root = cJSON_Parse(payload.constData());
    if (!root || !cJSON_IsObject(root)) {
        if (root) cJSON_Delete(root);
        qWarning() << "MQTT payload JSON parse failed";
        return;
    }

    cJSON* jDev   = cJSON_GetObjectItem(root, "device_id");
    cJSON* jTrack = cJSON_GetObjectItem(root, "track_id");
    cJSON* jScore = cJSON_GetObjectItem(root, "score");
    cJSON* jEmb   = cJSON_GetObjectItem(root, "embedding");

    QString deviceId = (jDev && jDev->valuestring) ? QString::fromUtf8(jDev->valuestring) : QString();
    int trackId = jTrack ? jTrack->valueint : 0;
    double detectScore = jScore ? jScore->valuedouble : 0.0;

    // 解析 embedding 数组
    std::vector<float> emb;
    if (jEmb && cJSON_IsArray(jEmb)) {
        int n = cJSON_GetArraySize(jEmb);
        emb.reserve(n);
        for (int i = 0; i < n; ++i) {
            cJSON* item = cJSON_GetArrayItem(jEmb, i);
            emb.push_back(item ? (float)item->valuedouble : 0.f);
        }
    }
    cJSON_Delete(root);

    if (emb.empty()) {
        qWarning() << "MQTT payload has no embedding";
        return;
    }
    if ((int)emb.size() != 128) {
        qWarning() << "MQTT embedding dim != 128, got" << emb.size();
        return;
    }

    // 检测 score 下限
    if (detectScore < config_->minDetectionScore) {
        return;
    }

    // L2 归一化校验（文档说已归一化，但保险起见）
    double norm = 0.0;
    for (float v : emb) norm += (double)v * v;
    norm = std::sqrt(norm);
    if (norm > 1e-6 && (norm < 0.9 || norm > 1.1)) {
        for (float& v : emb) v /= (float)norm;
    }

    // 比对
    MatchResult mr = recognizer_->match(emb);
    QString timeStr = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

    if (mr.matched) {
        // 已知人员：冷却去重
        if (!recognizer_->shouldNotify(trackId, nowMs)) {
            emit detectionProcessed(
                QStringLiteral("识别: %1 (%2) [冷却中]").arg(mr.name).arg(mr.score, 0, 'f', 3),
                true, mr.score, mr.name);
            return;
        }
        int lvl = mr.alertLevel;
        if (lvl < 0 || lvl > 2) lvl = 0;
        const LevelStyle& ls = config_->levels[lvl];
        QString msg = formatMessage(config_->knownMessage, mr.name, mr.score,
                                    deviceId, trackId, timeStr);
        ToastWindow::show(ls.title.isEmpty() ? mr.name : ls.title, msg, ls.color, ls.durationMs);
        emit detectionProcessed(
            QStringLiteral("识别: %1 (相似度 %2)").arg(mr.name).arg(mr.score, 0, 'f', 3),
            true, mr.score, mr.name);
    } else {
        // 未知人员：缓存 embedding 供"从检测入库"
        lastUnknownEmb_ = emb;
        lastDeviceId_ = deviceId;
        lastTrackId_ = trackId;
        lastScore_ = detectScore;

        if (config_->alertOnUnknown) {
            if (!recognizer_->shouldNotify(trackId, nowMs)) {
                emit detectionProcessed(
                    QStringLiteral("未知人员 (检测 %1) [冷却中]").arg(detectScore, 0, 'f', 2),
                    false, mr.score, QString());
                return;
            }
            int lvl = config_->unknownAlertLevel;
            if (lvl < 0 || lvl > 2) lvl = 1;
            const LevelStyle& ls = config_->levels[lvl];
            QString msg = formatMessage(config_->unknownMessage, QStringLiteral("未知"),
                                        mr.score, deviceId, trackId, timeStr);
            ToastWindow::show(config_->unknownTitle, msg, ls.color, ls.durationMs);
        }
        emit detectionProcessed(
            QStringLiteral("未知人员 (相似度 %1)").arg(mr.score, 0, 'f', 3),
            false, mr.score, QString());
    }
}
