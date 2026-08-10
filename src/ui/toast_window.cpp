#include "toast_window.h"

#include <QApplication>
#include <QScreen>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QMouseEvent>
#include <QFontMetrics>
#include <QFont>
#include <QRect>
#include <vector>
#include <algorithm>

static std::vector<ToastWindow*>& toastList() {
    static std::vector<ToastWindow*> list;
    return list;
}

static constexpr int kToastW   = 340;
static constexpr int kMargin   = 14;
static constexpr int kPad      = 12;
static constexpr int kAccent   = 5;
static constexpr int kGap      = 8;

ToastWindow::ToastWindow(const QString& title, const QString& msg, const QColor& color,
                         int durationMs, const QString& actionLabel,
                         std::function<void()> onAction)
    : QWidget(nullptr, Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint),
      title_(title), msg_(msg), color_(color), durationMs_(durationMs),
      actionLabel_(actionLabel), onAction_(std::move(onAction))
{
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);
    setFocusPolicy(Qt::NoFocus);
    setFixedWidth(kToastW);

    // 计算高度
    QFont tf = font(); tf.setBold(true); tf.setPointSize(10);
    QFont mf = font(); mf.setPointSize(9);
    QFontMetrics tm(tf), mm(mf);
    int textW = kToastW - kPad * 2 - kAccent;
    QRect titleRect = tm.boundingRect(QRect(0, 0, textW, 0), Qt::TextWordWrap, title_);
    QRect msgRect   = mm.boundingRect(QRect(0, 0, textW, 0), Qt::TextWordWrap, msg_);
    int h = kPad + titleRect.height() + 4 + msgRect.height();
    if (!actionLabel_.isEmpty()) h += kPad + mm.height();
    h += kPad;
    setFixedHeight(qMax(h, 56));

    if (durationMs_ > 0) {
        timer_ = new QTimer(this);
        timer_->setSingleShot(true);
        connect(timer_, &QTimer::timeout, this, [this]() { close(); });
        timer_->start(durationMs_);
    }
    QWidget::show();
}

ToastWindow::~ToastWindow() {
    auto& list = toastList();
    list.erase(std::remove(list.begin(), list.end(), this), list.end());
    // 重新排列剩余弹窗
    for (auto* t : list) t->relayoutAll();
}

void ToastWindow::relayoutAll() {
    auto& list = toastList();
    QScreen* sc = QApplication::primaryScreen();
    QRect avail = sc ? sc->availableGeometry() : QRect(0, 0, 1920, 1080);
    int bottom = avail.bottom();
    int right  = avail.right();
    int j = 0;
    // 最新的（列表末尾）放最底
    for (auto it = list.rbegin(); it != list.rend(); ++it, ++j) {
        ToastWindow* t = *it;
        int y = bottom - kMargin - (j + 1) * t->height() - j * kGap;
        int x = right - kMargin - t->width();
        t->move(x, y);
    }
}

void ToastWindow::show(const QString& title, const QString& message,
                       const QColor& color, int durationMs,
                       const QString& actionLabel, std::function<void()> onAction) {
    auto& list = toastList();
    // 限制最大可见数
    int maxVis = 3;
    while ((int)list.size() >= maxVis) {
        ToastWindow* old = list.front();
        list.erase(list.begin());
        old->hide();
        old->deleteLater();
    }
    auto* t = new ToastWindow(title, message, color, durationMs, actionLabel, std::move(onAction));
    list.push_back(t);
    t->relayoutAll();
}

QRect ToastWindow::actionRect() const {
    if (actionLabel_.isEmpty()) return QRect();
    QFont mf = font(); mf.setPointSize(9);
    QFontMetrics mm(mf);
    int tw = mm.horizontalAdvance(actionLabel_) + 16;
    int th = mm.height() + 6;
    return QRect(width() - kPad - tw, height() - kPad - th, tw, th);
}

void ToastWindow::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QRect r = rect().adjusted(0, 0, -1, -1);
    QPainterPath path;
    path.addRoundedRect(r, 10, 10);
    p.setPen(Qt::NoPen);
    // 背景
    QColor bg(30, 30, 32, 235);
    p.setBrush(bg);
    p.drawPath(path);
    // 左侧色条
    QPainterPath accent;
    accent.addRoundedRect(QRect(0, 0, kAccent, height()), 3, 3);
    p.setBrush(color_);
    p.drawPath(accent);

    // 标题
    QFont tf = font(); tf.setBold(true); tf.setPointSize(10);
    p.setFont(tf);
    p.setPen(QColor(245, 245, 245));
    int textX = kAccent + kPad;
    int textW = width() - textX - kPad;
    QFontMetrics tm(tf);
    QRect titleRect = tm.boundingRect(QRect(textX, kPad, textW, 0), Qt::TextWordWrap, title_);
    p.drawText(titleRect, Qt::AlignLeft | Qt::TextWordWrap, title_);

    // 正文
    QFont mf = font(); mf.setPointSize(9);
    p.setFont(mf);
    p.setPen(QColor(210, 210, 210));
    int msgY = titleRect.bottom() + 4;
    QFontMetrics mm(mf);
    QRect msgRect = mm.boundingRect(QRect(textX, msgY, textW, 0), Qt::TextWordWrap, msg_);
    p.drawText(msgRect, Qt::AlignLeft | Qt::TextWordWrap, msg_);

    // 动作按钮
    if (!actionLabel_.isEmpty()) {
        QRect ar = actionRect();
        QPainterPath bp;
        bp.addRoundedRect(ar, 4, 4);
        p.setPen(QPen(color_.lighter(130), 1));
        p.setBrush(QColor(255, 255, 255, 26));
        p.drawPath(bp);
        p.setPen(QColor(235, 235, 235));
        p.drawText(ar, Qt::AlignCenter, actionLabel_);
        actionRect_ = ar;
    }
}

void ToastWindow::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        if (!actionLabel_.isEmpty() && actionRect_.contains(e->pos())) {
            if (onAction_) onAction_();
        }
        close();
    }
}
