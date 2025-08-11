#ifndef RELAYCONTROLLER_H
#define RELAYCONTROLLER_H

#pragma once

#include <QObject>
#include <QString>
#include <memory> // 使用智能指针管理生命周期

// 声明，避免头文件包含
class RelayIOModule;
class MotorRegisterAccessor;
class ICommProtocol; // 使用接口而不是具体实现

class RelayController : public QObject
{
    Q_OBJECT
public:
    // 构造函数现在接受一个指向 ICommProtocol 接口的指针
    explicit RelayController(ICommProtocol* protocol, QObject *parent = nullptr);
    ~RelayController();

    Q_INVOKABLE bool openPort(const QString &portName);
    Q_INVOKABLE void openChannel(int ch);
    Q_INVOKABLE void closeChannel(int ch);

signals:
    void portOpened(bool success);
    void errorOccurred(const QString &msg);

private:
    ICommProtocol* m_protocol = nullptr; // 现在是接口类型
    MotorRegisterAccessor* m_accessor = nullptr;
    RelayIOModule* m_relayModule = nullptr;
    int m_slaveId = 1;
};

#endif // RELAYCONTROLLER_H
