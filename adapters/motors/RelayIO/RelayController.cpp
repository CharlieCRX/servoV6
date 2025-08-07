#include "RelayController.h"
#include "SerialCommProtocol.h"
#include "MotorRegisterAccessor.h"
#include "RelayIOModule.h"

RelayController::RelayController(QObject *parent)
    : QObject(parent)
{
    m_protocol = new SerialCommProtocol(9600, 8, 1, QSerialPort::NoFlowControl, QSerialPort::NoParity, this);
    m_accessor = new MotorRegisterAccessor(m_protocol);
    m_relayModule = new RelayIOModule(m_accessor, m_slaveId, 0); // 0为基地址，可改
}

RelayController::~RelayController()
{
    if (m_protocol && m_protocol->isOpen())
        m_protocol->close();
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
