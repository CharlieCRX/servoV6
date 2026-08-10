// ============================================================================
// test_plc_vnext_smoke.cpp —— Step 0 冒烟测试
// ============================================================================
// 目的：验证独立构建通道与 gtest_discover_tests(TEST_PREFIX "plc_vnext.") 自
//       "第一天起可执行"。本文件不承载任何业务断言（业务测试自 Step 1 起加入）。
// ============================================================================
#include <gtest/gtest.h>

namespace plc_vnext {

// 该用例保证 plc_vnext_tests 至少被发现一个 "plc_vnext." 开头的具体测试，
// 满足验收判据：ctest -N 能列出以 plc_vnext. 开头的测试。
TEST(PlcVnextSmoke, BuildChannelIsUsable) {
    EXPECT_TRUE(true);
}

}  // namespace plc_vnext
