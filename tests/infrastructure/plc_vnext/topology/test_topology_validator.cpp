// ============================================================================
// test_topology_validator.cpp —— Step 6 topology: TopologyValidator
// ============================================================================
// 对应 6.1 红用例：ValidTopology_Passes / MagicMismatch /
//   SchemaVersionUnsupported / InvalidSlotInValidRole / DuplicateSlot_AcrossValidRoles /
//   InvalidGroupCode / ReservedFieldNonZero / SecondGroupValid_IsNotRejected
// ============================================================================
#include <algorithm>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "infrastructure/plc_vnext/topology/TopologyDecoder.h"
#include "infrastructure/plc_vnext/topology/TopologyValidator.h"
#include "tests/infrastructure/plc_vnext/support/TopologyFixture.h"

namespace plc_vnext::topology {
namespace {

contracts::TopologySnapshot makeValidSnapshot() {
    auto regs = test::makeValidTopologyRegisters();
    std::string diag;
    auto snap = TopologyDecoder::decode(regs, diag);
    EXPECT_TRUE(snap.has_value()) << diag;
    return *snap;
}

bool hasIssue(const std::vector<TopologyIssue>& issues, TopologyIssueKind kind) {
    return std::any_of(issues.begin(), issues.end(),
                       [kind](const TopologyIssue& i) { return i.kind == kind; });
}

TEST(TopologyValidatorTest, ValidTopology_Passes) {
    EXPECT_TRUE(TopologyValidator::validate(makeValidSnapshot()).empty());
}

TEST(TopologyValidatorTest, MagicMismatch) {
    auto snap = makeValidSnapshot();
    snap.header.magic = static_cast<int32_t>(0xDEADBEEFu);
    auto issues = TopologyValidator::validate(snap);
    ASSERT_EQ(issues.size(), 1u);
    EXPECT_EQ(issues[0].kind, TopologyIssueKind::MagicMismatch);
}

TEST(TopologyValidatorTest, SchemaVersionUnsupported) {
    auto snap = makeValidSnapshot();
    snap.header.schemaVersion = 2;
    auto issues = TopologyValidator::validate(snap);
    ASSERT_EQ(issues.size(), 1u);
    EXPECT_EQ(issues[0].kind, TopologyIssueKind::UnsupportedSchema);
}

TEST(TopologyValidatorTest, InvalidSlotInValidRole) {
    auto snap = makeValidSnapshot();
    snap.groups[0].roles[0].plcAxisIndex = 20;  // 越界 0..15
    auto issues = TopologyValidator::validate(snap);
    ASSERT_EQ(issues.size(), 1u);
    EXPECT_EQ(issues[0].kind, TopologyIssueKind::SlotOutOfRange);
}

TEST(TopologyValidatorTest, DuplicateSlot_AcrossValidRoles) {
    auto snap = makeValidSnapshot();
    // Role[0] 已占用 slot 0；把 Role[1] 也设为有效且占用 slot 0
    snap.groups[0].roles[1].valid = true;
    snap.groups[0].roles[1].plcAxisIndex = 0;
    auto issues = TopologyValidator::validate(snap);
    EXPECT_TRUE(hasIssue(issues, TopologyIssueKind::DuplicateSlot));
}

TEST(TopologyValidatorTest, InvalidGroupCode) {
    auto snap = makeValidSnapshot();
    snap.groups[1].groupCode = 5;  // 须等于组下标 1
    auto issues = TopologyValidator::validate(snap);
    EXPECT_TRUE(hasIssue(issues, TopologyIssueKind::GroupCodeMismatch));
}

TEST(TopologyValidatorTest, ReservedFieldNonZero) {
    auto snap = makeValidSnapshot();
    snap.header.reserved = 1;  // 当前 schema=1 要求 Reserved 写 0
    auto issues = TopologyValidator::validate(snap);
    EXPECT_TRUE(hasIssue(issues, TopologyIssueKind::ReservedFieldUnexpected));
}

TEST(TopologyValidatorTest, SecondGroupValid_IsNotRejected) {
    auto snap = makeValidSnapshot();
    snap.groups[1].valid = true;  // 第二组有效本身不得构成客户端错误
    auto issues = TopologyValidator::validate(snap);
    EXPECT_TRUE(issues.empty());
}

}  // namespace
}  // namespace plc_vnext::topology
