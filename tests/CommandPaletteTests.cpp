#include "MenuFixtures.h"
#include "UI/CommandPalette.h"

// The palette's search, model, menu reading and commands.
namespace {
CommandPaletteEntry entry(const QString &id, bool enabled = true)
{
    return {.id = id, .title = id, .shortcut = std::nullopt, .isEnabled = enabled, .isOn = false, .perform = [] {}};
}

QStringList ids(const std::vector<CommandPaletteEntry> &entries)
{
    QStringList result;
    for (const CommandPaletteEntry &each : entries)
        result << each.id;
    return result;
}

const CommandPaletteEntry &titled(const std::vector<CommandPaletteEntry> &entries, const QString &title)
{
    for (const CommandPaletteEntry &each : entries)
        if (each.title == title)
            return each;
    throw std::runtime_error(title.toStdString() + " not listed");
}
}

class CommandPaletteTests : public QObject {
    Q_OBJECT
private slots:
    void lettersInOrderMatchAndWordStartsCount();
    void scoresAreSwiftsOwn();
    void rankingPutsTheBestAndEnabledFirst();
    void wholeWordsBeatScatteredLetters();
    void typedWordsStartingWords();
    void theModelMovesAndWraps();
    void aMenuBarReadsAsPaths();
    void realMenuBarRunsItsCommands();
    void layerCommandsAddAMask();
    void toolsTakeTheirModes();
};

void CommandPaletteTests::lettersInOrderMatchAndWordStartsCount()
{
    QVERIFY(CommandPaletteSearch::score("gb", "Filter › Gaussian Blur…"));
    QVERIFY(CommandPaletteSearch::score("BLUR", "Filter › Gaussian Blur…"));
    QVERIFY(!CommandPaletteSearch::score("bg", "Filter › Gaussian Blur…"));
    QCOMPARE(CommandPaletteSearch::score("", "Anything").value(), 0);
    QVERIFY(CommandPaletteSearch::score("gb", "Filter › Gaussian Blur…").value() > CommandPaletteSearch::score("gb", "Edit › Debug Tab").value());
}

void CommandPaletteTests::scoresAreSwiftsOwn()
{
    // Swift 6.0.3 running v1.4.9's CommandPaletteSearch on each pair.
    const std::vector<std::tuple<QString, QString, int>> cases{
        {"gb", "Filter › Gaussian Blur…", 1001795}, {"gb", "Edit › Debug Tab", 1000196}, {"BLUR", "Filter › Gaussian Blur…", 4999995},
        {"lasso", "Tool › Lasso", 4999997}, {"lasso", "Tool › Polygonal Lasso", 4999995},
        {"lasso", "Layer › New Adjustment Layer › Hue/Saturation…", 1002589}, {"new layer", "Layer › New Blank Layer", 3999995},
        {"new layer", "Layer › New Adjustment Layer › Curves…", 3999991}, {"ussian", "Filter › Gaussian Blur…", 2999995},
        {"gaussianbl", "Filter › Gaussian Blur…", 1999995}, {"  blur ", "Filter › Gaussian Blur…", 4999995}, {"2", "View › Zoom 2x", 4999997},
        {"x2", "Layer › Box2", 2999997}, {"e u", "Edit › Undo", 3999998}, {"fl", "Layer › Flip Layer Horizontal", 4999993},
        {"é", "Café › Crème", 2999997}, {"ab", "a1b c", 1000999}, {"b", "1b", 5000000}, {"mask", "Layer › Add Layer Mask (Hide All)", 4999992},
        {"hide all", "Layer › Add Layer Mask (Hide All)", 4999992}, {"\tblur", "Filter › Gaussian Blur…", 4999995}, {"ab", "x-ab", 4999999},
        {"lay mask", "Layer › Add Layer Mask", 3999995}, {"b a", "a bab", 1999999}, {"b a", "ab a", 2999999},
        {"café", "File › Open Recent › Cafe\u0301", 4999994}, {"cafe\u0301", "File › Open Recent › Café", 4999994},
        {"café", "File › Open Recent › Café", 4999994}, {"e\u0301", "File › Open Recent › Café", 2999994}};
    for (const auto &[query, title, score] : cases)
        QVERIFY2(CommandPaletteSearch::score(query, title).value() == score, qPrintable(query + " in " + title));
    QVERIFY(!CommandPaletteSearch::score("new layer", "Layer › Duplicate Layer"));
}

void CommandPaletteTests::rankingPutsTheBestAndEnabledFirst()
{
    const std::vector<CommandPaletteEntry> entries{entry("Edit › Paste"), entry("Layer › Flip Layer Horizontal"), entry("Filter › Gaussian Blur…"),
                                                   entry("Filter › Motion Blur…"), entry("Edit › Undo", false), entry("Image › Levels…")};
    QCOMPARE(ids(CommandPaletteSearch::rank(entries, "gau")).value(0), QString("Filter › Gaussian Blur…"));
    // Equal scores keep menu order.
    QCOMPARE(ids(CommandPaletteSearch::rank(entries, "blur")), (QStringList{"Filter › Gaussian Blur…", "Filter › Motion Blur…"}));
    QCOMPARE(ids(CommandPaletteSearch::rank(entries, "fl")).value(0), QString("Layer › Flip Layer Horizontal"));
    QCOMPARE(ids(CommandPaletteSearch::rank(entries, "")),
             (QStringList{"Edit › Paste", "Layer › Flip Layer Horizontal", "Filter › Gaussian Blur…", "Filter › Motion Blur…", "Image › Levels…", "Edit › Undo"}));
    // Disabled after enabled, whatever it scores.
    QCOMPARE(ids(CommandPaletteSearch::rank(entries, "e u")).last(), QString("Edit › Undo"));
    QCOMPARE(ids(CommandPaletteSearch::rank({entry("Edit › Undo", false), entry("Layer › Merge Layers")}, "e")),
             (QStringList{"Layer › Merge Layers", "Edit › Undo"}));
}

void CommandPaletteTests::wholeWordsBeatScatteredLetters()
{
    const std::vector<CommandPaletteEntry> entries{entry("Layer › New Adjustment Layer › Hue/Saturation…"), entry("Tool › Lasso"), entry("Tool › Polygonal Lasso")};
    QCOMPARE(ids(CommandPaletteSearch::rank(entries, "lasso")), (QStringList{"Tool › Lasso", "Tool › Polygonal Lasso", "Layer › New Adjustment Layer › Hue/Saturation…"}));
    CommandPaletteModel model(entries);
    model.setQuery("lasso");
    model.move(1);
    // Return writes the same text back: the choice stays.
    model.setQuery("lasso");
    QCOMPARE(model.selected().value().id, QString("Tool › Polygonal Lasso"));
    model.setQuery("lass");
    QCOMPARE(model.selection, 0);
}

void CommandPaletteTests::typedWordsStartingWords()
{
    const std::vector<CommandPaletteEntry> entries{entry("Layer › New Adjustment Layer › Curves…"), entry("Layer › New Adjustment Layer › Exposure…"),
                                                   entry("Layer › Duplicate Layer"), entry("Layer › New Blank Layer")};
    QCOMPARE(ids(CommandPaletteSearch::rank(entries, "new layer")).value(0), QString("Layer › New Blank Layer"));
}

void CommandPaletteTests::theModelMovesAndWraps()
{
    CommandPaletteModel model({entry("A"), entry("B"), entry("C")});
    model.move(-1);
    QCOMPARE(model.selection, 2);
    model.move(1);
    QCOMPARE(model.selection, 0);
    model.move(4);
    QCOMPARE(model.selected().value().id, QString("B"));
    model.setQuery("zzz");
    QVERIFY(!model.selected());
    model.move(1);
    QCOMPARE(model.selection, 0);
}

void CommandPaletteTests::aMenuBarReadsAsPaths()
{
    QMenuBar bar;
    QMenu *filter = bar.addMenu("Fil&ter");
    QAction *blur = filter->addAction("&Gaussian Blur…");
    blur->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_P));
    filter->addSeparator();
    filter->addSection("Section");
    filter->addAction("Black && White…")->setEnabled(false);
    filter->addAction("Hidden")->setVisible(false);
    QAction *grid = filter->addMenu("Show")->addAction("Grid");
    grid->setCheckable(true);
    grid->setChecked(true);
    filter->addAction("Twin");
    filter->addAction("Twin");
    filter->addAction("Search Commands…");
    bar.addMenu("Skipped")->addAction("Inside");
    const std::vector<CommandPaletteEntry> entries = CommandPaletteMenu::entries(bar, {"Search Commands…", "Skipped"});
    QCOMPARE(ids(entries), (QStringList{"Filter › Gaussian Blur…", "Filter › Black & White…", "Filter › Show › Grid", "Filter › Twin", "Filter › Twin (2)"}));
    QCOMPARE(entries[0].shortcut.value(), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_P).toString(QKeySequence::NativeText));
    QVERIFY(!entries[1].shortcut && !entries[1].isEnabled && entries[0].isEnabled);
    QVERIFY(entries[2].isOn && !entries[0].isOn);
    QCOMPARE(entries[4].title, QString("Filter › Twin"));
    // An entry triggers its own action; a gone one warns.
    QSignalSpy triggered(blur, &QAction::triggered);
    entries[0].perform();
    QCOMPARE(triggered.count(), 1);
    delete blur;
    QTest::ignoreMessage(QtWarningMsg, "palette: menu entry gone: \"Filter › Gaussian Blur…\"");
    entries[0].perform();
    QCOMPARE(CommandPaletteMenu::plain("A&&&B&"), QString("A&B"));
}

void CommandPaletteTests::realMenuBarRunsItsCommands()
{
    Bar bar;
    const auto read = [&] { return CommandPaletteMenu::entries(*bar.window.menuBar(), {"Search Commands…"}); };
    const std::vector<CommandPaletteEntry> listed = read();
    QVERIFY(std::ranges::none_of(listed, [](const CommandPaletteEntry &e) { return e.title == "View › Search Commands…"; }));
    QVERIFY(titled(listed, "Filter › Gaussian Blur…").title == "Filter › Gaussian Blur…");
    QVERIFY(!titled(listed, "View › Zoom In").isEnabled);
    QCOMPARE(titled(listed, "View › Fit Canvas").shortcut.value(), QKeySequence(Qt::CTRL | Qt::Key_0).toString(QKeySequence::NativeText));
    QCOMPARE(titled(listed, "Image › Black & White…").title, QString("Image › Black & White…"));
    const CommandPaletteEntry grid = titled(listed, "View › Pixel Grid (800% and above)");
    QVERIFY(grid.isOn && bar.session().showsPixelGrid());
    grid.perform();
    QVERIFY(!bar.session().showsPixelGrid() && !titled(read(), grid.title).isOn);
    grid.perform();
    QVERIFY(bar.session().showsPixelGrid());
}

void CommandPaletteTests::layerCommandsAddAMask()
{
    EditorSession session;
    std::vector<CommandPaletteEntry> commands = CommandPaletteCommands::layerCommands(session);
    QCOMPARE(ids(commands), (QStringList{"Layer › Add Layer Mask", "Layer › Add Layer Mask (Hide All)"}));
    QVERIFY(!commands[0].isEnabled && !commands[1].isEnabled);
    session.createDocument(4, 4);
    session.insert(white(), QPointF(2, 2));
    session.selectAll();
    commands = CommandPaletteCommands::layerCommands(session);
    QCOMPARE(ids(commands), (QStringList{"Layer › Add Layer Mask from Selection", "Layer › Add Layer Mask Hiding Selection"}));
    QVERIFY(commands[0].isEnabled && commands[1].isEnabled);
    commands[1].perform();
    QCOMPARE(session.history.undoName(), QString("Hide Selection"));
    QVERIFY(!CommandPaletteCommands::layerCommands(session)[0].isEnabled);
    session.undo();
    session.deselect();
    CommandPaletteCommands::layerCommands(session)[0].perform();
    QCOMPARE(session.history.undoName(), QString("Add Reveal-All Mask"));
}

void CommandPaletteTests::toolsTakeTheirModes()
{
    EditorSession session;
    std::vector<CommandPaletteEntry> tools = CommandPaletteCommands::tools(session);
    QCOMPARE(tools.size(), size_t(23));
    QCOMPARE(tools[0].id, QString("Tool › Move / Transform"));
    QCOMPARE(tools[4].shortcut.value(), QString("L"));
    QVERIFY(std::ranges::none_of(tools, [](const CommandPaletteEntry &e) { return e.isEnabled; }));
    session.createDocument(4, 4);
    tools = CommandPaletteCommands::tools(session);
    QVERIFY(std::ranges::all_of(tools, [](const CommandPaletteEntry &e) { return e.isEnabled; }));
    // Each named mode, reached from its sibling.
    const std::vector<std::tuple<QString, NavigationTool, std::function<bool()>>> modes{
        {"Polygonal Lasso", NavigationTool::lasso, [&] { return session.lassoKind() == LassoKind::polygonal; }},
        {"Lasso", NavigationTool::lasso, [&] { return session.lassoKind() == LassoKind::freehand; }},
        {"Elliptical Marquee", NavigationTool::marquee, [&] { return session.marqueeKind() == LassoKind::ellipse; }},
        {"Rectangular Marquee", NavigationTool::marquee, [&] { return session.marqueeKind() == LassoKind::rectangle; }},
        {"Object Selection", NavigationTool::wand, [&] { return session.wandMode() == WandMode::object; }},
        {"Magic Wand", NavigationTool::wand, [&] { return session.wandMode() == WandMode::wand; }},
        {"Eraser", NavigationTool::brush, [&] { return session.brushMode() == BrushToolMode::erase; }},
        {"Brush", NavigationTool::brush, [&] { return session.brushMode() == BrushToolMode::paint; }},
        {"Blur", NavigationTool::blur, [&] { return session.blurMode() == BlurToolMode::blur; }},
        {"Smudge", NavigationTool::blur, [&] { return session.blurMode() == BlurToolMode::smudge; }},
        {"Liquify", NavigationTool::blur, [&] { return session.blurMode() == BlurToolMode::liquify; }},
        {"Ellipse", NavigationTool::shape, [&] { return session.shapeKind() == ShapeKind::ellipse; }},
        {"Rectangle", NavigationTool::shape, [&] { return session.shapeKind() == ShapeKind::rectangle; }},
        {"Line", NavigationTool::shape, [&] { return session.shapeKind() == ShapeKind::line; }},
        {"Move / Transform", NavigationTool::move, [] { return true; }}, {"Crop", NavigationTool::crop, [] { return true; }},
        {"Spot Healing Brush", NavigationTool::spotHealing, [] { return true; }}, {"Clone Stamp", NavigationTool::cloneStamp, [] { return true; }},
        {"Gradient", NavigationTool::gradient, [] { return true; }}, {"Type", NavigationTool::type, [] { return true; }},
        {"Eyedropper", NavigationTool::eyedropper, [] { return true; }}, {"Hand", NavigationTool::hand, [] { return true; }}};
    for (const auto &[name, tool, holds] : modes) {
        titled(tools, "Tool › " + name).perform();
        QVERIFY2(session.tool() == tool && holds(), qPrintable(name));
    }
    titled(tools, "Tool › Zoom").perform();
    QVERIFY(session.tool() == NavigationTool::zoom);
    // A refused tool change leaves the mode alone.
    session.insert(white(), QPointF(2, 2));
    session.beginLevels();
    QVERIFY(session.levels());
    titled(tools, "Tool › Rectangle").perform();
    QVERIFY(session.tool() == NavigationTool::zoom && session.shapeKind() == ShapeKind::line);
}

QTEST_MAIN(CommandPaletteTests)
#include "CommandPaletteTests.moc"
