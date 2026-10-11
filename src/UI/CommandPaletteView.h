#pragma once
#include "UI/CommandPalette.h"
#include <QLineEdit>
#include <QPointer>
#include <QScrollArea>

class QLabel;

// The ranked rows under the search field.
class CommandPaletteRows : public QWidget {
    Q_OBJECT
public:
    CommandPaletteRows(CommandPaletteModel &model, std::function<void(const CommandPaletteEntry &)> run, QWidget *parent);

    int rowHeight() const;
    // Where row `index` sits, inside the rows.
    QRect rowRect(int index) const;
    void refresh();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    CommandPaletteModel &m_model;
    std::function<void(const CommandPaletteEntry &)> m_run;
    std::vector<CommandPaletteEntry> m_shown;
};

// The palette: a search field over the ranked commands.
class CommandPaletteView : public QWidget {
    Q_OBJECT
public:
    CommandPaletteView(std::vector<CommandPaletteEntry> entries, std::function<void(const CommandPaletteEntry &)> run, QWidget *parent);

    CommandPaletteModel &model() { return m_model; }
    QLineEdit &field() { return *m_field; }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void refresh();

    CommandPaletteModel m_model;
    std::function<void(const CommandPaletteEntry &)> m_run;
    QLineEdit *m_field;
    QScrollArea *m_scroller;
    CommandPaletteRows *m_rows;
    QLabel *m_empty;
};

// The palette over the window; a click elsewhere hides it.
class CommandPaletteController : public QObject {
    Q_OBJECT
public:
    // Left out of the palette: the palette itself.
    static const std::set<QString> skipped;

    explicit CommandPaletteController(QWidget &window) : m_window(window) {}
    // A hidden palette lets go of the focus it held.
    bool eventFilter(QObject *watched, QEvent *event) override;

    bool isOpen() const { return m_panel && m_panel->isVisible(); }
    QWidget *panel() const { return m_panel; }
    // Opens over the window, or closes; `search` works inside.
    void toggle(EditorSession &session, const QMenuBar &bar, QAction *search);
    void close();
    // Closes, gives the window back its keys, then runs it.
    void run(const CommandPaletteEntry &entry);

private:
    QWidget &m_window;
    QPointer<QWidget> m_panel;
};
