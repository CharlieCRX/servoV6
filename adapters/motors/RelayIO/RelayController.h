#ifndef RELAYCONTROLLER_H
#define RELAYCONTROLLER_H

#pragma once

#include <QObject>
#include <QString>

class RelayIOModule;
class MotorRegisterAccessor;
class SerialCommProtocol;

class RelayController : public QObject
{
    Q_OBJECT
public:
    explicit RelayController(QObject *parent = nullptr);
    ~RelayController();

    Q_INVOKABLE bool openPort(const QString &portName);
    Q_INVOKABLE void openChannel(int ch);
    Q_INVOKABLE void closeChannel(int ch);

signals:
    void portOpened(bool success);
    void errorOccurred(const QString &msg);

private:
    SerialCommProtocol* m_protocol = nullptr;
    MotorRegisterAccessor* m_accessor = nullptr;
    RelayIOModule* m_relayModule = nullptr;
    int m_slaveId = 1; // 可改为可配置
};

#endif // RELAYCONTROLLER_H
