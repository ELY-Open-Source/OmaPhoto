#include "MenuFixtures.h"
#include "UI/CommandPaletteView.h"
#include "UI/CanvasRulers.h"
#include "UI/FloatingPanel.h"
#include "UI/LayersPanel.h"
#include <QPushButton>
#include <QScrollArea>

// Canvas Only in the window, in and out.
namespace {
struct Shown : Bar {
    Shown()
    {
        window.show();
        if (!QTest::qWaitForWindowActive(&window))
            throw std::runtime_error("window never active");
        session().createDocument(200, 100);
        session().setShowsRulers(true);
    }
    CanvasView &canvas() { return *window.findChild<CanvasView *>(); }
    QWidget &named(const char *name) { return *window.findChild<QWidget *>(name); }
    // What Canvas Only hides, each shown or not.
    QList<bool> chrome()
    {
        return {window.menuBar()->isVisible(), named("toolbar").isVisible(), named("toolRail").isVisible(), named("statusBar").isVisible(),
                named("toolHeader").isVisible(), window.findChild<LayersPanel *>()->isVisible(), named("layersPanelEdge").isVisible(),
                window.findChild<CanvasRulerView *>()->isVisible()};
    }
    void press(Qt::Key key, Qt::KeyboardModifiers modifiers = {})
    {
        QTest::keyClick(QApplication::focusWidget() ? QApplication::focusWidget() : &window, key, modifiers);
    }
};
}

class CanvasOnlyWindowTests : public QObject {
    Q_OBJECT
private slots:
    void fPutsEveryBarAwayAndBringsThemBack();
    void theCanvasSitsOnBlack();
    void escapeLeavesOnceNothingIsLeftToCancel();
    void aFieldOrAPanelKeepsItsKeys();
    void aFullscreenWindowKeepsTheScreen();
    void theEntryRestsWhileTextOrADialogHoldsF();
};

void CanvasOnlyWindowTests::fPutsEveryBarAwayAndBringsThemBack()
{
    Shown shown;
    QAction &entry = shown.action("toggleFullscreen");
    QVERIFY(entry.text() == "Toggle Fullscreen" && entry.isCheckable() && !entry.isChecked() && entry.shortcut() == QKeySequence(Qt::Key_F));
    QCOMPARE(shown.chrome(), (QList<bool>{true, true, true, true, true, true, true, true}));
    shown.canvas().setFocus();
    shown.press(Qt::Key_F);
    QVERIFY(shown.session().canvasOnly() && entry.isChecked());
    QVERIFY(shown.window.windowState().testFlag(Qt::WindowFullScreen));
    QCOMPARE(shown.chrome(), (QList<bool>{false, false, false, false, false, false, false, false}));
    // Nothing else is left: the canvas fills the window.
    QTRY_COMPARE(QRect(shown.canvas().mapTo(&shown.window, QPoint(0, 0)), shown.canvas().size()), shown.window.rect());
    // A new tool's bar stays away too.
    shown.session().selectTool(NavigationTool::brush);
    QVERIFY(!shown.named("toolHeader").isVisible());
    // Keys reach the hidden menus: the palette, then F back.
    shown.press(Qt::Key_F, Qt::ControlModifier);
    QTRY_VERIFY(shown.named("commandPalette").isVisible());
    shown.press(Qt::Key_Escape);
    QTRY_VERIFY(!shown.named("commandPalette").isVisible());
    QVERIFY(shown.session().canvasOnly());
    shown.canvas().setFocus();
    shown.press(Qt::Key_F);
    QVERIFY(!shown.session().canvasOnly() && !entry.isChecked());
    QVERIFY(!shown.window.windowState().testFlag(Qt::WindowFullScreen));
    QCOMPARE(shown.chrome(), (QList<bool>{true, true, true, true, true, true, true, true}));
}

void CanvasOnlyWindowTests::theCanvasSitsOnBlack()
{
    Shown shown;
    const QColor base = shown.canvas().palette().color(QPalette::Base);
    QVERIFY(base != QColor(Qt::black));
    QCOMPARE(shown.canvas().grab().toImage().pixelColor(2, 2), base);
    shown.window.toggleCanvasOnly();
    QTRY_COMPARE(shown.canvas().grab().toImage().pixelColor(2, 2), QColor(Qt::black));
    shown.window.toggleCanvasOnly();
    QTRY_COMPARE(shown.canvas().grab().toImage().pixelColor(2, 2), base);
    // The flag alone repaints a canvas whose size holds.
    EditorSession session;
    session.createDocument(20, 20);
    CanvasView alone(session);
    alone.resize(100, 100);
    alone.synchronizeDisplay();
    QVERIFY(!alone.synchronizeDisplay());
    session.setCanvasOnly(true);
    QVERIFY(alone.synchronizeDisplay());
    QVERIFY(!alone.synchronizeDisplay());
}

void CanvasOnlyWindowTests::escapeLeavesOnceNothingIsLeftToCancel()
{
    Shown shown;
    QImage pixels(10, 10, QImage::Format_RGBA8888_Premultiplied);
    pixels.fill(Qt::red);
    shown.session().insert(ImportedImage(pixels, QImage(), "Red"), QPointF(10, 10));
    shown.window.toggleCanvasOnly();
    QVERIFY(shown.action("toggleFullscreen").isChecked());
    shown.canvas().setFocus();
    // The first Escape cancels the transform; the second leaves.
    shown.session().selectTool(NavigationTool::move);
    shown.session().beginTransform();
    shown.press(Qt::Key_Escape);
    QVERIFY(!shown.session().transformEdit() && shown.session().canvasOnly());
    // Escape with a key held is someone else's.
    for (const Qt::KeyboardModifier held : {Qt::ShiftModifier, Qt::ControlModifier, Qt::AltModifier, Qt::MetaModifier}) {
        shown.press(Qt::Key_Escape, held);
        QVERIFY(shown.session().canvasOnly());
    }
    shown.press(Qt::Key_Escape);
    QVERIFY(!shown.session().canvasOnly() && !shown.action("toggleFullscreen").isChecked());
    QCOMPARE(shown.chrome(), (QList<bool>{true, true, true, true, true, true, true, true}));
    // Out of Canvas Only, Escape is the canvas's alone.
    shown.press(Qt::Key_Escape);
    QVERIFY(!shown.session().canvasOnly());
}

void CanvasOnlyWindowTests::aFieldOrAPanelKeepsItsKeys()
{
    Shown shown;
    QImage pixels(10, 10, QImage::Format_RGBA8888_Premultiplied);
    pixels.fill(Qt::red);
    shown.session().insert(ImportedImage(pixels, QImage(), "Red"), QPointF(10, 10));
    shown.session().selectTool(NavigationTool::move);
    auto *field = shown.window.findChild<QLineEdit *>("transformX");
    QVERIFY(field);
    field->setFocus();
    QTRY_VERIFY(field->hasFocus());
    // A field types its F.
    field->selectAll();
    shown.press(Qt::Key_F);
    QVERIFY(!shown.session().canvasOnly() && field->text().endsWith('f'));
    shown.window.toggleCanvasOnly();
    // The field is hidden; a docked panel keeps Escape.
    auto *panel = new FloatingPanel(QStringLiteral("docked"), shown.window);
    QWidget dock(&shown.window);
    dock.setLayout(new QVBoxLayout);
    dock.show();
    bool closed = false;
    panel->onClose = [&] { closed = true; };
    auto *inside = new QPushButton("Inside");
    panel->show(QStringLiteral("Docked"), inside, &dock);
    inside->setFocus();
    QTRY_VERIFY(inside->hasFocus());
    QTest::keyClick(inside, Qt::Key_Escape);
    QVERIFY(closed && shown.session().canvasOnly());
    // Another window's Escape is its own.
    QDialog other;
    other.show();
    QVERIFY(QTest::qWaitForWindowActive(&other));
    QTest::keyClick(&other, Qt::Key_Escape);
    QVERIFY(shown.session().canvasOnly());
}

void CanvasOnlyWindowTests::aFullscreenWindowKeepsTheScreen()
{
    Shown shown;
    // A round trip first: nothing of it is kept.
    shown.window.toggleCanvasOnly();
    shown.window.toggleCanvasOnly();
    shown.window.showFullScreen();
    QTRY_VERIFY(shown.window.windowState().testFlag(Qt::WindowFullScreen));
    shown.window.toggleCanvasOnly();
    // Already fullscreen: only the toolbar goes, as on macOS.
    QVERIFY(shown.window.menuBar()->isVisible() && !shown.named("toolbar").isVisible());
    shown.window.toggleCanvasOnly();
    QVERIFY(shown.window.windowState().testFlag(Qt::WindowFullScreen));
    QVERIFY(shown.window.menuBar()->isVisible() && shown.named("toolbar").isVisible());
    // A maximised window comes back maximised.
    shown.window.showMaximized();
    QTRY_VERIFY(shown.window.windowState() == Qt::WindowMaximized);
    shown.window.toggleCanvasOnly();
    QVERIFY(shown.window.windowState().testFlag(Qt::WindowFullScreen));
    shown.window.toggleCanvasOnly();
    QCOMPARE(shown.window.windowState(), Qt::WindowStates(Qt::WindowMaximized));
}

void CanvasOnlyWindowTests::theEntryRestsWhileTextOrADialogHoldsF()
{
    Shown shown;
    QAction &entry = shown.action("toggleFullscreen");
    QVERIFY(entry.isEnabled());
    shown.session().selectTool(NavigationTool::type);
    shown.session().beginText(QPointF(5, 5), true);
    QVERIFY(!entry.isEnabled());
    // Open text types its F.
    shown.canvas().setFocus();
    shown.press(Qt::Key_F);
    QVERIFY(!shown.session().canvasOnly() && shown.session().textDraft().value().style.content == "f");
    shown.session().cancelText();
    QVERIFY(entry.isEnabled());
    shown.session().openColorPicker(false);
    QVERIFY(!entry.isEnabled());
    shown.session().closeColorPicker(false);
    shown.session().clearProject();
    QVERIFY(!entry.isEnabled());
}

QTEST_MAIN(CanvasOnlyWindowTests)
#include "CanvasOnlyWindowTests.moc"
