#include "AcceptanceFixtures.h"
#include "DialogDesk.h"
#include "Document/Guides.h"
#include "Document/ShapeTool.h"
#include "IO/ProjectStore.h"
#include "UI/ColorPickerSheet.h"
#include "UI/ColorRangeSheet.h"
#include "UI/GridSettingsSheet.h"
#include "UI/ProjectTabs.h"
#include <QMenu>

// The acceptance run, part four: 1.4.5's selections, grid, canvases, tabs.
class AcceptanceNewTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }
    void colorRangeGridAndSnapping();
    void presetsQuickLookUngroupAndTabs();
};

namespace {
// A drag with keys held at every step.
void drag(App &app, QPointF from, QPointF to, Qt::KeyboardModifiers keys)
{
    app.press(from, keys);
    for (int step = 1; step <= 8; ++step) {
        const QPointF view = app.at(from + (to - from) * (step / 8.0));
        QMouseEvent event(QEvent::MouseMove, view, view, app.canvas().mapToGlobal(view.toPoint()), Qt::NoButton, Qt::LeftButton, keys);
        QApplication::sendEvent(&app.canvas(), &event);
    }
    QTest::mouseRelease(&app.canvas(), Qt::LeftButton, keys, app.at(to));
}

void typeInSheet(App &app, const char *name, const QString &text)
{
    auto *field = app.window.findChild<GridSettingsSheet *>()->findChild<PickerField *>(QString::fromLatin1(name));
    QVERIFY(field);
    field->setFocus();
    field->selectAll();
    QTest::keyClicks(field, text);
    QTest::keyClick(field, Qt::Key_Return);
}

ProjectTabButton &tabButton(App &app, const QString &title)
{
    for (ProjectTabButton *button : app.window.tabs()->buttons())
        if (button->tab->title() == title)
            return *button;
    throw std::runtime_error("no such tab");
}
}

void AcceptanceNewTests::colorRangeGridAndSnapping()
{
    App app;
    QVERIFY(scene(1200, 800).save(app.path("photo.png")));
    app.session().createDocument(1200, 800);
    app.importFile(app.path("photo.png"));
    // Color Range samples the red disc on the canvas.
    app.action("colorRange").trigger();
    QVERIFY(app.session().colorRange());
    app.press(QPointF(600, 400));
    app.release(QPointF(600, 400));
    QTRY_VERIFY(app.session().selection().has_value());
    // The panel's OK applies it.
    QPushButton *ok = nullptr;
    for (QPushButton *button : app.window.findChild<ColorRangeSheet *>()->findChildren<QPushButton *>())
        if (button->text() == QStringLiteral("OK"))
            ok = button;
    QVERIFY(ok);
    ok->click();
    QVERIFY(!app.session().colorRange());
    const QRectF disc = app.session().selection().value().path.boundingRect();
    QVERIFY2(qAbs(disc.center().x() - 600) < 4 && qAbs(disc.width() - 400) < 12 && qAbs(disc.height() - 400) < 12,
             qPrintable(QString("%1 %2").arg(disc.width()).arg(disc.height())));
    app.action("deselect").trigger();
    // Grid Settings: typed spacing applies; Restore Defaults undoes it.
    ProjectController &controller = app.workspace.current().controller;
    bool done = false;
    controller.gridSettings([&done] { done = true; });
    QTRY_VERIFY(app.window.findChild<GridSettingsSheet *>());
    typeInSheet(app, "gridSpacing", QStringLiteral("100"));
    QTRY_VERIFY(done);
    QVERIFY(app.session().layoutGrid() == LayoutGrid(100, 8));
    done = false;
    controller.gridSettings([&done] { done = true; });
    QTRY_VERIFY(app.window.findChild<GridSettingsSheet *>());
    app.window.findChild<GridSettingsSheet *>()->findChild<QPushButton *>(QStringLiteral("gridDefaults"))->click();
    app.window.findChild<GridSettingsSheet *>()->findChild<QPushButton *>(QStringLiteral("gridOK"))->click();
    QTRY_VERIFY(done);
    QVERIFY(app.session().layoutGrid() == LayoutGrid());
    // Marquee and rectangle land on a guide, unless Ctrl.
    app.session().setShowsGuides(true);
    app.session().addGuide({.id = QUuid::createUuid(), .axis = CanvasGuide::Axis::vertical, .position = 300});
    app.session().selectTool(NavigationTool::marquee);
    drag(app, QPointF(296, 100), QPointF(500, 300), Qt::NoModifier);
    QCOMPARE(app.session().selection().value().path.boundingRect().left(), 300.0);
    app.action("deselect").trigger();
    drag(app, QPointF(296, 100), QPointF(500, 300), Qt::ControlModifier);
    QVERIFY(qAbs(app.session().selection().value().path.boundingRect().left() - 296) <= 2);
    app.action("deselect").trigger();
    app.session().selectTool(NavigationTool::shape);
    app.session().setShapeKind(ShapeKind::rectangle);
    drag(app, QPointF(296, 500), QPointF(450, 650), Qt::NoModifier);
    QCOMPARE(app.session().activeLayer().value().transform.origin.x(), 300.0);
    drag(app, QPointF(296, 100), QPointF(450, 250), Qt::ControlModifier);
    QVERIFY(qAbs(app.session().activeLayer().value().transform.origin.x() - 296) <= 2);
}

void AcceptanceNewTests::presetsQuickLookUngroupAndTabs()
{
    App app;
    // A preset fills the fields and checks the match.
    auto *more = app.window.findChild<QToolButton *>(QStringLiteral("presetSizes"));
    QVERIFY(more && more->menu());
    QAction *story = nullptr;
    for (QAction *entry : more->menu()->actions())
        if (entry->text() == QStringLiteral("Instagram Story"))
            story = entry;
    QVERIFY(story);
    story->trigger();
    QVERIFY(story->isChecked());
    QCOMPARE(app.window.findChild<QLineEdit *>(QStringLiteral("heightInput"))->text(), QStringLiteral("1920"));
    app.window.findChild<QPushButton *>(QStringLiteral("createCanvas"))->click();
    QCOMPARE(QSize(int(app.session().document().value().width), int(app.session().document().value().height)), QSize(1080, 1920));
    // Ungroup: the layers take the folder's place; undo returns.
    app.action("newBlankLayer").trigger();
    const QUuid first = app.session().activeLayerID().value();
    app.action("newBlankLayer").trigger();
    const QUuid second = app.session().activeLayerID().value();
    app.session().selectLayers({first, second}, second);
    app.session().groupSelectedLayers();
    const QUuid folder = app.session().activeLayerID().value();
    const auto order = [&app] {
        QList<QUuid> ids;
        for (const ImageLayer &layer : app.session().document().value().layers)
            ids << layer.id;
        return ids;
    };
    const QList<QUuid> grouped = order();
    app.canvas().setFocus();
    QTest::keyClick(&app.canvas(), Qt::Key_G, Qt::ControlModifier | Qt::ShiftModifier);
    // The two layers stand where the folder stood, in order.
    QList<QUuid> expected = grouped;
    expected.removeAll(folder);
    QCOMPARE(order(), expected);
    QCOMPARE(int(order().indexOf(first)) + 1, int(order().indexOf(second)));
    for (const ImageLayer &layer : app.session().document().value().layers)
        QVERIFY(!layer.isGroup && !layer.parentID);
    QTest::keyClick(&app.canvas(), Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(order(), grouped);
    // A save writes the Quick Look preview, 1024 long.
    const QString project = app.path("Story.comp");
    DialogDesk desk;
    desk.replies = {project};
    app.action("saveAs").trigger();
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo(project).isDir() && !app.session().isModified(), 30000);
    const QImage preview(QDir(project).filePath(QStringLiteral("QuickLook/Preview.jpg")));
    QCOMPARE(preview.size(), QSize(576, 1024));
    QCOMPARE(int(ProjectStore::load(project).manifest.layers.size()), 4);
    // A tab dragged past its neighbour; the rest gather.
    app.workspace.newCanvas();
    app.workspace.newCanvas();
    QStringList before;
    for (const std::shared_ptr<ProjectTab> &tab : app.workspace.tabs())
        before << tab->title();
    const int from = int(before.indexOf(QStringLiteral("Story")));
    QVERIFY(from >= 0 && from + 1 < before.size());
    QWindow *window = app.window.windowHandle();
    ProjectTabButton &dragged = tabButton(app, QStringLiteral("Story"));
    const QPoint start = dragged.mapTo(&app.window, QPoint(20, 14));
    QTest::mousePress(window, Qt::LeftButton, {}, start);
    QTest::mouseMove(window, start + QPoint(10, 0));
    QTest::mouseMove(window, start + QPoint(dragged.width() + 40, 0));
    QTest::mouseRelease(window, Qt::LeftButton, {}, start + QPoint(dragged.width() + 40, 0));
    QCOMPARE(app.workspace.tabs()[from]->title(), before[from + 1]);
    QCOMPARE(app.workspace.tabs()[from + 1]->title(), QStringLiteral("Story"));
    for (int tab = 0; tab < 12; ++tab)
        app.workspace.newCanvas();
    OverflowTabsPill &pill = *app.window.tabs()->pill();
    QTRY_VERIFY(pill.isVisible());
    QTest::mousePress(&pill, Qt::LeftButton);
    auto *menu = pill.findChild<QMenu *>(QStringLiteral("projectTabsOverflowMenu"));
    QVERIFY(menu && menu->isVisible());
    app.shot(QStringLiteral("17-tabs"));
    QAction *hidden = menu->actions().front();
    const QString chosen = hidden->text();
    hidden->trigger();
    menu->close();
    QTest::mouseRelease(&pill, Qt::LeftButton);
    QCOMPARE(app.workspace.current().title(), chosen);
}

QTEST_MAIN(AcceptanceNewTests)
#include "AcceptanceNewTests.moc"
