#include "align.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <utility>

void arcfaceReference5(int alignSize, float out[10]) {
    // 标准 ArcFace 112x112 参考点：左眼 右眼 鼻 左嘴 右嘴
    static const float ref112[10] = {
        38.2946f, 51.6963f,
        73.5318f, 51.5014f,
        56.0252f, 71.7366f,
        41.5493f, 92.3655f,
        70.7299f, 92.2041f
    };
    float s = (alignSize > 0) ? (float)alignSize / 112.0f : 1.0f;
    for (int i = 0; i < 10; ++i) out[i] = ref112[i] * s;
}

// 解 4x4 线性方程组 M x = rhs（高斯消元 + 部分主元）
static bool solve4(double M[4][4], double rhs[4], double x[4]) {
    for (int col = 0; col < 4; ++col) {
        // 选主元
        int piv = col;
        double maxv = std::fabs(M[col][col]);
        for (int r = col + 1; r < 4; ++r) {
            double v = std::fabs(M[r][col]);
            if (v > maxv) { maxv = v; piv = r; }
        }
        if (maxv < 1e-12) return false;
        if (piv != col) {
            for (int c = 0; c < 4; ++c) std::swap(M[col][c], M[piv][c]);
            std::swap(rhs[col], rhs[piv]);
        }
        double inv = 1.0 / M[col][col];
        for (int r = col + 1; r < 4; ++r) {
            double f = M[r][col] * inv;
            if (f == 0.0) continue;
            for (int c = col; c < 4; ++c) M[r][c] -= f * M[col][c];
            rhs[r] -= f * rhs[col];
        }
    }
    // 回代
    for (int i = 3; i >= 0; --i) {
        double s = rhs[i];
        for (int c = i + 1; c < 4; ++c) s -= M[i][c] * x[c];
        x[i] = s / M[i][i];
    }
    return true;
}

QImage alignFace(const QImage& srcIn, const Landmarks5& lm, int alignSize) {
    QImage src = srcIn.convertToFormat(QImage::Format_RGB888);
    if (src.width() <= 0 || src.height() <= 0 || alignSize <= 0)
        return QImage();

    float ref[10];
    arcfaceReference5(alignSize, ref);

    // 最小二乘求相似变换 [a -b; b a] + (tx,ty)：dst = M*src + t
    // 每点 2 行：[sx,-sy,1,0]->dx ; [sy,sx,0,1]->dy
    double M[4][4] = { {0} };
    double rhs[4] = { 0 };
    auto accum = [&](double row[4], double val) {
        for (int i = 0; i < 4; ++i) {
            rhs[i] += row[i] * val;
            for (int j = 0; j < 4; ++j) M[i][j] += row[i] * row[j];
        }
    };
    for (int k = 0; k < 5; ++k) {
        double sx = lm.pts[2 * k + 0];
        double sy = lm.pts[2 * k + 1];
        double dx = ref[2 * k + 0];
        double dy = ref[2 * k + 1];
        double r1[4] = { sx, -sy, 1.0, 0.0 };
        double r2[4] = { sy,  sx, 0.0, 1.0 };
        accum(r1, dx);
        accum(r2, dy);
    }
    double x[4] = { 0 };
    if (!solve4(M, rhs, x)) return QImage();
    double a = x[0], b = x[1], tx = x[2], ty = x[3];

    // 反向映射：src = M^{-1} (dst - t)，M=[[a,-b],[b,a]]，det=a^2+b^2
    double det = a * a + b * b;
    if (det < 1e-12) return QImage();
    double invDet = 1.0 / det;
    // M^{-1} = invDet * [[a, b],[-b, a]]
    double ia =  a * invDet, ib =  b * invDet;
    double ic = -b * invDet, id =  a * invDet;

    const int sw = src.width(), sh = src.height();
    const uchar* sbits = src.constBits();
    const int sstride = src.bytesPerLine();

    QImage out(alignSize, alignSize, QImage::Format_RGB888);
    out.fill(Qt::black);
    uchar* obits = out.bits();
    const int ostride = out.bytesPerLine();

    for (int oy = 0; oy < alignSize; ++oy) {
        uchar* orow = obits + oy * ostride;
        double vy = oy - ty;
        for (int ox = 0; ox < alignSize; ++ox) {
            double ux = ox - tx;
            // src 坐标
            double fx = ia * ux + ib * vy;
            double fy = ic * ux + id * vy;
            // 双线性
            int x0 = (int)std::floor(fx);
            int y0 = (int)std::floor(fy);
            double w1 = fx - x0;
            double w0 = 1.0 - w1;
            double h1 = fy - y0;
            double h0 = 1.0 - h1;
            // 越界置黑
            if (x0 < -1 || y0 < -1 || x0 >= sw || y0 >= sh) {
                orow[ox * 3 + 0] = 0;
                orow[ox * 3 + 1] = 0;
                orow[ox * 3 + 2] = 0;
                continue;
            }
            int x1 = x0 + 1, y1 = y0 + 1;
            x0 = x0 < 0 ? 0 : (x0 >= sw ? sw - 1 : x0);
            y0 = y0 < 0 ? 0 : (y0 >= sh ? sh - 1 : y0);
            x1 = x1 < 0 ? 0 : (x1 >= sw ? sw - 1 : x1);
            y1 = y1 < 0 ? 0 : (y1 >= sh ? sh - 1 : y1);
            const uchar* p00 = sbits + y0 * sstride + x0 * 3;
            const uchar* p01 = sbits + y0 * sstride + x1 * 3;
            const uchar* p10 = sbits + y1 * sstride + x0 * 3;
            const uchar* p11 = sbits + y1 * sstride + x1 * 3;
            double w00 = w0 * h0, w01 = w1 * h0, w10 = w0 * h1, w11 = w1 * h1;
            for (int c = 0; c < 3; ++c) {
                double v = p00[c] * w00 + p01[c] * w01 + p10[c] * w10 + p11[c] * w11;
                int iv = (int)(v + 0.5);
                if (iv < 0) iv = 0; if (iv > 255) iv = 255;
                orow[ox * 3 + c] = (uchar)iv;
            }
        }
    }
    return out;
}
