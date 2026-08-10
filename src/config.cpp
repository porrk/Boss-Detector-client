#include "config.h"

#include <QSettings>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QStringList>
#include <QTextCodec>

Config::Config() {
    levels[0] = { QStringLiteral("已知人员"), QColor("#2E7D32"), 4000, false };
    levels[1] = { QStringLiteral("提醒"),     QColor("#F9A825"), 6000, true  };
    levels[2] = { QStringLiteral("重要告警"), QColor("#C62828"), 0,    true  };
}

bool ensureConfigExists(const QString& path) {
    if (QFile::exists(path)) return true;
    QFile src(":/default_config.ini");
    if (!src.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    QByteArray data = src.readAll();
    // 确保目录存在
    QFileInfo fi(path);
    QDir().mkpath(fi.absolutePath());
    QFile dst(path);
    if (!dst.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    dst.write(data);
    dst.close();
    return true;
}

QString resolvePath(const QString& baseDir, const QString& p) {
    if (p.isEmpty()) return p;
    QFileInfo fi(p);
    if (fi.isAbsolute()) return p;
    return QDir(baseDir).filePath(p);
}

static int asInt(const QSettings& s, const char* key, int def) {
    return s.value(QString::fromLatin1(key), def).toInt();
}
static double asDouble(const QSettings& s, const char* key, double def) {
    return s.value(QString::fromLatin1(key), def).toDouble();
}
static bool asBool(const QSettings& s, const char* key, int def) {
    return s.value(QString::fromLatin1(key), def).toInt() != 0;
}

void Config::load(const QString& iniPath) {
    QSettings s(iniPath, QSettings::IniFormat);
    s.setIniCodec("UTF-8");

    minimizeOnClose = asBool(s, "general/minimize_on_close", 1);
    singleInstance  = asBool(s, "general/single_instance", 1);

    modelPath      = s.value("face/model_path", modelPath).toString();
    detectMinScore = asDouble(s, "face/detect_min_score", detectMinScore);
    alignSize      = asInt(s, "face/align_size", alignSize);
    inputMean      = asDouble(s, "face/input_mean", inputMean);
    inputStd       = asDouble(s, "face/input_std", inputStd);
    swapRb         = asBool(s, "face/swap_rb", 1);

    threshold         = asDouble(s, "recognition/threshold", threshold);
    minDetectionScore = asDouble(s, "recognition/min_detection_score", minDetectionScore);
    cooldownSeconds   = asInt(s, "recognition/cooldown_seconds", cooldownSeconds);
    alertOnUnknown    = asBool(s, "recognition/alert_on_unknown", 1);
    unknownAlertLevel = asInt(s, "recognition/unknown_alert_level", unknownAlertLevel);

    aggregateLowThreshold  = asDouble(s, "recognition/aggregate_low_threshold", aggregateLowThreshold);
    aggregateHighThreshold = asDouble(s, "recognition/aggregate_high_threshold", aggregateHighThreshold);
    faceMatchThreshold     = asDouble(s, "recognition/face_match_threshold", faceMatchThreshold);
    aggregateSoftCap       = asInt(s, "recognition/aggregate_soft_cap", aggregateSoftCap);

    mqttEnabled  = asBool(s, "mqtt/enabled", 0);
    host         = s.value("mqtt/host", host).toString();
    port         = asInt(s, "mqtt/port", 1883);
    topic        = s.value("mqtt/topic", topic).toString();
    clientId     = s.value("mqtt/client_id", clientId).toString();
    username     = s.value("mqtt/username", username).toString();
    password     = s.value("mqtt/password", password).toString();
    qos          = asInt(s, "mqtt/qos", 1);
    keepalive    = asInt(s, "mqtt/keepalive", 60);
    reconnectDelay = asInt(s, "mqtt/reconnect_delay", 5);

    dbPath = s.value("storage/db_path", dbPath).toString();

    popupDurationMs = asInt(s, "ui/popup_duration_ms", 6000);
    popupMaxVisible = asInt(s, "ui/popup_max_visible", 3);

    unknownTitle   = s.value("notifications/unknown_title", unknownTitle).toString();
    unknownMessage = s.value("notifications/unknown_message", unknownMessage).toString();
    knownMessage   = s.value("notifications/known_message", knownMessage).toString();

    for (int i = 0; i < 3; ++i) {
        const LevelStyle& d = levels[i];
        QString hex = d.color.name().mid(1).toUpper(); // 去掉 '#'
        QString defLine = QString("%1|%2|%3|%4")
            .arg(d.title, hex).arg(d.durationMs).arg(d.sound ? 1 : 0);
        QString line = s.value(QString("notifications/level_%1").arg(i), defLine).toString();
        QStringList parts = line.split('|');
        LevelStyle ls = d;
        if (parts.size() >= 1 && !parts[0].isEmpty()) ls.title = parts[0];
        if (parts.size() >= 2) {
            QColor c("#" + parts[1]);
            if (c.isValid()) ls.color = c;
        }
        if (parts.size() >= 3) ls.durationMs = parts[2].toInt();
        if (parts.size() >= 4) ls.sound = parts[3].toInt() != 0;
        levels[i] = ls;
    }
}

void Config::save(const QString& iniPath) {
    QSettings s(iniPath, QSettings::IniFormat);
    s.setIniCodec("UTF-8");

    s.setValue("general/minimize_on_close", minimizeOnClose ? 1 : 0);
    s.setValue("general/single_instance",  singleInstance  ? 1 : 0);

    s.setValue("face/model_path",        modelPath);
    s.setValue("face/detect_min_score",  detectMinScore);
    s.setValue("face/align_size",        alignSize);
    s.setValue("face/input_mean",        inputMean);
    s.setValue("face/input_std",         inputStd);
    s.setValue("face/swap_rb",           swapRb ? 1 : 0);

    s.setValue("recognition/threshold",          threshold);
    s.setValue("recognition/min_detection_score", minDetectionScore);
    s.setValue("recognition/cooldown_seconds",    cooldownSeconds);
    s.setValue("recognition/alert_on_unknown",    alertOnUnknown ? 1 : 0);
    s.setValue("recognition/unknown_alert_level", unknownAlertLevel);
    s.setValue("recognition/aggregate_low_threshold",  aggregateLowThreshold);
    s.setValue("recognition/aggregate_high_threshold", aggregateHighThreshold);
    s.setValue("recognition/face_match_threshold",     faceMatchThreshold);
    s.setValue("recognition/aggregate_soft_cap",       aggregateSoftCap);

    s.setValue("mqtt/enabled", mqttEnabled ? 1 : 0);
    s.setValue("mqtt/host", host);
    s.setValue("mqtt/port", port);
    s.setValue("mqtt/topic", topic);
    s.setValue("mqtt/client_id", clientId);
    s.setValue("mqtt/username", username);
    s.setValue("mqtt/password", password);
    s.setValue("mqtt/qos", qos);
    s.setValue("mqtt/keepalive", keepalive);
    s.setValue("mqtt/reconnect_delay", reconnectDelay);

    s.setValue("storage/db_path", dbPath);

    s.setValue("ui/popup_duration_ms", popupDurationMs);
    s.setValue("ui/popup_max_visible", popupMaxVisible);

    s.setValue("notifications/unknown_title",   unknownTitle);
    s.setValue("notifications/unknown_message", unknownMessage);
    s.setValue("notifications/known_message",   knownMessage);

    for (int i = 0; i < 3; ++i) {
        QString hex = levels[i].color.name().mid(1).toUpper();
        s.setValue(QString("notifications/level_%1").arg(i),
                   QString("%1|%2|%3|%4")
                       .arg(levels[i].title, hex)
                       .arg(levels[i].durationMs)
                       .arg(levels[i].sound ? 1 : 0));
    }
    s.sync();
}

QString formatMessage(QString tmpl, const QString& name, double score,
                      const QString& deviceId, int trackId, const QString& timeStr) {
    tmpl.replace(QStringLiteral("{name}"),       name);
    tmpl.replace(QStringLiteral("{device_id}"),  deviceId);
    tmpl.replace(QStringLiteral("{track_id}"),   QString::number(trackId));
    tmpl.replace(QStringLiteral("{score:.2f}"),  QString::number(score, 'f', 2));
    tmpl.replace(QStringLiteral("{score}"),      QString::number(score, 'f', 2));
    tmpl.replace(QStringLiteral("{time}"),       timeStr);
    return tmpl;
}
