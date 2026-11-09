#include <thread>

#include <gtest/gtest.h>

#include "core/bounded_queue.h"

namespace torustiq {

TEST(BoundedQueueTest, FifoOrder) {
    BoundedQueue<int> q(4);
    EXPECT_TRUE(q.Push(1));
    EXPECT_TRUE(q.Push(2));
    EXPECT_EQ(q.Pop(), 1);
    EXPECT_EQ(q.Pop(), 2);
}

TEST(BoundedQueueTest, CloseDrainsThenEnds) {
    BoundedQueue<int> q(4);
    q.Push(1);
    q.Close();
    EXPECT_FALSE(q.Push(2));
    EXPECT_EQ(q.Pop(), 1);
    EXPECT_EQ(q.Pop(), std::nullopt);
}

TEST(BoundedQueueTest, AbortDiscardsPending) {
    BoundedQueue<int> q(4);
    q.Push(1);
    q.Abort();
    EXPECT_EQ(q.Pop(), std::nullopt);
}

TEST(BoundedQueueTest, PushBlocksWhenFullUntilPop) {
    BoundedQueue<int> q(1);
    q.Push(1);
    std::thread producer([&] { EXPECT_TRUE(q.Push(2)); });
    EXPECT_EQ(q.Pop(), 1);
    producer.join();
    EXPECT_EQ(q.Pop(), 2);
}

TEST(BoundedQueueTest, AbortUnblocksProducer) {
    BoundedQueue<int> q(1);
    q.Push(1);
    std::thread producer([&] { EXPECT_FALSE(q.Push(2)); });
    q.Abort();
    producer.join();
}

TEST(BoundedQueueTest, ManyProducersOneConsumer) {
    BoundedQueue<int> q(8);
    constexpr int kPerProducer = 1000;
    std::vector<std::thread> producers;
    for (int p = 0; p < 4; ++p) {
        producers.emplace_back([&] {
            for (int i = 0; i < kPerProducer; ++i) {
                q.Push(1);
            }
        });
    }
    std::thread closer([&] {
        for (auto& t : producers) {
            t.join();
        }
        q.Close();
    });
    int sum = 0;
    while (auto v = q.Pop()) {
        sum += *v;
    }
    closer.join();
    EXPECT_EQ(sum, 4 * kPerProducer);
}

} // namespace torustiq
