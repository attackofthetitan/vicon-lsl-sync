#pragma once

#include <QLabel>
#include <QPaintEvent>
#include <QPainter>
#include <QSize>
#include <QString>

namespace vicon_lsl::gui_detail {

// A one-line label that cuts its text short with "…" instead of wrapping or
// widening the window, and shows the full text as a tooltip when it does.
class ElidingLabel : public QLabel {
public:
    explicit ElidingLabel(const QString& text = {}, QWidget* parent = nullptr,
                          Qt::TextElideMode mode = Qt::ElideRight)
        : QLabel(text, parent), mode_(mode) {
        QLabel::setWordWrap(false);
        // Asks for the full text width but can shrink to minimumSizeHint().
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
    }

    // Turn off when the caller sets its own tooltip.
    void setAutomaticToolTip(bool enabled) { automatic_tooltip_ = enabled; }

    // Lets the layout shrink the label to a few characters.
    QSize minimumSizeHint() const override {
        QSize hint = QLabel::minimumSizeHint();
        const int floor_width = fontMetrics().horizontalAdvance(QStringLiteral("mm…"));
        hint.setWidth(qMin(hint.width(), floor_width));
        return hint;
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        const QRect area = contentsRect();
        const QString full = text();
        const QString shown = fontMetrics().elidedText(full, mode_, area.width());
        if (automatic_tooltip_) {
            const QString wanted = shown == full ? QString() : full;
            if (toolTip() != wanted) setToolTip(wanted);
        }
        if (shown == full) {
            QLabel::paintEvent(event);
            return;
        }
        QPainter painter(this);
        painter.setPen(palette().color(foregroundRole()));
        painter.drawText(area, static_cast<int>(alignment()) | Qt::TextSingleLine, shown);
    }

private:
    Qt::TextElideMode mode_ = Qt::ElideRight;
    bool automatic_tooltip_ = true;
};

} // namespace vicon_lsl::gui_detail
