#pragma once
#include <QImage>
#include <QString>
#include <vector>

struct Config;
class SFaceRunner;

struct DetectedFace {
    float score = 0;
    int x = 0, y = 0, w = 0, h = 0;
    float lm[10] = {0};
};

// 图片 -> 检测 -> 5点对齐 -> SFace -> L2归一化特征
class FacePipeline {
public:
    struct Result {
        bool ok = false;
        QString error;
        std::vector<float> embedding;   // 已 L2 归一化（128 维）
        QImage preview;                 // 带检测框的预览图
        int faceCount = 0;
        float bestScore = 0;
    };

    void configure(const Config& cfg);
    void setSFace(SFaceRunner* s) { sface_ = s; }
    Result processImage(const QString& imagePath);

private:
    SFaceRunner* sface_ = nullptr;
    double detectMinScore_ = 0.5;
    int    alignSize_ = 112;
    double inputMean_ = 127.5;
    double inputStd_ = 128.0;
    bool   swapRb_ = true;
};
