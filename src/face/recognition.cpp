#include "recognition.h"

void Recognizer::setTemplates(const std::vector<FaceTemplate>& tpls) {
    cache_.clear();
    for (const auto& t : tpls) {
        std::vector<float> e = embeddingFromJson(t.embeddingJson);
        if (!e.empty()) {
            Item it;
            it.emb = std::move(e);
            it.t = t;
            cache_.push_back(std::move(it));
        }
    }
}

MatchResult Recognizer::match(const std::vector<float>& emb) const {
    MatchResult res;
    if (emb.empty() || cache_.empty()) return res;
    double best = -2.0;
    const Item* bestItem = nullptr;
    for (const auto& it : cache_) {
        double s = 0.0;
        size_t n = emb.size() < it.emb.size() ? emb.size() : it.emb.size();
        const float* a = emb.data();
        const float* b = it.emb.data();
        for (size_t i = 0; i < n; ++i) s += (double)a[i] * (double)b[i];
        if (s > best) { best = s; bestItem = &it; }
    }
    if (bestItem && best >= threshold_) {
        res.matched     = true;
        res.templateId  = bestItem->t.id;
        res.name        = bestItem->t.name;
        res.alertLevel  = bestItem->t.alertLevel;
    }
    res.score = best;
    return res;
}

bool Recognizer::shouldNotify(int trackId, qint64 nowMs) {
    if (trackId <= 0) return true; // 无 trackId 不做去重
    long long key = (long long)trackId;
    auto it = lastNotify_.find(key);
    if (it != lastNotify_.end() && nowMs - it->second < (qint64)cooldownSeconds_ * 1000)
        return false;
    lastNotify_[key] = nowMs;
    return true;
}
