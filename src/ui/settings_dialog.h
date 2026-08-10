#pragma once
#include "../config.h"
#include <QDialog>

class QCheckBox; class QDoubleSpinBox; class QSpinBox; class QLineEdit; class QComboBox;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);
private slots:
    void onOk();
    void onPreviewLevel(int level);
    void pickColor(int level);
private:
    // 常规
    QCheckBox* minimizeOnClose_ = nullptr;
    QCheckBox* singleInstance_  = nullptr;
    // 识别
    QDoubleSpinBox* threshold_  = nullptr;
    QDoubleSpinBox* minScore_   = nullptr;
    QSpinBox*  cooldown_        = nullptr;
    QCheckBox* alertUnknown_    = nullptr;
    QComboBox* unknownLevel_    = nullptr;
    // 通知
    QLineEdit* lvlTitle_[3];
    QLineEdit* lvlColor_[3];
    QSpinBox*  lvlDuration_[3];
    QCheckBox* lvlSound_[3];
    QLineEdit* unknownTitle_    = nullptr;
    QLineEdit* unknownMessage_  = nullptr;
    QLineEdit* knownMessage_    = nullptr;
    // MQTT
    QCheckBox* mqttEnabled_     = nullptr;
    QLineEdit* host_            = nullptr;
    QSpinBox*  port_            = nullptr;
    QLineEdit* topic_           = nullptr;
    QLineEdit* clientId_        = nullptr;
    QLineEdit* username_        = nullptr;
    QLineEdit* password_        = nullptr;
    QSpinBox*  qos_             = nullptr;
    QSpinBox*  keepalive_       = nullptr;
    QSpinBox*  reconnect_       = nullptr;
};
