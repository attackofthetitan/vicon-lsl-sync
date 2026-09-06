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

// Shorten status text to one line when space is limited.
inline ElidingLabel* makeStateValue(const QString& text, const QString& accessible_name,
                                    Qt::TextElideMode mode = Qt::ElideRight) {
    auto* label = new ElidingLabel(text, nullptr, mode);
    label->setAccessibleName(accessible_name);
    return label;
}

// A vertical divider between related controls.
inline QFrame* makeSeparator() {
    auto* line = new QFrame();
    line->setFrameShape(QFrame::VLine);
    line->setFrameShadow(QFrame::Plain);
    line->setFixedWidth(1);
    return line;
}

// Wrap explanations so the full message stays readable.
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

// Keep the label beside its control when FlowLayout wraps rows.
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

inline void addWidgets(QBoxLayout* layout, std::initializer_list<QWidget*> widgets) {
    for (QWidget* widget : widgets) layout->addWidget(widget);
}

// Show the start of inactive text fields after resizing.
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

// Size the area to its content, including wrapped rows. Callers may cap its height.
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
