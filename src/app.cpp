#include "app.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QDir>
#include <QDebug>

App* g_app = nullptr;

bool App::loadConfigOnly() {
    baseDir = QCoreApplication::applicationDirPath();
    configPath = baseDir + "/config.ini";
    ensureConfigExists(configPath);
    config.load(configPath);
    return true;
}

bool App::initServices() {
    // 数据库
    QString dbPath = resolvePath(baseDir, config.dbPath);
    if (!db.open(dbPath)) {
        qWarning() << "打开数据库失败:" << db.lastError() << "path=" << dbPath;
        return false;
    }

    // 模型
    QString modelPath = resolvePath(baseDir, config.modelPath);
    if (!sface.load(modelPath)) {
        qWarning() << "加载 SFace 模型失败:" << sface.lastError() << "path=" << modelPath;
        // 非致命：GUI 仍可用，但入库会失败
    }

    // 流水线 / 识别器
    pipeline.configure(config);
    pipeline.setSFace(&sface);
    recognizer.setThreshold(config.threshold);
    recognizer.setCooldown(config.cooldownSeconds);
    reloadTemplates();

    // V2: 装配 Notifier
    notifier.setRecognizer(&recognizer);
    notifier.setConfig(&config);

    servicesOk_ = true;
    // V2: 启动 MQTT（如果配置启用）
    if (config.mqttEnabled) {
        startMqtt();
    }
    return true;
}

void App::startMqtt() {
    stopMqtt();
    if (!config.mqttEnabled || config.host.isEmpty()) return;
    mqtt.configure(config.host, config.port, config.clientId,
                   config.username, config.password, config.topic,
                   config.qos, config.keepalive, config.reconnectDelay);
    mqtt.start();
}

void App::stopMqtt() {
    mqtt.stop();
}

void App::reloadTemplates() {
    auto tpls = db.listAll();
    recognizer.setTemplates(tpls);
}
