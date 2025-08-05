#ifndef TEST_P100SMOTOR_H
#define TEST_P100SMOTOR_H

#include <QObject>

class P100SMotorTest : public QObject
{
    Q_OBJECT

private slots:
//    void positionAndReset_shouldWork();

//    void setAndGetJogRPM_shouldWork();
//    void setAndGetMoveRPM_shouldWork();
//    void setRPM_outOfRange_shouldFail();

//    // 位置移动相关
//    void prepareMoveParameters_shouldDecomposeCorrectly();
//    void sendMoveCommand_shouldWriteCorrectRegisters();
//    void testFullPositionMove_AbsoluteAndRelative();
    void testJog_StartAndStop();
};

#endif // TEST_P100SMOTOR_H
