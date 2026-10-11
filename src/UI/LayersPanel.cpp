#include "UI/LayersPanel.h"
#include "UI/LayerAppearanceControls.h"
#include "UI/LayerIcons.h"
#include <QStyle>
#include <QStyleOption>
#include "UI/LayerMaskMenu.h"
#include "UI/NativeLayerList.h"
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QMenu>
#include <QVBoxLayout>

namespace {
QFrame *divider(QWidget *parent)
{
    auto *line = new QFrame(parent);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Plain);
    line->setForegroundRole(QPalette::Mid);
    return line;
}

QLabel *text(const QString &words, int pixels, QFont::Weight weight, QWidget *parent)
{
    auto *label = new QLabel(words, parent);
    QFont font = label->font();
    font.setPixelSize(pixels);
    font.setWeight(weight);
    label->setFont(font);
    return label;
}
}

LayersPanel::LayersPanel(EditorSession &session, QWidget *parent)
    : QWidget(parent), m_session(session), m_count(text(QStringLiteral("0"), 11, QFont::Normal, this)), m_body(new QStackedWidget(this)),
      m_list(new NativeLayerList(session, this)), m_emptyHint(text(QString(), 11, QFont::Normal, this))
{
    setObjectName(QStringLiteral("layersPanel"));
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    auto *heading = new QHBoxLayout;
    heading->setContentsMargins(18, 18, 18, 18);
    heading->addWidget(text(QStringLiteral("Layers"), 12, QFont::DemiBold, this));
    heading->addStretch(1);
    m_count->setObjectName(QStringLiteral("layerCount"));
    m_count->setForegroundRole(QPalette::PlaceholderText);
    heading->addWidget(m_count);
    column->addLayout(heading);
    column->addWidget(divider(this));
    column->addWidget(new LayerAppearanceControls(session, this));
    column->addWidget(divider(this));
    // The rows, or the words for none: create, import, add.
    auto *empty = new QWidget(this);
    auto *emptyColumn = new QVBoxLayout(empty);
    emptyColumn->setContentsMargins(16, 16, 16, 16);
    emptyColumn->setSpacing(10);
    emptyColumn->addStretch(1);
    m_emptyIcon = new QLabel(empty);
    m_emptyIcon->setAlignment(Qt::AlignCenter);
    emptyColumn->addWidget(m_emptyIcon);
    auto *title = text(QStringLiteral("No layers yet"), 13, QFont::Medium, empty);
    title->setAlignment(Qt::AlignCenter);
    title->setForegroundRole(QPalette::PlaceholderText);
    emptyColumn->addWidget(title);
    m_emptyHint->setObjectName(QStringLiteral("noLayersHint"));
    m_emptyHint->setAlignment(Qt::AlignCenter);
    m_emptyHint->setWordWrap(true);
    m_emptyHint->setForegroundRole(QPalette::PlaceholderText);
    emptyColumn->addWidget(m_emptyHint);
    emptyColumn->addStretch(1);
    m_body->setObjectName(QStringLiteral("layersBody"));
    m_body->addWidget(m_list);
    m_body->addWidget(empty);
    column->addWidget(m_body, 1);
    column->addWidget(divider(this));
    // Swift spaces the icons sixteen points apart.
    auto *footer = new QHBoxLayout;
    footer->setContentsMargins(8, 4, 8, 4);
    footer->setSpacing(0);
    m_addBlankLayer = footerButton(QStringLiteral("addBlankLayer"), QStringLiteral("New blank layer"), QStringLiteral("New blank layer (Ctrl+Shift+N)"), [this] { m_session.addBlankLayer(); });
    footer->addWidget(m_addBlankLayer);
    m_group = footerButton(QStringLiteral("groupLayers"), QStringLiteral("New folder"), QStringLiteral("Group selected layers (Ctrl+G)"), [this] { m_session.groupSelectedLayers(); });
    footer->addWidget(m_group);
    footer->addWidget(new LayerMaskMenu(session, this));
    // Presses open these menus; a click adds nothing.
    m_effects = footerButton(QStringLiteral("layerEffects"), QStringLiteral("Layer effects"), QStringLiteral("Add layer effect"), [] {});
    auto *effects = new QMenu(m_effects);
    for (const LayerEffectKind kind : allLayerEffectKinds)
        effects->addAction(rawValue(kind) + QStringLiteral("…"), this, [this, kind] { m_session.addEffect(kind); });
    m_effects->setMenu(effects);
    m_effects->setPopupMode(QToolButton::InstantPopup);
    footer->addWidget(m_effects);
    m_adjustments = footerButton(QStringLiteral("addAdjustment"), QStringLiteral("New adjustment layer"), QStringLiteral("New adjustment layer"), [] {});
    auto *kinds = new QMenu(m_adjustments);
    for (const AdjustmentKind kind : allAdjustmentKinds)
        kinds->addAction(rawValue(kind).replace(QLatin1Char('&'), QStringLiteral("&&")), this, [this, kind] { m_session.addAdjustment(kind); });
    m_adjustments->setMenu(kinds);
    m_adjustments->setPopupMode(QToolButton::InstantPopup);
    footer->addWidget(m_adjustments);
    footer->addStretch(1);
    m_delete = footerButton(QStringLiteral("deleteLayer"), QStringLiteral("Delete selected layer"), QStringLiteral("Delete selected layer"),
                            [this] { m_session.deleteLayerOrMask(); });
    footer->addWidget(m_delete);
    column->addLayout(footer);
    applyIcons();
    connect(&m_session, &EditorSession::changed, this, &LayersPanel::synchronize);
    synchronize();
}

QIcon footerIcon(LayerIcon icon, const QWidget &button)
{
    // Disabled, the menus' own dimmed ink: Qt dims their icons.
    QStyleOption option;
    option.initFrom(&button);
    const QPixmap menuInk = LayerIcons::pixmap(icon, 16, button.palette().color(QPalette::Active, QPalette::PlaceholderText), button.devicePixelRatio());
    QIcon made(LayerIcons::pixmap(icon, 16, button.palette().color(QPalette::Active, QPalette::WindowText), button.devicePixelRatio()));
    made.addPixmap(button.style()->generatedIconPixmap(QIcon::Disabled, menuInk, &option), QIcon::Disabled);
    return made;
}

// The icons take the palette's ink, again after each theme.
void LayersPanel::applyIcons()
{
    const QColor ink = palette().color(QPalette::PlaceholderText);
    m_emptyIcon->setPixmap(LayerIcons::pixmap(LayerIcon::layers, 25, ink, devicePixelRatio()));
    // Buttons in the primary ink; the two menus keep theirs.
    m_addBlankLayer->setIcon(footerIcon(LayerIcon::newLayer, *m_addBlankLayer));
    m_group->setIcon(footerIcon(LayerIcon::newFolder, *m_group));
    m_effects->setIcon(LayerIcons::pixmap(LayerIcon::sparkles, 16, ink, devicePixelRatio()));
    m_adjustments->setIcon(LayerIcons::pixmap(LayerIcon::halfFilledCircle, 16, ink, devicePixelRatio()));
    m_delete->setIcon(footerIcon(LayerIcon::trash, *m_delete));
}

void LayersPanel::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    // Once the buttons carry the new palette too.
    if (event->type() == QEvent::PaletteChange)
        QMetaObject::invokeMethod(this, &LayersPanel::applyIcons, Qt::QueuedConnection);
}

QToolButton *LayersPanel::footerButton(const QString &name, const QString &label, const QString &tip, const std::function<void()> &run)
{
    auto *button = new QToolButton(this);
    button->setObjectName(name);
    button->setAccessibleName(label);
    button->setToolTip(tip);
    button->setAutoRaise(true);
    button->setFixedSize(32, 40);
    connect(button, &QToolButton::clicked, this, run);
    return button;
}

void LayersPanel::synchronize()
{
    const std::optional<CanvasDocument> &document = m_session.document();
    const size_t count = document ? document->layers.size() : 0;
    m_count->setText(QString::number(count));
    m_body->setCurrentIndex(count > 0 ? 0 : 1);
    m_emptyHint->setText(document ? QStringLiteral("Import an image or add a blank layer.") : QStringLiteral("Create a canvas or import an image."));
    // Swift 1.4.8: no flash for a stroke; the session refuses.
    const bool editable = m_session.layersLookEditable();
    const std::optional<ImageLayer> active = m_session.activeLayer();
    m_addBlankLayer->setEnabled(editable);
    m_group->setEnabled(editable);
    m_effects->setEnabled(editable && active && active->asset);
    m_adjustments->setEnabled(editable);
    const bool mask = m_session.isMaskSelected(), several = m_session.selectedLayerIDs().size() > 1;
    const QString what = m_session.selectedEffect() ? QStringLiteral("Delete selected effect")
        : mask                                    ? QStringLiteral("Delete layer mask")
        : several                                 ? QStringLiteral("Delete selected layers")
                                                  : QStringLiteral("Delete selected layer");
    m_delete->setToolTip(what);
    m_delete->setAccessibleName(what);
    m_delete->setEnabled(editable && active);
    // Swift's .task(id:): a new request begins its editor.
    if (m_session.adjustmentEditingID() != m_adjustmentTask) {
        m_adjustmentTask = m_session.adjustmentEditingID();
        if (m_adjustmentTask)
            m_session.beginAdjustmentEditing(*m_adjustmentTask);
    }
}
