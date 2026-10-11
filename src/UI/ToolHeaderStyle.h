#pragma once
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QWidget>
#include <vector>

// Shared metrics: switching tools never moves the canvas.
namespace ToolHeaderStyle {
inline constexpr int height = 42;
QFont titleFont();
QFont controlFont();
}

// A tool's bar: its title, then its controls' row.
class ToolHeaderBar : public QWidget {
    // findChild needs the meta-object; Qt 6.11 checks at compile time.
    Q_OBJECT
public:
    explicit ToolHeaderBar(const QString &title, QWidget *parent = nullptr);

    QLabel *const title;
    // Controls go before the stretch that ends the row.
    QHBoxLayout *const row;
    // Swift's horizontal ScrollView: these scroll, not squeeze, when narrow.
    QScrollArea *scrolling(const std::vector<QWidget *> &controls, const QString &name);
};
