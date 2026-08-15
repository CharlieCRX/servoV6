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
#include "presentation/viewmodel/UiControlCommandAdapter.h"  // ★ UI-1：UI 唯一可写入口（ControlCommand source=Ui）
#include "infrastructure/joystick/AndroidGamepadJoystick.h"
#include "infrastructure/logger/Logger.h"
// ★ Phase 5：UDP 链路统一协调层最小接线（MotionControlService + 生产 IControlRuntime）
#include "application_vnext/PlcRuntimeDriverAdapter.h"
#include "application_vnext/control/GatewayControlRuntime.h"
#include "application_vnext/control/MotionControlService.h"
#include "infrastructure/plc_vnext/PlcRuntimeGateway.h"
#include "infrastructure/plc_vnext/transport/AsioModbusTcpClient.h"
#include <sstream>
#include <iomanip>
#include <memory>
#include <optional>

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

// ★ Phase 6：统一控制链路的长生命周期持有者（组合根）。
//   MotionControlService 保存 driver/runtime 的**引用**，因此它们必须与 service 同生命周期，
//   不能是 if 块内的栈对象。本结构以声明逆序析构，保证安全顺序：
//     UdpServer → MotionControlService → GatewayControlRuntime/Driver
//       → PlcRuntimeGateway → Modbus Client
//   由组合根开关 kUnifiedLoopEnabled 决定是否构造；server 仅当 UDP 子开关开启时存在。
struct UnifiedControlStack {
    std::shared_ptr<plc_vnext::transport::AsioModbusTcpClient> client;
    std::unique_ptr<plc_vnext::PlcRuntimeGateway> gateway;
    std::unique_ptr<application_vnext::PlcRuntimeDriverAdapter> driver;
    std::unique_ptr<application_vnext::control::GatewayControlRuntime> runtime;
    std::unique_ptr<application_vnext::control::MotionControlService> service;
    std::unique_ptr<UdpServer> server;   // 仅 kEnableUdpVnext 开启时创建
};

// ★ Phase 6：Legacy 组合根的持有者（仅 kUnifiedLoopEnabled=false 时构造）。
//   Unified 模式**完全不创建**旧 client / legacy manager / 旧 QtAxisViewModel，实现
//   「单 client / 单 poll / 单写链路」，杜绝旧 UI 与 MotionControlService 并存写 PLC。
struct LegacyStack {
    // ---- Modbus 通讯（两分组）----
    std::unique_ptr<plc::protocol::AsioModbusTcpClient> clientA, clientB;
    std::unique_ptr<plc::protocol::PlcPoller>          pollerA, pollerB;
    std::unique_ptr<plc::protocol::PlcDevice>          deviceA, deviceB;
    std::unique_ptr<plc::ModbusSystemDriver>           driverA, driverB;
    SystemManager manager;
    SystemContext* ctxA = nullptr;
    SystemContext* ctxB = nullptr;

    // ---- 旧 UI 控制链（QtAxisViewModel 直连 AxisViewModelCore 写 PLC）----
    // 统一索引：0..5 = A_Y,A_Z,A_R,A_X,A_X1,A_X2；6..11 = B_Y,B_Z,B_R,B_X,B_X1,B_X2
    std::vector<std::unique_ptr<AxisViewModelCore>> vmCores;
    std::vector<std::unique_ptr<QtAxisViewModel>>   qtVMs;
    std::vector<QtAxisViewModel*>                   allViewModels;
    std::unique_ptr<EmergencyStopViewModel>  emergencyVM_A, emergencyVM_B;
    std::unique_ptr<GantryViewModel>         gantryVM_A, gantryVM_B;
    std::unique_ptr<ConnectionViewModel>     connectionVM_A, connectionVM_B;
};

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

    // ★ Phase 6 组合根迁移开关（默认关闭 = Legacy 模式；开启 = Unified 模式）。
    //   两种组合根**二选一构造**，杜绝两套对象并存（文档 §12.6 迁移约束）：
    //     - kUnifiedLoopEnabled = true  : Unified 模式 —— 只创建 vnext client +
    //       MotionControlService，**不创建**旧 client / legacy manager / 旧 QtAxisViewModel
    //       （旧 UI 控制入口随之不存在，无法写 PLC），实现「单 client / 单 poll / 单写链路」；
    //     - kUnifiedLoopEnabled = false : Legacy 模式 —— 旧 client + legacy manager +
    //       旧 UI 控制链（现状），不创建统一栈。
    constexpr bool kUnifiedLoopEnabled = false;
    // UDP 子开关：仅 Unified 模式下有意义，控制是否启动真实 UDP server。
    constexpr bool kEnableUdpVnext = false;
    (void)kEnableUdpVnext;  // Unified 关闭时未被 if constexpr 引用，此处消解未使用警告

    // 两套组合根的持有者（长生命周期，声明逆序析构，避免 service 引用悬空）。
    std::optional<LegacyStack>          legacy;
    std::optional<UnifiedControlStack>  ustack;

    if constexpr (kUnifiedLoopEnabled) {
        // ════════════════════════════════════════════════════════════
        // ★ Unified 组合根：单 client / 单 poll / 单 MotionControlService::tick()
        // ════════════════════════════════════════════════════════════
        plc_vnext::transport::AsioModbusTcpClient::Config vnextCfg;
        vnextCfg.host = "192.168.1.88";   // 与 PLC A 一致
        vnextCfg.port = 502;
        vnextCfg.unitId = 0x01;
        vnextCfg.timeoutMs = 1000;
        UnifiedControlStack& s = ustack.emplace();
        s.client = std::make_shared<plc_vnext::transport::AsioModbusTcpClient>(vnextCfg);
        s.client->start();
        s.gateway = std::make_unique<plc_vnext::PlcRuntimeGateway>(s.client);
        s.driver = std::make_unique<application_vnext::PlcRuntimeDriverAdapter>(*s.gateway);
        s.runtime =
            std::make_unique<application_vnext::control::GatewayControlRuntime>(*s.gateway);
        s.service = std::make_unique<application_vnext::control::MotionControlService>(*s.driver,
                                                                                        *s.runtime);
        // ⚠ P0-A（龙门 UI 放开阻断项）：必须在每次成功 boot / Topology Revision 变化后，
        //   从 PLC D1600 `GantryParam` 读取、解码、校验，并调用
        //   s.service->applyGantryConfig(g, cfg) 注入 service；读取失败或 valid=false 时
        //   UI 必须禁用「建立联动」。当前 D1600 C++ 读路径尚未落地（readGantryParam 为
        //   Python 探针 + 布局 gantryParamBase），此注入点为待接线占位 —— 未接线前逻辑 X
        //   UI 不放开。
        if constexpr (kEnableUdpVnext) {
            UdpServer::Config udpCfg;
            udpCfg.listenPort = 62000;
            udpCfg.bindAddress = "0.0.0.0";
            udpCfg.recvBufferSize = 4096;
            s.server = std::make_unique<UdpServer>(*s.service, udpCfg);
            if (!s.server->start()) {
                LOG_ERROR(LogLayer::APP, "System", "UDP Server failed to start");
            }
        }
        LOG_WARN(LogLayer::APP, "System",
                 "Phase 6 Unified mode ENABLED: single vnext client + MotionControlService. "
                 "Legacy client/manager/old ViewModel NOT created -> one-writer-per-axis.");
    } else {
        // ════════════════════════════════════════════════════════════
        // ★ Legacy 组合根：旧 client + legacy manager + 旧 UI 控制链（现状）
        // ════════════════════════════════════════════════════════════
        LegacyStack& L = legacy.emplace();

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

    L.clientA = std::make_unique<plc::protocol::AsioModbusTcpClient>(cfgA);
    L.clientB = std::make_unique<plc::protocol::AsioModbusTcpClient>(cfgB);
    L.clientA->start();
    L.clientB->start();
    LOG_INFO(LogLayer::APP, "System", "Modbus TCP clients started");

    // 1c. PlcPoller（每个分组共享同一个寄存器注册表）
    L.pollerA = std::make_unique<plc::protocol::PlcPoller>(registry);
    L.pollerB = std::make_unique<plc::protocol::PlcPoller>(registry);

    // 1d. PlcDevice（寄存器读写门面）
    L.deviceA = std::make_unique<plc::protocol::PlcDevice>(plc::protocol::INOVANCE_PROFILE);
    L.deviceB = std::make_unique<plc::protocol::PlcDevice>(plc::protocol::INOVANCE_PROFILE);
    L.deviceA->bindTransport(L.clientA.get());
    L.deviceB->bindTransport(L.clientB.get());

    // 1e. 组装 ModbusSystemDriver
    L.driverA = std::make_unique<plc::ModbusSystemDriver>();
    L.driverB = std::make_unique<plc::ModbusSystemDriver>();
    L.driverA->setModbusClient(L.clientA.get());
    L.driverA->setDevice(L.deviceA.get());
    L.driverA->setPoller(std::move(L.pollerA));
    // m_clock 默认使用 SteadyClock，无需额外设置

    L.driverB->setModbusClient(L.clientB.get());
    L.driverB->setDevice(L.deviceB.get());
    L.driverB->setPoller(std::move(L.pollerB));

    // ============================
    // 2. 系统分组管理
    // ============================
    ContextRejection reason;
    L.manager.createGroup("Machine_A", reason);   // Y, Z, R 轴
    L.manager.createGroup("Machine_B", reason);   // X1, X2 轴（龙门）

    L.ctxA = nullptr;
    L.ctxB = nullptr;
    L.manager.tryGetGroup("Machine_A", L.ctxA, reason);
    L.manager.tryGetGroup("Machine_B", L.ctxB, reason);
    L.ctxA->setDriver(L.driverA.get());
    L.ctxB->setDriver(L.driverB.get());

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
        L.ctxA->setAxisIdentity(id, "Machine_A");
    }
    for (auto id : ALL_AXES) {
        L.ctxB->setAxisIdentity(id, "Machine_B");
    }

    // ============================
    // 4. 首次同步（将 PLC 当前状态注入 SystemContext）
    // ============================
    // 注意：使用真实 Modbus 通讯后，初始状态由 PLC 硬件决定，
    // 不再通过代码"强制设置"（forceState / setSimulatedJogVelocity 等 Fake 专用接口已移除）。
    L.driverA->pollFeedback(*L.ctxA);
    L.driverB->pollFeedback(*L.ctxB);

    // ============================
    // 4. ViewModels（按 分组+轴 维度，两组各含6轴）
    // ============================
    // 构造顺序对应 LegacyStack.qtVMs 统一索引：0..5=A_Y..A_X2；6..11=B_Y..B_X2
    auto makeVm = [&L](const std::string& group, AxisId id) {
        auto core = std::make_unique<AxisViewModelCore>(L.manager, group, id);
        L.qtVMs.push_back(std::make_unique<QtAxisViewModel>(core.get()));
        L.vmCores.push_back(std::move(core));
        L.allViewModels.push_back(L.qtVMs.back().get());
    };
    makeVm("Machine_A", AxisId::Y);
    makeVm("Machine_A", AxisId::Z);
    makeVm("Machine_A", AxisId::R);
    makeVm("Machine_A", AxisId::X);
    makeVm("Machine_A", AxisId::X1);
    makeVm("Machine_A", AxisId::X2);
    makeVm("Machine_B", AxisId::Y);
    makeVm("Machine_B", AxisId::Z);
    makeVm("Machine_B", AxisId::R);
    makeVm("Machine_B", AxisId::X);
    makeVm("Machine_B", AxisId::X1);
    makeVm("Machine_B", AxisId::X2);

    // ─────────────── 4b. 急停安全 ViewModel ───────────────
    L.emergencyVM_A = std::make_unique<EmergencyStopViewModel>(L.manager, "Machine_A");
    L.emergencyVM_B = std::make_unique<EmergencyStopViewModel>(L.manager, "Machine_B");

    // ─────────────── 4c. 龙门 ViewModel ───────────────
    L.gantryVM_A = std::make_unique<GantryViewModel>(L.manager, "Machine_A");
    L.gantryVM_B = std::make_unique<GantryViewModel>(L.manager, "Machine_B");

    // ─────────────── 4d. 连接状态 ViewModel（★ P1/P2 新增）───────────────
    L.connectionVM_A = std::make_unique<ConnectionViewModel>(L.manager, "Machine_A");
    L.connectionVM_B = std::make_unique<ConnectionViewModel>(L.manager, "Machine_B");
    }   // ═══ end Legacy 组合根 ═══

    // ★ Phase 2/6：统一状态快照 -> QML 只读适配器（严格只读，不提交命令）。
    //   Phase 6 组合根把真实 MotionControlService 注入快照投影：
    //     - kUnifiedLoopEnabled 开启时：注入统一栈的 service，QML 展示统一快照；
    //     - 关闭时：传 nullptr，安全展示默认「离线/全局锁定」态（保持现状）。
    UiControlAdapter snapshotAdapter(
        kUnifiedLoopEnabled ? ustack->service.get() : nullptr);

    // ★ UI-1：UI 唯一可写入口（ControlCommand source=Ui -> MotionControlService）。
    //   - Unified 开启时注入真实 service，QML 经 controlCommand 提交；
    //   - Legacy / 未注入时传 nullptr，所有提交返回空（安全：QML 按钮禁用，绝不直写 PLC）。
    UiControlCommandAdapter commandAdapter(
        kUnifiedLoopEnabled ? ustack->service.get() : nullptr);

    // ============================
    // 5. QML 引擎初始化与依赖注入
    // ============================
    QQmlApplicationEngine engine;

    if constexpr (kUnifiedLoopEnabled) {
        // Unified 模式：旧 UI 控制链不存在。group_*/emergency/gantry/connection 全部绑定
        // nullptr（QML 已对 null 容错 → 控制按钮禁用/空展示），杜绝旧 UI 直写 PLC。
        // ⚠ 准确边界：Unified 模式下 UI 是「统一快照只读展示」（经 controlSnapshot），
        //   不是统一控制来源。当前可控来源 = UDP（子开关开启时）+ 摇杆；UI 控制按钮迁移
        //   至 ControlCommand::submit() 属待完成工作（文档 §8.1），不在本阶段范围内。
        engine.rootContext()->setContextProperty("group_A_Y",  nullptr);
        engine.rootContext()->setContextProperty("group_A_Z",  nullptr);
        engine.rootContext()->setContextProperty("group_A_R",  nullptr);
        engine.rootContext()->setContextProperty("group_A_X",  nullptr);
        engine.rootContext()->setContextProperty("group_A_X1", nullptr);
        engine.rootContext()->setContextProperty("group_A_X2", nullptr);
        engine.rootContext()->setContextProperty("group_B_Y",  nullptr);
        engine.rootContext()->setContextProperty("group_B_Z",  nullptr);
        engine.rootContext()->setContextProperty("group_B_R",  nullptr);
        engine.rootContext()->setContextProperty("group_B_X",  nullptr);
        engine.rootContext()->setContextProperty("group_B_X1", nullptr);
        engine.rootContext()->setContextProperty("group_B_X2", nullptr);
        engine.rootContext()->setContextProperty("emergencyVM_A", nullptr);
        engine.rootContext()->setContextProperty("emergencyVM_B", nullptr);
        engine.rootContext()->setContextProperty("gantryVM_A", nullptr);
        engine.rootContext()->setContextProperty("gantryVM_B", nullptr);
        engine.rootContext()->setContextProperty("connectionVM_A", nullptr);
        engine.rootContext()->setContextProperty("connectionVM_B", nullptr);
    } else {
        // Legacy 模式：旧 UI 控制链绑定真实 ViewModel。
        LegacyStack& L = *legacy;
        engine.rootContext()->setContextProperty("group_A_Y",  L.qtVMs[0].get());
        engine.rootContext()->setContextProperty("group_A_Z",  L.qtVMs[1].get());
        engine.rootContext()->setContextProperty("group_A_R",  L.qtVMs[2].get());
        engine.rootContext()->setContextProperty("group_A_X",  L.qtVMs[3].get());
        engine.rootContext()->setContextProperty("group_A_X1", L.qtVMs[4].get());
        engine.rootContext()->setContextProperty("group_A_X2", L.qtVMs[5].get());
        engine.rootContext()->setContextProperty("group_B_Y",  L.qtVMs[6].get());
        engine.rootContext()->setContextProperty("group_B_Z",  L.qtVMs[7].get());
        engine.rootContext()->setContextProperty("group_B_R",  L.qtVMs[8].get());
        engine.rootContext()->setContextProperty("group_B_X",  L.qtVMs[9].get());
        engine.rootContext()->setContextProperty("group_B_X1", L.qtVMs[10].get());
        engine.rootContext()->setContextProperty("group_B_X2", L.qtVMs[11].get());
        engine.rootContext()->setContextProperty("emergencyVM_A", L.emergencyVM_A.get());
        engine.rootContext()->setContextProperty("emergencyVM_B", L.emergencyVM_B.get());
        engine.rootContext()->setContextProperty("gantryVM_A", L.gantryVM_A.get());
        engine.rootContext()->setContextProperty("gantryVM_B", L.gantryVM_B.get());
        engine.rootContext()->setContextProperty("connectionVM_A", L.connectionVM_A.get());
        engine.rootContext()->setContextProperty("connectionVM_B", L.connectionVM_B.get());
    }

    // ★ Phase 2：统一状态快照（UiControlAdapter）暴露给 QML（只读对照面板）
    engine.rootContext()->setContextProperty("controlSnapshot", &snapshotAdapter);
    // ★ UI-1：UI 唯一可写入口（UiControlCommandAdapter）暴露给 QML
    engine.rootContext()->setContextProperty("controlCommand", &commandAdapter);

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
    //   ⚠ 注意：Phase 6 组合根注入真实 service 后，摇杆命令才真正生效：
    //     - kUnifiedLoopEnabled 开启时：注入统一栈的 service（唯一协调层入口），
    //       摇杆 StartJog/StopJog 经其仲裁/执行；
    //     - 关闭时：传 nullptr，摇杆保持选轴/死区/模式状态，命令不提交（安全降级）。
    //   命令链路正确性已由 presentation_tests 用真实 service + Fake gateway 单测覆盖
    //   （见 test_motion_controller.cpp）。
    MotionController motionCtrl(&interpreter, &axisModel,
                                kUnifiedLoopEnabled ? ustack->service.get() : nullptr);

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
    // 6. 全局 Tick Loop —— ★ Phase 6 统一调度循环
    //    单一 QTimer（20ms，文档 §5.2 要求 20~50ms）驱动；两模式互斥，任一时刻只有
    //    一条链路在写 PLC：
    //      - kUnifiedLoopEnabled=true : 只驱动统一协调层（唯一 poll / 仲裁 / 会话 /
    //        快照发布），旧 poll/ViewModel tick 不驱动（旧链路停写，保证互斥）；
    //      - kUnifiedLoopEnabled=false: 完全保持现状的旧链路。
    // ============================
    QTimer systemClock;
    QObject::connect(&systemClock, &QTimer::timeout, [&]() {
        if constexpr (kUnifiedLoopEnabled) {
            // ---- Phase 6 Unified 唯一控制循环 ----
            //   server.tick() 先于 service.tick()：UDP 收包 → submit 本轮即入队，
            //   同一 tick 内即可被仲裁/执行（收包 → submit → 统一 tick 顺序）。
            //   service.tick(): 取命令 → 急停/停止优先（读前）→ 读 runtime/safety/连接
            //     （每 tick 唯一一次 runtime 读）→ 更新领域与全局锁定 → 过期 → 仲裁/执行
            //     → tick 会话（含 JogPolicy 心跳，按单调时钟 deadline 维持，无独立心跳线程）
            //     → 发布不可变快照。
            //   snapshotAdapter.refresh(): GUI 线程把统一快照投影到 QML（只读）。
            if (ustack->server)  ustack->server->tick();   // UDP 收包 → submit
            if (ustack->service) ustack->service->tick();  // 仲裁/执行（含本轮 UDP 命令）
            snapshotAdapter.refresh();
        } else {
            // ---- Legacy 链路（现状）：物理引擎 + 反馈注入 + ViewModel 推进 ----
            LegacyStack& L = *legacy;
            // 6a. 所有分组推进物理引擎 + 反馈注入
            for (const auto& groupName : L.manager.groupNames()) {
                SystemContext* ctx = nullptr;
                ContextRejection r;
                if (L.manager.tryGetGroup(groupName, ctx, r) && ctx) {
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
            for (auto* vm : L.allViewModels) {
                vm->tick();
            }

            // 6c. 急停安全 ViewModel 推进（每帧同步急停控制器状态）
            L.emergencyVM_A->tick();
            L.emergencyVM_B->tick();

            // 6d. 龙门 ViewModel 推进（每帧推进 Orchestrator + 刷新状态投影）
            L.gantryVM_A->tick();
            L.gantryVM_B->tick();

            // 6e. 连接状态 ViewModel 推进（★ P1/P2 新增 — 每帧刷新 TCP 连接状态投影）
            L.connectionVM_A->tick();
            L.connectionVM_B->tick();

            // 6g. ★ Phase 2：统一状态快照投影（GUI 线程内读 ControlStateStore，只读）
            snapshotAdapter.refresh();
        }
    });
    systemClock.start(20);  // ★ Phase 6：统一调度循环 20ms（文档 §5.2：20~50ms）

    // 7. 周期性状态摘要（每秒输出一次）
    QTimer summaryClock;
    QObject::connect(&summaryClock, &QTimer::timeout, [&]() {
        if constexpr (kUnifiedLoopEnabled) {
            // Unified 模式：无旧 ViewModel，输出统一协调层运行摘要（连接 / 全局锁定）。
            LOG_SUMMARY(LogLayer::UI, "Telemetry",
                "=== Unified control loop === connected="
                + std::to_string(snapshotAdapter.connected())
                + " globalLocked=" + std::to_string(snapshotAdapter.globallyLocked()));
        } else {
            LegacyStack& L = *legacy;
            LOG_SUMMARY(LogLayer::UI, "Telemetry",
                "=== Machine_A === "
                + formatAxisSummary(*L.qtVMs[0]) + "  "
                + formatAxisSummary(*L.qtVMs[1]) + "  "
                + formatAxisSummary(*L.qtVMs[2]) + "  "
                + formatAxisSummary(*L.qtVMs[3]) + "  "
                + formatAxisSummary(*L.qtVMs[4]) + "  "
                + formatAxisSummary(*L.qtVMs[5]));
            LOG_SUMMARY(LogLayer::UI, "Telemetry",
                "=== Machine_B === "
                + formatAxisSummary(*L.qtVMs[6]) + "  "
                + formatAxisSummary(*L.qtVMs[7]) + "  "
                + formatAxisSummary(*L.qtVMs[8]) + "  "
                + formatAxisSummary(*L.qtVMs[9]) + "  "
                + formatAxisSummary(*L.qtVMs[10]) + "  "
                + formatAxisSummary(*L.qtVMs[11]));
        }
    });
    summaryClock.start(1000);  // 1s 周期

    int result = app.exec();

    Logger::shutdown();
    return result;
}
