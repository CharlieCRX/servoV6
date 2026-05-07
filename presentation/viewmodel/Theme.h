#pragma once

#include <QObject>
#include <QColor>

/**
 * @class Theme
 * @brief QML 全局主题单例 — 提供缩放基准和工业控制配色
 *
 * 设计约束：
 *  - 绝不暴露 Q_OBJECT 属性外部可变性（@ref setter-private）
 *  - Color 通过 QColor 暴露，保障跨模块边界安全
 *  - 字体大小基于像素，缩放因子 scale 作用于所有数字属性
 */
class Theme : public QObject
{
    Q_OBJECT

    // ── 缩放基准 ──
    Q_PROPERTY(qreal scale READ scale CONSTANT FINAL)

    // ── 背景色系 ──
    Q_PROPERTY(QColor panelBg  READ panelBg  CONSTANT FINAL)
    Q_PROPERTY(QColor bgDark   READ bgDark   CONSTANT FINAL)

    // ── 边框色 ──
    Q_PROPERTY(QColor borderMain READ borderMain CONSTANT FINAL)

    // ── 文字色系 ──
    Q_PROPERTY(QColor textMain READ textMain CONSTANT FINAL)
    Q_PROPERTY(QColor textDim  READ textDim  CONSTANT FINAL)

    // ── 状态色系 ──
    Q_PROPERTY(QColor colorDisabled READ colorDisabled CONSTANT FINAL)
    Q_PROPERTY(QColor colorIdle     READ colorIdle     CONSTANT FINAL)
    Q_PROPERTY(QColor colorMoving   READ colorMoving   CONSTANT FINAL)
    Q_PROPERTY(QColor colorError    READ colorError    CONSTANT FINAL)

    // ── 排版 ──
    Q_PROPERTY(int fontSmall  READ fontSmall  CONSTANT FINAL)
    Q_PROPERTY(int fontNormal READ fontNormal CONSTANT FINAL)
    Q_PROPERTY(int fontLarge  READ fontLarge  CONSTANT FINAL)
    Q_PROPERTY(int fontGiant  READ fontGiant  CONSTANT FINAL)

public:
    /**
     * @brief 获取全局唯一实例（线程不安全-只应在 GUI 线程调用）
     */
    static Theme& instance();

    // ── Getter ──
    qreal scale()       const { return 1.0; }
    QColor panelBg()    const { return QColor("#161b22"); }
    QColor bgDark()     const { return QColor("#0d1117"); }
    QColor borderMain() const { return QColor("#30363d"); }
    QColor textMain()   const { return QColor("#e0e0e0"); }
    QColor textDim()    const { return QColor("#8b949e"); }
    QColor colorDisabled() const { return QColor("#484f58"); }
    QColor colorIdle()     const { return QColor("#4caf50"); }
    QColor colorMoving()   const { return QColor("#58a6ff"); }
    QColor colorError()    const { return QColor("#f85149"); }
    int fontSmall()  const { return 10; }
    int fontNormal() const { return 13; }
    int fontLarge()  const { return 16; }
    int fontGiant()  const { return 24; }

    // ── 禁止拷贝/移动 ──
    Theme(const Theme&) = delete;
    Theme& operator=(const Theme&) = delete;

private:
    Theme()  = default;
    ~Theme() = default;
};
