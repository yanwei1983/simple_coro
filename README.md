# simple_coro
c++ 20 coroutine simaple helper

[english](README_en.md)

## 由来
其实呢在c++20出现协程之后, 我一直在等待标准库出现coroutine的基建设施

可惜的是大老爷们,从来不考虑我们这些码农的心情

所以参考了很多文章,堆出这个我自己用着还行,别人不见的会喜欢,而且功能简陋的协程"库"

因为代码比较少,希望对你构建你自己的协程"库",有一定的帮助


## 如何使用

将一个原始的函数 R (Args ...) 修改为 Task<R> (Args ...) 则该函数即为协程函数**coroutine**

Task在我这里分为两种:**sync_task**和l**azy_task**
- **sync_task** 代表函数被调用时, 先运行,直到第一个挂起点
- **lazy_task** 代表函数被调用时, 直接挂起

当一个task被挂起后, 需要使用**co_await**来等待一个挂起点返回结果

```
auto result = co_await other_lazy_func();
```

当一个函数中出现了**co_await**,那么该函数就必是一个**coroutine**


#### 如何在一个普通函数中调用协程函数
如果我们不关心返回协程的结果,那么可以用**sync_task**来包裹返回值,不去**co_await**,就可在普通函数中执行协程函数

```
sync_task<void> sync_func();

void normal_func()
{
    sync_func(); //因为是sync,所以进入函数后,会继续执行,如果不想阻塞,可以不用co_await
}

```


当然也可以使用**sync_wait**来等待
```
void normal_func()
{
    auto task = sync_func();
    sync_wait(task);
}

```


#### 如何将一个异步函数包裹为一个协程函数

这里我们以类似grpc的异步回调来举例, 我们现在栈上申明了一个**CoEvent**对象

把这个对象放入lambda函数中, 然后co_await这个CoEvent对象,来完成协程化处理

当异步操作完成时, 回调该函数时, 通过resume来将将协程从挂起点恢复继续执行

```

sync_task<grpc::Status> Co_Read(Resp* response)
{
    GrpcCoEvent<grpc::Status> ev;
    Base::PostRead(response, [&ev](grpc::Status st) { ev.resume(st); });
    auto st = co_await ev;
    co_return st;
}

```


#### 如何将一个协程函数切换到另外线程上处理

我们实现了简单的scheduler类来将task放入其中运行

- **coro_scheduler** 简单的将task放入列表中等待执行
- **coro_scheduler_single_thread** 开一条线程,去等待task运行

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

#### 使用schedule_on来切换到某个schedule上
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
        auto this_coro = get_current_scheduler(); //获取当前协程对象

        printf("before switch coro exec on %d\n", get_thread_id());
        co_await schedule_on(m_scheduler); //将协程切换到处理线程上
        printf("after switch coro exec on %d \n", get_thread_id());

        co_await schedule_on(this_coro); //将协程切换回来
        printf("after switch coro exec on %d \n", get_thread_id());

        co_return 77;
    }

};

```
**请一定要小心这种切换执行线程的协程, 协程函数的入参请注意生命周期**
**请一定要小心处理这种异步回调,或者跨线程回调的情况, 如果co_return的时候,调用线程已经嗝屁了, 应该是会在处理线程完成co_return**

要调用 get_current_scheduler, 必须要使用 set_current_scheduler在执行线程中设置当前线程

#### 如何运行到一半挂起,等待下次运行继续呢(generator/IEnumerable)

```
lazy_task<int> test_yield()
{
    for(int i = 0; i < 10; i++)
    {
        co_yield i; //返回i,并挂起本协程 依次返回0~9
    }
    co_return 10;
}

void func()
{
    auto caller = test_yield();
    while(caller.move_next()) 
    {
        print("%d\n", caller.get());  //打印0~10
    }
}

```
这样你就可以在中途先挂起本协程, 然后每帧运行一次,直到运行结束