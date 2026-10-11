#include "Document/EditorSession.h"
#include "UI/NativeLayerList.h"
#include <QScrollBar>
#include <QtTest>

// Swift 1.4.8: a layer chosen on the canvas shows.
namespace {
std::unique_ptr<EditorSession> twentyLayers()
{
    auto session = std::make_unique<EditorSession>();
    session->createDocument(10, 10);
    for (int each = 0; each < 20; ++each)
        session->addBlankLayer();
    return session;
}

QUuid rowID(const NativeLayerList &list, int row)
{
    return list.cells().at(size_t(row))->layerID();
}

// Row `row` lies wholly inside the list's view.
bool shown(const NativeLayerList &list, int row)
{
    const QWidget &cell = *list.cells().at(size_t(row));
    return list.viewport()->rect().contains(QRect(cell.mapTo(list.viewport(), QPoint(0, 0)), cell.size()));
}
}

class LayerRevealTests : public QObject {
    Q_OBJECT
private slots:
    void selectingAnOffscreenLayerRevealsItsRow();
    void refreshingTheSameSelectionDoesNotScrollAgain();
    void changingPrimaryLayerWithinTheSameSelectionRevealsIt();
    void selectingAChildExpandsOnlyItsAncestorFolders();
    void multipleSelectionRevealsThePrimaryChild();
};

void LayerRevealTests::selectingAnOffscreenLayerRevealsItsRow()
{
    const auto session = twentyLayers();
    NativeLayerList list(*session);
    list.resize(260, 160);
    list.show();
    QVERIFY(QTest::qWaitForWindowExposed(&list));
    list.verticalScrollBar()->setValue(0);
    QVERIFY(!shown(list, 19));
    session->selectLayer(rowID(list, 19));
    QCOMPARE(session->selectedLayerIDs(), QSet<QUuid>{rowID(list, 19)});
    QVERIFY(shown(list, 19) && list.verticalScrollBar()->value() > 0);
}

void LayerRevealTests::refreshingTheSameSelectionDoesNotScrollAgain()
{
    const auto session = twentyLayers();
    NativeLayerList list(*session);
    list.resize(260, 160);
    list.show();
    QVERIFY(QTest::qWaitForWindowExposed(&list));
    session->selectLayer(rowID(list, 19));
    QVERIFY(shown(list, 19));
    // Scrolled away by hand, a rename leaves it there.
    list.verticalScrollBar()->setValue(0);
    session->renameLayer(rowID(list, 0), QStringLiteral("Renamed"));
    QCOMPARE(list.verticalScrollBar()->value(), 0);
    session->selectLayer(std::nullopt);
    QCOMPARE(list.verticalScrollBar()->value(), 0);
    session->selectLayer(rowID(list, 19));
    QVERIFY(shown(list, 19));
}

void LayerRevealTests::changingPrimaryLayerWithinTheSameSelectionRevealsIt()
{
    const auto session = twentyLayers();
    NativeLayerList list(*session);
    list.resize(260, 160);
    list.show();
    QVERIFY(QTest::qWaitForWindowExposed(&list));
    const QSet<QUuid> both{rowID(list, 0), rowID(list, 19)};
    session->selectLayers(both, rowID(list, 19));
    QVERIFY(shown(list, 19));
    session->selectLayers(both, rowID(list, 0));
    QCOMPARE(session->selectedLayerIDs(), both);
    QVERIFY(shown(list, 0) && list.verticalScrollBar()->value() == 0);
}

void LayerRevealTests::selectingAChildExpandsOnlyItsAncestorFolders()
{
    EditorSession session;
    session.createDocument(10, 10);
    session.addGroup();
    const QUuid outer = session.activeLayerID().value();
    session.addGroup();
    const QUuid inner = session.activeLayerID().value();
    session.addBlankLayer();
    const QUuid child = session.activeLayerID().value();
    session.selectLayer(std::nullopt);
    session.addGroup();
    const QUuid unrelated = session.activeLayerID().value();
    for (const QUuid folder : {outer, inner, unrelated})
        session.toggleGroupExpansion(folder);
    QCOMPARE(session.collapsedGroupIDs(), (QSet<QUuid>{outer, inner, unrelated}));
    // The last signal sees the folders open.
    std::vector<QSet<QUuid>> heard;
    connect(&session, &EditorSession::changed, &session, [&] { heard.push_back(session.collapsedGroupIDs()); });
    session.selectLayer(child);
    QCOMPARE(session.collapsedGroupIDs(), QSet<QUuid>{unrelated});
    QCOMPARE(heard.back(), QSet<QUuid>{unrelated});
    QVERIFY(std::ranges::any_of(session.layerRows(), [&](const LayerHierarchy::Entry &entry) { return entry.layer.id == child; }));
    session.selectLayer(unrelated);
    QCOMPARE(session.collapsedGroupIDs(), QSet<QUuid>{unrelated});
    // Only the inner folder shut: the signal sees it open.
    session.toggleGroupExpansion(inner);
    heard.clear();
    session.selectLayer(child);
    QCOMPARE(session.collapsedGroupIDs(), QSet<QUuid>{unrelated});
    QCOMPARE(heard.back(), QSet<QUuid>{unrelated});
}

void LayerRevealTests::multipleSelectionRevealsThePrimaryChild()
{
    EditorSession session;
    session.createDocument(10, 10);
    session.addGroup();
    const QUuid folder = session.activeLayerID().value();
    session.addBlankLayer();
    const QUuid child = session.activeLayerID().value();
    session.selectLayer(std::nullopt);
    session.addBlankLayer();
    const QUuid other = session.activeLayerID().value();
    session.toggleGroupExpansion(folder);
    QCOMPARE(session.collapsedGroupIDs(), QSet<QUuid>{folder});
    session.selectLayers({child, other}, child);
    QVERIFY(session.collapsedGroupIDs().isEmpty());
    QCOMPARE(session.selectedLayerIDs(), (QSet<QUuid>{child, other}));
    // The other as primary opens nothing: it is no folder's.
    session.toggleGroupExpansion(folder);
    session.selectLayers({child, other}, other);
    QCOMPARE(session.collapsedGroupIDs(), QSet<QUuid>{folder});
}

QTEST_MAIN(LayerRevealTests)
#include "LayerRevealTests.moc"
