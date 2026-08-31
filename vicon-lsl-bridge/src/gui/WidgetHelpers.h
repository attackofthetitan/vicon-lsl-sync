#pragma once

#include <QBoxLayout>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGridLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QString>

#include <initializer_list>

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

inline QLabel* makeStateValue(const QString& text, const QString& accessible_name) {
    auto* label = new QLabel(text);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
    label->setAccessibleName(accessible_name);
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

inline QScrollArea* scrollable(QWidget* page) {
    auto* area = new QScrollArea();
    area->setWidgetResizable(true);
    area->setFrameShape(QFrame::NoFrame);
    area->setWidget(page);
    return area;
}

} // namespace vicon_lsl::gui_detail
