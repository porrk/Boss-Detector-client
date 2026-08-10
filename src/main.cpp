#include "app.h"
#include "ui/main_window.h"

#include <QApplication>
#include <QSharedMemory>
#include <QMessageBox>
#include <QStyle>
#include <QIcon>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("face_client");
    QApplication::setOrganizationName("earendil");

    App a;
    g_app = &a;
    a.loadConfigOnly();

    // 单实例（按配置）
    QSharedMemory shm("face_client_singleton_mutex_v1");
    if (a.config.singleInstance) {
        // attach 探测是否已有实例
        if (shm.attach()) {
            shm.detach();
            QMessageBox::information(nullptr, QStringLiteral("已在运行"),
                QStringLiteral("人脸识别客户端已在运行，请从系统托盘恢复。"));
            return 0;
        }
        if (!shm.create(16)) {
            QMessageBox::information(nullptr, QStringLiteral("已在运行"),
                QStringLiteral("人脸识别客户端已在运行。"));
            return 0;
        }
    }

    if (!a.initServices()) {
        QMessageBox::critical(nullptr, QStringLiteral("初始化失败"),
            QStringLiteral("初始化失败：") + a.db.lastError() +
            QStringLiteral("\n请检查 config.ini 与 templates.db 路径。"));
        return 1;
    }

    QIcon icon = QApplication::style()->standardIcon(QStyle::SP_ComputerIcon);
    QApplication::setWindowIcon(icon);

    MainWindow w;
    w.setWindowIcon(icon);
    w.show();

    // V2: MQTT 消息 -> Notifier 处理（识别比对 + 分级弹窗）
    QObject::connect(&a.mqtt, &MqttClient::messageReceived,
                     &a.notifier, [&a](const QString&, const QByteArray& payload) {
        a.notifier.handlePayload(payload);
    });

    return app.exec();
}
