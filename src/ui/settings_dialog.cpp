#include "settings_dialog.h"
#include "../app.h"
#include "toast_window.h"

#include <QTabWidget>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QColorDialog>
#include <QDateTime>

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("设置"));
    setMinimumWidth(520);
    const Config& c = g_app->config;

    auto* tabs = new QTabWidget;
    auto* root = new QVBoxLayout(this);
    root->addWidget(tabs);

    // ---------- 常规 ----------
    {
        auto* form = new QFormLayout;
        minimizeOnClose_ = new QCheckBox(QStringLiteral("关闭按钮最小化到托盘"));
        singleInstance_  = new QCheckBox(QStringLiteral("禁止多开（单实例）"));
        minimizeOnClose_->setChecked(c.minimizeOnClose);
        singleInstance_->setChecked(c.singleInstance);
        form->addRow(minimizeOnClose_);
        form->addRow(singleInstance_);
        tabs->addTab(new QWidget, QStringLiteral("常规"));
        tabs->widget(tabs->count()-1)->setLayout(form);
    }
    // ---------- 识别 ----------
    {
        auto* form = new QFormLayout;
        threshold_ = new QDoubleSpinBox; threshold_->setRange(0.0, 1.0); threshold_->setSingleStep(0.01); threshold_->setDecimals(3);
        minScore_  = new QDoubleSpinBox; minScore_->setRange(0.0, 1.0);  minScore_->setSingleStep(0.05); minScore_->setDecimals(2);
        cooldown_  = new QSpinBox; cooldown_->setRange(0, 3600); cooldown_->setSuffix(QStringLiteral(" 秒"));
        alertUnknown_ = new QCheckBox(QStringLiteral("未知人脸也提醒"));
        unknownLevel_ = new QComboBox;
        unknownLevel_->addItem(QStringLiteral("0 - 普通"), 0);
        unknownLevel_->addItem(QStringLiteral("1 - 提醒"), 1);
        unknownLevel_->addItem(QStringLiteral("2 - 重要告警"), 2);
        threshold_->setValue(c.threshold);
        minScore_->setValue(c.minDetectionScore);
        cooldown_->setValue(c.cooldownSeconds);
        alertUnknown_->setChecked(c.alertOnUnknown);
        unknownLevel_->setCurrentIndex(c.unknownAlertLevel);
        form->addRow(QStringLiteral("识别阈值（余弦相似度）"), threshold_);
        form->addRow(QStringLiteral("最小检测 score"), minScore_);
        form->addRow(QStringLiteral("冷却去重时长"), cooldown_);
        form->addRow(alertUnknown_);
        form->addRow(QStringLiteral("未知人脸提醒级别"), unknownLevel_);
        tabs->addTab(new QWidget, QStringLiteral("识别"));
        tabs->widget(tabs->count()-1)->setLayout(form);
    }
    // ---------- 通知 ----------
    {
        auto* outer = new QVBoxLayout;
        auto* gb = new QGroupBox(QStringLiteral("提醒级别样式（标题 | 颜色RRGGBB | 停留毫秒(0=不自动关) | 声音）"));
        auto* gv = new QVBoxLayout(gb);
        for (int i = 0; i < 3; ++i) {
            auto* row = new QHBoxLayout;
            row->addWidget(new QLabel(QStringLiteral("级别 %1").arg(i)));
            lvlTitle_[i]    = new QLineEdit(c.levels[i].title);
            lvlColor_[i]    = new QLineEdit(c.levels[i].color.name().mid(1).toUpper());
            lvlDuration_[i] = new QSpinBox; lvlDuration_[i]->setRange(0, 999999); lvlDuration_[i]->setValue(c.levels[i].durationMs);
            lvlSound_[i]    = new QCheckBox(QStringLiteral("声音")); lvlSound_[i]->setChecked(c.levels[i].sound);
            auto* pickBtn   = new QPushButton(QStringLiteral("选色…"));
            auto* prevBtn   = new QPushButton(QStringLiteral("预览"));
            row->addWidget(new QLabel(QStringLiteral("标题"))); row->addWidget(lvlTitle_[i], 2);
            row->addWidget(new QLabel(QStringLiteral("颜色"))); row->addWidget(lvlColor_[i], 1);
            row->addWidget(new QLabel(QStringLiteral("停留"))); row->addWidget(lvlDuration_[i]);
            row->addWidget(lvlSound_[i]);
            row->addWidget(pickBtn);
            row->addWidget(prevBtn);
            gv->addLayout(row);
            connect(pickBtn, &QPushButton::clicked, this, [this, i]{ pickColor(i); });
            connect(prevBtn, &QPushButton::clicked, this, [this, i]{ onPreviewLevel(i); });
        }
        outer->addWidget(gb);
        auto* form = new QFormLayout;
        unknownTitle_   = new QLineEdit(c.unknownTitle);
        unknownMessage_ = new QLineEdit(c.unknownMessage);
        knownMessage_   = new QLineEdit(c.knownMessage);
        form->addRow(QStringLiteral("未知人脸标题"), unknownTitle_);
        form->addRow(QStringLiteral("未知人脸消息"), unknownMessage_);
        form->addRow(QStringLiteral("已知人脸消息"), knownMessage_);
        outer->addLayout(form);
        outer->addStretch();
        tabs->addTab(new QWidget, QStringLiteral("通知"));
        tabs->widget(tabs->count()-1)->setLayout(outer);
    }
    // ---------- MQTT ----------
    {
        auto* form = new QFormLayout;
        mqttEnabled_ = new QCheckBox(QStringLiteral("启用 MQTT 监听（V2 功能，启用后生效）"));
        host_     = new QLineEdit(c.host);
        port_     = new QSpinBox; port_->setRange(1, 65535); port_->setValue(c.port);
        topic_    = new QLineEdit(c.topic);
        clientId_ = new QLineEdit(c.clientId);
        username_ = new QLineEdit(c.username);
        password_ = new QLineEdit(c.password); password_->setEchoMode(QLineEdit::Password);
        qos_      = new QSpinBox; qos_->setRange(0, 2); qos_->setValue(c.qos);
        keepalive_= new QSpinBox; keepalive_->setRange(5, 3600); keepalive_->setValue(c.keepalive); keepalive_->setSuffix(QStringLiteral(" 秒"));
        reconnect_= new QSpinBox; reconnect_->setRange(1, 300); reconnect_->setValue(c.reconnectDelay); reconnect_->setSuffix(QStringLiteral(" 秒"));
        mqttEnabled_->setChecked(c.mqttEnabled);
        form->addRow(mqttEnabled_);
        form->addRow(QStringLiteral("Broker 地址"), host_);
        form->addRow(QStringLiteral("端口"), port_);
        form->addRow(QStringLiteral("订阅 Topic"), topic_);
        form->addRow(QStringLiteral("ClientID"), clientId_);
        form->addRow(QStringLiteral("用户名"), username_);
        form->addRow(QStringLiteral("密码"), password_);
        form->addRow(QStringLiteral("QoS"), qos_);
        form->addRow(QStringLiteral("KeepAlive"), keepalive_);
        form->addRow(QStringLiteral("重连间隔"), reconnect_);
        tabs->addTab(new QWidget, QStringLiteral("MQTT"));
        tabs->widget(tabs->count()-1)->setLayout(form);
    }

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    root->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &SettingsDialog::onOk);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void SettingsDialog::pickColor(int level) {
    QColor cur("#" + lvlColor_[level]->text());
    QColor c = QColorDialog::getColor(cur.isValid() ? cur : QColor("#2E7D32"), this);
    if (c.isValid()) lvlColor_[level]->setText(c.name().mid(1).toUpper());
}

void SettingsDialog::onPreviewLevel(int level) {
    QString title = lvlTitle_[level]->text();
    QColor col("#" + lvlColor_[level]->text());
    if (!col.isValid()) col = QColor("#2E7D32");
    int dur = lvlDuration_[level]->value();
    QString msg = QStringLiteral("这是级别 %1 的弹窗预览。\n时间：%2")
                      .arg(level).arg(QDateTime::currentDateTime().toString("hh:mm:ss"));
    ToastWindow::show(title.isEmpty() ? QStringLiteral("预览") : title, msg, col, dur);
}

void SettingsDialog::onOk() {
    Config& c = g_app->config;
    c.minimizeOnClose = minimizeOnClose_->isChecked();
    c.singleInstance  = singleInstance_->isChecked();
    c.threshold         = threshold_->value();
    c.minDetectionScore = minScore_->value();
    c.cooldownSeconds   = cooldown_->value();
    c.alertOnUnknown    = alertUnknown_->isChecked();
    c.unknownAlertLevel = unknownLevel_->currentData().toInt();
    for (int i = 0; i < 3; ++i) {
        c.levels[i].title      = lvlTitle_[i]->text();
        QColor col("#" + lvlColor_[i]->text());
        if (col.isValid()) c.levels[i].color = col;
        c.levels[i].durationMs = lvlDuration_[i]->value();
        c.levels[i].sound      = lvlSound_[i]->isChecked();
    }
    c.unknownTitle   = unknownTitle_->text();
    c.unknownMessage = unknownMessage_->text();
    c.knownMessage   = knownMessage_->text();
    c.mqttEnabled = mqttEnabled_->isChecked();
    c.host = host_->text(); c.port = port_->value(); c.topic = topic_->text();
    c.clientId = clientId_->text(); c.username = username_->text(); c.password = password_->text();
    c.qos = qos_->value(); c.keepalive = keepalive_->value(); c.reconnectDelay = reconnect_->value();

    c.save(g_app->configPath);
    // 立即生效
    g_app->recognizer.setThreshold(c.threshold);
    g_app->recognizer.setCooldown(c.cooldownSeconds);
    g_app->pipeline.configure(c);
    // V2: MQTT 配置变更后重启
    if (c.mqttEnabled) {
        g_app->startMqtt();
    } else {
        g_app->stopMqtt();
    }
    accept();
}
