#pragma once

#include <QLayout>
#include <QLayoutItem>
#include <QList>
#include <QMargins>
#include <QRect>
#include <QSize>

namespace vicon_lsl::gui_detail {

// Places items left to right and wraps to a new line when the width runs out.
// A row of controls in a QHBoxLayout cannot shrink below the sum of its items,
// so a narrow panel forces horizontal scrolling and clips whatever does not
// fit. This layout reports the widest single item as its minimum instead, so
// the same controls stay reachable at any width.
//
// No Q_OBJECT: this needs no meta-object, so it stays header-only and does not
// depend on moc.
class FlowLayout : public QLayout {
public:
    explicit FlowLayout(int horizontal_spacing = 6, int vertical_spacing = 4)
        : horizontal_spacing_(horizontal_spacing), vertical_spacing_(vertical_spacing) {
        setContentsMargins(0, 0, 0, 0);
    }

    ~FlowLayout() override {
        while (QLayoutItem* item = takeAt(0)) delete item;
    }

    FlowLayout(const FlowLayout&) = delete;
    FlowLayout& operator=(const FlowLayout&) = delete;

    void addItem(QLayoutItem* item) override { items_.append(item); }
    int count() const override { return static_cast<int>(items_.size()); }
    QLayoutItem* itemAt(int index) const override { return items_.value(index); }

    QLayoutItem* takeAt(int index) override {
        return (index >= 0 && index < items_.size()) ? items_.takeAt(index) : nullptr;
    }

    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }

    int heightForWidth(int width) const override {
        return layoutItems(QRect(0, 0, width, 0), true);
    }

    void setGeometry(const QRect& rect) override {
        QLayout::setGeometry(rect);
        layoutItems(rect, false);
    }

    QSize sizeHint() const override { return minimumSize(); }

    QSize minimumSize() const override {
        QSize size;
        for (const QLayoutItem* item : items_) {
            if (item->isEmpty()) continue;
            size = size.expandedTo(item->minimumSize());
        }
        const QMargins margins = contentsMargins();
        return size + QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
    }

private:
    int layoutItems(const QRect& rect, bool measure_only) const {
        const QMargins margins = contentsMargins();
        const QRect area = rect.adjusted(margins.left(), margins.top(), -margins.right(), -margins.bottom());
        int x = area.x();
        int y = area.y();
        int line_height = 0;
        for (QLayoutItem* item : items_) {
            // A hidden widget, such as the load progress bar before a load
            // starts, must not reserve a slot on the line.
            if (item->isEmpty()) continue;
            const QSize hint = item->sizeHint();
            int next_x = x + hint.width();
            if (next_x - area.x() > area.width() && line_height > 0) {
                x = area.x();
                y += line_height + vertical_spacing_;
                next_x = x + hint.width();
                line_height = 0;
            }
            if (!measure_only) {
                item->setGeometry(QRect(QPoint(x, y), hint));
            }
            x = next_x + horizontal_spacing_;
            line_height = qMax(line_height, hint.height());
        }
        return y + line_height - rect.y() + margins.bottom();
    }

    QList<QLayoutItem*> items_;
    int horizontal_spacing_;
    int vertical_spacing_;
};

} // namespace vicon_lsl::gui_detail
