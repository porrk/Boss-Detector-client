#pragma once
#include "../db.h"
#include <vector>
#include <map>
#include <cstdint>

struct MatchResult {
    bool    matched = false;
    qint64  templateId = 0;
    QString name;
    int     alertLevel = 0;
    double  score = 0;     // 余弦相似度
};

class Recognizer {
public:
    void setThreshold(double t) { threshold_ = t; }
    void setCooldown(int seconds) { cooldownSeconds_ = seconds; }
    void setTemplates(const std::vector<FaceTemplate>& tpls);

    MatchResult match(const std::vector<float>& emb) const;

    // 冷却去重：trackId>0 时按 trackId 记录；<=0 时不做去重
    bool shouldNotify(int trackId, qint64 nowMs);
    void clearCooldown() { lastNotify_.clear(); }

private:
    struct Item { std::vector<float> emb; FaceTemplate t; };
    std::vector<Item> cache_;
    double threshold_ = 0.5;
    int    cooldownSeconds_ = 30;
    std::map<long long, qint64> lastNotify_;
};
