#pragma once

#include <QObject>

class RelayIOModuleTest : public QObject {
    Q_OBJECT

private slots:
    void defaultConstructor_shouldSetDefaults();
    void customConstructor_shouldSetParametersCorrectly();

    void open_withInvalidPort_shouldFail();
    void open_withValidPort_shouldSucceed();

    void relayChannels_openClose_shouldSucceed();
    void readRelayState_shouldReturnExpectedValue();

    // 新增测试
    void relayIOModule_openCloseChannel_shouldSucceed();
    void relayIOModule_readChannelState_shouldReturnExpectedValue();
};
