#pragma once
#include <QDragMoveEvent>
#include <type_traits>

// Qt 6.12 takes a drag point as QPointF; 6.4, QPoint.
using DragAt = std::conditional_t<std::is_constructible_v<QDragMoveEvent, QPointF, Qt::DropActions, const QMimeData *, Qt::MouseButtons, Qt::KeyboardModifiers>,
                                  QPointF, QPoint>;
