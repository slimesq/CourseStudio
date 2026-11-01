#pragma once
#include <QRectF>
#include <QPointF>
#include <cmath>
#include <QList>
#include <QString>
#include <QDebug>
#include <algorithm>
#include "WGCCapture.h"

// --- Module 1: Geometry Algorithm Toolkit ---
class GeometryHelper
{
public:
    enum ResizeHandle
    {
        None,
        Top,
        Bottom,
        Left,
        Right,
        TopLeft,
        TopRight,
        BottomLeft,
        BottomRight
    };

    struct SnapResult
    {
        bool active = false;
        float pos = 0;
    };

    // Determine which handle the mouse is hovering over
    static ResizeHandle getHandleAt(QRectF const& rect, QPointF const& pos, qreal handleSize = 12.0)
    {
        qreal half = handleSize / 2.0;
        auto check = [&](qreal x, qreal y) {
            return QRectF(x - half, y - half, handleSize, handleSize).contains(pos);
        };

        if (check(0, 0))
            return TopLeft;
        if (check(rect.width(), 0))
            return TopRight;
        if (check(0, rect.height()))
            return BottomLeft;
        if (check(rect.width(), rect.height()))
            return BottomRight;
        if (check(rect.width() / 2, 0))
            return Top;
        if (check(rect.width() / 2, rect.height()))
            return Bottom;
        if (check(0, rect.height() / 2))
            return Left;
        if (check(rect.width(), rect.height() / 2))
            return Right;

        return None;
    }

    // Calculate new position and size after resizing
    static void calculateResize(ResizeHandle dir,
                                QPointF const& currPos,
                                QPointF const& anchorPos,
                                qreal& outW,
                                qreal& outH,
                                QPointF& outPos,
                                qreal minSize = 10.0)
    {
        qreal dx = currPos.x() - anchorPos.x();
        qreal dy = currPos.y() - anchorPos.y();

        outPos = anchorPos;  // Default anchor stays put

        switch (dir)
        {
            case BottomRight:
                outW = qMax(minSize, dx);
                outH = qMax(minSize, dy);
                break;
            case TopLeft:
                outW = qMax(minSize, -dx);
                outH = qMax(minSize, -dy);
                outPos = anchorPos - QPointF(outW, outH);
                break;
            case TopRight:
                outW = qMax(minSize, dx);
                outH = qMax(minSize, -dy);
                outPos = QPointF(anchorPos.x(), anchorPos.y() - outH);
                break;
            case BottomLeft:
                outW = qMax(minSize, -dx);
                outH = qMax(minSize, dy);
                outPos = QPointF(anchorPos.x() - outW, anchorPos.y());
                break;
            case Right:
                outW = qMax(minSize, dx);
                outH = 0;
                break;
            case Bottom:
                outH = qMax(minSize, dy);
                outW = 0;
                break;
            case Left:
                outW = qMax(minSize, -dx);
                outPos = QPointF(anchorPos.x() - outW, anchorPos.y());
                break;
            case Top:
                outH = qMax(minSize, -dy);
                outPos = QPointF(anchorPos.x(), anchorPos.y() - outH);
                break;
            default:
                break;
        }
    }

    // Smart Snapping Calculation
    static void calculateSnap(QRectF const& itemRect,
                              QRectF const& canvasRect,
                              QPointF& currentPos,
                              SnapResult& outX,
                              SnapResult& outY,
                              int threshold = 3)
    {
        // --- X Axis Snapping (Left, Center, Right) ---
        outX.active = false;
        float currentX = currentPos.x();
        float width = itemRect.width();

        float diffLeft = std::abs(currentX - canvasRect.left());
        float diffRight = std::abs((currentX + width) - canvasRect.right());
        float diffCenter = std::abs((currentX + width / 2.0) - canvasRect.center().x());

        float minDiffX = std::min({diffLeft, diffRight, diffCenter});

        if (minDiffX < threshold)
        {
            outX.active = true;
            if (minDiffX == diffLeft)
            {
                currentPos.setX(canvasRect.left());
                outX.pos = canvasRect.left();
            }
            else if (minDiffX == diffRight)
            {
                currentPos.setX(canvasRect.right() - width);
                outX.pos = canvasRect.right();
            }
            else
            {
                currentPos.setX(canvasRect.center().x() - width / 2.0);
                outX.pos = canvasRect.center().x();
            }
        }

        // --- Y Axis Snapping (Top, Center, Bottom) ---
        outY.active = false;
        float currentY = currentPos.y();
        float height = itemRect.height();

        float diffTop = std::abs(currentY - canvasRect.top());
        float diffBottom = std::abs((currentY + height) - canvasRect.bottom());
        float diffVCenter = std::abs((currentY + height / 2.0) - canvasRect.center().y());

        float minDiffY = std::min({diffTop, diffBottom, diffVCenter});

        if (minDiffY < threshold)
        {
            outY.active = true;
            if (minDiffY == diffTop)
            {
                currentPos.setY(canvasRect.top());
                outY.pos = canvasRect.top();
            }
            else if (minDiffY == diffBottom)
            {
                currentPos.setY(canvasRect.bottom() - height);
                outY.pos = canvasRect.bottom();
            }
            else
            {
                currentPos.setY(canvasRect.center().y() - height / 2.0);
                outY.pos = canvasRect.center().y();
            }
        }
    }

    static Qt::CursorShape getCursorForHandle(ResizeHandle h)
    {
        switch (h)
        {
            case TopLeft:
            case BottomRight:
                return Qt::SizeFDiagCursor;
            case TopRight:
            case BottomLeft:
                return Qt::SizeBDiagCursor;
            case Top:
            case Bottom:
                return Qt::SizeVerCursor;
            case Left:
            case Right:
                return Qt::SizeHorCursor;
            default:
                return Qt::ArrowCursor;
        }
    }
};

// --- Module 2: Resource Self-Healing Tools ---
class SourceRecoveryHelper
{
public:
    // Attempt to recover lost window handle by name
    static unsigned long long recoverWindowHandle(unsigned long long oldHandle,
                                                  QString const& nameHint)
    {
        auto validWindows = WGCCapture::getCapturableWindows();

        // 1. Check if current handle is still valid
        for (auto const& win : validWindows)
        {
            if ((unsigned long long)win.hwnd == oldHandle)
            {
                return oldHandle;
            }
        }

        // 2. Handle invalid, try fuzzy matching by name
        if (!nameHint.isEmpty())
        {
            qWarning() << "[SourceRecovery] Handle" << oldHandle
                       << "lost. Searching for:" << nameHint;

            QString cleanName = nameHint;
            if (cleanName.startsWith("[Win] "))
                cleanName = cleanName.mid(6);
            else if (cleanName.startsWith("[窗口] "))
                cleanName = cleanName.mid(5);

            QString searchName = cleanName.toLower();

            for (auto const& win : validWindows)
            {
                QString winTitle = win.title.toLower();
                if (winTitle == searchName || winTitle.contains(searchName) ||
                    searchName.contains(winTitle))
                {
                    unsigned long long newHandle = (unsigned long long)win.hwnd;
                    qDebug() << " -> [Success] Recovered window:" << win.title
                             << "New Handle:" << newHandle;
                    return newHandle;
                }
            }
        }
        return 0;  // Not found
    }

    // Parse ID String
    static unsigned long long parseHandleId(QString const& idOrName, bool* isMonitor)
    {
        QString cleanId = idOrName;
        if (idOrName.startsWith("mon:"))
        {
            *isMonitor = true;
            cleanId = idOrName.mid(4);
        }
        else if (idOrName.startsWith("win:"))
        {
            *isMonitor = false;
            cleanId = idOrName.mid(4);
        }

        bool ok = false;
        unsigned long long handle = cleanId.toULongLong(&ok);
        if (!ok && cleanId.startsWith("0x"))
            handle = cleanId.mid(2).toULongLong(&ok, 16);
        return handle;
    }
};
// [Correction] Removed the extra trailing '}' here
