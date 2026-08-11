#include "obme/OrderFlowImbalance.hpp"

#include <gtest/gtest.h>

#include <cmath>

namespace obme {
namespace {

// Feeds a two-sided snapshot to establish "previous" state; ignores the sample.
void prime(OrderFlowImbalance& ofi, Price bpx, Quantity bq, Price apx, Quantity aq) {
    ofi.update(true, bpx, bq, true, apx, aq);
}

TEST(OFI, FirstUpdateIsInvalid) {
    OrderFlowImbalance ofi;
    auto s = ofi.update(true, 100, 10, true, 101, 10);
    EXPECT_FALSE(s.valid);
    EXPECT_EQ(s.ofi, 0);
    EXPECT_DOUBLE_EQ(s.mid, 100.5);
}

TEST(OFI, SamePricesUsesQueueDeltas) {
    OrderFlowImbalance ofi;
    prime(ofi, 100, 10, 101, 10);
    auto s = ofi.update(true, 100, 15, true, 101, 8);
    ASSERT_TRUE(s.valid);
    // e^b = 15 - 10 = 5 ; e^a = 8 - 10 = -2 ; OFI = 5 - (-2) = 7
    EXPECT_EQ(s.ofi, 7);
    EXPECT_DOUBLE_EQ(s.mid, 100.5);
}

TEST(OFI, BidImprovesCountsFullNewQueue) {
    OrderFlowImbalance ofi;
    prime(ofi, 99, 10, 101, 10);
    auto s = ofi.update(true, 100, 12, true, 101, 10);  // bid price up
    ASSERT_TRUE(s.valid);
    // e^b = 12 (whole new bid queue) ; e^a = 10 - 10 = 0 ; OFI = 12
    EXPECT_EQ(s.ofi, 12);
}

TEST(OFI, BidFallsVacatesOldQueue) {
    OrderFlowImbalance ofi;
    prime(ofi, 100, 10, 101, 10);
    auto s = ofi.update(true, 99, 7, true, 101, 10);  // bid price down
    ASSERT_TRUE(s.valid);
    // e^b = -10 (old bid queue vacated) ; e^a = 0 ; OFI = -10
    EXPECT_EQ(s.ofi, -10);
}

TEST(OFI, AskImprovesCountsFullNewQueue) {
    OrderFlowImbalance ofi;
    prime(ofi, 100, 10, 101, 10);
    auto s = ofi.update(true, 100, 10, true, 100, 6);  // ask price down (improves)
    ASSERT_TRUE(s.valid);
    // e^b = 0 ; e^a = 6 (whole new ask queue) ; OFI = 0 - 6 = -6
    EXPECT_EQ(s.ofi, -6);
}

TEST(OFI, AskRisesVacatesOldQueue) {
    OrderFlowImbalance ofi;
    prime(ofi, 100, 10, 101, 10);
    auto s = ofi.update(true, 100, 10, true, 102, 8);  // ask price up
    ASSERT_TRUE(s.valid);
    // e^b = 0 ; e^a = -10 (old ask queue vacated) ; OFI = 0 - (-10) = 10
    EXPECT_EQ(s.ofi, 10);
}

TEST(OFI, OneSidedSnapshotIsInvalid) {
    OrderFlowImbalance ofi;
    prime(ofi, 100, 10, 101, 10);
    auto s = ofi.update(true, 100, 10, false, 0, 0);  // ask side empty
    EXPECT_FALSE(s.valid);
    EXPECT_EQ(s.ofi, 0);
    EXPECT_TRUE(std::isnan(s.mid));
    // Recovering to two-sided still yields an invalid sample (prev was one-sided).
    auto s2 = ofi.update(true, 100, 10, true, 101, 10);
    EXPECT_FALSE(s2.valid);
    // But the following one is valid again.
    auto s3 = ofi.update(true, 100, 12, true, 101, 10);
    EXPECT_TRUE(s3.valid);
    EXPECT_EQ(s3.ofi, 2);  // e^b = 12-10 = 2, e^a = 0
}

TEST(OFI, ResetClearsHistory) {
    OrderFlowImbalance ofi;
    prime(ofi, 100, 10, 101, 10);
    ofi.reset();
    auto s = ofi.update(true, 100, 15, true, 101, 10);
    EXPECT_FALSE(s.valid);  // no history after reset
}

}  // namespace
}  // namespace obme
