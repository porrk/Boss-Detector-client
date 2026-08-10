#include "template_dialog.h"
#include "../app.h"
#include "../config.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QTextEdit>
#include <QPushButton>
#include <QFileDialog>
#include <QMessageBox>
#include <QPixmap>
#include <QStackedWidget>
#include <QFutureWatcher>
#include <QtConcurrent>

TemplateDialog::TemplateDialog(bool addMode, const FaceTemplate& initial, QWidget* parent)
    : QDialog(parent), addMode_(addMode), initial_(initial)
{
    setWindowTitle(addMode_ ? QStringLiteral("新增模板") : QStringLiteral("编辑模板"));
    setMinimumWidth(460);

    existingNames_ = g_app->db.listDistinctNames();
    existingTemplates_ = g_app->db.listAll();

    auto* root = new QVBoxLayout(this);

    // ---- 模式切换（仅新增）----
    if (addMode_) {
        modeCombo_ = new QComboBox;
        modeCombo_->addItem(QStringLiteral("➕ 新增人员"));
        modeCombo_->addItem(QStringLiteral("⇄ 归入现有人员"));
        if (existingNames_.empty()) modeCombo_->setVisible(false); // 无人可归入
        root->addWidget(modeCombo_);
    }

    // ---- 图片区 ----
    imagePreview_ = new QLabel(QStringLiteral("（未选择图片）"));
    imagePreview_->setAlignment(Qt::AlignCenter);
    imagePreview_->setMinimumHeight(240);
    imagePreview_->setFrameShape(QFrame::Box);
    imagePreview_->setStyleSheet("background:#222;color:#aaa;");

    pickBtn_  = new QPushButton(QStringLiteral("选择图片…"));
    detectBtn_ = new QPushButton(QStringLiteral("检测并提取特征"));
    detectBtn_->setEnabled(false);
    auto* imgRow = new QHBoxLayout;
    imgRow->addWidget(pickBtn_);
    imgRow->addWidget(detectBtn_);
    imgRow->addStretch();

    // ---- 表单 ----
    auto* form = new QFormLayout;
    nameStack_ = new QStackedWidget;
    nameEdit_  = new QLineEdit;
    nameCombo_ = new QComboBox;
    for (const auto& n : existingNames_) nameCombo_->addItem(n);
    nameStack_->addWidget(nameEdit_);   // index 0
    nameStack_->addWidget(nameCombo_);  // index 1
    nameStack_->setCurrentIndex(0);

    levelCombo_ = new QComboBox;
    levelCombo_->addItem(QStringLiteral("0 - 已知人员（普通）"), 0);
    levelCombo_->addItem(QStringLiteral("1 - 提醒"), 1);
    levelCombo_->addItem(QStringLiteral("2 - 重要告警"), 2);
    noteEdit_ = new QTextEdit;
    noteEdit_->setMaximumHeight(70);
    form->addRow(QStringLiteral("姓名"), nameStack_);
    form->addRow(QStringLiteral("提醒级别"), levelCombo_);
    form->addRow(QStringLiteral("备注"), noteEdit_);

    statusLabel_ = new QLabel(QStringLiteral("请选择一张含正脸的图片"));
    statusLabel_->setWordWrap(true);

    okBtn_ = new QPushButton(QStringLiteral("确定"));
    auto* cancelBtn = new QPushButton(QStringLiteral("取消"));
    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch();
    btnRow->addWidget(okBtn_);
    btnRow->addWidget(cancelBtn);

    root->addWidget(imagePreview_);
    root->addLayout(imgRow);
    root->addLayout(form);
    root->addWidget(statusLabel_);
    root->addLayout(btnRow);

    connect(pickBtn_, &QPushButton::clicked, this, &TemplateDialog::onPickImage);
    connect(detectBtn_, &QPushButton::clicked, this, &TemplateDialog::onDetect);
    connect(okBtn_, &QPushButton::clicked, this, &TemplateDialog::onOk);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    if (modeCombo_)
        connect(modeCombo_, qOverload<int>(&QComboBox::currentIndexChanged),
                this, &TemplateDialog::onModeChanged);
    connect(nameCombo_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &TemplateDialog::onExistingNameChanged);

    watcher_ = new QFutureWatcher<FacePipeline::Result>(this);
    connect(watcher_, &QFutureWatcher<FacePipeline::Result>::finished,
            this, &TemplateDialog::onPipelineDone);

    // 编辑模式预填
    if (!addMode_) {
        nameEdit_->setText(initial_.name);
        levelCombo_->setCurrentIndex(initial_.alertLevel);
        noteEdit_->setPlainText(initial_.note);
        if (!initial_.embeddingJson.isEmpty()) {
            embedding_ = embeddingFromJson(initial_.embeddingJson);
            statusLabel_->setText(QStringLiteral("已保留原有特征（%1 维）；重新选图检测可重置为单样本")
                                  .arg(embedding_.size()));
        }
        okBtn_->setEnabled(true);
    } else {
        okBtn_->setEnabled(false);
        onModeChanged();
    }
}

void TemplateDialog::prefillFromDetection(const std::vector<float>& emb, double score,
                                          const QString& note) {
    prefilled_ = true;
    embedding_ = emb;
    l2Normalize(embedding_);
    embedScore_ = score;
    imagePreview_->setVisible(false);
    pickBtn_->setVisible(false);
    detectBtn_->setVisible(false);
    if (!note.isEmpty()) noteEdit_->setPlainText(note);
    okBtn_->setEnabled(true);
    statusLabel_->setText(QStringLiteral("已从检测预填特征（%1 维，置信度 %2），可直接入库")
                          .arg(embedding_.size()).arg(score, 0, 'f', 2));
}

void TemplateDialog::setPreview(const QImage& img) {
    previewImg_ = img;
    QPixmap pm = QPixmap::fromImage(img);
    if (!pm.isNull())
        imagePreview_->setPixmap(pm.scaledToWidth(420, Qt::SmoothTransformation));
}

void TemplateDialog::onPickImage() {
    QString path = QFileDialog::getOpenFileName(this, QStringLiteral("选择图片"),
        QString(), QStringLiteral("图片 (*.jpg *.jpeg *.png *.bmp *.webp)"));
    if (path.isEmpty()) return;
    imagePath_ = path;
    QImage img(path);
    if (img.isNull()) {
        QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("无法解码该图片"));
        return;
    }
    setPreview(img.convertToFormat(QImage::Format_RGB888));
    embedding_.clear();
    detectBtn_->setEnabled(true);
    statusLabel_->setText(QStringLiteral("已选择图片，点击“检测并提取特征”"));
    okBtn_->setEnabled(false);
}

void TemplateDialog::onDetect() {
    if (imagePath_.isEmpty()) return;
    if (!g_app->sface.isLoaded()) {
        QMessageBox::warning(this, QStringLiteral("模型未加载"),
            QStringLiteral("SFace 模型未加载：") + g_app->sface.lastError() +
            QStringLiteral("\n请检查 config.ini 中 face/model_path。"));
        return;
    }
    detectBtn_->setEnabled(false);
    okBtn_->setEnabled(false);
    statusLabel_->setText(QStringLiteral("检测中…（可能需要 0.1~1 秒）"));
    QString path = imagePath_;
    watcher_->setFuture(QtConcurrent::run([path]() {
        return g_app->pipeline.processImage(path);
    }));
}

void TemplateDialog::onPipelineDone() {
    FacePipeline::Result r = watcher_->result();
    detectBtn_->setEnabled(true);
    if (!r.ok) {
        statusLabel_->setText(QStringLiteral("失败：") + r.error);
        return;
    }
    embedding_ = std::move(r.embedding);
    embedScore_ = r.bestScore;
    setPreview(r.preview);
    statusLabel_->setText(QStringLiteral("✓ 提取成功，特征维度 %1，检测置信度 %2")
                          .arg(embedding_.size()).arg(r.bestScore, 0, 'f', 2));
    okBtn_->setEnabled(true);
}

void TemplateDialog::onModeChanged() {
    if (!modeCombo_) return;
    bool agg = modeCombo_->currentIndex() == 1;
    nameStack_->setCurrentIndex(agg ? 1 : 0);
    // 归入模式：级别/备注只读（聚合只加样本，改元数据请用编辑）
    levelCombo_->setEnabled(!agg);
    noteEdit_->setEnabled(!agg);
    if (agg) {
        syncFromExisting();
    } else {
        statusLabel_->setText(prefilled_
            ? QStringLiteral("已从检测预填特征，可直接入库")
            : QStringLiteral("请选择图片并提取特征"));
    }
}

void TemplateDialog::onExistingNameChanged() {
    if (modeCombo_ && modeCombo_->currentIndex() == 1) syncFromExisting();
}

void TemplateDialog::syncFromExisting() {
    QString name = nameCombo_->currentText().trimmed();
    if (name.isEmpty()) return;
    auto tpl = g_app->db.getByName(name);
    if (!tpl) return;
    levelCombo_->setCurrentIndex(tpl->alertLevel);
    noteEdit_->setPlainText(tpl->note);
    int n = g_app->db.countSamples(tpl->id);
    statusLabel_->setText(QStringLiteral("「%1」现有 %2 条样本，将聚合新特征")
                          .arg(name).arg(n));
}

bool TemplateDialog::doAggregate(const QString& name) {
    auto tpl = g_app->db.getByName(name);
    if (!tpl) {
        QMessageBox::warning(this, QStringLiteral("错误"),
            QStringLiteral("找不到人员「%1」").arg(name));
        return false;
    }
    auto agg = embeddingFromJson(tpl->embeddingJson);
    double s = agg.empty() ? 0.0 : cosSim(embedding_, agg);
    int n = g_app->db.countSamples(tpl->id);
    const Config& c = g_app->config;

    // 低相似度：可能贴错人
    if (s < c.aggregateLowThreshold) {
        auto btn = QMessageBox::question(this, QStringLiteral("相似度过低"),
            QStringLiteral("新特征与「%1」聚合体相似度仅 %2，可能不是同一人。\n是否仍然聚合？")
                .arg(name).arg(s, 0, 'f', 2),
            QMessageBox::Yes | QMessageBox::No);
        if (btn != QMessageBox::Yes) return false; // 丢弃
    } else if (s > c.aggregateHighThreshold) {
        // 高相似度：可能重复录入
        auto btn = QMessageBox::question(this, QStringLiteral("高度相似"),
            QStringLiteral("新特征与「%1」高度相似（%2），可能重复录入。\n是否仍入库？")
                .arg(name).arg(s, 0, 'f', 2),
            QMessageBox::Yes | QMessageBox::No);
        if (btn != QMessageBox::Yes) return false;
    }
    // 软上限
    if (n >= c.aggregateSoftCap) {
        auto btn = QMessageBox::question(this, QStringLiteral("样本较多"),
            QStringLiteral("「%1」已有 %2 条样本（软上限 %3）。\n是否继续添加？")
                .arg(name).arg(n).arg(c.aggregateSoftCap),
            QMessageBox::Yes | QMessageBox::No);
        if (btn != QMessageBox::Yes) return false;
    }

    // 写库
    if (!g_app->db.addSample(tpl->id, embedding_, embedScore_)) {
        QMessageBox::warning(this, QStringLiteral("失败"),
            QStringLiteral("添加样本失败：") + g_app->db.lastError());
        return false;
    }
    if (!g_app->db.recomputeAggregate(tpl->id)) {
        QMessageBox::warning(this, QStringLiteral("失败"),
            QStringLiteral("重算聚合体失败：") + g_app->db.lastError());
        return false;
    }
    auto fresh = g_app->db.getByName(name);
    if (fresh) result_ = *fresh;
    return true;
}

bool TemplateDialog::doNewPerson(const QString& name) {
    FaceTemplate t;
    t.name = name;
    t.embeddingJson = embeddingToJson(embedding_);
    t.embeddingModel = QStringLiteral("opencv_sface_2021dec");
    t.alertLevel = levelCombo_->currentData().toInt();
    t.note = noteEdit_->toPlainText().trimmed();
    if (!g_app->db.add(t)) {
        QMessageBox::warning(this, QStringLiteral("失败"),
            QStringLiteral("新增失败：") + g_app->db.lastError());
        return false;
    }
    // 首条样本
    if (!g_app->db.addSample(t.id, embedding_, embedScore_)) {
        QMessageBox::warning(this, QStringLiteral("失败"),
            QStringLiteral("写入样本失败：") + g_app->db.lastError());
        return false;
    }
    // 聚合体即该单样本（已 normalize），确保一致
    g_app->db.recomputeAggregate(t.id);
    result_ = t;
    return true;
}

void TemplateDialog::onOk() {
    if (embedding_.empty()) {
        QMessageBox::warning(this, QStringLiteral("提示"),
            addMode_ ? QStringLiteral("请先提取特征") : QStringLiteral("无有效特征"));
        return;
    }

    // ---- 编辑模式 ----
    if (!addMode_) {
        QString name = nameEdit_->text().trimmed();
        if (name.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("请填写姓名"));
            return;
        }
        if (name != initial_.name) {
            auto ex = g_app->db.getByName(name);
            if (ex && ex->id != initial_.id) {
                QMessageBox::warning(this, QStringLiteral("姓名冲突"),
                    QStringLiteral("姓名「%1」已被其他人占用，请改用其他姓名。").arg(name));
                return;
            }
        }
        FaceTemplate t = initial_;
        t.name = name;
        t.alertLevel = levelCombo_->currentData().toInt();
        t.note = noteEdit_->toPlainText().trimmed();
        t.embeddingModel = QStringLiteral("opencv_sface_2021dec");
        bool reextracted = !embedding_.empty()
            && embeddingToJson(embedding_) != initial_.embeddingJson;
        if (reextracted) t.embeddingJson = embeddingToJson(embedding_);
        if (!g_app->db.update(t)) {
            QMessageBox::warning(this, QStringLiteral("失败"),
                QStringLiteral("更新失败：") + g_app->db.lastError());
            return;
        }
        if (reextracted) {
            if (!g_app->db.replaceSamples(t.id, embedding_, embedScore_)) {
                QMessageBox::warning(this, QStringLiteral("提示"),
                    QStringLiteral("样本重置失败：") + g_app->db.lastError());
            }
        }
        result_ = t;
        accept();
        return;
    }

    // ---- 新增模式 ----
    bool aggregateMode = modeCombo_ && modeCombo_->isVisible() && modeCombo_->currentIndex() == 1;

    if (aggregateMode) {
        QString name = nameCombo_->currentText().trimmed();
        if (name.isEmpty()) { QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("请选择人员")); return; }
        if (doAggregate(name)) accept();
        return;
    }

    // 新增人员
    QString name = nameEdit_->text().trimmed();
    if (name.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("请填写姓名"));
        return;
    }
    // 姓名查重
    if (g_app->db.getByName(name)) {
        auto btn = QMessageBox::question(this, QStringLiteral("姓名已存在"),
            QStringLiteral("姓名「%1」已存在。\n是否为同一人（聚合到现有）？").arg(name),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
        if (btn == QMessageBox::Yes) {
            if (doAggregate(name)) accept();
            return;
        } else if (btn == QMessageBox::Cancel) {
            return;
        }
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请改用不同的姓名。"));
        return;
    }
    // 撞脸检查
    double bestSim = -1.0; QString bestName;
    for (const auto& t : existingTemplates_) {
        auto e = embeddingFromJson(t.embeddingJson);
        if (e.empty()) continue;
        double s = cosSim(embedding_, e);
        if (s > bestSim) { bestSim = s; bestName = t.name; }
    }
    if (!bestName.isEmpty() && bestSim > g_app->config.faceMatchThreshold) {
        auto btn = QMessageBox::question(this, QStringLiteral("撞脸提示"),
            QStringLiteral("这张脸与已登记的「%1」高度相似（相似度 %2）。\n是否为同一人（聚合）？")
                .arg(bestName).arg(bestSim, 0, 'f', 2),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
        if (btn == QMessageBox::Yes) {
            if (doAggregate(bestName)) accept();
            return;
        } else if (btn == QMessageBox::Cancel) {
            return;
        }
        // No -> 继续新增
    }
    // 真正新增
    if (doNewPerson(name)) accept();
}
