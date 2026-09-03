#pragma once

#include <QLabel>
#include <QPaintEvent>
#include <QPainter>
#include <QSize>
#include <QString>

namespace vicon_lsl::gui_detail {

// A status value that shortens itself instead of reshaping the row it sits in.
//
// A wrapping QLabel grows taller when its text is long, so one chatty value
// pushes its whole grid row down and the dashboard reads as broken alignment.
// A plain non-wrapping QLabel instead demands the full width of its text, which
// makes the window impossible to narrow. This label always occupies exactly one
// line, reports a small minimum width so the layout can shrink, and elides the
// text it cannot fit. The untruncated value stays reachable as a tooltip.
class ElidingLabel : public QLabel {
public:
    explicit ElidingLabel(const QString& text = {}, QWidget* parent = nullptr,
                          Qt::TextElideMode mode = Qt::ElideRight)
        : QLabel(text, parent), mode_(mode) {
        QLabel::setWordWrap(false);
        // Preferred, not Ignored: the label still asks for the width its text
        // wants, so a headline reads at full length, but it may be squeezed all
        // the way down to minimumSizeHint() when the window is narrow.
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
    }

    void setElideMode(Qt::TextElideMode mode) {
        mode_ = mode;
        update();
    }

    // Turn off while a caller supplies a richer tooltip of its own, such as a
    // destination path shown together with its validation summary.
    void setAutomaticToolTip(bool enabled) { automatic_tooltip_ = enabled; }

    // Only the floor changes. A non-wrapping QLabel reports its full text width
    // as the minimum, which is what stops a window from narrowing; the preferred
    // width stays QLabel's own, so a label given room still reads in full.
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
            if (toolTip() != wanted) const_cast<ElidingLabel*>(this)->setToolTip(wanted);
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
