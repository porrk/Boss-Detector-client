#pragma once
#include "config.h"
#include "db.h"
#include "onnx/sface_runner.h"
#include "face/face_pipeline.h"
#include "face/recognition.h"
#include "net/mqtt_client.h"
#include "notify/notifier.h"
#include <QString>

// 全局应用上下文：持有配置、数据库、推理引擎、流水线、识别器
class App {
public:
    Config       config;
    Db           db;
    SFaceRunner  sface;
    FacePipeline pipeline;
    Recognizer   recognizer;
    MqttClient   mqtt;
    Notifier     notifier;

    QString baseDir;        // exe 所在目录
    QString configPath;     // baseDir/config.ini

    bool loadConfigOnly();  // 仅加载配置（供单实例判断）
    bool initServices();    // 打开库、加载模型、装配流水线
    void reloadTemplates(); // 重新拉取模板并刷新识别器缓存
    void startMqtt();       // 按当前配置启动/重启 MQTT（V2）
    void stopMqtt();

private:
    bool servicesOk_ = false;
};

extern App* g_app;
