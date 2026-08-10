#pragma once
#include "../db.h"
#include "../face/face_pipeline.h"
#include <QDialog>
#include <QImage>
#include <vector>
#include <memory>

class QLabel; class QLineEdit; class QComboBox; class QTextEdit; class QPushButton;
class QStackedWidget;
template <typename T> class QFutureWatcher;

class TemplateDialog : public QDialog {
    Q_OBJECT
public:
    TemplateDialog(bool addMode, const FaceTemplate& initial, QWidget* parent = nullptr);
    FaceTemplate result() const { return result_; }

    // 议题5：从检测入库预填 embedding（跳过选图/检测）
    void prefillFromDetection(const std::vector<float>& emb, double score,
                              const QString& note = QString());
private slots:
    void onPickImage();
    void onDetect();
    void onPipelineDone();
    void onOk();
    void onModeChanged();
    void onExistingNameChanged();
private:
    void setPreview(const QImage& img);
    void syncFromExisting();   // 归入模式：按选中姓名回填级别/备注/样本数
    // 归入现有人员（含低/高相似度弹窗 + 软上限 + 写库）；返回 false 表示用户放弃
    bool doAggregate(const QString& name);
    // 新增人员（写库）
    bool doNewPerson(const QString& name);

    bool addMode_;
    bool prefilled_ = false;      // 从检测预填
    FaceTemplate initial_;
    FaceTemplate result_;

    QComboBox*   modeCombo_ = nullptr;   // 新增人员 / 归入现有人员（仅 addMode）
    QStackedWidget* nameStack_ = nullptr;
    QLineEdit*   nameEdit_  = nullptr;   // page 0
    QComboBox*   nameCombo_ = nullptr;   // page 1（现有人员）
    QComboBox*   levelCombo_ = nullptr;
    QTextEdit*   noteEdit_  = nullptr;
    QLabel*      imagePreview_ = nullptr;
    QLabel*      statusLabel_  = nullptr;
    QPushButton* pickBtn_  = nullptr;
    QPushButton* detectBtn_ = nullptr;
    QPushButton* okBtn_    = nullptr;

    std::vector<QString>      existingNames_;
    std::vector<FaceTemplate> existingTemplates_; // 缓存：撞脸/聚合查询

    QString imagePath_;
    QImage  previewImg_;
    std::vector<float> embedding_;
    double  embedScore_ = 0.0;   // 检测置信度，用于加权
    QFutureWatcher<FacePipeline::Result>* watcher_ = nullptr;
};
