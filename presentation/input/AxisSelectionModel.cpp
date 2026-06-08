#include "AxisSelectionModel.h"
#include <QDebug>

AxisSelectionModel::AxisSelectionModel(QObject* parent)
    : QObject(parent)
{
}

void AxisSelectionModel::setAxes(const std::vector<AxisId>& axes)
{
    if (axes.empty()) return;
    m_axes = axes;
    m_currentIndex = 0;
    qDebug() << "CurrentAxis =" << currentAxisName();
    emit currentAxisChanged(currentAxis());
}

void AxisSelectionModel::selectLeft()
{
    // ← 切换到上一个轴（循环）
    if (m_axes.empty()) return;
    m_currentIndex = (m_currentIndex - 1 + static_cast<int>(m_axes.size())) % static_cast<int>(m_axes.size());
    qDebug() << "CurrentAxis =" << currentAxisName();
    emit currentAxisChanged(currentAxis());
}

void AxisSelectionModel::selectRight()
{
    // → 切换到下一个轴（循环）
    if (m_axes.empty()) return;
    m_currentIndex = (m_currentIndex + 1) % static_cast<int>(m_axes.size());
    qDebug() << "CurrentAxis =" << currentAxisName();
    emit currentAxisChanged(currentAxis());
}

void AxisSelectionModel::setCurrentAxis(AxisId id)
{
    for (int i = 0; i < static_cast<int>(m_axes.size()); ++i) {
        if (m_axes[i] == id) {
            if (m_currentIndex != i) {
                m_currentIndex = i;
                qDebug() << "CurrentAxis =" << currentAxisName();
                emit currentAxisChanged(currentAxis());
            }
            return;
        }
    }
}

QString AxisSelectionModel::currentAxisName() const
{
    if (m_axes.empty()) return QStringLiteral("?");
    return QString::fromLatin1(axisIdToString(currentAxis()));
}