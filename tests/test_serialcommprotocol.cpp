#include <QObject>
#include <QTest>
#include "SerialCommProtocol.h"

class SerialCommProtocolTest : public QObject {
    Q_OBJECT

private slots:
    void defaultConstructor_shouldSetParametersCorrectly();
    void open_withInvalidPort_shouldFail();
};

void SerialCommProtocolTest::defaultConstructor_shouldSetParametersCorrectly() {
    SerialCommProtocol protocol;
    QVERIFY(!protocol.isOpen());
}

void SerialCommProtocolTest::open_withInvalidPort_shouldFail() {
    SerialCommProtocol protocol;
    bool result = protocol.open("INVALID_PORT_NAME");
    QVERIFY(result == false);
}
