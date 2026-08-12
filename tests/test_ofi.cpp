#include "obme/OrderFlowImbalance.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

namespace obme {
namespace {

// Feeds a two-sided L1 snapshot to establish "previous" state; ignores result.
void prime(OrderFlowImbalance& ofi, Price bpx, Quantity bq, Price apx, Quantity aq) {
    ofi.update(true, bpx, bq, true, apx, aq);
}

TEST(OFI, FirstUpdateIsInvalid) {
    OrderFlowImbalance ofi;
    auto s = ofi.update(true, 100, 10, true, 101, 10);
    EXPECT_FALSE(s.valid);
    EXPECT_EQ(s.l1, 0);
    EXPECT_DOUBLE_EQ(s.mid, 100.5);
}

TEST(OFI, SamePricesUsesQueueDeltas) {
    OrderFlowImbalance ofi;
    prime(ofi, 100, 10, 101, 10);
    auto s = ofi.update(true, 100, 15, true, 101, 8);
    ASSERT_TRUE(s.valid);
    // e^b = 15 - 10 = 5 ; e^a = 8 - 10 = -2 ; OFI = 5 - (-2) = 7
    EXPECT_EQ(s.l1, 7);
    EXPECT_EQ(s.deep, 7);  // single tracked level -> deep == l1
    EXPECT_DOUBLE_EQ(s.mid, 100.5);
}

TEST(OFI, BidImprovesCountsFullNewQueue) {
    OrderFlowImbalance ofi;
    prime(ofi, 99, 10, 101, 10);
    auto s = ofi.update(true, 100, 12, true, 101, 10);  // bid price up
    ASSERT_TRUE(s.valid);
    EXPECT_EQ(s.l1, 12);  // e^b = 12 (whole new queue), e^a = 0
}

TEST(OFI, BidFallsVacatesOldQueue) {
    OrderFlowImbalance ofi;
    prime(ofi, 100, 10, 101, 10);
    auto s = ofi.update(true, 99, 7, true, 101, 10);  // bid price down
    ASSERT_TRUE(s.valid);
    EXPECT_EQ(s.l1, -10);  // e^b = -10 (old queue vacated), e^a = 0
}

TEST(OFI, AskImprovesCountsFullNewQueue) {
    OrderFlowImbalance ofi;
    prime(ofi, 100, 10, 101, 10);
    auto s = ofi.update(true, 100, 10, true, 100, 6);  // ask price down (improves)
    ASSERT_TRUE(s.valid);
    EXPECT_EQ(s.l1, -6);  // e^b = 0, e^a = 6 -> OFI = -6
}

TEST(OFI, AskRisesVacatesOldQueue) {
    OrderFlowImbalance ofi;
    prime(ofi, 100, 10, 101, 10);
    auto s = ofi.update(true, 100, 10, true, 102, 8);  // ask price up
    ASSERT_TRUE(s.valid);
    EXPECT_EQ(s.l1, 10);  // e^b = 0, e^a = -10 -> OFI = 10
}

TEST(OFI, OneSidedSnapshotIsInvalid) {
    OrderFlowImbalance ofi;
    prime(ofi, 100, 10, 101, 10);
    auto s = ofi.update(true, 100, 10, false, 0, 0);  // ask side empty
    EXPECT_FALSE(s.valid);
    EXPECT_EQ(s.l1, 0);
    EXPECT_TRUE(std::isnan(s.mid));
    auto s2 = ofi.update(true, 100, 10, true, 101, 10);
    EXPECT_FALSE(s2.valid);  // prev was one-sided
    auto s3 = ofi.update(true, 100, 12, true, 101, 10);
    EXPECT_TRUE(s3.valid);
    EXPECT_EQ(s3.l1, 2);  // e^b = 12-10 = 2, e^a = 0
}

TEST(OFI, ResetClearsHistory) {
    OrderFlowImbalance ofi;
    prime(ofi, 100, 10, 101, 10);
    ofi.reset();
    auto s = ofi.update(true, 100, 15, true, 101, 10);
    EXPECT_FALSE(s.valid);  // no history after reset
}

// ---- Multi-level (deep) OFI ------------------------------------------------

TEST(OFI, DeepSumsContributionsAcrossLevels) {
    OrderFlowImbalance ofi(2);
    // Prime with two levels per side.
    ofi.update({100, 99}, {10, 10}, {101, 102}, {10, 10});
    // Level 0: bid qty 10->13 same px -> e^b=3, ask same -> e^a=0 -> +3.
    // Level 1: bid px 99->98 (down) -> e^b=-10; ask qty 10->4 same px -> e^a=-6 -> -10-(-6)=-4.
    auto s = ofi.update({100, 98}, {13, 5}, {101, 102}, {10, 4});
    ASSERT_TRUE(s.valid);
    ASSERT_EQ(s.per_level.size(), 2u);
    EXPECT_EQ(s.per_level[0], 3);
    EXPECT_EQ(s.per_level[1], -4);
    EXPECT_EQ(s.l1, 3);
    EXPECT_EQ(s.deep, -1);  // 3 + (-4)
}

TEST(OFI, DeepIgnoresLevelsMissingOnEitherSide) {
    OrderFlowImbalance ofi(3);
    // Previous had 2 levels; current has 2 levels. Level 2 never present -> 0.
    ofi.update({100, 99}, {10, 10}, {101, 102}, {10, 10});
    auto s = ofi.update({100, 99}, {12, 10}, {101, 102}, {10, 10});
    ASSERT_TRUE(s.valid);
    ASSERT_EQ(s.per_level.size(), 3u);
    EXPECT_EQ(s.per_level[0], 2);  // bid 10->12 same px
    EXPECT_EQ(s.per_level[1], 0);  // unchanged
    EXPECT_EQ(s.per_level[2], 0);  // absent both snapshots
    EXPECT_EQ(s.deep, 2);
}

TEST(OFI, L1OfiUnaffectedByDepthSetting) {
    // Level-0 OFI must equal the L1 result regardless of how many levels tracked.
    OrderFlowImbalance l1(1);
    OrderFlowImbalance deep(5);
    l1.update({100}, {10}, {101}, {10});
    deep.update({100, 99}, {10, 5}, {101, 102}, {10, 5});
    auto s1 = l1.update({100}, {14}, {101}, {10});
    auto sd = deep.update({100, 99}, {14, 5}, {101, 102}, {10, 5});
    EXPECT_EQ(s1.l1, sd.l1);  // both 4
    EXPECT_EQ(s1.l1, 4);
}

}  // namespace
}  // namespace obme
