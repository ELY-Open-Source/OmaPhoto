#include "MenuFixtures.h"
#include "UI/CommandPaletteView.h"
#include <QFontDatabase>
#include <QLabel>
#include <QScrollBar>

// The palette in the app's window, on Ctrl+F.
namespace {
struct Shown : Bar {
    Shown()
    {
        window.resize(1000, 700);
        window.show();
        if (!QTest::qWaitForWindowActive(&window))
            throw std::runtime_error("window never active");
    }
    QWidget *panel() const { return window.findChild<QWidget *>("commandPalette"); }
    CommandPaletteView &view() const
    {
        CommandPaletteView *found = nullptr;
        for (CommandPaletteView *each : window.findChildren<CommandPaletteView *>())
            if (each->isVisible())
                found = each;
        if (!found)
            throw std::runtime_error("no palette shown");
        return *found;
    }
    void open()
    {
        QTest::keyClick(&window, Qt::Key_F, Qt::ControlModifier);
        QTRY_VERIFY(panel() && panel()->isVisible());
    }
};
}

class CommandPaletteWindowTests : public QObject {
    Q_OBJECT
private slots:
    void ctrlFOpensItOverTheWindowAndAgainCloses();
    void typingRanksAndReturnRunsTheChosenRow();
    void aDisabledRowStaysOpenAndEscapeCloses();
    void aClickRunsARowAndAClickOutsideCloses();
    void rowsDrawChecksKeysAndTheEmptyWords();
    void aRowDrawsItsKeyAtItsEnd();
};

void CommandPaletteWindowTests::ctrlFOpensItOverTheWindowAndAgainCloses()
{
    Shown shown;
    QAction &search = shown.action("searchCommands");
    QVERIFY(search.text() == "Search Commands…" && search.isEnabled());
    // First in View, a separator after it.
    const QList<QAction *> view = qobject_cast<QMenu *>(search.parent())->actions();
    QVERIFY(view[0] == &search && view[1]->isSeparator());
    const QPoint centred = shown.window.frameGeometry().center() - QPoint(230, 145);
    shown.open();
    QCOMPARE(shown.panel()->size(), QSize(460, 290));
    QCOMPARE(shown.panel()->pos(), centred);
    QTRY_VERIFY(shown.view().field().hasFocus());
    QCOMPARE(shown.view().field().placeholderText(), QString("Search commands and tools"));
    // Every menu entry, the mask commands and the tools.
    const std::vector<CommandPaletteEntry> &entries = shown.view().model().entries();
    QCOMPARE(entries.back().id, QString("Tool › Zoom"));
    QVERIFY(std::ranges::any_of(entries, [](const CommandPaletteEntry &e) { return e.id == "Layer › Add Layer Mask"; }));
    QVERIFY(std::ranges::none_of(entries, [](const CommandPaletteEntry &e) { return e.title.contains("Search Commands"); }));
    // Ctrl+F inside the palette puts it away.
    QTest::keyClick(&shown.view().field(), Qt::Key_F, Qt::ControlModifier);
    QTRY_VERIFY(!shown.panel()->isVisible());
    shown.open();
    QVERIFY(shown.view().field().text().isEmpty());
    // The last palette goes once the event loop runs.
    QTRY_COMPARE(shown.window.findChildren<CommandPaletteView *>().size(), qsizetype(1));
}

void CommandPaletteWindowTests::typingRanksAndReturnRunsTheChosenRow()
{
    Shown shown;
    shown.session().createDocument(400, 300);
    shown.session().zoom(1);
    shown.open();
    // Up from the top wraps to the last row, shown.
    auto *scroller = shown.view().findChild<QScrollArea *>();
    QTest::keyClick(&shown.view().field(), Qt::Key_Up);
    QVERIFY(shown.view().model().results().size() > 100);
    QCOMPARE(shown.view().model().selection, int(shown.view().model().results().size()) - 1);
    QCOMPARE(scroller->verticalScrollBar()->value(), scroller->verticalScrollBar()->maximum());
    QTest::keyClick(&shown.view().field(), Qt::Key_Up);
    QCOMPARE(shown.view().model().selection, int(shown.view().model().results().size()) - 2);
    QTest::keyClick(&shown.view().field(), Qt::Key_Down);
    QTest::keyClick(&shown.view().field(), Qt::Key_Down);
    QCOMPARE(shown.view().model().selection, 0);
    QCOMPARE(scroller->verticalScrollBar()->value(), 0);
    QTest::keyClicks(&shown.view().field(), "zoom in");
    QCOMPARE(shown.view().model().selected().value().id, QString("View › Zoom In"));
    QTest::keyClick(&shown.view().field(), Qt::Key_Return);
    QVERIFY(!shown.panel()->isVisible());
    // The hidden palette keeps no focus: Undo is no field’s.
    QVERIFY(!QApplication::focusWidget());
    // It runs once the event loop is next free.
    QCOMPARE(shown.session().viewport.zoom(), 1.0);
    QTRY_COMPARE(shown.session().viewport.zoom(), 1.25);
    // A mode by its name; Enter runs the chosen row.
    shown.open();
    QTest::keyClicks(&shown.view().field(), "lasso");
    QCOMPARE(shown.view().model().results()[1].id, QString("Tool › Polygonal Lasso"));
    QTest::keyClick(&shown.view().field(), Qt::Key_Down);
    QTest::keyClick(&shown.view().field(), Qt::Key_Enter);
    QTRY_VERIFY(shown.session().tool() == NavigationTool::lasso && shown.session().lassoKind() == LassoKind::polygonal);
}

void CommandPaletteWindowTests::aDisabledRowStaysOpenAndEscapeCloses()
{
    Shown shown;
    shown.open();
    QTest::keyClicks(&shown.view().field(), "zoom in");
    QVERIFY(!shown.view().model().selected().value().isEnabled);
    QTest::ignoreMessage(QtWarningMsg, "palette: disabled: \"View › Zoom In\"");
    QTest::keyClick(&shown.view().field(), Qt::Key_Return);
    QVERIFY(shown.panel()->isVisible());
    QTest::keyClick(&shown.view().field(), Qt::Key_Escape);
    QVERIFY(!shown.panel()->isVisible());
    QVERIFY(!shown.session().document());
}

void CommandPaletteWindowTests::aClickRunsARowAndAClickOutsideCloses()
{
    Shown shown;
    shown.session().createDocument(40, 30);
    shown.open();
    QTest::keyClicks(&shown.view().field(), "tool eraser");
    auto *rows = shown.view().findChild<CommandPaletteRows *>();
    QCOMPARE(shown.view().model().results()[0].id, QString("Tool › Eraser"));
    QTest::mouseClick(rows, Qt::RightButton, {}, rows->rowRect(0).center());
    QVERIFY(shown.panel()->isVisible());
    QTest::mouseClick(rows, Qt::LeftButton, {}, rows->rowRect(0).center());
    QTRY_VERIFY(shown.session().tool() == NavigationTool::brush && shown.session().brushMode() == BrushToolMode::erase);
    shown.open();
    QTest::mouseClick(shown.panel(), Qt::LeftButton, {}, QPoint(-20, -20));
    QTRY_VERIFY(!shown.panel()->isVisible());
}

void CommandPaletteWindowTests::rowsDrawChecksKeysAndTheEmptyWords()
{
    Shown shown;
    shown.open();
    CommandPaletteView &view = shown.view();
    auto *rows = view.findChild<CommandPaletteRows *>();
    QTest::keyClicks(&view.field(), "pixel grid");
    QCOMPARE(view.model().results()[0].id, QString("View › Pixel Grid (800% and above)"));
    QVERIFY(view.model().results()[0].isOn);
    const QImage drawn = rows->grab().toImage();
    const QRect row = rows->rowRect(0);
    // The chosen row is tinted; its checkmark draws first.
    const QColor tint = drawn.pixelColor(row.right() - 3, row.center().y());
    QVERIFY(tint != rows->palette().color(QPalette::Window));
    int inked = 0;
    for (int x = row.left() + 10; x < row.left() + 22; ++x)
        for (int y = row.top(); y < row.bottom(); ++y)
            inked += drawn.pixelColor(x, y) != drawn.pixelColor(row.right() - 3, y);
    QVERIFY(inked > 6);
    // Text starts past the check; off, the title is there.
    const auto gap = [&](const QImage &image) {
        int ink = 0;
        for (int x = row.left() + 23; x < row.left() + 27; ++x)
            for (int y = row.top() + 2; y < row.bottom() - 2; ++y)
                ink += image.pixelColor(x, y) != image.pixelColor(row.right() - 3, y);
        return ink;
    };
    QCOMPARE(gap(drawn), 0);
    QTest::keyClick(&view.field(), Qt::Key_Escape);
    shown.session().setShowsPixelGrid(false);
    shown.open();
    QTest::keyClicks(&shown.view().field(), "pixel grid");
    QVERIFY(!shown.view().model().results()[0].isOn);
    QVERIFY(gap(shown.view().findChild<CommandPaletteRows *>()->grab().toImage()) > 0);
    QVERIFY(!shown.view().findChild<QLabel *>("noCommands")->isVisible());
    QTest::keyClicks(&shown.view().field(), "qqqq");
    QVERIFY(shown.view().model().results().empty());
    QVERIFY(shown.view().findChild<QLabel *>("noCommands")->isVisible());
    QTest::keyClick(&shown.view().field(), Qt::Key_Return);
    QVERIFY(shown.panel()->isVisible());
}

void CommandPaletteWindowTests::aRowDrawsItsKeyAtItsEnd()
{
    Shown shown;
    shown.open();
    const auto keyInk = [&](const QString &query) {
        shown.view().field().setText(query);
        auto *rows = shown.view().findChild<CommandPaletteRows *>();
        const QImage drawn = rows->grab().toImage();
        const QRect row = rows->rowRect(0);
        int ink = 0;
        for (int x = row.right() - 40; x < row.right() - 10; ++x)
            for (int y = row.top() + 2; y < row.bottom() - 2; ++y)
                ink += drawn.pixelColor(x, y) != drawn.pixelColor(row.right() - 3, y);
        return ink;
    };
    // A long title ends, elided, six points before the key.
    QVERIFY(keyInk("fit canvas") > 20);
    QCOMPARE(shown.view().model().results()[0].shortcut.value(), QKeySequence(Qt::CTRL | Qt::Key_0).toString(QKeySequence::NativeText));
    QVERIFY(keyInk("tool lasso") > 5);
    QCOMPARE(shown.view().model().results()[0].shortcut.value(), QString("L"));
    QCOMPARE(keyInk("pixel grid"), 0);
    QVERIFY(!shown.view().model().results()[0].shortcut);
    // A long title ends, elided, six points before its key.
    const QString key = QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_P).toString(QKeySequence::NativeText);
    CommandPaletteView view({{.id = "Long", .title = QString("Layer › ").repeated(30), .shortcut = key, .isEnabled = true, .isOn = false, .perform = [] {}}},
                            [](const CommandPaletteEntry &) {}, nullptr);
    view.resize(460, 290);
    view.show();
    auto *rows = view.findChild<CommandPaletteRows *>();
    QFont fixed = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    fixed.setPixelSize(12);
    const QRect row = rows->rowRect(0);
    const int keyLeft = row.right() - 10 - QFontMetrics(fixed).horizontalAdvance(key);
    const QImage drawn = rows->grab().toImage();
    int between = 0;
    for (int x = keyLeft - 5; x < keyLeft - 1; ++x)
        for (int y = row.top() + 2; y < row.bottom() - 2; ++y)
            between += drawn.pixelColor(x, y) != drawn.pixelColor(row.right() - 3, y);
    QCOMPARE(between, 0);
    int title = 0;
    for (int x = keyLeft - 40; x < keyLeft - 8; ++x)
        for (int y = row.top() + 2; y < row.bottom() - 2; ++y)
            title += drawn.pixelColor(x, y) != drawn.pixelColor(row.right() - 3, y);
    QVERIFY(title > 10);
}

QTEST_MAIN(CommandPaletteWindowTests)
#include "CommandPaletteWindowTests.moc"
