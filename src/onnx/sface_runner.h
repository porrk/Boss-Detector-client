#pragma once
#include <QString>
#include <QImage>
#include <vector>
#include <memory>

// ONNX Runtime 封装：跑 SFace，输出 128 维特征（未归一化，由调用方归一化）
class SFaceRunner {
public:
    SFaceRunner();
    ~SFaceRunner();
    bool load(const QString& modelPath, int threads = 1);
    bool isLoaded() const;
    int  alignSize() const;

    // alignedFace: 已对齐到 alignSize×alignSize。返回模型原始输出（需自行 L2 归一化）
    std::vector<float> extract(const QImage& alignedFace,
                               double mean, double std, bool swapRb);
    QString lastError() const { return lastError_; }
private:
    struct Impl;
    std::unique_ptr<Impl> p_;
    QString lastError_;
};
