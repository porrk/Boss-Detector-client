#pragma once
#include "../db.h"
#include <QMainWindow>
#include <vector>

class QTableWidget; class QSystemTrayIcon; class QAction; class QCloseEvent; class QLabel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
protected:
    void closeEvent(QCloseEvent* e) override;
private slots:
    void onAdd();
    void onEdit();
    void onDelete();
    void onImport();
    void onExport();
    void onTestToast();
    void onSettings();
    void onQuit();
    void onTrayActivated(int reason);
    void onTableSelectionChanged();
    void refreshTable();
    // V2
    void onMqttStateChanged(int state);
    void onDetectionProcessed(const QString& summary, bool matched, double score, const QString& name);
    void onSaveDetectionAsTemplate();
private:
    void createActions();
    void createToolBar();
    void createTray();
    int  currentId() const;

    QTableWidget*    table_     = nullptr;
    QSystemTrayIcon* tray_      = nullptr;
    QAction* addAct_      = nullptr;
    QAction* editAct_     = nullptr;
    QAction* delAct_      = nullptr;
    QAction* importAct_   = nullptr;
    QAction* exportAct_   = nullptr;
    QAction* testToastAct_= nullptr;
    QAction* settingsAct_ = nullptr;
    QAction* quitAct_     = nullptr;
    QAction* showAct_     = nullptr;
    QAction* saveDetectAct_ = nullptr;  // V2: 从最近检测入库
    QLabel*  mqttStatus_    = nullptr;  // V2: MQTT 连接状态
    QLabel*  lastDetectLbl_ = nullptr;  // V2: 最近检测摘要
    bool forceQuit_ = false;
    std::vector<FaceTemplate> rows_;
};
