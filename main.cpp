#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTimer>
#include <QUrl>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QDir>
#include <vector>

#include "application/SystemManager.h"
#include "domain/entity/AxisId.h"
#include "domain/entity/ContextRejection.h"
#include "infrastructure/udp/UdpServer.h"
#include "infrastructure/plc/ModbusSystemDriver.h"
#include "infrastructure/plc/protocol/AsioModbusTcpClient.h"
#include "infrastructure/plc/protocol/PlcDevice.h"
#include "infrastructure/plc/protocol/PlcPoller.h"
#include "infrastructure/plc/protocol/RegisterRegistry.h"
#include "infrastructure/plc/protocol/RegisterAddressAll.h"
#include "infrastructure/plc/protocol/ProtocolProfile.h"
#include "infrastructure/plc/protocol/RegisterMetadata.h"
#include "presentation/viewmodel/AxisViewModelCore.h"
#include "presentation/viewmodel/QtAxisViewModel.h"
#include "presentation/viewmodel/EmergencyStopViewModel.h"
#include "presentation/viewmodel/GantryViewModel.h"
#include "presentation/viewmodel/ConnectionViewModel.h"
#include "presentation/input/GamepadInputInterpreter.h"
#include "presentation/input/AxisSelectionModel.h"
#include "presentation/input/AxisSelectionController.h"
#include "presentation/input/MotionController.h"
#include "presentation/viewmodel/UiControlAdapter.h"   // ★ Phase 2：统一快照 -> QML 只读
#include "infrastructure/joystick/AndroidGamepadJoystick.h"
#include "infrastructure/logger/Logger.h"
#include <sstream>
#include <iomanip>
#include <memory>

// ════════════════════════════════════════════════════
// windows.h 通过 Asio 间接引入，定义了 ERROR 和 NO_ERROR 宏，与 LogLevel::ERROR 冲突
// ════════════════════════════════════════════════════
#ifdef ERROR
#undef ERROR
#endif
#ifdef NO_ERROR
#undef NO_ERROR
#endif

// 辅助：将单个轴的摘要格式化为紧凑字符串
// 输出如 "Y: pos=+0041.4 Standstill"
static std::string formatAxisSummary(QtAxisViewModel& vm)
{
    std::string full = vm.fullName().toStdString();
    // 从 "Machine_A/Y" 提取轴短名 "Y"
    auto slash = full.rfind('/');
    std::string shortName = (slash != std::string::npos) ? full.substr(slash + 1) : full;

    std::ostringstream oss;
    oss << shortName << ": pos=" << std::fixed << std::setprecision(1) << std::showpos
        << vm.position() << std::noshowpos
        << " " << vm.stateText().toStdString();
    if (vm.errorCount() > 0)
        oss << " errs=" << vm.errorCount();
    return oss.str();
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    // ============================
    // 0. 初始化全局可观测性基础设施 (Logger)
    // ============================
    LoggerConfig logCfg;
    logCfg.enableConsole = true;
    logCfg.enableFile = true;
    logCfg.minConsoleLevel = LogLevel::INFO;    // 控制台：屏蔽 DEBUG / TRACE 噪音
    logCfg.minFileLevel    = LogLevel::INFO;    // 日志文件：同样屏蔽 DEBUG / TRACE，节省磁盘空间

    QString logBasePath;
#ifdef Q_OS_ANDROID
    // Android: 日志输出到 /storage/emulated/0/Documents/servo/logs
    logBasePath = QStringLiteral("/storage/emulated/0/Documents/servo");
#else
    logBasePath = QDir::currentPath();
#endif

    logCfg.logDirectory = QString("%1/logs").arg(logBasePath).toStdString();
    Logger::init(logCfg);

    LOG_INFO(LogLayer::APP, "System", "========================================");
    LOG_INFO(LogLayer::APP, "System", "servoV6 Application Starting...");
    LOG_INFO(LogLayer::APP, "System", "Log Directory: " + logCfg.logDirectory);
    LOG_INFO(LogLayer::APP, "System", "========================================");

    QQuickStyle::setStyle("Basic");

    // ============================
    // 1. Modbus 通讯层（每个分组独立的 ModbusSystemDriver）
    // ============================

    // 1a. 寄存器注册表（所有需要轮询的线圈和保持寄存器）
    plc::protocol::RegisterRegistry registry;
    {
        using namespace plc::reg;

        // ── 系统全局 ──
        registry.add(system_global::command::ESTOP_TRIGGER);
        registry.add(system_global::feedback::ESTOP_ACTIVE);
        registry.add(system_global::feedback::GANTRY_ERROR_CODE);

        // ══ X 轴 ══
        for (auto reg : { x_axis::command::ENABLE_REQUEST, x_axis::command::LINKAGE_ENABLE,
              x_axis::command::HOME_TRIGGER, x_axis::command::SET_REL_ZERO,
              x_axis::command::CLEAR_REL_ZERO, x_axis::command::CLEAR_ABS_POS,
              x_axis::command::ABS_MOVE_TRIGGER, x_axis::command::REL_MOVE_TRIGGER,
              x_axis::command::X1_JOG_FORWARD, x_axis::command::X1_JOG_BACKWARD,
              x_axis::command::X2_JOG_FORWARD, x_axis::command::X2_JOG_BACKWARD,
              x_axis::command::ALARM_RESET,
              x_axis::command::X1_ABS_MOVE_TRIGGER, x_axis::command::X2_ABS_MOVE_TRIGGER,
              x_axis::command::X_ESTOP_TRIGGER,
              x_axis::command::ABS_TARGET, x_axis::command::REL_TARGET,
              x_axis::command::X1_ABS_TARGET, x_axis::command::X2_ABS_TARGET,
              x_axis::command::JOG_SPEED, x_axis::command::MOVE_SPEED,
              x_axis::command::TOLERANCE_LIMIT,
              x_axis::feedback::SOFT_LIMIT_NEG, x_axis::feedback::SOFT_LIMIT_POS,
              x_axis::feedback::MOVE_DONE, x_axis::feedback::ABS_MOVING,
              x_axis::feedback::REL_MOVING, x_axis::feedback::JOGGING,
              x_axis::feedback::TOLERANCE_FLAG, x_axis::feedback::TOLERANCE_TIMEOUT,
              x_axis::feedback::LINKAGE_STATE, x_axis::feedback::SOFT_LIMIT_STATE,
              x_axis::feedback::REL_POSITION_OLD, x_axis::feedback::ABS_POSITION_OLD,
              x_axis::feedback::STATE, x_axis::feedback::ALARM_CODE,
              x_axis::feedback::ABS_POSITION, x_axis::feedback::REL_POSITION,
              x_axis::feedback::REL_ZERO_OFFSET,
              x_axis::feedback::X1_SOFT_LIMIT_POS, x_axis::feedback::X1_SOFT_LIMIT_NEG,
              x_axis::feedback::X2_SOFT_LIMIT_POS, x_axis::feedback::X2_SOFT_LIMIT_NEG,
              x_axis::feedback::X1_CURRENT_POS, x_axis::feedback::X2_CURRENT_POS,
              x_axis::feedback::REL_ZERO_RECORD }) { registry.add(reg); }

        // ══ Y 轴 ══
        for (auto reg : { y_axis::command::ENABLE_REQUEST, y_axis::command::HOME_TRIGGER,
              y_axis::command::SET_REL_ZERO, y_axis::command::CLEAR_REL_ZERO,
              y_axis::command::CLEAR_ABS_POS,
              y_axis::command::ABS_MOVE_TRIGGER, y_axis::command::REL_MOVE_TRIGGER,
              y_axis::command::JOG_FORWARD, y_axis::command::JOG_BACKWARD,
              y_axis::command::ALARM_RESET,
              y_axis::command::ABS_TARGET, y_axis::command::REL_TARGET,
              y_axis::command::JOG_SPEED, y_axis::command::MOVE_SPEED,
              y_axis::feedback::MOVE_DONE, y_axis::feedback::ABS_MOVING,
              y_axis::feedback::REL_MOVING, y_axis::feedback::JOGGING,
              y_axis::feedback::REL_POSITION_OLD, y_axis::feedback::ABS_POSITION_OLD,
              y_axis::feedback::STATE, y_axis::feedback::ALARM_CODE,
              y_axis::feedback::ABS_POSITION, y_axis::feedback::REL_POSITION,
              y_axis::feedback::REL_ZERO_OFFSET,
              y_axis::feedback::SOFT_LIMIT_POS, y_axis::feedback::SOFT_LIMIT_NEG,
              y_axis::feedback::REL_ZERO_RECORD }) { registry.add(reg); }

        // ══ Z 轴 ══
        for (auto reg : { z_axis::command::ENABLE_REQUEST, z_axis::command::HOME_TRIGGER,
              z_axis::command::SET_REL_ZERO, z_axis::command::CLEAR_REL_ZERO,
              z_axis::command::CLEAR_ABS_POS,
              z_axis::command::ABS_MOVE_TRIGGER, z_axis::command::REL_MOVE_TRIGGER,
              z_axis::command::JOG_FORWARD, z_axis::command::JOG_BACKWARD,
              z_axis::command::ALARM_RESET,
              z_axis::command::ABS_TARGET, z_axis::command::REL_TARGET,
              z_axis::command::JOG_SPEED, z_axis::command::MOVE_SPEED,
              z_axis::feedback::MOVE_DONE, z_axis::feedback::ABS_MOVING,
              z_axis::feedback::REL_MOVING, z_axis::feedback::JOGGING,
              z_axis::feedback::REL_POSITION_OLD, z_axis::feedback::ABS_POSITION_OLD,
              z_axis::feedback::STATE, z_axis::feedback::ALARM_CODE,
              z_axis::feedback::ABS_POSITION, z_axis::feedback::REL_POSITION,
              z_axis::feedback::REL_ZERO_OFFSET,
              z_axis::feedback::SOFT_LIMIT_POS, z_axis::feedback::SOFT_LIMIT_NEG,
              z_axis::feedback::REL_ZERO_RECORD }) { registry.add(reg); }

        // ══ R 轴 ══
        for (auto reg : { r_axis::command::ENABLE_REQUEST, r_axis::command::HOME_TRIGGER,
              r_axis::command::SET_REL_ZERO, r_axis::command::CLEAR_REL_ZERO,
              r_axis::command::CLEAR_ABS_POS,
              r_axis::command::ABS_MOVE_TRIGGER, r_axis::command::REL_MOVE_TRIGGER,
              r_axis::command::JOG_FORWARD, r_axis::command::JOG_BACKWARD,
              r_axis::command::ALARM_RESET,
              r_axis::command::ABS_TARGET, r_axis::command::REL_TARGET,
              r_axis::command::JOG_SPEED, r_axis::command::MOVE_SPEED,
              r_axis::feedback::MOVE_DONE, r_axis::feedback::ABS_MOVING,
              r_axis::feedback::REL_MOVING, r_axis::feedback::JOGGING,
              r_axis::feedback::REL_POSITION_OLD, r_axis::feedback::ABS_POSITION_OLD,
              r_axis::feedback::STATE, r_axis::feedback::ALARM_CODE,
              r_axis::feedback::ABS_POSITION, r_axis::feedback::REL_POSITION,
              r_axis::feedback::ABS_ZERO_OFFSET,
              r_axis::feedback::REL_ZERO_RECORD }) { registry.add(reg); }
    }

    // 1b. Modbus TCP 客户端配置（两个分组分别连接不同 PLC IP）
    plc::protocol::AsioModbusTcpClient::Config cfgA, cfgB;
    cfgA.host = "192.168.1.88";    // PLC A IP
    cfgA.port = 502;
    cfgA.unitId = 0x01;
    cfgA.timeoutMs = 1000;
    cfgB.host = "127.0.0.1";    // PLC B IP
    cfgB.port = 502;
    cfgB.unitId = 0x01;
    cfgB.timeoutMs = 1000;

    auto clientA = std::make_unique<plc::protocol::AsioModbusTcpClient>(cfgA);
    auto clientB = std::make_unique<plc::protocol::AsioModbusTcpClient>(cfgB);
    clientA->start();
    clientB->start();
    LOG_INFO(LogLayer::APP, "System", "Modbus TCP clients started");

    // 1c. PlcPoller（每个分组共享同一个寄存器注册表）
    auto pollerA = std::make_unique<plc::protocol::PlcPoller>(registry);
    auto pollerB = std::make_unique<plc::protocol::PlcPoller>(registry);

    // 1d. PlcDevice（寄存器读写门面）
    auto deviceA = std::make_unique<plc::protocol::PlcDevice>(plc::protocol::INOVANCE_PROFILE);
    auto deviceB = std::make_unique<plc::protocol::PlcDevice>(plc::protocol::INOVANCE_PROFILE);
    deviceA->bindTransport(clientA.get());
    deviceB->bindTransport(clientB.get());

    // 1e. 组装 ModbusSystemDriver
    plc::ModbusSystemDriver driverA, driverB;
    driverA.setModbusClient(clientA.get());
    driverA.setDevice(deviceA.get());
    driverA.setPoller(std::move(pollerA));
    // m_clock 默认使用 SteadyClock，无需额外设置

    driverB.setModbusClient(clientB.get());
    driverB.setDevice(deviceB.get());
    driverB.setPoller(std::move(pollerB));

    // ============================
    // 2. 系统分组管理
    // ============================
    SystemManager manager;
    ContextRejection reason;

    manager.createGroup("Machine_A", reason);   // Y, Z, R 轴
    manager.createGroup("Machine_B", reason);   // X1, X2 轴（龙门）

    SystemContext* ctxA = nullptr;
    SystemContext* ctxB = nullptr;
    manager.tryGetGroup("Machine_A", ctxA, reason);
    manager.tryGetGroup("Machine_B", ctxB, reason);
    ctxA->setDriver(&driverA);
    ctxB->setDriver(&driverB);

    constexpr std::array<AxisId, 6> ALL_AXES = {
        AxisId::X, AxisId::X1, AxisId::X2, AxisId::Y, AxisId::Z, AxisId::R
    };

    // ============================
    // 3. 为所有 Axis 实体注册身份（groupName + axisId），用于日志系统 TraceScope 上下文
    //    必须在首次 pollFeedback 之前执行，确保 applyFeedback 日志能携带正确的轴名和分组
    //    使用 setAxisIdentity() 绕过龙门语义拦截（龙门轴在初始 NotSynchronized 状态下
    //    tryReadAxis 会拒绝访问，导致 X/X1/X2 永远无法注册身份）
    // ============================
    for (auto id : ALL_AXES) {
        ctxA->setAxisIdentity(id, "Machine_A");
    }
    for (auto id : ALL_AXES) {
        ctxB->setAxisIdentity(id, "Machine_B");
    }

    // ============================
    // 4. 首次同步（将 PLC 当前状态注入 SystemContext）
    // ============================
    // 注意：使用真实 Modbus 通讯后，初始状态由 PLC 硬件决定，
    // 不再通过代码"强制设置"（forceState / setSimulatedJogVelocity 等 Fake 专用接口已移除）。
    driverA.pollFeedback(*ctxA);
    driverB.pollFeedback(*ctxB);

    // ============================
    // 3b. UDP 服务器（远程 R 轴控制）
    // ============================
    UdpServer::Config udpCfg;
    udpCfg.listenPort = 62000;
    udpCfg.bindAddress = "0.0.0.0";
    udpCfg.recvBufferSize = 4096;

    UdpServer udpServer(manager, udpCfg);
    if (!udpServer.start()) {
        LOG_ERROR(LogLayer::APP, "System", "UDP Server failed to start");
    }

    // ============================
    // 4. ViewModels（按 分组+轴 维度，两组各含6轴）
    // ============================
    // Machine_A 的全部轴
    auto vmCore_A_Y  = std::make_unique<AxisViewModelCore>(manager, "Machine_A", AxisId::Y);
    auto vmCore_A_Z  = std::make_unique<AxisViewModelCore>(manager, "Machine_A", AxisId::Z);
    auto vmCore_A_R  = std::make_unique<AxisViewModelCore>(manager, "Machine_A", AxisId::R);
    auto vmCore_A_X  = std::make_unique<AxisViewModelCore>(manager, "Machine_A", AxisId::X);
    auto vmCore_A_X1 = std::make_unique<AxisViewModelCore>(manager, "Machine_A", AxisId::X1);
    auto vmCore_A_X2 = std::make_unique<AxisViewModelCore>(manager, "Machine_A", AxisId::X2);

    // Machine_B 的全部轴
    auto vmCore_B_Y  = std::make_unique<AxisViewModelCore>(manager, "Machine_B", AxisId::Y);
    auto vmCore_B_Z  = std::make_unique<AxisViewModelCore>(manager, "Machine_B", AxisId::Z);
    auto vmCore_B_R  = std::make_unique<AxisViewModelCore>(manager, "Machine_B", AxisId::R);
    auto vmCore_B_X  = std::make_unique<AxisViewModelCore>(manager, "Machine_B", AxisId::X);
    auto vmCore_B_X1 = std::make_unique<AxisViewModelCore>(manager, "Machine_B", AxisId::X1);
    auto vmCore_B_X2 = std::make_unique<AxisViewModelCore>(manager, "Machine_B", AxisId::X2);

    // Qt 包装
    QtAxisViewModel qtVM_A_Y(vmCore_A_Y.get());
    QtAxisViewModel qtVM_A_Z(vmCore_A_Z.get());
    QtAxisViewModel qtVM_A_R(vmCore_A_R.get());
    QtAxisViewModel qtVM_A_X(vmCore_A_X.get());
    QtAxisViewModel qtVM_A_X1(vmCore_A_X1.get());
    QtAxisViewModel qtVM_A_X2(vmCore_A_X2.get());

    QtAxisViewModel qtVM_B_Y(vmCore_B_Y.get());
    QtAxisViewModel qtVM_B_Z(vmCore_B_Z.get());
    QtAxisViewModel qtVM_B_R(vmCore_B_R.get());
    QtAxisViewModel qtVM_B_X(vmCore_B_X.get());
    QtAxisViewModel qtVM_B_X1(vmCore_B_X1.get());
    QtAxisViewModel qtVM_B_X2(vmCore_B_X2.get());

    // ─────────────── 4b. 急停安全 ViewModel ───────────────
    // 每个分组一个 EmergencyStopViewModel，在 tick loop 中读取紧急急停状态
    EmergencyStopViewModel emergencyVM_A(manager, "Machine_A");
    EmergencyStopViewModel emergencyVM_B(manager, "Machine_B");

    // ─────────────── 4c. 龙门 ViewModel ───────────────
    // 每个分组一个 GantryViewModel，桥接 Domain 龙门控制器状态到 QML
    GantryViewModel gantryVM_A(manager, "Machine_A");
    GantryViewModel gantryVM_B(manager, "Machine_B");

    // ─────────────── 4d. 连接状态 ViewModel（★ P1/P2 新增）───────────────
    // 每个分组一个 ConnectionViewModel，桥接基础设施层 TCP 连接状态到 QML
    // 提供：连接状态指示灯（绿/红）+ 状态文本 + 手动重连按钮
    ConnectionViewModel connectionVM_A(manager, "Machine_A");
    ConnectionViewModel connectionVM_B(manager, "Machine_B");

    // ★ Phase 2：统一状态快照 -> QML 只读适配器（严格只读，不提交命令）
    //   真实 vnext 链路（SystemManagerVnext + MotionControlService）由 Phase 6
    //   组合根注入；接入前传 nullptr，安全展示默认「离线/全局锁定」态。
    //   Phase 6 替换为：MotionControlService svc(driver, runtime); UiControlAdapter snapshotAdapter(&svc);
    UiControlAdapter snapshotAdapter(nullptr);

    // ============================
    // 5. QML 引擎初始化与依赖注入
    // ============================
    QQmlApplicationEngine engine;

    engine.rootContext()->setContextProperty("group_A_Y",  &qtVM_A_Y);
    engine.rootContext()->setContextProperty("group_A_Z",  &qtVM_A_Z);
    engine.rootContext()->setContextProperty("group_A_R",  &qtVM_A_R);
    engine.rootContext()->setContextProperty("group_A_X",  &qtVM_A_X);
    engine.rootContext()->setContextProperty("group_A_X1", &qtVM_A_X1);
    engine.rootContext()->setContextProperty("group_A_X2", &qtVM_A_X2);

    engine.rootContext()->setContextProperty("group_B_Y",  &qtVM_B_Y);
    engine.rootContext()->setContextProperty("group_B_Z",  &qtVM_B_Z);
    engine.rootContext()->setContextProperty("group_B_R",  &qtVM_B_R);
    engine.rootContext()->setContextProperty("group_B_X",  &qtVM_B_X);
    engine.rootContext()->setContextProperty("group_B_X1", &qtVM_B_X1);
    engine.rootContext()->setContextProperty("group_B_X2", &qtVM_B_X2);

    // 急停安全 ViewModel
    engine.rootContext()->setContextProperty("emergencyVM_A", &emergencyVM_A);
    engine.rootContext()->setContextProperty("emergencyVM_B", &emergencyVM_B);

    // 龙门 ViewModel
    engine.rootContext()->setContextProperty("gantryVM_A", &gantryVM_A);
    engine.rootContext()->setContextProperty("gantryVM_B", &gantryVM_B);

    // 连接状态 ViewModel（★ P1/P2 新增）
    engine.rootContext()->setContextProperty("connectionVM_A", &connectionVM_A);
    engine.rootContext()->setContextProperty("connectionVM_B", &connectionVM_B);

    // ★ Phase 2：统一状态快照（UiControlAdapter）暴露给 QML（只读对照面板）
    engine.rootContext()->setContextProperty("controlSnapshot", &snapshotAdapter);

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
        &app, []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

    // ============================
    // 5b. ★ 游戏手柄输入管线初始化 ★
    // 信号链: AndroidGamepadJoystick::changed()
    //       → GamepadInputInterpreter::onGamepadChanged()
    //       → emit inputEvent(InputEvent)
    //       → AxisSelectionController::onInputEvent()
    //       → AxisSelectionModel::selectLeft()/selectRight()
    //       → qDebug "CurrentAxis = Y/Z/R"
    // ============================
    // ★ 关键：在 Qt 主线程首次触摸单例，确保 thread affinity 正确
    // 否则 JNI 首次调用时单例在 Android Input 线程构造，该线程无 event loop，QueuedConnection 永不执行
    (void)AndroidGamepadJoystick::instance();

    // ★ 注册 InputEvent 为 Qt 元类型，确保跨线程 QueuedConnection 正常工作
    qRegisterMetaType<InputEvent>();
    LOG_INFO(LogLayer::APP, "System", "InputEvent metatype registered");

    AxisSelectionModel axisModel;
    GamepadInputInterpreter interpreter;

    // ★ 右摇杆 → 点动控制器（必须在 AxisSelectionController 之前构造，因为 axisCtrl 需要引用它）
    //   信号链: interpreter::inputEvent(Motion) → MotionController::onInputEvent()
    //          → 当前轴 ViewModel::jogPositivePressed/Released 或 jogNegativePressed/Released
    //   跨轴跳跃保护: axisModel::currentAxisChanged → MotionController::onCurrentAxisChanged()
    // ★ Phase 4：MotionController 不再持有 ViewModel、不再直接写 PLC，改为提交
    //   ControlCommand 给统一协调层 MotionControlService。
    //   第三参数 = MotionControlService*（唯一协调层入口）。Phase 6 组合根注入真实
    //   service（并统一驱动 tick）前传 nullptr：摇杆保持选轴/死区/模式状态，命令提交
    //   待 Phase 6 接线后生效。命令链路正确性已由 presentation_tests 用真实 service +
    //   Fake gateway 单测覆盖（见 test_motion_controller.cpp）。
    //   ⚠ 注意：此处为「Phase 4 组件/集成测试已完成，主程序尚未启用摇杆统一链路」的
    //   安全降级（旧 ViewModel 控制链路在 Phase 4 已从摇杆移除），并非现有程序摇杆
    //   功能已完成迁移；真机摇杆运动控制须待 Phase 6 注入 service 并驱动 tick 后生效。
    MotionController motionCtrl(&interpreter, &axisModel, /*service*/nullptr);

    AxisSelectionController axisCtrl(&interpreter, &axisModel);
    axisCtrl.setMotionController(&motionCtrl);  // ★ 注入 MotionController，JOG 活跃时阻止左摇杆选轴
    interpreter.start();

    // ★ Phase 4：摇杆经 MotionControlService 统一协调，不再把轴映射到 QtAxisViewModel
    //   （AxisId → AxisTarget 由 MotionController::currentAxisTarget() 内部完成，A 组）。
    //   旧的逐轴 ViewModel 注册已移除。

    // ★ 暴露 AxisSelectionModel 给 QML，让摇杆切换轴能更新 UI
    engine.rootContext()->setContextProperty("axisSelectionModel", &axisModel);

    // ★ 暴露 MotionController 给 QML，让 QML 的模式切换器与 C++ 同步
    engine.rootContext()->setContextProperty("motionController", &motionCtrl);

    engine.loadFromModule("servoV6", "Main");

    // ============================
    // 6. 全局 Tick Loop（统一 pollFeedback）
    // ============================
    std::vector<QtAxisViewModel*> allViewModels = {
        &qtVM_A_Y, &qtVM_A_Z, &qtVM_A_R, &qtVM_A_X, &qtVM_A_X1, &qtVM_A_X2,
        &qtVM_B_Y, &qtVM_B_Z, &qtVM_B_R, &qtVM_B_X, &qtVM_B_X1, &qtVM_B_X2
    };

    QTimer systemClock;
    QObject::connect(&systemClock, &QTimer::timeout, [&]() {
        // 6a. 所有分组推进物理引擎 + 反馈注入
        for (const auto& groupName : manager.groupNames()) {
            SystemContext* ctx = nullptr;
            ContextRejection r;
            if (manager.tryGetGroup(groupName, ctx, r) && ctx) {
                auto* drv = ctx->driver();
                if (!drv) continue;

                // 6a-1. 反馈注入（轴 + 龙门 + 急停）
                drv->pollFeedback(*ctx);

                // 6a-2. 消费 EmergencyStopController 产生的 pending command
                auto& estopCtrl = ctx->emergencyStopController();
                if (estopCtrl.hasPendingCommand()) {
                    auto commResult = drv->send(estopCtrl.popPendingCommand());
                    if (!commResult.ok()) {
                        LOG_WARN(LogLayer::APP, "System",
                            "[" + groupName + "] EmergencyStop command delivery failed: " + commResult.diagnostic);
                    }
                }
            }
        }

        // 6b. 所有 ViewModel 推进状态机
        for (auto* vm : allViewModels) {
            vm->tick();
        }

        // 6c. 急停安全 ViewModel 推进（每帧同步急停控制器状态）
        emergencyVM_A.tick();
        emergencyVM_B.tick();

        // 6d. 龙门 ViewModel 推进（每帧推进 Orchestrator + 刷新状态投影）
        gantryVM_A.tick();
        gantryVM_B.tick();

        // 6e. 连接状态 ViewModel 推进（★ P1/P2 新增 — 每帧刷新 TCP 连接状态投影）
        connectionVM_A.tick();
        connectionVM_B.tick();

        // 6f. UDP 消息处理（收包 → 分发 → 回包）
        udpServer.tick();

        // 6g. ★ Phase 2：统一状态快照投影（GUI 线程内读 ControlStateStore，只读）
        snapshotAdapter.refresh();
    });
    systemClock.start(10);  // 10ms 物理心跳

    // 7. 周期性状态摘要（每秒输出一次，按分组分行）
    QTimer summaryClock;
    QObject::connect(&summaryClock, &QTimer::timeout, [&]() {
        LOG_SUMMARY(LogLayer::UI, "Telemetry",
            "=== Machine_A === "
            + formatAxisSummary(qtVM_A_Y) + "  "
            + formatAxisSummary(qtVM_A_Z) + "  "
            + formatAxisSummary(qtVM_A_R) + "  "
            + formatAxisSummary(qtVM_A_X) + "  "
            + formatAxisSummary(qtVM_A_X1) + "  "
            + formatAxisSummary(qtVM_A_X2));
        LOG_SUMMARY(LogLayer::UI, "Telemetry",
            "=== Machine_B === "
            + formatAxisSummary(qtVM_B_Y) + "  "
            + formatAxisSummary(qtVM_B_Z) + "  "
            + formatAxisSummary(qtVM_B_R) + "  "
            + formatAxisSummary(qtVM_B_X) + "  "
            + formatAxisSummary(qtVM_B_X1) + "  "
            + formatAxisSummary(qtVM_B_X2));
    });
    summaryClock.start(1000);  // 1s 周期

    int result = app.exec();

    Logger::shutdown();
    return result;
}
