#include <gtest/gtest.h>
#include "infrastructure/FakeGantryEventBus.h"

class FakeGantryEventBusTest : public ::testing::Test {
protected:
    FakeGantryEventBus bus;
};

// TS3.3.1: publish 追加到列表
TEST_F(FakeGantryEventBusTest, PublishAppendsToPublishedList) {
    auto event = GantryEvents::Event::alarmRaised("X1");

    bus.publish(event);

    ASSERT_EQ(bus.publishedEvents().size(), 1u);
    EXPECT_EQ(bus.publishedEvents()[0].type, GantryEvents::Type::AlarmRaised);
    EXPECT_EQ(bus.publishedEvents()[0].description, "Alarm raised on X1");
}

// TS3.3.2: publishAll 保持顺序
TEST_F(FakeGantryEventBusTest, PublishAllPreservesOrder) {
    std::vector<GantryEvents::Event> events = {
        GantryEvents::Event::coupled(),
        GantryEvents::Event::alarmRaised("X1")
    };

    bus.publishAll(events);

    ASSERT_EQ(bus.publishedEvents().size(), 2u);
    EXPECT_EQ(bus.publishedEvents()[0].type, GantryEvents::Type::Coupled);
    EXPECT_EQ(bus.publishedEvents()[1].type, GantryEvents::Type::AlarmRaised);
}

// TS3.3.3: clear 清空列表
TEST_F(FakeGantryEventBusTest, ClearEmptiesPublishedList) {
    bus.publish(GantryEvents::Event::coupled());
    bus.publish(GantryEvents::Event::decoupled("test"));
    ASSERT_EQ(bus.publishedEvents().size(), 2u);

    bus.clear();

    EXPECT_EQ(bus.publishedEvents().size(), 0u);
}
