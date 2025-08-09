// This is the header file for the BluetoothCommProtocol class.
// It inherits from the ICommProtocol interface and uses QML-specific macros
// to expose its functionality to the QML engine.

#ifndef BLUETOOTHCOMMPROTOCOL_H
#define BLUETOOTHCOMMPROTOCOL_H

#include <QObject>
#include <QBluetoothSocket>
#include <QBluetoothDeviceDiscoveryAgent>
#include <QMutex>
#include <QByteArray>
#include <QList>
#include <QVariantMap>
#include <QMetaType> // for Q_DECLARE_METATYPE
#include <mutex>


// Include the new interface header file.
#include "ICommProtocol.h"

// Define a QML-friendly RegisterBlock structure that can be used with Q_INVOKABLE methods.
// Q_GADGET is a lightweight way to make a struct introspectable for QML.
struct QmlRegisterBlock {
    Q_GADGET
    Q_PROPERTY(QList<quint16> data MEMBER data)
public: // 确保 data 成员可以被外部访问
    QList<quint16> data;
};

// Declare the type so it can be used in QML
Q_DECLARE_METATYPE(QmlRegisterBlock)

// The BluetoothCommProtocol class is a singleton that can be accessed from QML.
class BluetoothCommProtocol : public QObject, public ICommProtocol
{
    Q_OBJECT
    // Define a QML property for the list of discovered devices.
    Q_PROPERTY(QVariantList deviceList READ deviceList NOTIFY deviceListChanged)
    // Define a QML property for the connection status.
    Q_PROPERTY(bool isOpen READ isOpen NOTIFY connected)
    // Define a QML property for the name of the connected device.
    Q_PROPERTY(QString connectedName READ connectedName NOTIFY connected)

public:
    explicit BluetoothCommProtocol(QObject* parent = nullptr);
    ~BluetoothCommProtocol();

    // ICommProtocol interface implementation
    bool open(const std::string& deviceName, bool reOpen = false) override;
    void close() override;
    bool isOpen() const override;
    bool read(int mID, int regType, int startReg, int stopReg, RegisterBlock& out) override;
    bool write(int mID, int regType, int reg, const RegisterBlock& in) override;

    // QML-invokable wrapper methods with QML-friendly types.
    Q_INVOKABLE bool openDevice(const QString& deviceNameOrAddress);
    Q_INVOKABLE bool readRegisters(int mID, int regType, int startReg, int stopReg, QmlRegisterBlock& out);
    Q_INVOKABLE bool writeRegisters(int mID, int regType, int reg, const QmlRegisterBlock& in);
    Q_INVOKABLE void startScan();

    // QML-accessible properties' read methods
    QString connectedName() const;
    QVariantList deviceList() const;

signals:
    // Signals to notify the QML UI of state changes.
    void connected();
    void disconnected();
    void errorOccured(const QString& error);
    void deviceFound(const QString& name, const QString& address);
    void deviceScanFinished();
    void deviceListChanged();

private slots:
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketReadyRead();
    void onDiscoveryDeviceDiscovered(const QBluetoothDeviceInfo& info);
    void onDiscoveryFinished();

private:
    bool scanAndConnectByNameOrAddress(const QString& nameOrAddress);
    bool connectToAddress(const QString& address, const QBluetoothUuid& uuid);

    // Member variables for the Bluetooth protocol.
    QBluetoothSocket* socket_;
    QBluetoothDeviceDiscoveryAgent* discoveryAgent_;

    mutable std::mutex readMutex_;
    QByteArray readBuffer_; // 接收缓存

    QVariantList devices_; // List to store discovered devices
    QString connectedAddress_;
    QString connectedName_;
    int connectTimeoutMs_;
    bool lastOpenResult_;
};

#endif // BLUETOOTHCOMMPROTOCOL_H
