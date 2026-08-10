#include "main_window.h"
#include "../app.h"
#include "../net/mqtt_client.h"
#include "../notify/notifier.h"
#include "template_dialog.h"
#include "settings_dialog.h"
#include "toast_window.h"

#include <QTableWidget>
#include <QHeaderView>
#include <QToolBar>
#include <QAction>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QMessageBox>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QCloseEvent>
#include <QApplication>
#include <QStyle>
#include <QIcon>
#include <QStatusBar>
#include <QHBoxLayout>
#include <QLabel>
#include <algorithm>

#include <cJSON.h>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("人脸识别客户端"));
    resize(820, 540);

    table_ = new QTableWidget(this);
    table_->setColumnCount(5);
    table_->setHorizontalHeaderLabels(
        { QStringLiteral("ID"), QStringLiteral("姓名"), QStringLiteral("提醒级别"),
          QStringLiteral("备注"), QStringLiteral("创建时间") });
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->verticalHeader()->setVisible(false);
    setCentralWidget(table_);

    createActions();
    createToolBar();
    createTray();

    connect(table_, &QTableWidget::itemSelectionChanged,
            this, &MainWindow::onTableSelectionChanged);

    refreshTable();
    onTableSelectionChanged();

    // V2: 状态栏 - MQTT 状态 + 最近检测
    mqttStatus_ = new QLabel(QStringLiteral("MQTT: 未启用"));
    mqttStatus_->setStyleSheet("padding: 0 8px;");
    lastDetectLbl_ = new QLabel(QStringLiteral("等待检测..."));
    lastDetectLbl_->setStyleSheet("padding: 0 8px;");
    statusBar()->addPermanentWidget(lastDetectLbl_);
    statusBar()->addPermanentWidget(mqttStatus_);

    // V2: 连接 MQTT 状态 + 检测结果信号
    connect(&g_app->mqtt, &MqttClient::stateChanged, this, &MainWindow::onMqttStateChanged);
    connect(&g_app->notifier, &Notifier::detectionProcessed, this, &MainWindow::onDetectionProcessed);
    onMqttStateChanged((int)g_app->mqtt.state());
}

void MainWindow::createActions() {
    addAct_ = new QAction(QStringLiteral("新增模板"), this);
    addAct_->setShortcut(QKeySequence::New);
    connect(addAct_, &QAction::triggered, this, &MainWindow::onAdd);

    editAct_ = new QAction(QStringLiteral("编辑"), this);
    connect(editAct_, &QAction::triggered, this, &MainWindow::onEdit);

    delAct_ = new QAction(QStringLiteral("删除"), this);
    delAct_->setShortcut(QKeySequence::Delete);
    connect(delAct_, &QAction::triggered, this, &MainWindow::onDelete);

    importAct_ = new QAction(QStringLiteral("导入JSON"), this);
    connect(importAct_, &QAction::triggered, this, &MainWindow::onImport);

    exportAct_ = new QAction(QStringLiteral("导出JSON"), this);
    connect(exportAct_, &QAction::triggered, this, &MainWindow::onExport);

    testToastAct_ = new QAction(QStringLiteral("测试弹窗"), this);
    connect(testToastAct_, &QAction::triggered, this, &MainWindow::onTestToast);

    settingsAct_ = new QAction(QStringLiteral("设置"), this);
    connect(settingsAct_, &QAction::triggered, this, &MainWindow::onSettings);

    quitAct_ = new QAction(QStringLiteral("退出"), this);
    connect(quitAct_, &QAction::triggered, this, &MainWindow::onQuit);

    saveDetectAct_ = new QAction(QStringLiteral("从检测入库"), this);
    saveDetectAct_->setEnabled(false);
    connect(saveDetectAct_, &QAction::triggered, this, &MainWindow::onSaveDetectionAsTemplate);
}

void MainWindow::createToolBar() {
    auto* tb = addToolBar(QStringLiteral("main"));
    tb->setMovable(false);
    tb->addAction(addAct_);
    tb->addAction(editAct_);
    tb->addAction(delAct_);
    tb->addSeparator();
    tb->addAction(importAct_);
    tb->addAction(exportAct_);
    tb->addSeparator();
    tb->addAction(testToastAct_);
    tb->addAction(settingsAct_);
    tb->addSeparator();
    tb->addAction(saveDetectAct_);
    tb->addSeparator();
    tb->addAction(quitAct_);
}

void MainWindow::createTray() {
    QIcon icon = style()->standardIcon(QStyle::SP_ComputerIcon);
    tray_ = new QSystemTrayIcon(icon, this);
    tray_->setToolTip(QStringLiteral("人脸识别客户端"));
    auto* menu = new QMenu(this);
    showAct_ = new QAction(QStringLiteral("显示主界面"), this);
    connect(showAct_, &QAction::triggered, this, [this]{
        showNormal(); raise(); activateWindow();
    });
    menu->addAction(showAct_);
    menu->addSeparator();
    menu->addAction(quitAct_);
    tray_->setContextMenu(menu);
    connect(tray_, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason r){ onTrayActivated((int)r); });
    tray_->show();
}

void MainWindow::onTrayActivated(int reason) {
    if (reason == QSystemTrayIcon::DoubleClick) {
        showNormal(); raise(); activateWindow();
    }
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (g_app->config.minimizeOnClose && !forceQuit_) {
        e->ignore();
        hide();
        if (tray_ && tray_->isVisible())
            tray_->showMessage(QStringLiteral("人脸识别客户端"),
                QStringLiteral("已最小化到托盘，双击图标可恢复。"),
                QSystemTrayIcon::Information, 3000);
        return;
    }
    e->accept();
}

void MainWindow::onQuit() {
    forceQuit_ = true;
    if (tray_) tray_->hide();
    close();
    qApp->quit();
}

void MainWindow::refreshTable() {
    rows_ = g_app->db.listAll();
    std::sort(rows_.begin(), rows_.end(),
        [](const FaceTemplate& a, const FaceTemplate& b){ return a.name < b.name; });
    table_->setRowCount((int)rows_.size());
    for (int i = 0; i < (int)rows_.size(); ++i) {
        const auto& t = rows_[i];
        table_->setItem(i, 0, new QTableWidgetItem(QString::number(t.id)));
        table_->setItem(i, 1, new QTableWidgetItem(t.name));
        table_->setItem(i, 2, new QTableWidgetItem(QString::number(t.alertLevel)));
        table_->setItem(i, 3, new QTableWidgetItem(t.note));
        table_->setItem(i, 4, new QTableWidgetItem(
            QDateTime::fromSecsSinceEpoch(t.createdAt).toString("yyyy-MM-dd hh:mm")));
    }
    table_->resizeColumnsToContents();
    table_->horizontalHeader()->setStretchLastSection(true);
    statusBar()->showMessage(QStringLiteral("共 %1 人").arg(rows_.size()));
    g_app->reloadTemplates();
}

int MainWindow::currentId() const {
    int row = table_->currentRow();
    if (row < 0 || row >= (int)rows_.size()) return -1;
    return (int)rows_[row].id;
}

void MainWindow::onTableSelectionChanged() {
    bool has = table_->currentRow() >= 0;
    editAct_->setEnabled(has);
    delAct_->setEnabled(has);
}

void MainWindow::onAdd() {
    FaceTemplate empty;
    TemplateDialog dlg(true, empty, this);
    if (dlg.exec() == QDialog::Accepted) {
        refreshTable();
    }
}

void MainWindow::onEdit() {
    int row = table_->currentRow();
    if (row < 0 || row >= (int)rows_.size()) return;
    TemplateDialog dlg(false, rows_[row], this);
    if (dlg.exec() == QDialog::Accepted) {
        refreshTable();
    }
}

void MainWindow::onDelete() {
    int id = currentId();
    if (id < 0) return;
    auto ret = QMessageBox::question(this, QStringLiteral("确认删除"),
        QStringLiteral("确定删除该模板？此操作不可撤销。"));
    if (ret != QMessageBox::Yes) return;
    if (!g_app->db.remove(id)) {
        QMessageBox::warning(this, QStringLiteral("失败"),
            QStringLiteral("删除失败：") + g_app->db.lastError());
        return;
    }
    refreshTable();
}

void MainWindow::onImport() {
    QString path = QFileDialog::getOpenFileName(this, QStringLiteral("导入模板 JSON"),
        QString(), QStringLiteral("JSON (*.json)"));
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, QStringLiteral("失败"), QStringLiteral("无法读取文件"));
        return;
    }
    QByteArray data = f.readAll();
    f.close();
    cJSON* root = cJSON_Parse(data.constData());
    if (!root || !cJSON_IsArray(root)) {
        QMessageBox::warning(this, QStringLiteral("失败"), QStringLiteral("JSON 格式错误，应为数组"));
        if (root) cJSON_Delete(root);
        return;
    }
    int n = cJSON_GetArraySize(root);
    int okCount = 0;
    for (int i = 0; i < n; ++i) {
        cJSON* o = cJSON_GetArrayItem(root, i);
        if (!cJSON_IsObject(o)) continue;
        FaceTemplate t;
        cJSON* jName = cJSON_GetObjectItem(o, "name");
        cJSON* jEmb  = cJSON_GetObjectItem(o, "embedding");
        if (!jName || !jEmb || !cJSON_IsArray(jEmb)) continue;
        t.name = QString::fromUtf8(jName->valuestring ? jName->valuestring : "");
        char* s = cJSON_PrintUnformatted(jEmb);
        std::vector<float> e = embeddingFromJson(QString::fromUtf8(s ? s : "[]"));
        if (s) cJSON_free(s);
        if (e.empty()) continue;
        l2Normalize(e);
        t.embeddingJson = embeddingToJson(e);
        cJSON* jm = cJSON_GetObjectItem(o, "embedding_model");
        cJSON* jl = cJSON_GetObjectItem(o, "alert_level");
        cJSON* jn = cJSON_GetObjectItem(o, "note");
        t.embeddingModel = jm && jm->valuestring ? QString::fromUtf8(jm->valuestring)
                                                 : QStringLiteral("opencv_sface_2021dec");
        t.alertLevel = jl ? jl->valueint : 0;
        t.note = jn && jn->valuestring ? QString::fromUtf8(jn->valuestring) : QString();
        if (g_app->db.add(t)) {
            g_app->db.addSample(t.id, e, 1.0);
            ++okCount;
        }
    }
    cJSON_Delete(root);
    refreshTable();
    QMessageBox::information(this, QStringLiteral("导入完成"),
        QStringLiteral("成功导入 %1 / %2 条").arg(okCount).arg(n));
}

void MainWindow::onExport() {
    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出模板 JSON"),
        QStringLiteral("templates_export.json"), QStringLiteral("JSON (*.json)"));
    if (path.isEmpty()) return;
    cJSON* root = cJSON_CreateArray();
    for (const auto& t : rows_) {
        cJSON* o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "name", t.name.toUtf8().constData());
        cJSON* emb = cJSON_Parse(t.embeddingJson.toUtf8().constData());
        if (emb) cJSON_AddItemToObject(o, "embedding", emb);
        cJSON_AddStringToObject(o, "embedding_model", t.embeddingModel.toUtf8().constData());
        cJSON_AddNumberToObject(o, "alert_level", t.alertLevel);
        cJSON_AddStringToObject(o, "note", t.note.toUtf8().constData());
        cJSON_AddItemToArray(root, o);
    }
    char* s = cJSON_Print(root);
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(s ? s : "[]");
        f.close();
    } else {
        QMessageBox::warning(this, QStringLiteral("失败"), QStringLiteral("无法写入文件"));
    }
    if (s) cJSON_free(s);
    cJSON_Delete(root);
}

void MainWindow::onTestToast() {
    const Config& c = g_app->config;
    const LevelStyle& ls = c.levels[1];
    QString timeStr = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    QString msg = formatMessage(c.knownMessage, QStringLiteral("张三(示例)"), 0.92,
                                QStringLiteral("edge-01"), 1, timeStr);
    ToastWindow::show(ls.title.isEmpty() ? QStringLiteral("测试弹窗") : ls.title,
                      msg, ls.color, ls.durationMs);
}

void MainWindow::onSettings() {
    SettingsDialog dlg(this);
    dlg.exec();
}

// ---------------- V2 ----------------

void MainWindow::onMqttStateChanged(int state) {
    QString text;
    switch (state) {
    case MqttClient::Disconnected: text = QStringLiteral("MQTT: 已断开"); break;
    case MqttClient::Connecting:   text = QStringLiteral("MQTT: 连接中..."); break;
    case MqttClient::Connected:    text = QStringLiteral("MQTT: 已连接"); break;
    case MqttClient::Subscribed:   text = QStringLiteral("MQTT: 已订阅"); break;
    default: break;
    }
    if (!g_app->config.mqttEnabled) text = QStringLiteral("MQTT: 未启用");
    mqttStatus_->setText(text);
}

void MainWindow::onDetectionProcessed(const QString& summary, bool matched,
                                       double score, const QString& name) {
    lastDetectLbl_->setText(summary);
    // 有未知检测缓存时启用"从检测入库"
    saveDetectAct_->setEnabled(!matched && g_app->notifier.hasLastUnknown());
}

void MainWindow::onSaveDetectionAsTemplate() {
    if (!g_app->notifier.hasLastUnknown()) {
        QMessageBox::information(this, QStringLiteral("提示"),
            QStringLiteral("暂无可入库的检测数据。请等待未知人员被检测到。"));
        return;
    }
    QString note = QStringLiteral("从MQTT检测入库 (设备:%1, track:%2)")
                       .arg(g_app->notifier.lastUnknownDeviceId())
                       .arg(g_app->notifier.lastUnknownTrackId());
    FaceTemplate empty;
    TemplateDialog dlg(true, empty, this);
    dlg.prefillFromDetection(g_app->notifier.lastUnknownEmbedding(),
                             g_app->notifier.lastUnknownScore(), note);
    if (dlg.exec() == QDialog::Accepted) {
        g_app->notifier.clearLastUnknown();
        saveDetectAct_->setEnabled(false);
        refreshTable();
    }
}
