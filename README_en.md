Here is the text without Markdown formatting:

# simple_coro
C++20 coroutine simple helper

[中文](README.md)

## Origin
After the introduction of coroutines in C++20, I have been waiting for the standard library to provide infrastructure for coroutines. 

Unfortunately, the big shots never consider the feelings of us coders. 

So, after referencing many articles, I crafted this simple "coroutine" library that I find useful for myself. 

It might not be everyone's cup of tea due to its basic functionality, but it serves its purpose well.

## How to Use
Modify an original function R (Args ...) to Task<R> (Args ...), and that function becomes a **coroutine** function.

In my implementation, Task is divided into two types: **sync_task** and **lazy_task**. 
- **sync_task**: Represents a function that runs immediately upon being called until the first suspension point.
- **lazy_task**: Represents a function that suspends immediately upon being called.

When a task is suspended, you need to use **co_await** to wait for a suspension point to return a result:
```
auto result = co_await other_lazy_func();
```

When a function contains **co_await**, it must be a **coroutine**.

### Calling Coroutine Functions in a Regular Function
If the result of the coroutine is not of interest, you can use **sync_task** to wrap the return value and avoid using **co_await** to execute the coroutine function in a regular function:
```
sync_task<void> sync_func();

void normal_func()
{
    sync_func(); // Since it's sync, it will continue execution upon entering the function. If you don't want to block, you can omit co_await.
}
```

You can also use **sync_wait** to wait:
```
void normal_func()
{
    auto task = sync_func();
    sync_wait(task);
}
```

### Wrapping an Asynchronous Function as a Coroutine Function
Here's an example similar to grpc's asynchronous callback. 

We declare a **CoEvent** object on the stack, put it into a lambda function, and then co_await this CoEvent object to achieve coroutine processing. 

When the asynchronous operation is complete and the function is called back, use resume to resume the coroutine from the suspension point:
```
sync_task<grpc::Status> Co_Read(Resp* response)
{
    GrpcCoEvent<grpc::Status> ev;
    Base::PostRead(response, [&ev](grpc::Status st) { ev.resume(st); });
    auto st = co_await ev;
    co_return st;
}
```

### Switching a Coroutine Function to Another Thread
We implemented a simple scheduler class to run tasks. 
- **coro_scheduler** simply puts tasks in a list and waits for execution. 
- **coro_scheduler_single_thread** opens a thread to wait for tasks to run:
```
void normal_func()
{
    coro_scheduler task_list;

    auto task1 = lazy_func1();
    task_list.add_task(task1);

    auto task2 = lazy_func2();
    task_list.add_task(task2);

    task_list.run_one();
}
```

### Using schedule_on to Switch to a Specific Schedule
```
struct TestST
{
    coro_scheduler_single_thread m_scheduler;

    TestST()
    {
        m_scheduler.start_thread();
    }

    ~TestST()
    {
        m_scheduler.stop_thread();
    }

    sync_task<int32_t> test_func_1()
    {
        EnterFunc enter_func(__FUNCTION__);
        auto this_coro = get_current_scheduler(); // Get the current coroutine object

        printf("before switch coro exec on %d\n", get_thread_id());
        co_await schedule_on(m_scheduler); // Switch the coroutine to the processing thread
        printf("after switch coro exec on %d \n", get_thread_id());

        co_await schedule_on(this_coro); // Switch the coroutine back
        printf("after switch coro exec on %d \n", get_thread_id());

        co_return 77;
    }
};
```

**Please be extremely cautious when dealing with coroutines that involve switching execution threads. Pay close attention to the lifecycle of coroutine function parameters.**
**Please be extremely careful when dealing with asynchronous callbacks or callbacks across threads. If the calling thread has exited before co_return, it might lead to unexpected behavior. Be sure to handle such cases properly.**

To call get_current_scheduler, you must use set_current_scheduler to set the current thread in the executing thread.


#### How to Suspend Coroutine and Continue Execution Next Time (like Generator/IEnumerable)

```cpp
lazy_task<int> test_yield()
{
    for (int i = 0; i < 10; i++)
    {
        co_yield i; // Return i and suspend the coroutine, sequentially yielding 0 to 9
    }
    co_return 10;
}

void func()
{
    auto caller = test_yield();
    while (caller.move_next())
    {
        print("%d\n", caller.get());  // Print 0 to 10
        // Suspend the coroutine and resume in the next frame
        // Continue running each frame until completion
    }
}
```

This way, you can suspend the coroutine, run it once per frame, and continue execution until completion.