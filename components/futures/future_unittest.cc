/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/futures/future.h"

#include <memory>
#include <string>
#include <tuple>

#include "base/task/thread_pool.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace futures {

class FutureTest : public testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
};

TEST_F(FutureTest, ValueSentInFutureTurn) {
  int value = 0;
  MakeReadyFuture(10).AndThen(
      base::BindLambdaForTesting([&value](int v) { value = v; }));
  EXPECT_EQ(value, 0);
  task_environment_.RunUntilIdle();
  EXPECT_EQ(value, 10);
}

TEST_F(FutureTest, CompleteCallbacksExecutedInFutureTurn) {
  Promise<int> promise;
  int value = 0;
  promise.GetFuture().AndThen(
      base::BindLambdaForTesting([&value](int v) { value = v; }));
  promise.SetValue(1);
  EXPECT_EQ(value, 0);
  task_environment_.RunUntilIdle();
  EXPECT_EQ(value, 1);
}

TEST_F(FutureTest, Transform) {
  double value = 0;

  MakeReadyFuture(1)
      .Transform(
          base::BindOnce([](int v) { return static_cast<double>(v) / 2; }))
      .AndThen(base::BindLambdaForTesting([&value](double v) { value = v; }));

  EXPECT_EQ(value, 0);
  task_environment_.RunUntilIdle();
  EXPECT_EQ(value, 0.5);
}

TEST_F(FutureTest, AndThen) {
  bool value = false;

  MakeReadyFuture(42)
      .AndThen(base::BindOnce([](int value) { return MakeReadyFuture(true); }))
      .AndThen(base::BindLambdaForTesting([&value](bool v) { value = v; }));

  EXPECT_FALSE(value);
  task_environment_.RunUntilIdle();
  EXPECT_TRUE(value);
}

TEST_F(FutureTest, GetValueSynchronously) {
  auto value = MakeReadyFuture(1).GetValueSynchronously();
  ASSERT_TRUE(value);
  EXPECT_EQ(*value, 1);
}

TEST_F(FutureTest, VoidFutures) {
  Promise<void> promise;
  bool called = false;
  promise.GetFuture().AndThen(
      base::BindLambdaForTesting([&]() { called = true; }));
  EXPECT_FALSE(called);
  promise.SetValueWithSideEffects();
  EXPECT_TRUE(called);
}

TEST_F(FutureTest, MakeReadyFuture) {
  int value = 0;
  MakeReadyFuture(1).AndThen(
      base::BindLambdaForTesting([&value](int v) { value = v; }));
  EXPECT_EQ(value, 0);
  task_environment_.RunUntilIdle();
  EXPECT_EQ(value, 1);
}

TEST_F(FutureTest, MakeFuture1) {
  int value = 0;

  MakeFuture<int>([&](auto resolve) {
    std::move(resolve).Run(42);
  }).AndThen(base::BindLambdaForTesting([&](int v) { value = v; }));

  task_environment_.RunUntilIdle();
  EXPECT_EQ(value, 42);
}

TEST_F(FutureTest, MakeFuture0) {
  bool called = false;

  MakeFuture([&](auto resolve) {
    std::move(resolve).Run();
  }).AndThen(base::BindLambdaForTesting([&]() { called = true; }));

  task_environment_.RunUntilIdle();
  EXPECT_TRUE(called);
}

TEST_F(FutureTest, MakeFutureVoid) {
  bool called = false;

  MakeFuture<void>([&](auto resolve) {
    std::move(resolve).Run();
  }).AndThen(base::BindLambdaForTesting([&]() { called = true; }));

  task_environment_.RunUntilIdle();
  EXPECT_TRUE(called);
}

TEST_F(FutureTest, MakeFutureN) {
  std::tuple<int, bool, double> values = {0, false, 0.0};

  MakeFuture<int, bool, double>([&](auto resolve) {
    std::move(resolve).Run(42, true, 1.3);
  }).AndThen(base::BindLambdaForTesting([&](std::tuple<int, bool, double> v) {
    values = v;
  }));

  task_environment_.RunUntilIdle();
  EXPECT_EQ(std::get<0>(values), 42);
  EXPECT_TRUE(std::get<1>(values));
  EXPECT_EQ(std::get<2>(values), 1.3);
}

TEST_F(FutureTest, MakeFutureDifferentThread) {
  base::RunLoop run_loop;
  int value = 0;

  auto set_value = [&value, &run_loop](int v) {
    value = v;
    run_loop.Quit();
  };

  auto worker_sequence = base::ThreadPool::CreateSequencedTaskRunner(
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
       base::TaskShutdownBehavior::BLOCK_SHUTDOWN});

  auto worker = [](base::OnceCallback<void(int)> callback) {
    std::move(callback).Run(42);
  };

  auto future = MakeFuture<int>([&](auto callback) {
    worker_sequence->PostTask(FROM_HERE,
                              base::BindOnce(worker, std::move(callback)));
  });

  future.AndThen(base::BindLambdaForTesting(set_value));
  run_loop.Run();

  EXPECT_EQ(value, 42);
}

Future<int> DoAsyncWork() {
  int value = co_await MakeReadyFuture(42);
  co_return value * 2;
}

TEST_F(FutureTest, CoroutineFreeFunction) {
  int value = 0;
  DoAsyncWork().AndThen(base::BindLambdaForTesting([&](int v) { value = v; }));
  task_environment_.RunUntilIdle();
  EXPECT_EQ(value, 84);
}

Future<int> DoAsyncWorkInt(std::unique_ptr<int> i) {
  int value = co_await MakeReadyFuture(42);
  co_return value*(*i);
}

TEST_F(FutureTest, CoroutineFreeFunctionArg) {
  int value = 0;
  DoAsyncWorkInt(std::make_unique<int>(2))
      .AndThen(base::BindLambdaForTesting([&](int v) { value = v; }));
  task_environment_.RunUntilIdle();
  EXPECT_EQ(value, 84);
}

Future<void> DoAsyncWorkVoid() {
  co_await MakeReadyFuture();
}

Future<void> Delay(base::TimeDelta delta) {
  return MakeFuture<void>([&](auto callback) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, std::move(callback), delta);
  });
}

TEST_F(FutureTest, CoroutineFreeVoidFunction) {
  bool called = false;
  DoAsyncWorkVoid().AndThen(
      base::BindLambdaForTesting([&]() { called = true; }));
  task_environment_.RunUntilIdle();
  EXPECT_TRUE(called);
}

struct HasAsync {
  auto GetWeakPtr() { return weak_factory_.GetWeakPtr(); }
  auto GetWeakPtr() const { return weak_factory_.GetWeakPtr(); }

  Future<int> DoAsyncWork() {
    co_await Delay(base::Milliseconds(10));
    co_return 16;
  }

  Future<void> DoAsyncWorkConst() const { co_await MakeReadyFuture(42); }

  base::WeakPtrFactory<HasAsync> weak_factory_{this};
};

TEST_F(FutureTest, CoroutineMemberFunction) {
  int value = 0;

  HasAsync instance;
  instance.DoAsyncWork().AndThen(
      base::BindLambdaForTesting([&](int v) { value = v; }));

  EXPECT_EQ(value, 0);
  task_environment_.FastForwardBy(base::Milliseconds(11));
  EXPECT_EQ(value, 16);
}

TEST_F(FutureTest, CoroutineClassDestroyed) {
  bool called = false;

  {
    HasAsync instance;
    instance.DoAsyncWork().AndThen(
        base::BindLambdaForTesting([&](int) { called = true; }));
  }

  task_environment_.FastForwardBy(base::Milliseconds(11));
  EXPECT_FALSE(called);
}

TEST_F(FutureTest, CoroutineConstMemberFunction) {
  int called = false;

  const HasAsync instance;
  instance.DoAsyncWorkConst().AndThen(
      base::BindLambdaForTesting([&]() { called = true; }));

  EXPECT_FALSE(called);
  task_environment_.RunUntilIdle();
  EXPECT_TRUE(called);
}

Future<void> DoAsyncWorkWithReceiver(HasAsync& receiver) {
  co_await MakeReadyFuture(42);
}

TEST_F(FutureTest, CoroutineExplicitReceiver) {
  bool called = false;

  HasAsync instance;
  DoAsyncWorkWithReceiver(instance).AndThen(
      base::BindLambdaForTesting([&]() { called = true; }));

  task_environment_.RunUntilIdle();
  EXPECT_TRUE(called);
}

struct Empty {};

Future<void> DoAsyncWorkWithEmptyObject(int i, const Empty& empty) {
  co_await MakeReadyFuture(i);
}

Future<void> DoAsyncWorkWithEmptyObjectPtr(int i, const Empty* empty) {
  co_await MakeReadyFuture(i);
}

TEST_F(FutureTest, CoroutineEmptyObjectRef) {
  bool called = false;

  Empty empty;
  DoAsyncWorkWithEmptyObject(42, empty).AndThen(
      base::BindLambdaForTesting([&]() { called = true; }));

  task_environment_.RunUntilIdle();
  EXPECT_TRUE(called);
}

TEST_F(FutureTest, CoroutineEmptyObjectPtr) {
  bool called = false;

  Empty empty;
  DoAsyncWorkWithEmptyObjectPtr(42, &empty)
      .AndThen(base::BindLambdaForTesting([&]() { called = true; }));

  task_environment_.RunUntilIdle();
  EXPECT_TRUE(called);
}

TEST_F(FutureTest, AsyncLambda) {
  int value = 0;

  auto fn = [](double d) -> Future<int> {
    int val = co_await MakeReadyFuture(42);
    co_return val + 2 + static_cast<int>(d);
  };

  fn(1.0).AndThen(base::BindLambdaForTesting([&](int val) { value = val; }));

  task_environment_.RunUntilIdle();
  EXPECT_EQ(value, 45);
}

TEST_F(FutureTest, AsyncLambdaZeroArg) {
  int value = 0;

  auto fn = []() -> Future<int> {
    int val = co_await MakeReadyFuture(42);
    co_return val + 2;
  };

  fn().AndThen(base::BindLambdaForTesting([&](int val) { value = val; }));

  task_environment_.RunUntilIdle();
  EXPECT_EQ(value, 44);
}

TEST_F(FutureTest, CoroutineCancelToken) {
  class CancelToken {
   public:
    auto GetWeakPtr() const { return weak_factory_.GetWeakPtr(); }

   private:
    base::WeakPtrFactory<CancelToken> weak_factory_{this};
  };

  class CancelController {
   public:
    void Cancel() { token_.emplace(); }
    const CancelToken& token() const { return *token_; }

   private:
    std::optional<CancelToken> token_{std::in_place};
  };

  auto fn = [](base::RepeatingCallback<void(int)> callback,
               const CancelToken&) -> Future<void> {
    co_await Delay(base::Milliseconds(10));
    callback.Run(1);
    co_await Delay(base::Milliseconds(10));
    callback.Run(2);
  };

  int last_i = 0;
  auto callback = base::BindLambdaForTesting([&](int i) { last_i = i; });

  CancelController cancel_controller;

  fn(callback, cancel_controller.token());

  task_environment_.FastForwardBy(base::Milliseconds(11));
  EXPECT_EQ(last_i, 1);
  cancel_controller.Cancel();
  task_environment_.FastForwardBy(base::Milliseconds(11));
  EXPECT_EQ(last_i, 1);
}

}  // namespace futures
