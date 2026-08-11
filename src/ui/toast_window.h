#pragma once
#include <QWidget>
#include <QString>
#include <QColor>
#include <functional>
#include <memory>

// 右下角自绘弹窗：自动堆叠、定时关闭、左侧色条按级别配色
class ToastWindow : public QWidget {
    Q_OBJECT
public:
    static void show(const QString& title, const QString& message,
                     const QColor& color, int durationMs,
                     const QString& actionLabel = QString(),
                     std::function<void()> onAction = nullptr);

    ~ToastWindow() override;
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
private:
    ToastWindow(const QString& title, const QString& msg, const QColor& color,
                int durationMs, const QString& actionLabel, std::function<void()> onAction);
    void relayoutAll();
    QRect actionRect() const;
    QRect closeRect() const;

    QString title_;
    QString msg_;
    QColor  color_;
    int     durationMs_;
    QString actionLabel_;
    std::function<void()> onAction_;
    QRect   actionRect_;
    bool    closeHover_ = false;
    class QTimer* timer_ = nullptr;
};
