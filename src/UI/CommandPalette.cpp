#include "UI/CommandPalette.h"
#include "Logging.h"
#include <QPointer>
#include <QTextBoundaryFinder>
#include <algorithm>
#include <map>

namespace {
// Swift's Characters: one string a grapheme.
QStringList characters(const QString &text)
{
    QStringList result;
    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, text);
    qsizetype start = 0;
    while (finder.toNextBoundary() != -1) {
        result << text.mid(start, finder.position() - start);
        start = finder.position();
    }
    return result;
}

char32_t first(const QString &character)
{
    return character.toUcs4().value(0);
}

bool isLetter(const QString &character)
{
    return QChar::isLetter(first(character));
}

// Each word starts a word of `title`, in order.
bool wordsStartWords(const QStringList &words, const QStringList &title)
{
    qsizetype from = 0;
    for (const QString &word : words) {
        const QStringList wanted = characters(word);
        std::optional<qsizetype> found;
        for (qsizetype at = from; at + wanted.size() <= title.size() && !found; ++at)
            if (std::equal(wanted.begin(), wanted.end(), title.begin() + at) && (at == 0 || !isLetter(title[at - 1])))
                found = at + wanted.size();
        if (!found)
            return false;
        from = found.value();
    }
    return true;
}

void collect(const QList<QAction *> &items, const QStringList &path, const std::set<QString> &skipping, std::map<QString, int> &seen,
             std::vector<CommandPaletteEntry> &result)
{
    for (QAction *item : items) {
        const QString name = CommandPaletteMenu::plain(item->text());
        if (item->isSeparator() || !item->isVisible() || skipping.contains(name) || name.isEmpty())
            continue;
        if (item->menu()) {
            collect(item->menu()->actions(), path + QStringList{name}, skipping, seen, result);
            continue;
        }
        const QString title = (path + QStringList{name}).join(QStringLiteral(" › "));
        const int count = seen[title]++;
        const QKeySequence key = item->shortcut();
        result.push_back({.id = count == 0 ? title : QStringLiteral("%1 (%2)").arg(title).arg(count + 1),
                          .title = title,
                          .shortcut = key.isEmpty() ? std::nullopt : std::optional(key.toString(QKeySequence::NativeText)),
                          .isEnabled = item->isEnabled(),
                          .isOn = item->isChecked(),
                          .perform = [item = QPointer<QAction>(item), title] {
                              if (!item) {
                                  qCWarning(lcApp) << "palette: menu entry gone:" << title;
                                  return;
                              }
                              item->trigger();
                          }});
    }
}
}

std::optional<int> CommandPaletteSearch::score(const QString &query, const QString &text)
{
    QStringList wanted;
    for (const QString &character : characters(query.toLower()))
        if (!QChar::isSpace(first(character)))
            wanted << character;
    if (wanted.isEmpty())
        return 0;
    const QStringList letters = characters(text.toLower());
    int total = 0;
    qsizetype from = 0;
    qsizetype previous = -2;
    for (const QString &character : wanted) {
        const qsizetype found = letters.indexOf(character, from);
        if (found < 0)
            return std::nullopt;
        const bool wordStart = found == 0 || !(isLetter(letters[found - 1]) || QChar::isNumber(first(letters[found - 1])));
        total += 1 + (wordStart ? 8 : 0) + (found == previous + 1 ? 5 : 0);
        previous = found;
        from = found + 1;
    }
    // How the text was found, best first; scattered letters last.
    const QString title = letters.join(QString());
    const QString phrase = trimmedWhitespaces(query.toLower());
    const QStringList words = phrase.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    int tier = 1;
    if (wordsStartWords({phrase}, letters))
        tier = 5;
    else if (words.size() > 1 && wordsStartWords(words, letters))
        tier = 4;
    else if (title.contains(phrase))
        tier = 3;
    else if (QString(title).remove(QLatin1Char(' ')).contains(wanted.join(QString())))
        tier = 2;
    // Same tier: shorter title wins; scattered letters by fall.
    return tier * 1'000'000 + (tier == 1 ? total * 100 : 0) - int(letters.size() / 4);
}

std::vector<CommandPaletteEntry> CommandPaletteSearch::rank(const std::vector<CommandPaletteEntry> &entries, const QString &query)
{
    struct Scored {
        const CommandPaletteEntry *entry;
        int score;
    };
    std::vector<Scored> scored;
    for (const CommandPaletteEntry &entry : entries)
        if (const std::optional<int> found = score(query, entry.title))
            scored.push_back({&entry, found.value()});
    std::stable_sort(scored.begin(), scored.end(), [&](const Scored &a, const Scored &b) {
        if (a.entry->isEnabled != b.entry->isEnabled)
            return a.entry->isEnabled;
        return !query.isEmpty() && a.score > b.score;
    });
    std::vector<CommandPaletteEntry> result;
    for (const Scored &each : scored)
        result.push_back(*each.entry);
    return result;
}

std::vector<CommandPaletteEntry> CommandPaletteMenu::entries(const QMenuBar &bar, const std::set<QString> &skipping)
{
    std::map<QString, int> seen;
    std::vector<CommandPaletteEntry> result;
    collect(bar.actions(), {}, skipping, seen, result);
    return result;
}

QString CommandPaletteMenu::plain(const QString &text)
{
    QString result;
    for (qsizetype at = 0; at < text.size(); ++at) {
        if (text[at] != QLatin1Char('&'))
            result += text[at];
        else if (at + 1 < text.size() && text[at + 1] == QLatin1Char('&'))
            result += text[++at];
    }
    return result;
}

std::vector<CommandPaletteEntry> CommandPaletteCommands::layerCommands(EditorSession &session)
{
    const std::optional<ImageLayer> active = session.activeLayer();
    const bool canAdd = session.canEditMask() && !(active && active->mask);
    const bool selected = session.selection().has_value();
    const auto add = [&session](bool revealing) { return [session = QPointer(&session), revealing] { if (session) session->addMask(revealing); }; };
    const QString reveal = selected ? QStringLiteral("Layer › Add Layer Mask from Selection") : QStringLiteral("Layer › Add Layer Mask");
    const QString hide = selected ? QStringLiteral("Layer › Add Layer Mask Hiding Selection") : QStringLiteral("Layer › Add Layer Mask (Hide All)");
    return {{.id = reveal, .title = reveal, .shortcut = std::nullopt, .isEnabled = canAdd, .isOn = false, .perform = add(true)},
            {.id = hide, .title = hide, .shortcut = std::nullopt, .isEnabled = canAdd, .isOn = false, .perform = add(false)}};
}

std::vector<CommandPaletteEntry> CommandPaletteCommands::tools(EditorSession &session)
{
    using Setup = std::function<void(EditorSession &)>;
    const std::vector<std::tuple<QString, QString, NavigationTool, Setup>> tools{
        {"Move / Transform", "V", NavigationTool::move, nullptr},
        {"Rectangular Marquee", "M", NavigationTool::marquee, [](EditorSession &s) { s.setMarqueeKind(LassoKind::rectangle); }},
        {"Elliptical Marquee", "M", NavigationTool::marquee, [](EditorSession &s) { s.setMarqueeKind(LassoKind::ellipse); }},
        {"Lasso", "L", NavigationTool::lasso, [](EditorSession &s) { s.setLassoKind(LassoKind::freehand); }},
        {"Polygonal Lasso", "L", NavigationTool::lasso, [](EditorSession &s) { s.setLassoKind(LassoKind::polygonal); }},
        {"Magic Wand", "W", NavigationTool::wand, [](EditorSession &s) { s.setWandMode(WandMode::wand); }},
        {"Object Selection", "W", NavigationTool::wand, [](EditorSession &s) { s.setWandMode(WandMode::object); }},
        {"Crop", "C", NavigationTool::crop, nullptr},
        {"Brush", "B", NavigationTool::brush, [](EditorSession &s) { s.setBrushMode(BrushToolMode::paint); }},
        {"Eraser", "E", NavigationTool::brush, [](EditorSession &s) { s.setBrushMode(BrushToolMode::erase); }},
        {"Spot Healing Brush", "J", NavigationTool::spotHealing, nullptr},
        {"Clone Stamp", "S", NavigationTool::cloneStamp, nullptr},
        {"Liquify", "R", NavigationTool::blur, [](EditorSession &s) { s.setBlurMode(BlurToolMode::liquify); }},
        {"Blur", "R", NavigationTool::blur, [](EditorSession &s) { s.setBlurMode(BlurToolMode::blur); }},
        {"Smudge", "R", NavigationTool::blur, [](EditorSession &s) { s.setBlurMode(BlurToolMode::smudge); }},
        {"Gradient", "G", NavigationTool::gradient, nullptr},
        {"Rectangle", "U", NavigationTool::shape, [](EditorSession &s) { s.setShapeKind(ShapeKind::rectangle); }},
        {"Ellipse", "U", NavigationTool::shape, [](EditorSession &s) { s.setShapeKind(ShapeKind::ellipse); }},
        {"Line", "U", NavigationTool::shape, [](EditorSession &s) { s.setShapeKind(ShapeKind::line); }},
        {"Type", "T", NavigationTool::type, nullptr},
        {"Eyedropper", "I", NavigationTool::eyedropper, nullptr},
        {"Hand", "H", NavigationTool::hand, nullptr},
        {"Zoom", "Z", NavigationTool::zoom, nullptr}};
    std::vector<CommandPaletteEntry> result;
    for (const auto &[name, key, tool, setup] : tools) {
        const QString id = QStringLiteral("Tool › %1").arg(name);
        result.push_back({.id = id, .title = id, .shortcut = key, .isEnabled = session.document().has_value(), .isOn = false,
                          .perform = [session = QPointer(&session), tool, setup] {
                              if (!session)
                                  return;
                              session->selectTool(tool);
                              // Only once the tool is in hand.
                              if (session->tool() == tool && setup)
                                  setup(*session);
                          }});
    }
    return result;
}

void CommandPaletteModel::setQuery(const QString &query)
{
    if (query != m_query)
        selection = 0;
    m_query = query;
}

std::optional<CommandPaletteEntry> CommandPaletteModel::selected() const
{
    const std::vector<CommandPaletteEntry> found = results();
    if (selection < 0 || selection >= int(found.size()))
        return std::nullopt;
    return found[size_t(selection)];
}

void CommandPaletteModel::move(int step)
{
    const int count = int(results().size());
    if (count == 0) {
        selection = 0;
        return;
    }
    selection = ((selection + step) % count + count) % count;
}
