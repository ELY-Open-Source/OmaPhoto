#include "ContentView.h"
#include "UI/LayerAppearanceControls.h"
#include "UI/LayersPanel.h"
#include "UI/NativeLayerList.h"
#include <QLabel>
#include <QSettings>
#include <QStackedWidget>
#include <QToolButton>
#include <QMenu>
#include <QStyle>
#include <QStyleOption>
#include <QtTest>
#include "SessionFixtures.h"

// The Layers panel: heading, rows or empty words, footer, width.
namespace {
template <typename Widget> Widget &find(QWidget &root, const char *name)
{
    Widget *found = root.findChild<Widget *>(QString::fromLatin1(name));
    if (!found)
        throw std::runtime_error(std::string("no widget named ") + name);
    return *found;
}

// The brightest ink an icon draws in a mode.
QColor brightest(const QIcon &icon, QIcon::Mode mode)
{
    const QImage drawn = icon.pixmap(16, mode).toImage().convertToFormat(QImage::Format_ARGB32);
    QColor best(0, 0, 0, 0);
    for (int y = 0; y < drawn.height(); ++y)
        for (int x = 0; x < drawn.width(); ++x)
            if (drawn.pixelColor(x, y).alpha() > best.alpha())
                best = drawn.pixelColor(x, y);
    return best;
}

void drag(QWidget &edge, int fromX, int toX)
{
    const QPoint start(fromX, 20), end(toX, 20);
    QTest::mousePress(&edge, Qt::LeftButton, Qt::NoModifier, start);
    QMouseEvent move(QEvent::MouseMove, QPointF(end), edge.mapToGlobal(end), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&edge, &move);
    QTest::mouseRelease(&edge, Qt::LeftButton, Qt::NoModifier, end);
}
}

class LayersPanelTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void init();
    void theHeadingCountsAndTheEmptyWordsFollowTheDocument();
    void theFooterAddsGroupsAndDeletesWithSwiftsWords();
    void theEdgeResizesThePanelWithinRangeAndRemembersIt();
    void aStoredWidthOutOfRangeIsTheDefault();
    void theMaskButtonUsesTheSelection();
    void footerButtonsTakeThePrimaryInkAndDimAsTheMenus();
};

void LayersPanelTests::initTestCase()
{
    // Settings land in ~/.qttest, not the user's own.
    QStandardPaths::setTestModeEnabled(true);
}

void LayersPanelTests::init()
{
    QSettings().remove(QStringLiteral("layersPanelWidth"));
}

void LayersPanelTests::theHeadingCountsAndTheEmptyWordsFollowTheDocument()
{
    EditorSession session;
    LayersPanel panel(session);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));
    auto &count = find<QLabel>(panel, "layerCount");
    auto &hint = find<QLabel>(panel, "noLayersHint");
    auto &body = find<QStackedWidget>(panel, "layersBody");
    QCOMPARE(count.text(), QString("0"));
    QCOMPARE(hint.text(), QString("Create a canvas or import an image."));
    QVERIFY(!panel.list().isVisible());
    session.createDocument(200, 100);
    QCOMPARE(hint.text(), QString("Import an image or add a blank layer."));
    QCOMPARE(body.currentIndex(), 1);
    session.addBlankLayer();
    session.addBlankLayer();
    QCOMPARE(count.text(), QString("2"));
    QCOMPARE(body.currentIndex(), 0);
    QVERIFY(panel.list().isVisible());
    QCOMPARE(panel.list().cells().size(), size_t(2));
    session.deleteSelectedLayers();
    session.deleteSelectedLayers();
    QCOMPARE(count.text(), QString("0"));
    QCOMPARE(body.currentIndex(), 1);
    // The appearance controls sit above the rows.
    QVERIFY(panel.findChild<LayerAppearanceControls *>() != nullptr);
    QVERIFY(panel.findChild<LayerAppearanceControls *>()->y() < body.y());
}

void LayersPanelTests::theFooterAddsGroupsAndDeletesWithSwiftsWords()
{
    EditorSession session;
    LayersPanel panel(session);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));
    auto &add = find<QToolButton>(panel, "addBlankLayer");
    auto &group = find<QToolButton>(panel, "groupLayers");
    auto &remove = find<QToolButton>(panel, "deleteLayer");
    auto &mask = find<QToolButton>(panel, "addLayerMask");
    QCOMPARE(add.toolTip(), QString("New blank layer (Ctrl+Shift+N)"));
    QVERIFY(!add.isEnabled() && !group.isEnabled() && !remove.isEnabled() && !mask.isEnabled());
    session.createDocument(200, 100);
    QVERIFY(add.isEnabled() && group.isEnabled() && !remove.isEnabled());
    QTest::mouseClick(&add, Qt::LeftButton);
    QTest::mouseClick(&add, Qt::LeftButton);
    QCOMPARE(session.document().value().layers.size(), size_t(2));
    QVERIFY(remove.isEnabled() && mask.isEnabled());
    QCOMPARE(remove.toolTip(), QString("Delete selected layer"));
    QCOMPARE(remove.accessibleName(), QString("Delete selected layer"));
    // With a mask selected, Delete takes the mask first.
    QTest::mouseClick(&mask, Qt::LeftButton);
    QVERIFY(session.activeLayer().value().mask.has_value() && session.isMaskSelected());
    QCOMPARE(remove.toolTip(), QString("Delete layer mask"));
    QTest::mouseClick(&remove, Qt::LeftButton);
    QVERIFY(!session.activeLayer().value().mask.has_value());
    QCOMPARE(session.document().value().layers.size(), size_t(2));
    // A multi-selection deletes as one step, and says so.
    const QUuid top = session.document().value().layers.back().id, bottom = session.document().value().layers.front().id;
    session.selectLayers({top, bottom}, top);
    QCOMPARE(remove.toolTip(), QString("Delete selected layers"));
    QTest::mouseClick(&remove, Qt::LeftButton);
    QVERIFY(session.document().value().layers.empty());
    QCOMPARE(session.history.undoName(), QString("Delete Layers"));
    // A folder from the footer, around the selection.
    session.addBlankLayer();
    QTest::mouseClick(&group, Qt::LeftButton);
    QCOMPARE(session.document().value().layers.size(), size_t(2));
    QVERIFY(session.activeLayer().value().isGroup);
    // Busy, the footer waits.
    session.setIsProjectBusy(true);
    QVERIFY(!add.isEnabled() && !group.isEnabled() && !remove.isEnabled() && !mask.isEnabled());
}

void LayersPanelTests::theEdgeResizesThePanelWithinRangeAndRemembersIt()
{
    EditorSession session;
    session.createDocument(200, 100);
    ContentView view(session);
    view.resize(1100, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    auto &edge = find<QWidget>(view, "layersPanelEdge");
    LayersPanel &panel = view.layersPanel();
    QCOMPARE(panel.width(), 252);
    QCOMPARE(edge.width(), 8);
    QCOMPARE(edge.cursor().shape(), Qt::SplitHCursor);
    QCOMPARE(edge.toolTip(), QString("Drag to resize the panel"));
    // The panel sits at the right, past the edge.
    QCOMPARE(panel.x(), edge.x() + edge.width());
    QCOMPARE(panel.x() + panel.width(), view.width());
    drag(edge, 4, -36);
    QCOMPARE(panel.width(), 292);
    QCOMPARE(QSettings().value("layersPanelWidth").toDouble(), 292.0);
    // Past the range, the panel stops at its bounds.
    drag(edge, 4, -300);
    QCOMPARE(panel.width(), int(LayersPanel::maximumWidth));
    drag(edge, 4, 400);
    QCOMPARE(panel.width(), int(LayersPanel::minimumWidth));
    QCOMPARE(QSettings().value("layersPanelWidth").toDouble(), LayersPanel::minimumWidth);
    // A right-button drag after the left one moves nothing.
    QTest::mousePress(&edge, Qt::RightButton, Qt::NoModifier, QPoint(4, 20));
    QMouseEvent move(QEvent::MouseMove, QPointF(-50, 20), edge.mapToGlobal(QPoint(-50, 20)), Qt::NoButton, Qt::RightButton, Qt::NoModifier);
    QApplication::sendEvent(&edge, &move);
    QTest::mouseRelease(&edge, Qt::RightButton, Qt::NoModifier, QPoint(-50, 20));
    QCOMPARE(panel.width(), int(LayersPanel::minimumWidth));
    // The next editor starts at the remembered width.
    ContentView next(session);
    QCOMPARE(next.layersPanel().width(), int(LayersPanel::minimumWidth));
}

void LayersPanelTests::aStoredWidthOutOfRangeIsTheDefault()
{
    QSettings().setValue(QStringLiteral("layersPanelWidth"), 9000);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("layersPanelWidth 9000 is out of range, using 252"));
    QCOMPARE(ContentView::layersPanelWidth(), 252.0);
    QSettings().setValue(QStringLiteral("layersPanelWidth"), QStringLiteral("wide"));
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("layersPanelWidth 0 is out of range, using 252"));
    QCOMPARE(ContentView::layersPanelWidth(), 252.0);
}

void LayersPanelTests::footerButtonsTakeThePrimaryInkAndDimAsTheMenus()
{
    EditorSession session;
    LayersPanel panel(session);
    // Opaque inks, each group its own: disabled reads the active.
    const QColor primary(230, 230, 230), secondary(150, 150, 150);
    QPalette inks = panel.palette();
    inks.setColor(QPalette::Active, QPalette::WindowText, primary);
    inks.setColor(QPalette::Inactive, QPalette::WindowText, primary);
    inks.setColor(QPalette::Active, QPalette::PlaceholderText, secondary);
    inks.setColor(QPalette::Inactive, QPalette::PlaceholderText, secondary);
    inks.setColor(QPalette::Disabled, QPalette::WindowText, QColor(60, 20, 20));
    inks.setColor(QPalette::Disabled, QPalette::PlaceholderText, QColor(20, 60, 20));
    panel.setPalette(inks);
    QVERIFY(!find<QToolButton>(panel, "addLayerMask").isEnabled());
    QTRY_COMPARE(brightest(find<QToolButton>(panel, "addBlankLayer").icon(), QIcon::Normal).rgb(), primary.rgb());
    // Swift 1.4.7: primary ink; disabled, dimmed as a menu's.
    const std::vector<std::pair<const char *, LayerIcon>> buttons{
        {"addBlankLayer", LayerIcon::newLayer}, {"groupLayers", LayerIcon::newFolder}, {"addLayerMask", LayerIcon::addMask}, {"deleteLayer", LayerIcon::trash}};
    for (const auto &[name, shape] : buttons) {
        QToolButton &button = find<QToolButton>(panel, name);
        QVERIFY2(brightest(button.icon(), QIcon::Normal).rgb() == primary.rgb(), name);
        QStyleOption option;
        option.initFrom(&button);
        const QPixmap menuInk = LayerIcons::pixmap(shape, 16, secondary, button.devicePixelRatio());
        const QImage dimmed = button.style()->generatedIconPixmap(QIcon::Disabled, menuInk, &option).toImage();
        QVERIFY2(button.icon().pixmap(menuInk.size(), QIcon::Disabled).toImage() == dimmed, name);
        QVERIFY2(button.icon().pixmap(menuInk.size(), QIcon::Disabled).toImage() != button.icon().pixmap(menuInk.size(), QIcon::Normal).toImage(), name);
    }
    for (const char *name : {"layerEffects", "addAdjustment"})
        QVERIFY2(brightest(find<QToolButton>(panel, name).icon(), QIcon::Normal).rgb() == secondary.rgb(), name);
    // Made enabled, they still dim as the menus when busy.
    EditorSession working;
    working.createDocument(20, 20);
    QImage pixels(4, 4, QImage::Format_RGBA8888_Premultiplied);
    pixels.fill(Qt::white);
    working.insert(ImportedImage(pixels, pixels, QStringLiteral("White")), QPointF(10, 10));
    LayersPanel live(working);
    live.setPalette(inks);
    QTRY_COMPARE(brightest(find<QToolButton>(live, "addBlankLayer").icon(), QIcon::Normal).rgb(), primary.rgb());
    QVERIFY(find<QToolButton>(live, "addBlankLayer").isEnabled() && find<QToolButton>(live, "addLayerMask").isEnabled());
    working.setIsProjectBusy(true);
    for (const auto &[name, shape] : buttons) {
        QToolButton &button = find<QToolButton>(live, name);
        QVERIFY2(!button.isEnabled(), name);
        QStyleOption option;
        option.initFrom(&button);
        const QPixmap menuInk = LayerIcons::pixmap(shape, 16, secondary, button.devicePixelRatio());
        QVERIFY2(button.icon().pixmap(menuInk.size(), QIcon::Disabled).toImage() == button.style()->generatedIconPixmap(QIcon::Disabled, menuInk, &option).toImage(), name);
    }
    // A theme's new ink reaches both.
    QPalette next = panel.palette();
    next.setColor(QPalette::WindowText, QColor(250, 200, 40));
    panel.setPalette(next);
    QTRY_COMPARE(brightest(find<QToolButton>(panel, "addBlankLayer").icon(), QIcon::Normal).rgb(), QColor(250, 200, 40).rgb());
    QTRY_COMPARE(brightest(find<QToolButton>(panel, "addLayerMask").icon(), QIcon::Normal).rgb(), QColor(250, 200, 40).rgb());
}

QTEST_MAIN(LayersPanelTests)
#include "LayersPanelTests.moc"

void LayersPanelTests::theMaskButtonUsesTheSelection()
{
    EditorSession session;
    session.createDocument(100, 40, true);
    QImage image(100, 40, QImage::Format_RGBA8888_Premultiplied);
    image.fill(Qt::red);
    session.insert(ImportedImage(image, image, "Red"));
    LayersPanel panel(session);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));
    auto &mask = find<QToolButton>(panel, "addLayerMask");
    QCOMPARE(mask.toolTip(), QString("Add layer mask (Alt-click for a black mask)"));
    // Alt-click: all black without a selection.
    QTest::mouseClick(&mask, Qt::LeftButton, Qt::AltModifier);
    QCOMPARE(session.history.undoName(), QString("Add Hide-All Mask"));
    session.undo();
    QTest::mouseClick(&mask, Qt::LeftButton);
    QCOMPARE(session.history.undoName(), QString("Add Reveal-All Mask"));
    session.undo();
    QPainterPath box;
    box.addRect(QRectF(20, 10, 30, 20));
    session.applySelection(box, SelectionMode::replace, "Select");
    QCOMPARE(mask.toolTip(), QString("Add layer mask revealing the selection (Alt-click to hide it)"));
    // The button reveals the selection, as Photoshop's.
    QTest::mouseClick(&mask, Qt::LeftButton);
    QCOMPARE(session.history.undoName(), QString("Reveal Selection"));
    QVERIFY(!session.selection().has_value() && session.isMaskSelected() && !mask.isEnabled());
    QCOMPARE(int(session.activeLayer().value().mask.value().asset.image().constScanLine(20)[30]), 255);
    QCOMPARE(int(session.activeLayer().value().mask.value().asset.image().constScanLine(5)[5]), 0);
    // Alt-click hides it instead.
    session.undo();
    QVERIFY(session.selection().has_value());
    QTest::mouseClick(&mask, Qt::LeftButton, Qt::AltModifier);
    QCOMPARE(session.history.undoName(), QString("Hide Selection"));
    QCOMPARE(int(session.activeLayer().value().mask.value().asset.image().constScanLine(20)[30]), 0);
    QCOMPARE(int(session.activeLayer().value().mask.value().asset.image().constScanLine(5)[5]), 255);
    // The row's Hide All hides the selection too.
    session.undo();
    LayerCell &cell = *panel.list().cells().at(0);
    panel.list().menuFor(cell, QPoint(cell.width() - 20, 20))->findChild<QAction *>("addBlackMask")->trigger();
    QCOMPARE(session.history.undoName(), QString("Hide Selection"));
    QCOMPARE(int(session.activeLayer().value().mask.value().asset.image().constScanLine(20)[30]), 0);
    QCOMPARE(int(session.activeLayer().value().mask.value().asset.image().constScanLine(5)[5]), 255);
}
