#pragma once
#include <QString>
#include <QColor>

struct LevelStyle {
    QString title;
    QColor  color;
    int     durationMs = 4000;
    bool    sound = false;
};

struct Config {
    Config();

    // general
    bool minimizeOnClose = true;
    bool singleInstance  = true;

    // face / preprocessing
    QString modelPath   = "models/face_recognition_sface_2021dec.onnx";
    double  detectMinScore = 0.5;
    int     alignSize   = 112;
    double  inputMean   = 127.5;
    double  inputStd    = 128.0;
    bool    swapRb      = true;   // true => 喂给模型 RGB

    // recognition
    double  threshold          = 0.5;
    double  minDetectionScore  = 0.5;
    int     cooldownSeconds    = 30;
    bool    alertOnUnknown     = true;
    int     unknownAlertLevel  = 1;

    // 聚合（多模板合并为加权平均聚合体）
    double  aggregateLowThreshold  = 0.20;  // 新特征与聚合体相似度低于此值警告(可能贴错人)
    double  aggregateHighThreshold = 0.95;  // 高于此值提示(可能重复录入)
    double  faceMatchThreshold     = 0.50;  // 新增人员时撞脸检查阈值
    int     aggregateSoftCap       = 10;    // 每人样本数软上限

    // mqtt
    bool    mqttEnabled   = false;
    QString host;
    int     port          = 1883;
    QString topic         = "facedetect/events";
    QString clientId      = "face_client";
    QString username;
    QString password;
    int     qos           = 1;
    int     keepalive     = 60;
    int     reconnectDelay= 5;

    // storage
    QString dbPath = "templates.db";

    // ui
    int popupDurationMs = 6000;
    int popupMaxVisible = 3;

    // notifications
    LevelStyle levels[3];
    QString unknownTitle   = QStringLiteral("未知人员");
    QString unknownMessage = QStringLiteral("检测到未登记人员 | 设备:{device_id} | 置信度:{score:.2f} | 时间:{time}");
    QString knownMessage   = QStringLiteral("识别到:{name} | 设备:{device_id} | 相似度:{score:.2f} | 时间:{time}");

    void load(const QString& iniPath);
    void save(const QString& iniPath);
};

// 若 config.ini 不存在，从内置资源 :/default_config.ini 生成
bool ensureConfigExists(const QString& path);

// 相对路径按 baseDir 解析；绝对路径原样返回
QString resolvePath(const QString& baseDir, const QString& p);

// 消息模板占位符替换：{name}{device_id}{track_id}{score}{score:.2f}{time}
QString formatMessage(QString tmpl, const QString& name, double score,
                      const QString& deviceId, int trackId, const QString& timeStr);
