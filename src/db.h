#pragma once
#include <QString>
#include <vector>
#include <cstdint>
#include <optional>

struct FaceTemplate {
    qint64  id = 0;
    QString name;
    QString embeddingJson;      // JSON 数组，128 个 float
    QString embeddingModel;
    int     alertLevel = 0;     // 0/1/2
    QString note;
    QString thumbPath;          // 可选缩略图
    qint64  createdAt = 0;
    qint64  updatedAt = 0;
};

// 特征向量 <-> JSON
std::vector<float> embeddingFromJson(const QString& json);
QString embeddingToJson(const std::vector<float>& emb);

// 就地 L2 归一化
void l2Normalize(std::vector<float>& v);

struct TemplateSample {
    qint64 id = 0;
    qint64 templateId = 0;
    std::vector<float> embedding;
    double score = 1.0;
    qint64 createdAt = 0;
};

// 余弦相似度（向量无需预先归一化）
double cosSim(const std::vector<float>& a, const std::vector<float>& b);
// 加权平均聚合：normalize(Σ score_i·emb_i)；Σscore≈0 退化为等权；无样本返回空
std::vector<float> weightedAggregate(const std::vector<TemplateSample>& samples);

class Db {
public:
    Db();
    ~Db();
    bool open(const QString& path);
    void close();
    bool createTables();

    std::vector<FaceTemplate> listAll();
    bool add(FaceTemplate& t);                 // 成功后回填 t.id
    bool update(const FaceTemplate& t);
    bool remove(qint64 id);
    std::optional<FaceTemplate> get(qint64 id);

    // ---- 聚合（多模板合并）----
    std::vector<QString> listDistinctNames();
    std::optional<FaceTemplate> getByName(const QString& name);
    int  countSamples(qint64 templateId);
    bool addSample(qint64 templateId, const std::vector<float>& emb, double score);
    std::vector<TemplateSample> listSamples(qint64 templateId);
    bool recomputeAggregate(qint64 templateId);           // 从 samples 重算并 UPDATE templates.embedding
    bool replaceSamples(qint64 templateId, const std::vector<float>& emb, double score); // 重置为单样本
    bool migrate();                                        // 一次性迁移（回填/合并同名/唯一索引）

    QString lastError() const { return lastError_; }
private:
    void* db_ = nullptr;        // sqlite3*
    QString lastError_;
};
