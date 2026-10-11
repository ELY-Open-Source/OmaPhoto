#include "UI/ToolHeaderStyle.h"
#include <QApplication>

QFont ToolHeaderStyle::titleFont()
{
    QFont font = QApplication::font();
    font.setPixelSize(13);
    font.setWeight(QFont::DemiBold);
    return font;
}

QFont ToolHeaderStyle::controlFont()
{
    QFont font = QApplication::font();
    font.setPixelSize(12);
    return font;
}

ToolHeaderBar::ToolHeaderBar(const QString &text, QWidget *parent)
    : QWidget(parent), title(new QLabel(text, this)), row(new QHBoxLayout(this))
{
    setFixedHeight(ToolHeaderStyle::height);
    setFont(ToolHeaderStyle::controlFont());
    title->setFont(ToolHeaderStyle::titleFont());
    row->setContentsMargins(18, 0, 18, 0);
    row->setSpacing(12);
    row->addWidget(title);
    row->addStretch(1);
}

QScrollArea *ToolHeaderBar::scrolling(const std::vector<QWidget *> &controls, const QString &name)
{
    auto *content = new QWidget;
    auto *line = new QHBoxLayout(content);
    line->setContentsMargins(0, 0, 0, 0);
    line->setSpacing(row->spacing());
    for (QWidget *control : controls)
        line->addWidget(control);
    // The content keeps its size at the leading edge.
    line->addStretch(1);
    auto *area = new QScrollArea(this);
    area->setObjectName(name);
    area->setWidget(content);
    area->setWidgetResizable(true);
    area->setFrameShape(QFrame::NoFrame);
    area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    area->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // It takes the spare room, as SwiftUI's does.
    row->insertWidget(row->count() - 1, area, 1);
    row->setStretch(row->count() - 1, 0);
    return area;
}
