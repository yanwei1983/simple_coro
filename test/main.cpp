#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <unordered_set>
#include <cstdio>

#include "CoroutineHelp.h"

struct EnterFunc
{
    EnterFunc(const char* _func_name)
        : func_name(_func_name)
    {
        printf("Enter %s\n", func_name);
    }

    ~EnterFunc()
    {
        printf("Leave %s\n", func_name);
    }

    const char* func_name;
};


lazy_task<int32_t> test_lazy_task_1_1()
{
    EnterFunc enter_func(__FUNCTION__);
    co_return 2;
}

sync_task<int32_t> test_sync_task_1_1()
{
    EnterFunc enter_func(__FUNCTION__);
    co_return 1;
}

sync_task<void> test_sync_task_1()
{
    EnterFunc enter_func(__FUNCTION__);

    printf("start call test_lazy_task_1_1\n");   
    auto task2 = test_lazy_task_1_1();      //这里不会执行协程，只是创建了一个协程对象

    printf("start call test_sync_task_1_1\n");   
    auto task1 = test_sync_task_1_1();      //这里其实就是执行了协程, 并返回了,结果已经存入task1中

    printf("start await test_sync_task_1_1\n");    
    auto result1 = co_await task1;          //这里是从task1中取出结果
    printf("test_sync_task_1_1 result: %d\n", result1);   

    printf("start await test_lazy_task_1_1\n");   
    auto result2 = co_await task2;          //这里task2才开始执行,并返回了结果
    printf("test_lazy_task_1_1 result: %d\n", result2);

    co_return;
}

inline int32_t get_thread_id()
{
#ifdef WIN32
    return GetCurrentThreadId();
#else
    constexpr int32_t ___NR_gettid = 186;
    return (int32_t)syscall(___NR_gettid);
#endif
}


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

TestST st;

sync_task<void> test_sync_task_2()
{
    EnterFunc enter_func(__FUNCTION__);


    auto result = co_await st.test_func_1();
    printf("coro exec on %d\n", get_thread_id());

    printf("TestST.test_func_1 result: %d\n", result);

    co_return;
}

int main(int argc, char** argv)
{
    coro_scheduler task_list;
    set_current_scheduler(&task_list);
    auto task1 = test_sync_task_1();
    printf("====================================\n");
    auto task2 = test_sync_task_2();
    printf("====================================\n");



    auto lazy_task1 = test_lazy_task_1_1();
    auto lazy_task2 = test_lazy_task_1_1();

   
    schedule_on(task_list, lazy_task1);
    schedule_on(task_list, lazy_task2);

    sync_wait(task_list);
    printf("====================================\n");

    sync_wait(task2);

    while(true)
    {
        task_list.run_once();
    }



    return 0;
}