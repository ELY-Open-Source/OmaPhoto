#include "MenuFixtures.h"
#include <QDialog>
#include <QtTest>

// The Image menu's canvas entries, out of CompositorMenusTests.
class ImageMenuTests : public QObject {
    Q_OBJECT
private slots:
    void imageEntriesSizeAndFlipTheCanvas();
};

void ImageMenuTests::imageEntriesSizeAndFlipTheCanvas()
{
    Bar bar;
    bar.window.show();
    const char *entries[] = {"canvasSize", "imageSize", "trim", "rotateCanvasClockwise", "rotateCanvasCounterclockwise", "flipCanvasHorizontal", "flipCanvasVertical"};
    for (const char *name : entries)
        QVERIFY2(!bar.action(name).isEnabled(), name);
    bar.session().createDocument(100, 50);
    bar.session().insert(white(), QPointF(20.5, 10.5));
    for (const char *name : entries)
        QVERIFY2(bar.action(name).isEnabled(), name);
    // Busy holds them all; a drawn frame holds the flips.
    bar.session().setIsProjectBusy(true);
    for (const char *name : entries)
        QVERIFY2(!bar.action(name).isEnabled(), name);
    bar.session().setIsProjectBusy(false);
    bar.session().selectTool(NavigationTool::crop);
    QVERIFY(bar.action("canvasSize").isEnabled() && bar.action("trim").isEnabled() && !bar.action("flipCanvasHorizontal").isEnabled());
    QVERIFY(!bar.action("rotateCanvasClockwise").isEnabled() && !bar.action("rotateCanvasCounterclockwise").isEnabled());
    bar.session().selectTool(NavigationTool::move);
    bar.action("flipCanvasHorizontal").trigger();
    QCOMPARE(bar.session().activeLayer().value().origin(), QPointF(79, 10));
    QCOMPARE(bar.session().history.undoName(), QString("Flip Canvas Horizontal"));
    bar.action("flipCanvasVertical").trigger();
    QCOMPARE(bar.session().activeLayer().value().origin(), QPointF(79, 39));
    // A quarter turn swaps the sides and carries the layer.
    bar.action("rotateCanvasClockwise").trigger();
    QCOMPARE(bar.session().document().value().size(), QSizeF(50, 100));
    QCOMPARE(bar.session().history.undoName(), QString("Rotate Canvas 90° Clockwise"));
    bar.action("rotateCanvasCounterclockwise").trigger();
    QCOMPARE(bar.session().document().value().size(), QSizeF(100, 50));
    QCOMPARE(bar.session().activeLayer().value().origin(), QPointF(79, 39));
    // The size entries open their sheets over the window.
    for (const auto &[name, title] : {std::pair("canvasSize", "Canvas Size"), std::pair("imageSize", "Image Size"), std::pair("trim", "Trim")}) {
        bar.action(name).trigger();
        auto *sheet = bar.window.findChild<QDialog *>();
        QVERIFY(sheet && sheet->isVisible());
        QCOMPARE(sheet->windowTitle(), QString::fromLatin1(title));
        QVERIFY(!bar.action(name).isEnabled());
        QTest::keyClick(sheet, Qt::Key_Escape);
        QTRY_VERIFY(!bar.session().isProjectBusy());
        QTRY_VERIFY(!bar.window.findChild<QDialog *>());
    }
}

QTEST_MAIN(ImageMenuTests)
#include "ImageMenuTests.moc"
