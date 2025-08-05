#include "test_p100smotor.h"

#include <QTest>
#include <QDebug>

#include "MotorRegisterAccessor.h"
#include "SerialCommProtocol.h"
#include "P100S/P100SMotor.h"
// 进行本测试之前，需要手动将 P100S 的伺服控制器设置为如下：
/*  从地址 PA71 = 5
 *  电子齿轮比 = 131072 / 10800
 */
//void P100SMotorTest::positionAndReset_shouldWork()
//{
//    // ✅ 初始化串口协议
//    SerialCommProtocol protocol;
//    QVERIFY2(protocol.open("COM10", true), "串口打开失败");

//    // ✅ 创建寄存器访问器和电机对象
//    MotorRegisterAccessor accessor(&protocol);
//    const int motorID = 5;
//    P100SMotor motor(motorID, &accessor);

//    // ✅ 获取当前圈数
//    double revBeforeReset = motor.getCurrentRevolutions();
//    qDebug() << "归零前圈数:" << revBeforeReset;

//    // ✅ 设置当前位置为 0
//    QVERIFY2(motor.setCurrentPositionAsZero(), "设置当前位置为零失败");

//    // ✅ 再次读取圈数
//    double revAfterReset = motor.getCurrentRevolutions();
//    qDebug() << "归零后圈数:" << revAfterReset;

//    // ✅ 断言圈数接近 0（允许浮点误差）
//    QVERIFY2(qAbs(revAfterReset) < 0.01, "归零后圈数不为 0");

//    protocol.close(); // 主动关闭串口
//}


//void P100SMotorTest::setAndGetJogRPM_shouldWork()
//{
//    // 初始化串口协议
//    SerialCommProtocol protocol;
//    QVERIFY2(protocol.open("COM10", true), "串口打开失败");

//    // 创建寄存器访问器和电机对象
//    MotorRegisterAccessor accessor(&protocol);
//    const int motorID = 5;
//    P100SMotor motor(motorID, &accessor);

//    // 设置点动转速
//    const int testJogRPM = 50;
//    QVERIFY2(motor.setJogRPM(testJogRPM), "设置点动转速失败");

//    // 获取点动转速，断言正确
//    int readJogRPM = motor.getJogRPM();
//    qDebug() << "设置后点动转速:" << readJogRPM;
//    QCOMPARE(readJogRPM, testJogRPM);

//    protocol.close(); // 主动关闭串口
//}

//void P100SMotorTest::setAndGetMoveRPM_shouldWork()
//{
//    // 初始化串口协议
//    SerialCommProtocol protocol;
//    QVERIFY2(protocol.open("COM10", true), "串口打开失败");

//    // 创建寄存器访问器和电机对象
//    MotorRegisterAccessor accessor(&protocol);
//    const int motorID = 5;
//    P100SMotor motor(motorID, &accessor);

//    // 设置位置移动转速
//    const int testMoveRPM = 150;
//    QVERIFY2(motor.setMoveRPM(testMoveRPM), "设置位置移动转速失败");

//    // 获取位置移动转速，断言正确
//    int readMoveRPM = motor.getMoveRPM();
//    qDebug() << "设置后位置移动转速:" << readMoveRPM;
//    QCOMPARE(readMoveRPM, testMoveRPM);

//    protocol.close(); // 主动关闭串口
//}

//void P100SMotorTest::setRPM_outOfRange_shouldFail()
//{
//    SerialCommProtocol protocol;
//    QVERIFY2(protocol.open("COM10", true), "串口打开失败");

//    MotorRegisterAccessor accessor(&protocol);
//    const int motorID = 5;
//    P100SMotor motor(motorID, &accessor);

//    QVERIFY(!motor.setJogRPM(-1));
//    QVERIFY(!motor.setJogRPM(7000));
//    QVERIFY(!motor.setMoveRPM(-10));
//    QVERIFY(!motor.setMoveRPM(7000));

//    protocol.close();
//}

//void P100SMotorTest::prepareMoveParameters_shouldDecomposeCorrectly() {
//    // 创建一个P100SMotor对象，但我们不需要实际的通信
//    P100SMotor motor(1, nullptr);

//    // --- 测试用例1: 整数圈数和正向小数 ---
//    double targetRev1 = 3.25;
//    uint16_t outRevolutions1, outPulses1;
//    QVERIFY(motor.prepareMoveParameters(targetRev1, outRevolutions1, outPulses1));
//    QCOMPARE(outRevolutions1, 3u);
//    QCOMPARE(outPulses1, static_cast<uint32_t>(0.25 * 10800));

//    // --- 测试用例2: 纯整数圈数 ---
//    double targetRev2 = 10.0;
//    uint16_t outRevolutions2, outPulses2;
//    QVERIFY(motor.prepareMoveParameters(targetRev2, outRevolutions2, outPulses2));
//    QCOMPARE(outRevolutions2, 10u);
//    QCOMPARE(outPulses2, 0u);

//    // --- 测试用例3: 负数圈数和负向小数 ---
//    double targetRev3 = -1.53524687;
//    uint16_t outRevolutions3, outPulses3;
//    QVERIFY(motor.prepareMoveParameters(targetRev3, outRevolutions3, outPulses3));
//    int16_t revolutions_signed = static_cast<int16_t>(outRevolutions3);
//    int16_t pulses_signed = static_cast<int16_t>(outPulses3);

//    QCOMPARE(revolutions_signed, -1);
//    QCOMPARE(pulses_signed, static_cast<int16_t>(-0.53524687 * 10800));

//    // --- 测试用例4: 负数纯整数 ---
//    double targetRev4 = -5.0;
//    uint16_t outRevolutions4, outPulses4;
//    QVERIFY(motor.prepareMoveParameters(targetRev4, outRevolutions4, outPulses4));
//    int16_t revolutions_signed4 = static_cast<int16_t>(outRevolutions4);
//    int16_t pulses_signed4 = static_cast<int16_t>(outPulses4);
//    QCOMPARE(revolutions_signed4, -5);
//    QCOMPARE(pulses_signed4, 0);
//}

//void P100SMotorTest::sendMoveCommand_shouldWriteCorrectRegisters()
//{
//    // --- 步骤 1: 准备测试环境 ---
//    SerialCommProtocol protocol;
//    QVERIFY(protocol.open("COM10", true));
//    MotorRegisterAccessor accessor(&protocol);
//    const int motorID = 5;
//    P100SMotor motor(motorID, &accessor);

//    // --- 步骤 2: 定义测试参数与执行 ---

//    // --- 测试用例1: 正常正值 ---
//    const uint16_t testRevolutions1 = 3;
//    const uint16_t testPulses1 = 2700;

//    QVERIFY2(motor.sendMoveCommand(testRevolutions1, testPulses1), "Positive Test failed!");

//    // --- 步骤 3: 验证写入的寄存器值 ---
//    uint16_t readRevolutions;
//    uint16_t readPulses;
//    uint16_t readTrigger;

//    // 验证 P4-2 (目标圈数)
//    QVERIFY(accessor.readReg16(motorID, P100SMotor::P4_2_SetMultiTurns, readRevolutions));
//    QCOMPARE(readRevolutions, testRevolutions1);

//    // 验证 P4-3 (圈内脉冲)
//    QVERIFY(accessor.readReg16(motorID, P100SMotor::P4_3_SetInnerPulse, readPulses));
//    QCOMPARE(readPulses, testPulses1);

//    // 验证 P3-31 (触发信号)
//    QVERIFY(accessor.readReg16(motorID, P100SMotor::P3_31_VirtualInput, readTrigger));
//    QCOMPARE(readTrigger, 0x0001u);

//    // --- 测试用例2: 负值测试 ---
//    const int16_t testRevolutions2_signed = -1;
//    const int16_t testPulses2_signed = -5400;

//    QVERIFY2(motor.sendMoveCommand(static_cast<uint16_t>(testRevolutions2_signed),
//                                   static_cast<uint16_t>(testPulses2_signed)), "Negative Test failed!");

//    // 验证 P4-2 (目标圈数) 的负值写入
//    QVERIFY(accessor.readReg16(motorID, P100SMotor::P4_2_SetMultiTurns, readRevolutions));
//    QCOMPARE(static_cast<int16_t>(readRevolutions), testRevolutions2_signed);

//    // 验证 P4-3 (圈内脉冲) 的负值写入
//    QVERIFY(accessor.readReg16(motorID, P100SMotor::P4_3_SetInnerPulse, readPulses));
//    QCOMPARE(static_cast<int16_t>(readPulses), testPulses2_signed);

//    // --- 步骤 4: 清理 ---
//    protocol.close();
//}

//void P100SMotorTest::testFullPositionMove_AbsoluteAndRelative()
//{
//    // --- 步骤 1: 准备测试环境 ---
//    SerialCommProtocol protocol;
//    QVERIFY(protocol.open("COM10", true));

//    MotorRegisterAccessor accessor(&protocol);
//    const int motorID = 5;
//    P100SMotor motor(motorID, &accessor);

//    // --- 步骤 2: 初始化和使能 ---
//    // 设置移动速度，并验证是否成功写入
//    const int testRPM = 100;
//    QVERIFY(motor.setMoveRPM(testRPM));
//    QCOMPARE(motor.getMoveRPM(), testRPM);

//    // 使能电机
//    QVERIFY(motor.enable());

//    // 强制将当前位置设置为零，确保测试起点一致
//    QVERIFY(motor.setCurrentPositionAsZero());

//    // --- 步骤 3: 绝对位置移动测试 ---
//    // 定义公差为 1/360 圈 (约等于 1 度)
//    const double tolerance = 1.0 / 360.0;
//    const double absoluteTargetRev = 1.568; // 目标移动到 1 圈

//    // 设置绝对位置
//    QVERIFY(motor.setAbsoluteTargetRevolutions(absoluteTargetRev));

//    // 触发移动并等待完成
//    QVERIFY2(motor.triggerMove(), "Absolute move failed to trigger or complete!");

//    // 验证最终位置是否接近目标位置，公差为 1 度
//    double currentRevolutions1 = motor.getCurrentRevolutions();
//    QVERIFY2(qAbs(currentRevolutions1 - absoluteTargetRev) <= tolerance,
//             "Absolute position verification failed! Current position is not within 1 degree tolerance.");

//    // --- 步骤 4: 相对位置移动测试 ---
//    const double relativeDeltaRev = -2.365; // 从当前位置再移动 1 圈
//    const double expectedFinalRev = absoluteTargetRev + relativeDeltaRev; // 预期最终位置

//    // 设置相对位置
//    QVERIFY(motor.setRelativeTargetRevolutions(relativeDeltaRev));

//    // 触发移动并等待完成
//    QVERIFY2(motor.triggerMove(), "Relative move failed to trigger or complete!");

//    // 验证最终位置是否接近预期位置，公差为 1 度
//    double currentRevolutions2 = motor.getCurrentRevolutions();
//    QVERIFY2(qAbs(currentRevolutions2 - expectedFinalRev) <= tolerance,
//             "Relative position verification failed! Current position is not within 1 degree tolerance.");

//    // --- 步骤 5: 清理 ---
//    protocol.close();
//}


void P100SMotorTest::testJog_StartAndStop()
{
    // --- 步骤 1: 准备测试环境 ---
    SerialCommProtocol protocol;
    QVERIFY(protocol.open("COM10", true));

    MotorRegisterAccessor accessor(&protocol);
    const int motorID = 5;
    P100SMotor motor(motorID, &accessor);
    QVERIFY(motor.initEnvironment());

    // --- 步骤 2: 初始化和使能 ---
    QVERIFY(motor.enable());
    QVERIFY(motor.setJogRPM(50));
    QVERIFY(motor.setCurrentPositionAsZero());

    // --- 步骤 3: 启动正向点动测试 ---
    qWarning("Starting positive jog test. The motor will move for 2 seconds.");
    QVERIFY2(motor.startPositiveRPMJog(), "Failed to start positive jog!");

    QThread::msleep(2000);

    double positionAfterStart = motor.getCurrentRevolutions();
    QVERIFY2(qAbs(positionAfterStart) > 0.0, "Motor did not move during positive jog.");

    // --- 步骤 4: 停止点动测试 ---
    qWarning("Stopping the jog movement. The motor should brake and stop.");
    QVERIFY2(motor.stopRPMJog(), "Failed to stop jog!");

    QThread::msleep(1000); // 假设停止过程需要 1 秒

    double positionAfterStop1 = motor.getCurrentRevolutions();
    QThread::msleep(500);
    double positionAfterStop2 = motor.getCurrentRevolutions();

    const double tolerance = 0.001;
    QVERIFY2(qAbs(positionAfterStop2 - positionAfterStop1) <= tolerance,
             "Motor position continued to change after jog stop!");

    // --- 步骤 5: 启动负向点动测试 ---
    qWarning("Starting negative jog test. The motor will move in the opposite direction for 2 seconds.");
    QVERIFY2(motor.startNegativeRPMJog(), "Failed to start negative jog!");

    QThread::msleep(2000);

    double positionAfterNegativeJog = motor.getCurrentRevolutions();
    QVERIFY2(positionAfterNegativeJog < positionAfterStop2, "Motor did not move in the negative direction.");

    // --- 步骤 6: 再次停止点动并清理 ---
    qWarning("Stopping the negative jog movement.");
    QVERIFY2(motor.stopRPMJog(), "Failed to stop negative jog!");

    QThread::msleep(1000);

    double finalPosition1 = motor.getCurrentRevolutions();
    QThread::msleep(500);
    double finalPosition2 = motor.getCurrentRevolutions();
    QVERIFY2(qAbs(finalPosition2 - finalPosition1) <= tolerance,
             "Motor position continued to change after final jog stop!");

    protocol.close();
}
