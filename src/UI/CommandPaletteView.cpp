#include "UI/CommandPaletteView.h"
#include "Logging.h"
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QApplication>
#include <QFontDatabase>
#include <QPainterPath>
#include <QVBoxLayout>

namespace {
constexpr int panelWidth = 460;
constexpr int panelHeight = 290;
constexpr int outer = 6;

QFont sized(QFont font, int pixels)
{
    font.setPixelSize(pixels);
    return font;
}
}

const std::set<QString> CommandPaletteController::skipped{QStringLiteral("Search Commands…")};

CommandPaletteRows::CommandPaletteRows(CommandPaletteModel &model, std::function<void(const CommandPaletteEntry &)> run, QWidget *parent)
    : QWidget(parent), m_model(model), m_run(std::move(run))
{
    setFont(sized(font(), 13));
}

int CommandPaletteRows::rowHeight() const
{
    return fontMetrics().height() + 12;
}

QRect CommandPaletteRows::rowRect(int index) const
{
    return QRect(outer, outer + index * rowHeight(), width() - 2 * outer, rowHeight());
}

void CommandPaletteRows::refresh()
{
    m_shown = m_model.results();
    setFixedHeight(outer + int(m_shown.size()) * rowHeight());
    update();
}

void CommandPaletteRows::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QFont shortcutFont = sized(QFontDatabase::systemFont(QFontDatabase::FixedFont), 12);
    for (int index = 0; index < int(m_shown.size()); ++index) {
        const QRect row = rowRect(index);
        if (!row.intersects(event->rect()))
            continue;
        const CommandPaletteEntry &entry = m_shown[size_t(index)];
        if (index == m_model.selection) {
            QColor chosen = palette().color(QPalette::Highlight);
            chosen.setAlphaF(0.35);
            QPainterPath round;
            round.addRoundedRect(row, 6, 6);
            painter.fillPath(round, chosen);
        }
        const QRect inside = row.adjusted(10, 0, -10, 0);
        const QColor ink = palette().color(entry.isEnabled ? QPalette::Active : QPalette::Disabled, QPalette::WindowText);
        int left = inside.left();
        if (entry.isOn) {
            // Swift's checkmark, twelve points with six after.
            QPainterPath check;
            const QPointF middle(left + 6, row.center().y() + 0.5);
            check.moveTo(middle + QPointF(-4, 0));
            check.lineTo(middle + QPointF(-1.5, 3));
            check.lineTo(middle + QPointF(4, -3.5));
            painter.setPen(QPen(ink, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawPath(check);
            left += 12 + 6;
        }
        int right = inside.right();
        if (entry.shortcut) {
            painter.setFont(shortcutFont);
            painter.setPen(palette().color(QPalette::PlaceholderText));
            const int width = painter.fontMetrics().horizontalAdvance(entry.shortcut.value());
            painter.drawText(QRect(right - width, row.top(), width + 1, row.height()), Qt::AlignVCenter | Qt::AlignRight, entry.shortcut.value());
            right -= width + 6;
        }
        painter.setFont(font());
        painter.setPen(ink);
        const QRect text(left, row.top(), right - left, row.height());
        painter.drawText(text, Qt::AlignVCenter | Qt::AlignLeft, fontMetrics().elidedText(entry.title, Qt::ElideRight, text.width()));
    }
}

void CommandPaletteRows::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    for (int index = 0; index < int(m_shown.size()); ++index)
        if (rowRect(index).contains(event->position().toPoint())) {
            m_run(m_shown[size_t(index)]);
            return;
        }
}

CommandPaletteView::CommandPaletteView(std::vector<CommandPaletteEntry> entries, std::function<void(const CommandPaletteEntry &)> run, QWidget *parent)
    : QWidget(parent), m_model(std::move(entries)), m_run(std::move(run)), m_field(new QLineEdit(this)),
      m_scroller(new QScrollArea(this)), m_rows(new CommandPaletteRows(m_model, m_run, m_scroller)), m_empty(new QLabel(QStringLiteral("No commands match"), this))
{
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    m_field->setObjectName(QStringLiteral("commandSearch"));
    m_field->setPlaceholderText(QStringLiteral("Search commands and tools"));
    m_field->setFrame(false);
    m_field->setFont(sized(m_field->font(), 17));
    m_field->setTextMargins(16, 13, 16, 13);
    m_field->installEventFilter(this);
    column->addWidget(m_field);
    auto *divider = new QWidget(this);
    divider->setFixedHeight(1);
    divider->setAutoFillBackground(true);
    divider->setBackgroundRole(QPalette::Mid);
    column->addWidget(divider);
    // The list runs on under the panel's bottom edge.
    m_scroller->setFrameShape(QFrame::NoFrame);
    m_scroller->setWidgetResizable(true);
    m_scroller->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroller->setWidget(m_rows);
    column->addWidget(m_scroller, 1);
    m_empty->setObjectName(QStringLiteral("noCommands"));
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setForegroundRole(QPalette::PlaceholderText);
    connect(m_field, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_model.setQuery(text);
        refresh();
    });
    refresh();
}

void CommandPaletteView::refresh()
{
    m_rows->refresh();
    m_empty->setGeometry(m_scroller->geometry());
    m_empty->setVisible(m_model.results().empty());
    m_scroller->ensureVisible(0, m_rows->rowRect(m_model.selection).center().y(), 0, m_rows->rowHeight() / 2 + outer);
}

void CommandPaletteView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    m_empty->setGeometry(m_scroller->geometry());
}

bool CommandPaletteView::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_field || event->type() != QEvent::KeyPress)
        return QWidget::eventFilter(watched, event);
    switch (static_cast<QKeyEvent *>(event)->key()) {
    case Qt::Key_Up:
    case Qt::Key_Down:
        m_model.move(static_cast<QKeyEvent *>(event)->key() == Qt::Key_Up ? -1 : 1);
        refresh();
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (const std::optional<CommandPaletteEntry> entry = m_model.selected())
            m_run(entry.value());
        return true;
    default:
        return QWidget::eventFilter(watched, event);
    }
}

void CommandPaletteController::toggle(EditorSession &session, const QMenuBar &bar, QAction *search)
{
    if (isOpen()) {
        close();
        return;
    }
    std::vector<CommandPaletteEntry> entries = CommandPaletteMenu::entries(bar, skipped);
    for (std::vector<CommandPaletteEntry> more : {CommandPaletteCommands::layerCommands(session), CommandPaletteCommands::tools(session)})
        entries.insert(entries.end(), more.begin(), more.end());
    if (!m_panel) {
        // A popup: a click elsewhere or Escape puts it away.
        m_panel = new QWidget(&m_window, Qt::Popup);
        m_panel->setObjectName(QStringLiteral("commandPalette"));
        m_panel->setFixedSize(panelWidth, panelHeight);
        m_panel->setAutoFillBackground(true);
        new QVBoxLayout(m_panel);
        m_panel->layout()->setContentsMargins(0, 0, 0, 0);
        m_panel->addAction(search);
        m_panel->installEventFilter(this);
    }
    for (CommandPaletteView *old : m_panel->findChildren<CommandPaletteView *>(Qt::FindDirectChildrenOnly)) {
        old->hide();
        old->deleteLater();
    }
    auto *view = new CommandPaletteView(std::move(entries), [this](const CommandPaletteEntry &entry) { run(entry); }, m_panel);
    m_panel->layout()->addWidget(view);
    // A popup has no frame: its geometry is its place.
    m_panel->setGeometry(QRect(m_window.frameGeometry().center() - QPoint(panelWidth / 2, panelHeight / 2), QSize(panelWidth, panelHeight)));
    m_panel->show();
    view->field().setFocus(Qt::PopupFocusReason);
    qCInfo(lcApp) << "palette opened with" << view->model().entries().size() << "entries";
}

bool CommandPaletteController::eventFilter(QObject *watched, QEvent *event)
{
    // Qt hands focus back only to the window's focus widget.
    if (watched == m_panel && event->type() == QEvent::Hide && m_panel->isAncestorOf(QApplication::focusWidget()))
        QApplication::focusWidget()->clearFocus();
    return QObject::eventFilter(watched, event);
}

void CommandPaletteController::close()
{
    if (m_panel)
        m_panel->hide();
}

void CommandPaletteController::run(const CommandPaletteEntry &entry)
{
    if (!entry.isEnabled) {
        qCWarning(lcApp) << "palette: disabled:" << entry.title;
        return;
    }
    close();
    m_window.activateWindow();
    qCInfo(lcApp) << "palette runs" << entry.title;
    QMetaObject::invokeMethod(this, entry.perform, Qt::QueuedConnection);
}
