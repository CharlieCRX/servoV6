#ifndef TEST_P100SMOTOR_H
#define TEST_P100SMOTOR_H

#include <QObject>

class P100SMotorTest : public QObject
{
    Q_OBJECT

private slots:
    void positionAndReset_shouldWork();

    void setAndGetJogRPM_shouldWork();
    void setAndGetMoveRPM_shouldWork();
    void setRPM_outOfRange_shouldFail();
};

#endif // TEST_P100SMOTOR_H
