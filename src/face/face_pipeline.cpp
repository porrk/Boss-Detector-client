#include "face_pipeline.h"
#include "align.h"
#include "../config.h"
#include "../db.h"          // l2Normalize
#include "../onnx/sface_runner.h"

#include <facedetectcnn.h>

#include <QPainter>
#include <QFont>
#include <QPen>
#include <QRect>
#include <QPoint>
#include <QImage>

void FacePipeline::configure(const Config& cfg) {
    detectMinScore_ = cfg.detectMinScore;
    alignSize_      = cfg.alignSize;
    inputMean_      = cfg.inputMean;
    inputStd_       = cfg.inputStd;
    swapRb_         = cfg.swapRb;
}

FacePipeline::Result FacePipeline::processImage(const QString& imagePath) {
    Result r;
    QImage img(imagePath);
    if (img.isNull()) {
        r.error = QStringLiteral("无法解码图片：") + imagePath;
        return r;
    }
    img = img.convertToFormat(QImage::Format_RGB888);
    const int W = img.width(), H = img.height();

    // libfacedetection 需要 BGR 连续缓冲（step = W*3）
    std::vector<unsigned char> bgr((size_t)W * H * 3);
    for (int y = 0; y < H; ++y) {
        const uchar* row = img.constScanLine(y);
        unsigned char* d = bgr.data() + (size_t)y * W * 3;
        for (int x = 0; x < W; ++x) {
            d[x * 3 + 0] = row[x * 3 + 2]; // B
            d[x * 3 + 1] = row[x * 3 + 1]; // G
            d[x * 3 + 2] = row[x * 3 + 0]; // R
        }
    }

    std::vector<FaceRect> faces = objectdetect_cnn(bgr.data(), W, H, W * 3);
    r.faceCount = (int)faces.size();

    int bestIdx = -1;
    float bestScore = -1.0f;
    for (int i = 0; i < (int)faces.size(); ++i) {
        if ((double)faces[i].score < detectMinScore_) continue;
        if (faces[i].score > bestScore) { bestScore = faces[i].score; bestIdx = i; }
    }

    // 预览：绘制所有框，最优框绿色
    QImage preview = img.copy();
    {
        QPainter p(&preview);
        QFont f; f.setPointSize(9); p.setFont(f);
        for (int i = 0; i < (int)faces.size(); ++i) {
            QRect rect(faces[i].x, faces[i].y, faces[i].w, faces[i].h);
            p.setPen(QPen(i == bestIdx ? Qt::green : Qt::yellow, 2));
            p.drawRect(rect);
            p.drawText(rect.topLeft() + QPoint(2, -3),
                       QString::number(faces[i].score, 'f', 2));
        }
        p.end();
    }
    r.preview = preview;

    if (bestIdx < 0) {
        r.error = QStringLiteral("未检测到符合置信度（%1）的人脸").arg(detectMinScore_, 0, 'f', 2);
        return r;
    }

    FaceRect& fr = faces[bestIdx];
    r.bestScore = fr.score;
    Landmarks5 lm;
    for (int i = 0; i < 10; ++i) lm.pts[i] = (float)fr.lm[i];

    QImage aligned = alignFace(img, lm, alignSize_);
    if (aligned.isNull()) { r.error = QStringLiteral("人脸对齐失败"); return r; }

    std::vector<float> emb = sface_->extract(aligned, inputMean_, inputStd_, swapRb_);
    if (emb.empty()) {
        r.error = sface_->lastError();
        if (r.error.isEmpty()) r.error = QStringLiteral("特征提取失败");
        return r;
    }
    l2Normalize(emb);
    r.embedding = std::move(emb);
    r.ok = true;
    return r;
}
