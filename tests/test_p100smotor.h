#ifndef TEST_P100SMOTOR_H
#define TEST_P100SMOTOR_H

#include <QObject>

class P100SMotorTest : public QObject
{
    Q_OBJECT

private slots:
    void setZero_shouldResetRevolutionsToZero();
};

#endif // TEST_P100SMOTOR_H
