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
    qDebug() << "[AxisModel] currentAxis=" << currentAxisName();
    emit currentAxisChanged(currentAxis());
}

void AxisSelectionModel::selectLeft()
{
    qDebug() << "[AxisModel] selectLeft currentIndex=" << m_currentIndex
             << "currentAxis=" << currentAxisName();
    if (m_axes.empty()) {
        qDebug() << "[AxisModel] axes list is empty";
        return;
    }
    const int oldIndex = m_currentIndex;
    m_currentIndex = (m_currentIndex - 1 + static_cast<int>(m_axes.size())) %
                     static_cast<int>(m_axes.size());
    qDebug() << "[AxisModel] left oldIndex=" << oldIndex
             << "newIndex=" << m_currentIndex
             << "axis=" << currentAxisName();
    emit currentAxisChanged(currentAxis());
}

void AxisSelectionModel::selectRight()
{
    qDebug() << "[AxisModel] selectRight currentIndex=" << m_currentIndex
             << "currentAxis=" << currentAxisName();
    if (m_axes.empty()) {
        qDebug() << "[AxisModel] axes list is empty";
        return;
    }
    const int oldIndex = m_currentIndex;
    m_currentIndex = (m_currentIndex + 1) % static_cast<int>(m_axes.size());
    qDebug() << "[AxisModel] right oldIndex=" << oldIndex
             << "newIndex=" << m_currentIndex
             << "axis=" << currentAxisName();
    emit currentAxisChanged(currentAxis());
}

void AxisSelectionModel::setCurrentAxis(AxisId id)
{
    for (int i = 0; i < static_cast<int>(m_axes.size()); ++i) {
        if (m_axes[i] == id) {
            if (m_currentIndex != i) {
                m_currentIndex = i;
                qDebug() << "[AxisModel] currentAxis=" << currentAxisName();
                emit currentAxisChanged(currentAxis());
            }
            return;
        }
    }
}

void AxisSelectionModel::setCurrentAxisByName(const QString& name)
{
    AxisId id;
    if (name == QStringLiteral("Y")) id = AxisId::Y;
    else if (name == QStringLiteral("Z")) id = AxisId::Z;
    else if (name == QStringLiteral("R")) id = AxisId::R;
    else if (name == QStringLiteral("X")) id = AxisId::X;
    else if (name == QStringLiteral("X1")) id = AxisId::X1;
    else if (name == QStringLiteral("X2")) id = AxisId::X2;
    else {
        qDebug() << "[AxisModel] unknown axis name=" << name;
        return;
    }

    qDebug() << "[AxisModel] setCurrentAxisByName name=" << name;
    setCurrentAxis(id);
}

void AxisSelectionModel::setCurrentGroupByName(const QString& name)
{
    plc_vnext::contracts::PlcGroupIndex group(0);
    if (name == QStringLiteral("Machine_A") || name == QStringLiteral("A")) {
        group = plc_vnext::contracts::PlcGroupIndex(0);
    } else if (name == QStringLiteral("Machine_B") || name == QStringLiteral("B")) {
        group = plc_vnext::contracts::PlcGroupIndex(1);
    } else {
        qDebug() << "[AxisModel] unknown group name=" << name;
        return;
    }

    if (m_currentGroup == group) return;
    m_currentGroup = group;
    qDebug() << "[AxisModel] currentGroup=" << currentGroupName();
    emit currentGroupChanged();
}

QString AxisSelectionModel::currentAxisName() const
{
    if (m_axes.empty()) return QStringLiteral("?");
    return QString::fromLatin1(axisIdToString(currentAxis()));
}

QString AxisSelectionModel::currentGroupName() const
{
    return m_currentGroup.value() == 1 ? QStringLiteral("Machine_B")
                                       : QStringLiteral("Machine_A");
}
