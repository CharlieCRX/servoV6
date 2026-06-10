#pragma once

#include <QObject>
#include <vector>
#include "domain/entity/AxisId.h"

/// @brief 纯数据模型：管理可选轴列表 + 当前选中索引
/// 不包含任何 InputEvent 消费逻辑（由 AxisSelectionController 负责）
class AxisSelectionModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString currentAxisName READ currentAxisName NOTIFY currentAxisChanged)

public:
    explicit AxisSelectionModel(QObject* parent = nullptr);

    /// @brief 初始化可选轴列表（默认 Y / Z / R）
    void setAxes(const std::vector<AxisId>& axes);

    /// @brief 左摇杆 ←：切换到上一个轴（循环）
    void selectLeft();

    /// @brief 左摇杆 →：切换到下一个轴（循环）
    void selectRight();

    /// @brief 直接设置当前轴（供 C++ 调用）
    void setCurrentAxis(AxisId id);

    /// @brief 通过轴名字符串设置当前轴（供 QML UI 点击时调用）
    /// 支持 "Y", "Z", "R", "X"
    Q_INVOKABLE void setCurrentAxisByName(const QString& name);

    AxisId currentAxis() const { return m_axes[m_currentIndex]; }
    QString currentAxisName() const;

signals:
    void currentAxisChanged(AxisId id);

private:
    std::vector<AxisId> m_axes = { AxisId::Y, AxisId::Z, AxisId::R, AxisId::X };
    int m_currentIndex = 0;
};