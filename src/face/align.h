#pragma once
#include <QImage>

// 5 个关键点：x0,y0,x1,y1,...,x4,y4（与 libfacedetection 的 FaceRect.lm[10] 一致）
struct Landmarks5 {
    float pts[10] = {0};
};

// ArcFace / SFace 标准 112 参考关键点，按 alignSize 缩放后写入 out[10]
void arcfaceReference5(int alignSize, float out[10]);

// 用 5 点相似变换把 src 对齐到 alignSize×alignSize，输出 RGB888
// 内部用最小二乘求 [a,-b;b,a]+t 的相似变换，再反向双线性采样
QImage alignFace(const QImage& src, const Landmarks5& lm, int alignSize);
