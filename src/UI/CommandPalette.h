#pragma once
#include "Document/EditorSession.h"
#include <QMenuBar>
#include <functional>
#include <optional>
#include <set>
#include <vector>

// One thing the palette runs: a menu command or tool.
struct CommandPaletteEntry {
    // The title, numbered when an earlier entry shares it.
    QString id;
    // Its full path, shown and searched: "Filter › Gaussian Blur…".
    QString title;
    // Its current key, "Ctrl+Alt+P", or none.
    std::optional<QString> shortcut;
    // Disabled now: listed, greyed, never run.
    bool isEnabled = true;
    // A toggle on now, checked as in its menu.
    bool isOn = false;
    std::function<void()> perform;
};

// Fuzzy matching: letters in order, word starts and runs count.
namespace CommandPaletteSearch {
std::optional<int> score(const QString &query, const QString &text);
// Matches best first, enabled ahead; no query keeps menu order.
std::vector<CommandPaletteEntry> rank(const std::vector<CommandPaletteEntry> &entries, const QString &query);
}

// The menu bar as entries, read each time it opens.
namespace CommandPaletteMenu {
// `skipping` holds titles left out: whole menus or single entries.
std::vector<CommandPaletteEntry> entries(const QMenuBar &bar, const std::set<QString> &skipping);
// A menu title without its mnemonic: "Fil&ter" is Filter.
QString plain(const QString &text);
}

namespace CommandPaletteCommands {
// The Layers panel's mask button, so the palette finds it.
std::vector<CommandPaletteEntry> layerCommands(EditorSession &session);
// Every tool and each of its modes, by name.
std::vector<CommandPaletteEntry> tools(EditorSession &session);
}

// The query, the ranked results and which one is chosen.
class CommandPaletteModel {
public:
    explicit CommandPaletteModel(std::vector<CommandPaletteEntry> entries) : m_entries(std::move(entries)) {}

    const std::vector<CommandPaletteEntry> &entries() const { return m_entries; }
    const QString &query() const { return m_query; }
    // Only a real change goes back to the top.
    void setQuery(const QString &query);
    int selection = 0;
    std::vector<CommandPaletteEntry> results() const { return CommandPaletteSearch::rank(m_entries, m_query); }
    std::optional<CommandPaletteEntry> selected() const;
    // Up and Down, wrapping at either end.
    void move(int step);

private:
    std::vector<CommandPaletteEntry> m_entries;
    QString m_query;
};
