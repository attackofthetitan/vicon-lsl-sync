#pragma once

#include <QBoxLayout>
#include <QHBoxLayout>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QEvent>
#include <QString>

#include "gui/ElidingLabel.h"

#include <initializer_list>
#include <utility>

namespace vicon_lsl::gui_detail {

inline QLabel* makeTooltipLabel(const QString& text, QWidget* control, const QString& tooltip) {
    auto* label = new QLabel(text);
    label->setToolTip(tooltip);
    if (control) {
        label->setBuddy(control);
        control->setToolTip(tooltip);
        if (control->accessibleName().isEmpty()) {
            QString name = text;
            name.remove('&');
            name.remove(':');
            control->setAccessibleName(name.trimmed());
        }
    }
    return label;
}

// Status values share one convention: a single line that elides when the window
// is narrow. Uniform row heights are what make a dashboard grid readable, and a
// value that wrapped to three lines used to drag its whole row out of line.
inline ElidingLabel* makeStateValue(const QString& text, const QString& accessible_name,
                                    Qt::TextElideMode mode = Qt::ElideRight) {
    auto* label = new ElidingLabel(text, nullptr, mode);
    label->setAccessibleName(accessible_name);
    return label;
}

// A hairline divider that groups related items on one row without the visual
// weight of a box around them.
inline QFrame* makeSeparator() {
    auto* line = new QFrame();
    line->setFrameShape(QFrame::VLine);
    line->setFrameShadow(QFrame::Plain);
    line->setFixedWidth(1);
    return line;
}

// A sentence of explanation, not a compact status value: these wrap onto as many
// lines as they need rather than eliding, because the detail is the point.
inline QLabel* makeMessageValue(const QString& text, const QString& accessible_name) {
    auto* label = new QLabel(text);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
    label->setAccessibleName(accessible_name);
    label->setMinimumWidth(120);
    return label;
}

inline QPushButton* makeButton(const QString& text, const QString& tooltip,
                               const QString& accessible_name = {},
                               const QKeySequence& shortcut = {}) {
    auto* button = new QPushButton(text);
    button->setToolTip(tooltip);
    if (!accessible_name.isEmpty()) button->setAccessibleName(accessible_name);
    if (!shortcut.isEmpty()) button->setShortcut(shortcut);
    return button;
}

inline QLineEdit* makeEdit(const QString& tooltip, const QString& text = {}) {
    auto* edit = new QLineEdit(text);
    edit->setToolTip(tooltip);
    return edit;
}

inline QCheckBox* makeCheck(const QString& text, const QString& tooltip) {
    auto* check = new QCheckBox(text);
    check->setToolTip(tooltip);
    return check;
}

inline QSpinBox* makeSpin(int minimum, int maximum, int value) {
    auto* spin = new QSpinBox();
    spin->setRange(minimum, maximum);
    spin->setValue(value);
    return spin;
}

inline QDoubleSpinBox* makeDoubleSpin(double minimum, double maximum, int decimals,
                                      double step, double value) {
    auto* spin = new QDoubleSpinBox();
    spin->setRange(minimum, maximum);
    spin->setDecimals(decimals);
    spin->setSingleStep(step);
    spin->setValue(value);
    return spin;
}

inline void addField(QGridLayout* layout, int row, int col, const QString& label,
                     QWidget* control, const QString& tooltip = {}, int span = 1) {
    layout->addWidget(tooltip.isEmpty() ? new QLabel(label)
                                        : makeTooltipLabel(label, control, tooltip), row, col);
    layout->addWidget(control, row, col + 1, 1, span);
}

// A caption and its control bound together as one unit, so a FlowLayout can
// keep them side by side and wrap the pair as a whole. A plain grid cannot: its
// columns are fixed, so a narrow panel scrolls sideways instead of reflowing.
inline QWidget* makeFieldChip(const QString& label, QWidget* control,
                              const QString& tooltip, int control_width = 160) {
    auto* chip = new QWidget();
    auto* row = new QHBoxLayout(chip);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(6);
    row->addWidget(makeTooltipLabel(label, control, tooltip));
    control->setMinimumWidth(70);
    control->setMaximumWidth(control_width);
    row->addWidget(control, 1);
    return chip;
}

inline void addSpinRow(QGridLayout* layout, int row, const QString& label,
                       const QString& tooltip,
                       std::initializer_list<QDoubleSpinBox*> spins) {
    auto* text = new QLabel(label);
    text->setToolTip(tooltip);
    layout->addWidget(text, row, 0);
    int col = 1;
    for (QDoubleSpinBox* spin : spins) {
        spin->setToolTip(tooltip);
        layout->addWidget(spin, row, col++);
    }
}

inline void addWidgets(QBoxLayout* layout, std::initializer_list<QWidget*> widgets) {
    for (QWidget* widget : widgets) layout->addWidget(widget);
}

// Keeps every text field showing the start of its value.
//
// A QLineEdit made narrower keeps the horizontal scroll offset it had when it
// was wide, so a study root read as "ders/9z/t6lypx..." and a stream name as
// "iconMarkers" - both look like the front of the value was lost. Watching the
// fields themselves resize is the exact moment to scroll them back.
class LineEditStartKeeper : public QObject {
public:
    explicit LineEditStartKeeper(QWidget* root) : QObject(root) {
        for (QLineEdit* edit : root->findChildren<QLineEdit*>()) {
            edit->installEventFilter(this);
            edit->setCursorPosition(0);
        }
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::Resize) {
            auto* edit = qobject_cast<QLineEdit*>(watched);
            // Leave the field alone while it is being typed in.
            if (edit && !edit->hasFocus()) edit->setCursorPosition(0);
        }
        return QObject::eventFilter(watched, event);
    }
};

template <typename Layout>
std::pair<QGroupBox*, Layout*> makeGroup(const QString& title) {
    auto* box = new QGroupBox(title);
    return {box, new Layout(box)};
}

inline std::pair<QWidget*, QVBoxLayout*> makePage() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(8);
    return {page, layout};
}

// A scroll area whose preferred height is the height its content actually
// needs.
//
// QScrollArea::sizeHint() stops at twenty-four lines of text no matter how tall
// the content is, so a block of controls taller than that is handed less room
// than it needs and grows a scroll bar even when the window has space to spare.
// Rows that wrap make this worse, because their height depends on the width the
// area ends up with. Callers still cap the area with setMaximumHeight() to
// decide how much of the window it may take.
class ContentSizedScrollArea : public QScrollArea {
public:
    QSize sizeHint() const override {
        QWidget* content = widget();
        if (!content) return QScrollArea::sizeHint();
        QSize hint = content->sizeHint();
        const int available = viewport()->width();
        if (content->hasHeightForWidth() && available > 0) {
            hint.setHeight(content->heightForWidth(available));
        }
        const int frame = 2 * frameWidth();
        return hint + QSize(frame, frame);
    }
};

inline QScrollArea* scrollable(QWidget* page) {
    auto* area = new QScrollArea();
    area->setWidgetResizable(true);
    area->setFrameShape(QFrame::NoFrame);
    area->setWidget(page);
    return area;
}

} // namespace vicon_lsl::gui_detail
