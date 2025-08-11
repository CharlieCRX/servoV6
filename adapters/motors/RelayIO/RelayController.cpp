#include "RelayController.h"
#include "SerialCommProtocol.h"
#include "MotorRegisterAccessor.h"
#include "RelayIOModule.h"

// 构造函数现在接受一个 ICommProtocol* 参数
RelayController::RelayController(ICommProtocol* protocol, QObject *parent)
    : QObject(parent), m_protocol(protocol)
{
    // 如果协议对象无效，处理错误
    if (!m_protocol) {
        emit errorOccurred("通信协议未初始化!");
        return;
    }

    // MotorRegisterAccessor 现在使用传入的协议
    m_accessor = new MotorRegisterAccessor(m_protocol);
    // 0为基地址，可改
    m_relayModule = new RelayIOModule(m_accessor, m_slaveId, 0);
}

RelayController::~RelayController()
{
    // 由于协议对象是从外部传入的，我们不应该在这里删除它。
    // 它的生命周期由创建它的对象管理。
    // 如果你在业务逻辑层使用 std::unique_ptr 或 std::shared_ptr，那就更安全了。

    // 如果有其他资源需要释放，可以在这里进行。
    delete m_relayModule;
    delete m_accessor;
}

bool RelayController::openPort(const QString &portName)
{
    bool ok = m_protocol->open(portName.toStdString());
    emit portOpened(ok);
    if (!ok) {
        emit errorOccurred(QString("打开串口 %1 失败").arg(portName));
    }
    return ok;
}

void RelayController::openChannel(int ch)
{
    if (!m_relayModule->openChannel(ch)) {
        emit errorOccurred(QString("打开继电器通道 %1 失败").arg(ch));
    }
}

void RelayController::closeChannel(int ch)
{
    if (!m_relayModule->closeChannel(ch)) {
        emit errorOccurred(QString("关闭继电器通道 %1 失败").arg(ch));
    }
}
