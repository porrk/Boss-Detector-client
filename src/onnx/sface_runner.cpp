#include "sface_runner.h"

#include <onnxruntime_cxx_api.h>
#include <array>
#include <cstring>

struct SFaceRunner::Impl {
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "sface"};
    Ort::Session session{nullptr};
    std::vector<int64_t> inputShape;
    std::vector<int64_t> outputShape;
    std::string inputName;
    std::string outputName;
    int alignSize = 112;
};

SFaceRunner::SFaceRunner() = default;
SFaceRunner::~SFaceRunner() = default;

bool SFaceRunner::load(const QString& modelPath, int threads) {
    p_ = std::make_unique<Impl>();
    try {
        Ort::SessionOptions opts;
        opts.SetIntraOpNumThreads(threads);
        opts.SetGraphOptimizationLevel(ORT_ENABLE_EXTENDED);
#ifdef _WIN32
        std::wstring wpath = modelPath.toStdWString();
        p_->session = Ort::Session(p_->env, wpath.c_str(), opts);
#else
        QByteArray bpath = modelPath.toLocal8Bit();
        p_->session = Ort::Session(p_->env, bpath.constData(), opts);
#endif

        Ort::AllocatorWithDefaultOptions alloc;
        auto inName  = p_->session.GetInputNameAllocated(0, alloc);
        auto outName = p_->session.GetOutputNameAllocated(0, alloc);
        p_->inputName  = inName.get();
        p_->outputName = outName.get();

        auto inInfo  = p_->session.GetInputTypeInfo(0).GetTensorTypeAndShapeInfo();
        auto outInfo = p_->session.GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo();
        p_->inputShape  = inInfo.GetShape();
        p_->outputShape = outInfo.GetShape();
        if (p_->inputShape.size() == 4 && p_->inputShape[2] > 0)
            p_->alignSize = (int)p_->inputShape[2];
        return true;
    } catch (const Ort::Exception& e) {
        lastError_ = QString::fromLocal8Bit(e.what());
        p_.reset();
        return false;
    } catch (const std::exception& e) {
        lastError_ = QString::fromLocal8Bit(e.what());
        p_.reset();
        return false;
    }
}

bool SFaceRunner::isLoaded() const { return p_ && (bool)p_->session; }
int  SFaceRunner::alignSize() const { return p_ ? p_->alignSize : 112; }

std::vector<float> SFaceRunner::extract(const QImage& alignedFace,
                                        double mean, double std, bool swapRb) {
    std::vector<float> out;
    if (!isLoaded()) { lastError_ = QStringLiteral("SFace 未加载"); return out; }

    QImage img = alignedFace.convertToFormat(QImage::Format_RGB888);
    int H = img.height(), W = img.width();
    if (W <= 0 || H <= 0) return out;

    const int C = 3;
    const size_t plane = (size_t)W * H;
    std::vector<float> tensor(C * plane);
    const float scale = (float)(1.0 / std);
    const float m = (float)mean;

    for (int y = 0; y < H; ++y) {
        const uchar* row = img.constScanLine(y);
        size_t base = (size_t)y * W;
        for (int x = 0; x < W; ++x) {
            uchar r = row[x * 3 + 0];
            uchar g = row[x * 3 + 1];
            uchar b = row[x * 3 + 2];
            float R = ((float)r - m) * scale;
            float G = ((float)g - m) * scale;
            float B = ((float)b - m) * scale;
            // NCHW：第 0 通道
            tensor[0 * plane + base + x] = swapRb ? R : B;
            tensor[1 * plane + base + x] = G;
            tensor[2 * plane + base + x] = swapRb ? B : R;
        }
    }

    try {
        Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        std::array<int64_t, 4> shape = {1, C, H, W};
        Ort::Value input = Ort::Value::CreateTensor<float>(
            mem, tensor.data(), tensor.size(), shape.data(), shape.size());
        const char* inNames[]  = { p_->inputName.c_str() };
        const char* outNames[] = { p_->outputName.c_str() };
        auto outputs = p_->session.Run(Ort::RunOptions{nullptr},
                                       inNames, &input, 1, outNames, 1);
        if (outputs.empty()) return out;
        float* data = outputs[0].GetTensorMutableData<float>();
        size_t n = outputs[0].GetTensorTypeAndShapeInfo().GetElementCount();
        out.assign(data, data + n);
    } catch (const Ort::Exception& e) {
        lastError_ = QString::fromLocal8Bit(e.what());
    } catch (const std::exception& e) {
        lastError_ = QString::fromLocal8Bit(e.what());
    }
    return out;
}
