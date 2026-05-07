#include "presentation/viewmodel/QtGantryViewModel.h"
#include "infrastructure/logger/Logger.h"
#include <cmath>

QtGantryViewModel::QtGantryViewModel(GantryViewModelCore* core, QObject* parent)
    : QObject(parent)
    , m_core(core)
    , m_lastCoupled(false)
    , m_lastAggState(-1)
    , m_lastPos(0.0)
    , m_lastX1Pos(0.0)
    , m_lastX2Pos(0.0)
    , m_lastX1Enabled(false)
    , m_lastX2Enabled(false)
    , m_lastAnyAlarm(false)
    , m_lastAnyLimit(false)
    , m_lastX1PosLimit(false)
    , m_lastX1NegLimit(false)
    , m_lastX2PosLimit(false)
    , m_lastX2NegLimit(false)
    , m_lastCanAccept(false)
    , m_lastCanCouple(false)
    , m_jogVelocity(15.0)
{
    LOG_INFO(LogLayer::UI, "QtGantryVM", "QtGantryViewModel created");
}

// ── Getters ──

bool QtGantryViewModel::isCoupled() const {
    return m_core->isCoupled();
}

int QtGantryViewModel::aggregatedState() const {
    return static_cast<int>(m_core->aggregatedState());
}

double QtGantryViewModel::position() const {
    return m_core->position();
}

double QtGantryViewModel::x1Position() const {
    return m_core->x1Position();
}

double QtGantryViewModel::x2Position() const {
    return m_core->x2Position();
}

bool QtGantryViewModel::x1Enabled() const {
    return m_core->x1Enabled();
}

bool QtGantryViewModel::x2Enabled() const {
    return m_core->x2Enabled();
}

bool QtGantryViewModel::isAnyAlarm() const {
    return m_core->isAnyAlarm();
}

bool QtGantryViewModel::isAnyLimit() const {
    return m_core->isAnyLimit();
}

bool QtGantryViewModel::x1PosLimit() const {
    return m_core->x1PosLimit();
}

bool QtGantryViewModel::x1NegLimit() const {
    return m_core->x1NegLimit();
}

bool QtGantryViewModel::x2PosLimit() const {
    return m_core->x2PosLimit();
}

bool QtGantryViewModel::x2NegLimit() const {
    return m_core->x2NegLimit();
}

bool QtGantryViewModel::canAcceptCommand() const {
    return m_core->canAcceptCommand();
}

bool QtGantryViewModel::canCouple() const {
    return m_core->canCouple();
}

QString QtGantryViewModel::stateDescription() const {
    return QString::fromStdString(m_core->stateDescription());
}

QString QtGantryViewModel::lastCommandResult() const {
    return QString::fromStdString(m_core->lastCommandResult());
}

double QtGantryViewModel::jogVelocity() const {
    return m_jogVelocity;
}

void QtGantryViewModel::setJogVelocity(double v) {
    if (v < 1.0) v = 1.0;
    if (v > 100.0) v = 100.0;
    if (std::abs(v - m_jogVelocity) > EPSILON) {
        m_jogVelocity = v;
        emit jogVelocityChanged();
        LOG_INFO(LogLayer::UI, "QtGantryVM", "JogVelocity set to " + std::to_string(v));
    }
}

void QtGantryViewModel::adjustJogVelocity(double delta) {
    setJogVelocity(m_jogVelocity + delta);
}

// ── 控制指令 ──

void QtGantryViewModel::requestCoupling() {
    m_core->requestCoupling();
}

void QtGantryViewModel::requestDecoupling(const QString& reason) {
    m_core->requestDecoupling(reason.toStdString());
}

void QtGantryViewModel::jogX1ForwardPressed() {
    m_core->jogX1Forward();
}

void QtGantryViewModel::jogX1ReversePressed() {
    m_core->jogX1Reverse();
}

void QtGantryViewModel::jogX2ForwardPressed() {
    m_core->jogX2Forward();
}

void QtGantryViewModel::jogX2ReversePressed() {
    m_core->jogX2Reverse();
}

void QtGantryViewModel::jogCoupledForwardPressed() {
    m_core->jogCoupledForward();
}

void QtGantryViewModel::jogCoupledReversePressed() {
    m_core->jogCoupledReverse();
}

void QtGantryViewModel::jogCoupledReleased() {
    m_core->jogCoupledRelease();
}

void QtGantryViewModel::moveAbsoluteX1(double target) {
    m_core->moveAbsoluteX1(target);
}

void QtGantryViewModel::moveAbsoluteX2(double target) {
    m_core->moveAbsoluteX2(target);
}

void QtGantryViewModel::moveAbsoluteCoupled(double target) {
    m_core->moveAbsoluteCoupled(target);
}

void QtGantryViewModel::moveRelativeX1(double delta) {
    m_core->moveRelativeX1(delta);
}

void QtGantryViewModel::moveRelativeX2(double delta) {
    m_core->moveRelativeX2(delta);
}

void QtGantryViewModel::moveRelativeCoupled(double delta) {
    m_core->moveRelativeCoupled(delta);
}

// ── Move (QML 文本输入便捷方法) ──

void QtGantryViewModel::moveCoupledTo(const QString& text) {
    bool ok = false;
    double target = text.toDouble(&ok);
    if (ok) {
        m_core->moveAbsoluteCoupled(target);
    } else {
        LOG_WARN(LogLayer::UI, "QtGantryVM", "moveCoupledTo: invalid input '" + text.toStdString() + "'");
    }
}

void QtGantryViewModel::moveX1To(const QString& text) {
    bool ok = false;
    double target = text.toDouble(&ok);
    if (ok) {
        m_core->moveAbsoluteX1(target);
    } else {
        LOG_WARN(LogLayer::UI, "QtGantryVM", "moveX1To: invalid input '" + text.toStdString() + "'");
    }
}

void QtGantryViewModel::moveX2To(const QString& text) {
    bool ok = false;
    double target = text.toDouble(&ok);
    if (ok) {
        m_core->moveAbsoluteX2(target);
    } else {
        LOG_WARN(LogLayer::UI, "QtGantryVM", "moveX2To: invalid input '" + text.toStdString() + "'");
    }
}

void QtGantryViewModel::moveCoupledRel(const QString& text) {
    bool ok = false;
    double delta = text.toDouble(&ok);
    if (ok) {
        m_core->moveRelativeCoupled(delta);
    } else {
        LOG_WARN(LogLayer::UI, "QtGantryVM", "moveCoupledRel: invalid input '" + text.toStdString() + "'");
    }
}

void QtGantryViewModel::moveX1Rel(const QString& text) {
    bool ok = false;
    double delta = text.toDouble(&ok);
    if (ok) {
        m_core->moveRelativeX1(delta);
    } else {
        LOG_WARN(LogLayer::UI, "QtGantryVM", "moveX1Rel: invalid input '" + text.toStdString() + "'");
    }
}

void QtGantryViewModel::moveX2Rel(const QString& text) {
    bool ok = false;
    double delta = text.toDouble(&ok);
    if (ok) {
        m_core->moveRelativeX2(delta);
    } else {
        LOG_WARN(LogLayer::UI, "QtGantryVM", "moveX2Rel: invalid input '" + text.toStdString() + "'");
    }
}

QString QtGantryViewModel::getCommandLog() const {
    return QString::fromStdString(m_core->getCommandLog());
}

void QtGantryViewModel::stop() {
    m_core->stop();
}

// ── Tick ──

void QtGantryViewModel::tick() {
    m_core->tick();

    // 节流发射信号：检测变更
    bool coupled = m_core->isCoupled();
    if (coupled != m_lastCoupled) {
        m_lastCoupled = coupled;
        emit coupledChanged();
    }

    int aggState = static_cast<int>(m_core->aggregatedState());
    if (aggState != m_lastAggState) {
        m_lastAggState = aggState;
        emit aggregatedStateChanged();
    }

    double pos = m_core->position();
    if (std::abs(pos - m_lastPos) > EPSILON) {
        m_lastPos = pos;
        emit positionChanged();
    }

    double x1Pos = m_core->x1Position();
    if (std::abs(x1Pos - m_lastX1Pos) > EPSILON) {
        m_lastX1Pos = x1Pos;
        emit x1PositionChanged();
    }

    double x2Pos = m_core->x2Position();
    if (std::abs(x2Pos - m_lastX2Pos) > EPSILON) {
        m_lastX2Pos = x2Pos;
        emit x2PositionChanged();
    }

    bool x1En = m_core->x1Enabled();
    if (x1En != m_lastX1Enabled) {
        m_lastX1Enabled = x1En;
        emit x1EnabledChanged();
    }

    bool x2En = m_core->x2Enabled();
    if (x2En != m_lastX2Enabled) {
        m_lastX2Enabled = x2En;
        emit x2EnabledChanged();
    }

    bool alarm = m_core->isAnyAlarm();
    if (alarm != m_lastAnyAlarm) {
        m_lastAnyAlarm = alarm;
        emit alarmChanged();
    }

    bool limit = m_core->isAnyLimit();
    if (limit != m_lastAnyLimit) {
        m_lastAnyLimit = limit;
        emit limitChanged();
    }

    bool x1PL = m_core->x1PosLimit();
    bool x1NL = m_core->x1NegLimit();
    if (x1PL != m_lastX1PosLimit || x1NL != m_lastX1NegLimit) {
        m_lastX1PosLimit = x1PL;
        m_lastX1NegLimit = x1NL;
        emit x1LimitChanged();
    }

    bool x2PL = m_core->x2PosLimit();
    bool x2NL = m_core->x2NegLimit();
    if (x2PL != m_lastX2PosLimit || x2NL != m_lastX2NegLimit) {
        m_lastX2PosLimit = x2PL;
        m_lastX2NegLimit = x2NL;
        emit x2LimitChanged();
    }

    bool canAccept = m_core->canAcceptCommand();
    if (canAccept != m_lastCanAccept) {
        m_lastCanAccept = canAccept;
        emit canAcceptCommandChanged();
    }

    bool canCouple = m_core->canCouple();
    if (canCouple != m_lastCanCouple) {
        m_lastCanCouple = canCouple;
        emit canCoupleChanged();
    }

    QString desc = QString::fromStdString(m_core->stateDescription());
    if (desc != m_lastDesc) {
        m_lastDesc = desc;
        emit stateDescriptionChanged();
    }

    QString cmdResult = QString::fromStdString(m_core->lastCommandResult());
    if (cmdResult != m_lastCmdResult) {
        m_lastCmdResult = cmdResult;
        emit lastCommandResultChanged();
    }
}
