#include "pch.h"

#include "event_queue.h"

namespace {
int callbackCount;

void countCallback(void*) {
	callbackCount++;
}

void fillQueue(EventQueue& queue, size_t count) {
	for (size_t i = 0; i < count; i++) {
		queue.insertTask(nullptr, 100 + i, countCallback);
		ASSERT_EQ(queue.size(), static_cast<int>(i + 1));
	}
}

struct ReentrantContext {
	EventQueue* Queue;
	std::vector<int>* Order;
};

void recordReentrant(void* argument) {
	auto* order = static_cast<std::vector<int>*>(argument);
	order->push_back(3);
}

void recordClose(void* argument) {
	auto* context = static_cast<ReentrantContext*>(argument);
	context->Order->push_back(2);
}

void recordOpenAndScheduleAgain(void* argument) {
	auto* context = static_cast<ReentrantContext*>(argument);
	context->Order->push_back(1);
	context->Queue->insertTask(nullptr, 0, {recordReentrant, context->Order});
	EXPECT_EQ(context->Queue->size(), 2);
}
} // namespace

TEST(SchedulerBatch, PoolCapacityIsReservedAllOrNothing) {
	ScheduledAction pair[] = {
			{1000, countCallback},
			{1001, countCallback},
	};
	ScheduledAction triple[] = {
			{1000, countCallback},
			{1001, countCallback},
			{1002, countCallback},
	};

	{
		EventQueue queue;
		fillQueue(queue, 61);
		EXPECT_TRUE(queue.insertBatch(triple, efi::size(triple)));
		EXPECT_EQ(queue.size(), 64);
	}
	{
		EventQueue queue;
		fillQueue(queue, 62);
		EXPECT_TRUE(queue.insertBatch(pair, efi::size(pair)));
		EXPECT_EQ(queue.size(), 64);
	}
	{
		EventQueue queue;
		fillQueue(queue, 62);
		EXPECT_FALSE(queue.insertBatch(triple, efi::size(triple)));
		EXPECT_EQ(queue.size(), 62);

		callbackCount = 0;
		EXPECT_EQ(queue.executeAll(2000), 62);
		EXPECT_EQ(callbackCount, 62);
		EXPECT_EQ(queue.size(), 0);
		EXPECT_TRUE(queue.insertBatch(triple, efi::size(triple)));
		EXPECT_EQ(queue.size(), 3);
	}
	{
		EventQueue queue;
		fillQueue(queue, 63);
		EXPECT_FALSE(queue.insertBatch(pair, efi::size(pair)));
		EXPECT_EQ(queue.size(), 63);
	}
	{
		EventQueue queue;
		fillQueue(queue, 64);
		EXPECT_FALSE(queue.insertBatch(pair, 1));
		EXPECT_EQ(queue.size(), 64);
	}
}

TEST(SchedulerBatch, DueOpenCanScheduleReentrantlyAfterCloseWasReserved) {
	EventQueue queue;
	std::vector<int> order;
	ReentrantContext context{&queue, &order};
	ScheduledAction injection[] = {
			{0, {recordOpenAndScheduleAgain, &context}},
			{0, {recordClose, &context}},
	};

	ASSERT_TRUE(queue.insertBatch(injection, efi::size(injection)));
	ASSERT_EQ(queue.size(), 2);
	EXPECT_EQ(queue.executeAll(0), 3);
	EXPECT_EQ(order, (std::vector<int>{1, 2, 3}));
	EXPECT_EQ(queue.size(), 0);
}

TEST(SchedulerBatch, ValidatorRejectsPartialOrAmbiguousBatches) {
	const efitick_t now = 1000;
	ScheduledAction valid[] = {
			{now + 1, countCallback},
			{now + 2, countCallback},
	};
	EXPECT_TRUE(isScheduleBatchValid(valid, efi::size(valid), now));
	EXPECT_FALSE(isScheduleBatchValid(nullptr, efi::size(valid), now));
	EXPECT_FALSE(isScheduleBatchValid(valid, 0, now));
	EXPECT_FALSE(isScheduleBatchValid(valid, MaxScheduleBatchSize + 1, now));

	ScheduledAction nullAction[] = {{now + 1, {}}};
	EXPECT_FALSE(isScheduleBatchValid(nullAction, efi::size(nullAction), now));

	ScheduledAction unsorted[] = {
			{now + 2, countCallback},
			{now + 1, countCallback},
	};
	EXPECT_FALSE(isScheduleBatchValid(unsorted, efi::size(unsorted), now));

	ScheduledAction tooFar[] = {{now + US2NT(MaximumScheduleDelayUs), countCallback}};
	EXPECT_FALSE(isScheduleBatchValid(tooFar, efi::size(tooFar), now));
}
