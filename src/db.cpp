#include "db.h"

#include <sqlite3.h>
#include <cJSON.h>
#include <QDateTime>
#include <cmath>
#include <algorithm>

// ---------------------------------------------------------------------------
// 特征向量工具
// ---------------------------------------------------------------------------
std::vector<float> embeddingFromJson(const QString& json) {
    std::vector<float> out;
    if (json.isEmpty()) return out;
    cJSON* root = cJSON_Parse(json.toUtf8().constData());
    if (!root) return out;
    if (cJSON_IsArray(root)) {
        int n = cJSON_GetArraySize(root);
        out.reserve(n);
        for (int i = 0; i < n; ++i) {
            cJSON* e = cJSON_GetArrayItem(root, i);
            out.push_back(e && cJSON_IsNumber(e) ? (float)cJSON_GetNumberValue(e) : 0.f);
        }
    }
    cJSON_Delete(root);
    return out;
}

QString embeddingToJson(const std::vector<float>& emb) {
    cJSON* root = cJSON_CreateFloatArray(emb.data(), (int)emb.size());
    char* s = cJSON_PrintUnformatted(root);
    QString out = QString::fromUtf8(s ? s : "[]");
    if (s) cJSON_free(s);
    cJSON_Delete(root);
    return out;
}

void l2Normalize(std::vector<float>& v) {
    double s = 0.0;
    for (float x : v) s += (double)x * x;
    if (s < 1e-12) return;
    float inv = (float)(1.0 / std::sqrt(s));
    for (float& x : v) x *= inv;
}

double cosSim(const std::vector<float>& a, const std::vector<float>& b) {
    size_t n = a.size() < b.size() ? a.size() : b.size();
    if (n == 0) return 0.0;
    double dot = 0.0, na = 0.0, nb = 0.0;
    for (size_t i = 0; i < n; ++i) {
        dot += (double)a[i] * b[i];
        na  += (double)a[i] * a[i];
        nb  += (double)b[i] * b[i];
    }
    if (na < 1e-12 || nb < 1e-12) return 0.0;
    return dot / (std::sqrt(na) * std::sqrt(nb));
}

std::vector<float> weightedAggregate(const std::vector<TemplateSample>& samples) {
    std::vector<float> out;
    if (samples.empty()) return out;
    size_t dim = 0;
    double wsum = 0.0;
    for (const auto& s : samples) {
        wsum += s.score;
        dim = std::max(dim, s.embedding.size());
    }
    if (dim == 0) return out;
    out.assign(dim, 0.0f);
    bool useScore = wsum > 1e-6;
    for (const auto& s : samples) {
        double w = useScore ? s.score : 1.0;
        size_t n = s.embedding.size();
        for (size_t i = 0; i < n; ++i) out[i] += (float)(w * (double)s.embedding[i]);
    }
    l2Normalize(out);
    return out;
}

// ---------------------------------------------------------------------------
// Db
// ---------------------------------------------------------------------------
Db::Db() = default;
Db::~Db() { close(); }

static QString colText(sqlite3_stmt* stmt, int idx) {
    const unsigned char* t = sqlite3_column_text(stmt, idx);
    return t ? QString::fromUtf8((const char*)t) : QString();
}

bool Db::open(const QString& path) {
    close();
    int rc = sqlite3_open_v2(path.toUtf8().constData(), (sqlite3**)&db_,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if (rc != SQLITE_OK) {
        lastError_ = QString::fromUtf8(sqlite3_errmsg((sqlite3*)db_));
        sqlite3_close_v2((sqlite3*)db_);
        db_ = nullptr;
        return false;
    }
    sqlite3_exec((sqlite3*)db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    if (!createTables()) return false;
    return migrate();
}

void Db::close() {
    if (db_) {
        sqlite3_close_v2((sqlite3*)db_);
        db_ = nullptr;
    }
}

bool Db::createTables() {
    const char* sql =
        "CREATE TABLE IF NOT EXISTS templates ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  name TEXT NOT NULL,"
        "  embedding TEXT NOT NULL,"
        "  embedding_model TEXT,"
        "  alert_level INTEGER NOT NULL DEFAULT 0,"
        "  note TEXT,"
        "  thumb_path TEXT,"
        "  created_at INTEGER NOT NULL,"
        "  updated_at INTEGER NOT NULL);"
        "CREATE INDEX IF NOT EXISTS idx_templates_name ON templates(name);"
        "CREATE TABLE IF NOT EXISTS template_samples ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  template_id INTEGER NOT NULL,"
        "  embedding TEXT NOT NULL,"
        "  score REAL NOT NULL DEFAULT 1.0,"
        "  created_at INTEGER NOT NULL);"
        "CREATE INDEX IF NOT EXISTS idx_samples_template_id ON template_samples(template_id);";
    char* err = nullptr;
    int rc = sqlite3_exec((sqlite3*)db_, sql, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        lastError_ = err ? QString::fromUtf8(err) : QStringLiteral("create table failed");
        if (err) sqlite3_free(err);
        return false;
    }
    return true;
}

std::vector<FaceTemplate> Db::listAll() {
    std::vector<FaceTemplate> out;
    if (!db_) return out;
    const char* sql =
        "SELECT id,name,embedding,embedding_model,alert_level,note,thumb_path,created_at,updated_at "
        "FROM templates ORDER BY id;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2((sqlite3*)db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        lastError_ = QString::fromUtf8(sqlite3_errmsg((sqlite3*)db_));
        return out;
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        FaceTemplate t;
        t.id             = sqlite3_column_int64(stmt, 0);
        t.name           = colText(stmt, 1);
        t.embeddingJson  = colText(stmt, 2);
        t.embeddingModel = colText(stmt, 3);
        t.alertLevel     = sqlite3_column_int(stmt, 4);
        t.note           = colText(stmt, 5);
        t.thumbPath      = colText(stmt, 6);
        t.createdAt      = sqlite3_column_int64(stmt, 7);
        t.updatedAt      = sqlite3_column_int64(stmt, 8);
        out.push_back(std::move(t));
    }
    sqlite3_finalize(stmt);
    return out;
}

bool Db::add(FaceTemplate& t) {
    if (!db_) return false;
    qint64 now = QDateTime::currentSecsSinceEpoch();
    t.createdAt = now;
    t.updatedAt = now;
    const char* sql =
        "INSERT INTO templates(name,embedding,embedding_model,alert_level,note,thumb_path,created_at,updated_at)"
        " VALUES(?,?,?,?,?,?,?,?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2((sqlite3*)db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        lastError_ = QString::fromUtf8(sqlite3_errmsg((sqlite3*)db_));
        return false;
    }
    sqlite3_bind_text(stmt, 1, t.name.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, t.embeddingJson.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, t.embeddingModel.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (stmt, 4, t.alertLevel);
    sqlite3_bind_text(stmt, 5, t.note.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, t.thumbPath.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 7, t.createdAt);
    sqlite3_bind_int64(stmt, 8, t.updatedAt);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        lastError_ = QString::fromUtf8(sqlite3_errmsg((sqlite3*)db_));
        return false;
    }
    t.id = sqlite3_last_insert_rowid((sqlite3*)db_);
    return true;
}

bool Db::update(const FaceTemplate& t) {
    if (!db_) return false;
    qint64 now = QDateTime::currentSecsSinceEpoch();
    const char* sql =
        "UPDATE templates SET name=?,embedding=?,embedding_model=?,alert_level=?,note=?,thumb_path=?,updated_at=?"
        " WHERE id=?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2((sqlite3*)db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        lastError_ = QString::fromUtf8(sqlite3_errmsg((sqlite3*)db_));
        return false;
    }
    sqlite3_bind_text(stmt, 1, t.name.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, t.embeddingJson.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, t.embeddingModel.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (stmt, 4, t.alertLevel);
    sqlite3_bind_text(stmt, 5, t.note.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, t.thumbPath.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 7, now);
    sqlite3_bind_int64(stmt, 8, t.id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        lastError_ = QString::fromUtf8(sqlite3_errmsg((sqlite3*)db_));
        return false;
    }
    return true;
}

bool Db::remove(qint64 id) {
    if (!db_) return false;
    const char* sql = "DELETE FROM templates WHERE id=?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2((sqlite3*)db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        lastError_ = QString::fromUtf8(sqlite3_errmsg((sqlite3*)db_));
        return false;
    }
    sqlite3_bind_int64(stmt, 1, id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        lastError_ = QString::fromUtf8(sqlite3_errmsg((sqlite3*)db_));
        return false;
    }
    // 表空时重置 AUTOINCREMENT，下次新增 ID 从 1 开始
    sqlite3_stmt* cntStmt = nullptr;
    if (sqlite3_prepare_v2((sqlite3*)db_, "SELECT COUNT(*) FROM templates;", -1, &cntStmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(cntStmt) == SQLITE_ROW && sqlite3_column_int(cntStmt, 0) == 0) {
            sqlite3_exec((sqlite3*)db_, "DELETE FROM sqlite_sequence WHERE name='templates';", nullptr, nullptr, nullptr);
        }
        sqlite3_finalize(cntStmt);
    }
    return true;
}

std::optional<FaceTemplate> Db::get(qint64 id) {
    if (!db_) return std::nullopt;
    const char* sql =
        "SELECT id,name,embedding,embedding_model,alert_level,note,thumb_path,created_at,updated_at "
        "FROM templates WHERE id=?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2((sqlite3*)db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
    sqlite3_bind_int64(stmt, 1, id);
    std::optional<FaceTemplate> out;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        FaceTemplate t;
        t.id             = sqlite3_column_int64(stmt, 0);
        t.name           = colText(stmt, 1);
        t.embeddingJson  = colText(stmt, 2);
        t.embeddingModel = colText(stmt, 3);
        t.alertLevel     = sqlite3_column_int(stmt, 4);
        t.note           = colText(stmt, 5);
        t.thumbPath      = colText(stmt, 6);
        t.createdAt      = sqlite3_column_int64(stmt, 7);
        t.updatedAt      = sqlite3_column_int64(stmt, 8);
        out = std::move(t);
    }
    sqlite3_finalize(stmt);
    return out;
}

// ---------------------------------------------------------------------------
// 聚合（多模板）
// ---------------------------------------------------------------------------
std::vector<QString> Db::listDistinctNames() {
    std::vector<QString> out;
    if (!db_) return out;
    const char* sql = "SELECT DISTINCT name FROM templates ORDER BY name;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2((sqlite3*)db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return out;
    while (sqlite3_step(stmt) == SQLITE_ROW) out.push_back(colText(stmt, 0));
    sqlite3_finalize(stmt);
    return out;
}

std::optional<FaceTemplate> Db::getByName(const QString& name) {
    if (!db_) return std::nullopt;
    const char* sql =
        "SELECT id,name,embedding,embedding_model,alert_level,note,thumb_path,created_at,updated_at "
        "FROM templates WHERE name=? LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2((sqlite3*)db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
    QByteArray nb = name.toUtf8();
    sqlite3_bind_text(stmt, 1, nb.constData(), -1, SQLITE_TRANSIENT);
    std::optional<FaceTemplate> out;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        FaceTemplate t;
        t.id             = sqlite3_column_int64(stmt, 0);
        t.name           = colText(stmt, 1);
        t.embeddingJson  = colText(stmt, 2);
        t.embeddingModel = colText(stmt, 3);
        t.alertLevel     = sqlite3_column_int(stmt, 4);
        t.note           = colText(stmt, 5);
        t.thumbPath      = colText(stmt, 6);
        t.createdAt      = sqlite3_column_int64(stmt, 7);
        t.updatedAt      = sqlite3_column_int64(stmt, 8);
        out = std::move(t);
    }
    sqlite3_finalize(stmt);
    return out;
}

int Db::countSamples(qint64 templateId) {
    if (!db_) return 0;
    const char* sql = "SELECT COUNT(*) FROM template_samples WHERE template_id=?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2((sqlite3*)db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return 0;
    sqlite3_bind_int64(stmt, 1, templateId);
    int n = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) n = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return n;
}

bool Db::addSample(qint64 templateId, const std::vector<float>& emb, double score) {
    if (!db_) return false;
    QString json = embeddingToJson(emb);
    const char* sql =
        "INSERT INTO template_samples(template_id,embedding,score,created_at) VALUES(?,?,?,?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2((sqlite3*)db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        lastError_ = QString::fromUtf8(sqlite3_errmsg((sqlite3*)db_));
        return false;
    }
    sqlite3_bind_int64(stmt, 1, templateId);
    sqlite3_bind_text(stmt, 2, json.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 3, score);
    sqlite3_bind_int64(stmt, 4, QDateTime::currentSecsSinceEpoch());
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        lastError_ = QString::fromUtf8(sqlite3_errmsg((sqlite3*)db_));
        return false;
    }
    return true;
}

std::vector<TemplateSample> Db::listSamples(qint64 templateId) {
    std::vector<TemplateSample> out;
    if (!db_) return out;
    const char* sql =
        "SELECT id,template_id,embedding,score,created_at FROM template_samples "
        "WHERE template_id=? ORDER BY id;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2((sqlite3*)db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return out;
    sqlite3_bind_int64(stmt, 1, templateId);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        TemplateSample s;
        s.id         = sqlite3_column_int64(stmt, 0);
        s.templateId = sqlite3_column_int64(stmt, 1);
        s.embedding  = embeddingFromJson(colText(stmt, 2));
        s.score      = sqlite3_column_double(stmt, 3);
        s.createdAt  = sqlite3_column_int64(stmt, 4);
        out.push_back(std::move(s));
    }
    sqlite3_finalize(stmt);
    return out;
}

bool Db::recomputeAggregate(qint64 templateId) {
    if (!db_) return false;
    auto samples = listSamples(templateId);
    if (samples.empty()) return true; // 无样本，保持现状
    std::vector<float> agg = weightedAggregate(samples);
    if (agg.empty()) return false;
    QString json = embeddingToJson(agg);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "UPDATE templates SET embedding=?, updated_at=? WHERE id=?;";
    if (sqlite3_prepare_v2((sqlite3*)db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        lastError_ = QString::fromUtf8(sqlite3_errmsg((sqlite3*)db_));
        return false;
    }
    sqlite3_bind_text(stmt, 1, json.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, QDateTime::currentSecsSinceEpoch());
    sqlite3_bind_int64(stmt, 3, templateId);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        lastError_ = QString::fromUtf8(sqlite3_errmsg((sqlite3*)db_));
        return false;
    }
    return true;
}

bool Db::replaceSamples(qint64 templateId, const std::vector<float>& emb, double score) {
    if (!db_) return false;
    sqlite3_stmt* del = nullptr;
    if (sqlite3_prepare_v2((sqlite3*)db_, "DELETE FROM template_samples WHERE template_id=?;",
                           -1, &del, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int64(del, 1, templateId);
    sqlite3_step(del);
    sqlite3_finalize(del);
    if (!addSample(templateId, emb, score)) return false;
    return recomputeAggregate(templateId);
}

bool Db::migrate() {
    if (!db_) return false;
    // 1) 回填：为尚无样本的 template 插入一条样本(score=1.0, 用其现有 embedding)
    const char* sqlBackfill =
        "INSERT INTO template_samples(template_id, embedding, score, created_at) "
        "SELECT t.id, t.embedding, 1.0, t.created_at FROM templates t "
        "WHERE NOT EXISTS (SELECT 1 FROM template_samples s WHERE s.template_id=t.id);";
    sqlite3_stmt* bf = nullptr;
    if (sqlite3_prepare_v2((sqlite3*)db_, sqlBackfill, -1, &bf, nullptr) != SQLITE_OK) {
        lastError_ = QString::fromUtf8(sqlite3_errmsg((sqlite3*)db_));
        return false;
    }
    sqlite3_step(bf);
    sqlite3_finalize(bf);

    // 2) 合并同名：同名多行 -> 保留 min id，样本归并，删除多余行，重算聚合体
    sqlite3_stmt* dup = nullptr;
    const char* sqlDup = "SELECT name FROM templates GROUP BY name HAVING COUNT(*)>1;";
    if (sqlite3_prepare_v2((sqlite3*)db_, sqlDup, -1, &dup, nullptr) != SQLITE_OK) {
        lastError_ = QString::fromUtf8(sqlite3_errmsg((sqlite3*)db_));
        return false;
    }
    while (sqlite3_step(dup) == SQLITE_ROW) {
        QString name = colText(dup, 0);
        sqlite3_stmt* ids = nullptr;
        const char* sqlIds = "SELECT id FROM templates WHERE name=? ORDER BY id;";
        if (sqlite3_prepare_v2((sqlite3*)db_, sqlIds, -1, &ids, nullptr) != SQLITE_OK) continue;
        QByteArray nb = name.toUtf8();
        sqlite3_bind_text(ids, 1, nb.constData(), -1, SQLITE_TRANSIENT);
        std::vector<qint64> idList;
        while (sqlite3_step(ids) == SQLITE_ROW) idList.push_back(sqlite3_column_int64(ids, 0));
        sqlite3_finalize(ids);
        if (idList.size() <= 1) continue;
        qint64 keeper = idList[0];
        for (size_t i = 1; i < idList.size(); ++i) {
            qint64 victim = idList[i];
            sqlite3_stmt* mv = nullptr;
            if (sqlite3_prepare_v2((sqlite3*)db_,
                    "UPDATE template_samples SET template_id=? WHERE template_id=?;",
                    -1, &mv, nullptr) == SQLITE_OK) {
                sqlite3_bind_int64(mv, 1, keeper);
                sqlite3_bind_int64(mv, 2, victim);
                sqlite3_step(mv);
                sqlite3_finalize(mv);
            }
            sqlite3_stmt* dl = nullptr;
            if (sqlite3_prepare_v2((sqlite3*)db_, "DELETE FROM templates WHERE id=?;",
                    -1, &dl, nullptr) == SQLITE_OK) {
                sqlite3_bind_int64(dl, 1, victim);
                sqlite3_step(dl);
                sqlite3_finalize(dl);
            }
        }
        recomputeAggregate(keeper);
    }
    sqlite3_finalize(dup);

    // 3) 姓名唯一索引（残留重名则失败但不致命）
    sqlite3_exec((sqlite3*)db_, "DROP INDEX IF EXISTS idx_templates_name;", nullptr, nullptr, nullptr);
    char* err = nullptr;
    int rc = sqlite3_exec((sqlite3*)db_,
        "CREATE UNIQUE INDEX idx_templates_name ON templates(name);",
        nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        lastError_ = err ? QString::fromUtf8(err) : QStringLiteral("unique index failed");
        if (err) sqlite3_free(err);
    }
    return true;
}
