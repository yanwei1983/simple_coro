#ifndef COROUTINE_HELP_H
#define COROUTINE_HELP_H

#include <atomic>
#include <condition_variable>
#include <coroutine>
#include <deque>
#include <exception>
#include <thread>
#include <type_traits>
#include <variant>

template<typename T, bool LAZY>
class task;

class coro_scheduler;
static inline coro_scheduler* get_current_scheduler();
static inline void set_current_scheduler(coro_scheduler* scheduler);

namespace task_detail
{
    enum task_result
    {
        empty     = 0,
        value     = 1,
        exception = 2,
        detached  = 3
    };

    template<typename T>
    struct result_holder
    {
        std::variant<std::monostate, std::conditional_t<std::is_void_v<T>, std::monostate, T>, std::exception_ptr, std::monostate> m_data;

        bool is_detached() const { return m_data.index() == task_result::detached; }
        bool is_set() const { return m_data.index() != std::variant_npos; }
        bool has_exception() const { return m_data.index() == task_result::exception; }

        void set_exception(std::exception_ptr e) noexcept { m_data.template emplace<task_result::exception>(std::move(e)); }
        void throw_exception() { std::rethrow_exception(std::get<task_result::exception>(m_data)); }

        template<typename U>
            requires std::convertible_to<U, T> && std::constructible_from<T, U>
        void set_value(U&& u) noexcept(std::is_nothrow_constructible_v<T, U>)
        {
            m_data.template emplace<task_result::value>(std::forward<U>(u));
        }

        void set_value_void() { m_data.template emplace<task_result::value>(); }

        void detach() { m_data.template emplace<task_result::detached>(); }
    };

    template<typename T>
    struct result : public result_holder<T>
    {
        using base = result_holder<T>;
        using base::m_data;
        template<typename U>
            requires std::convertible_to<U, T> && std::constructible_from<T, U>
        void return_value(U&& value) noexcept(std::is_nothrow_constructible_v<T, U>)
        {
            if(base::is_detached())
                return;
            base::set_value(std::forward<U>(value));
        }

        T& get_or_throw() &
        {
            if(base::has_exception())
            {
                base::throw_exception();
            }
            return get_value();
        }

        T&& get_or_throw() &&
        {
            if(base::has_exception())
            {
                base::throw_exception();
            }
            return get_value();
        }

        T&  get_value() & { return std::get<task_result::value>(m_data); }
        T&& get_value() && { return std::get<task_result::value>(std::move(m_data)); }
    };

    template<>
    struct result<void> : public result_holder<void>
    {
        void return_void() noexcept { set_value_void(); }

        void get_or_throw()
        {
            if(has_exception())
            {
                throw_exception();
            }
        }
    };

    template<typename T, bool LAZY>
    struct promise : public result<T>
    {
        using handle_type = std::coroutine_handle<promise>;

        promise() noexcept = default;

        auto get_return_object() noexcept { return task<T, LAZY>{handle_type::from_promise(*this)}; }

        void unhandled_exception() noexcept { result<T>::set_exception(std::current_exception()); }

        std::conditional_t<LAZY, std::suspend_always, std::suspend_never> initial_suspend() noexcept { return {}; }

        struct final_awaitable
        {
            bool await_ready() const noexcept { return false; }

            // After this coroutine finishes and suspends at final_suspend，
            // transfer the execution to the continuation_ (which is the
            // outer coroutine that awaits the current coroutine.)
            // For an eager run task,
            std::coroutine_handle<> await_suspend(std::coroutine_handle<promise> h) noexcept
            {
                auto& p = h.promise();
                if(p.is_detached() == false)
                {
                    if(p.m_next_coroutine)
                    {
                        return p.m_next_coroutine;
                    }
                    else
                    {
                        return std::noop_coroutine();
                    }
                }

                if(p.m_next_coroutine)
                {
                    p.m_next_coroutine.destroy();
                }

                h.destroy();
                return std::noop_coroutine();
            }

            void await_resume() noexcept {}
        };

        auto final_suspend() noexcept { return final_awaitable{}; }

        // the next coroutine to execute
        std::coroutine_handle<> m_next_coroutine;
    };


    thread_local coro_scheduler* s_scheduler = nullptr;
    inline coro_scheduler* get_current_scheduler()
    {
        return s_scheduler;
    }

    inline void set_current_scheduler(coro_scheduler* scheduler)
    {
        s_scheduler = scheduler;
    }

} // namespace task_detail

// default to a lazy task
template<typename T = void, bool LAZY = true>
class task
{
public:
    using promise_type = task_detail::promise<T, LAZY>;
    using value_type   = T;
    using handle_type  = promise_type::handle_type;

private:
    /// Handle to the current coroutine
    handle_type m_handle;

public:
    task() noexcept
        : m_handle(nullptr)
    {
    }

    explicit task(handle_type h)
        : m_handle(h)
    {
    }

    task(task&& t) noexcept
        : m_handle(t.m_handle)
    {
        t.m_handle = nullptr;
    }

    /// Disable copy construction/assignment.
    task(const task&)            = delete;
    task& operator=(const task&) = delete;

    /// Frees the coroutine stack frame.
    ~task()
    {
        if(m_handle == nullptr)
            return;

        if(m_handle.done() == false)
        {
            m_handle.promise().detach();
        }
        else
        {
            m_handle.destroy();
        }
    }

    void detach() noexcept { m_handle = nullptr; }

    task& operator=(task&& other) noexcept
    {
        if(std::addressof(other) != this)
        {
            if(m_handle)
            {
                m_handle.destroy();
            }
            m_handle       = other.m_handle;
            other.m_handle = nullptr;
        }
        return *this;
    }

    bool is_ready() const noexcept { return m_handle == nullptr || m_handle.done(); }

    decltype(auto) get() & { return m_handle.promise().get_or_throw(); }
    decltype(auto) get() && { return std::move(m_handle.promise()).get_or_throw(); }

    void resume() { m_handle.resume(); }

    struct awaitable
    {
        handle_type m_coroutine;

        awaitable(handle_type h) noexcept
            : m_coroutine(h)
        {
        }

        bool await_ready() const noexcept { return m_coroutine == nullptr || m_coroutine.done(); }

        std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) noexcept
        {
            // Set current coroutine to be able to continue the awaiting_coroutine on completion.
            m_coroutine.promise().m_next_coroutine = h;
            if constexpr(LAZY)
            {
                return m_coroutine;
            }
            else
            {
                return std::noop_coroutine();
            }
        }

        decltype(auto) await_resume() & { return this->m_coroutine.promise().get_or_throw(); }
        decltype(auto) await_resume() && { return std::move(m_coroutine.promise()).get_or_throw(); }
    };

    auto operator co_await() const& noexcept { return awaitable{m_handle}; }
    auto operator co_await() const&& noexcept { return awaitable{m_handle}; }
};

template<typename T>
using lazy_task = task<T, true>;

template<typename T>
using sync_task = task<T, false>;

// sync_task 可以直接执行, lazy_task 只能被 co_await

template<typename T>
struct CoEventBase
{
    sync_task<T>::handle_type m_awaiting;
    bool                      m_ready = false;

    T    await_resume() { return m_awaiting.promise().get_or_throw(); }
    bool await_ready() { return m_ready; }
    void await_suspend(sync_task<T>::handle_type awaiting) { m_awaiting = awaiting; }
};

template<typename T>
struct CoEvent : public CoEventBase<T>
{
    using CoEventBase<T>::m_awaiting;
    using CoEventBase<T>::m_ready;
    template<typename U>
        requires std::convertible_to<U, T> && std::constructible_from<T, U>
    void resume(U&& value) noexcept(std::is_nothrow_constructible_v<T, U>)
    {
        m_ready = true;
        if(m_awaiting && !m_awaiting.done())
        {
            m_awaiting.promise().set_value(std::forward<U>(value));
            m_awaiting.resume();
        }
    }
};

template<>
struct CoEvent<void> : public CoEventBase<void>
{
    void resume()
    {
        m_ready = true;
        if(m_awaiting && !m_awaiting.done())
        {
            m_awaiting.promise().set_value_void();
            m_awaiting.resume();
        }
    }
};

class coro_scheduler
{
public:
    struct schedule_operation
    {
        coro_scheduler* m_scheduler;
        schedule_operation(coro_scheduler* scheduler)
            : m_scheduler(scheduler)
        {
        }

        bool await_ready() { return false; }

        void await_suspend(std::coroutine_handle<> awaiting) 
        { 
            if(m_scheduler != nullptr)
            {
                m_scheduler->add_task(awaiting); 
            }
        }

        void await_resume() {}
    };

    schedule_operation schedule() { return schedule_operation{this}; }

    void schedule(std::coroutine_handle<> coro) 
    { 
        add_task(coro); 
    }

    virtual void add_task(std::coroutine_handle<> coro) 
    { 
        std::unique_lock<std::mutex> lock(m_mutex);
        m_task_list.push_back(coro); 
    }

    virtual void run_once()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        auto task_list_copy = m_task_list;
        m_task_list.clear();
        lock.unlock();
        
        while(task_list_copy.empty() == false)
        {
            auto coro = task_list_copy.front();
            task_list_copy.pop_front();
            coro.resume();
        }
    }

    void run_until_empty()
    {
        while(true)
        {
            run_once();
            std::unique_lock<std::mutex> lock(m_mutex);
            if(m_task_list.empty())
            {
                break;
            }
        }
    }

protected:
    std::mutex                   m_mutex;

    std::deque<std::coroutine_handle<>> m_task_list;
};

class coro_scheduler_single_thread : public coro_scheduler
{
public:
    coro_scheduler_single_thread() {}

    ~coro_scheduler_single_thread() { stop_thread(); }

    void start_thread()
    {
        m_stop_thread = false;
        m_thread      = std::make_unique<std::thread>(
            [this]()
            {
                set_current_scheduler(this);
                while(!m_stop_thread)
                {
                    run_once();                    
                }

                //exit
                
            });
    }

    virtual void run_once() override
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        if(m_task_list.empty() == true)
        {
            if(m_stop_thread)
            {
                return;
            }
            m_cond.wait(lock);

            if(m_task_list.empty() == true)
            {
                return;
            }
        }

        auto coro = m_task_list.front();
        m_task_list.pop_front();
        lock.unlock();
        coro.resume();
    }

    virtual void add_task(std::coroutine_handle<> coro) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_task_list.push_back(coro);
        m_cond.notify_one();
    }

    void stop_thread()
    {
        if(m_thread)
        {
            m_stop_thread = true;
            m_cond.notify_one();

            m_thread->join();
            m_thread.reset();
        }
    }

protected:
    std::unique_ptr<std::thread> m_thread;

    std::condition_variable      m_cond;
    std::atomic<bool>            m_stop_thread = false;
    
};

static inline coro_scheduler* get_current_scheduler()
{
    return task_detail::get_current_scheduler();
}

static inline void set_current_scheduler(coro_scheduler* scheduler)
{
    task_detail::set_current_scheduler(scheduler);
}

static inline auto schedule_on(coro_scheduler& scheduler)
{
    return scheduler.schedule();
}

static inline auto schedule_on(coro_scheduler* scheduler)
{
    if(scheduler != nullptr)
    {
        return scheduler->schedule();
    }
    else
    {
        printf("schedule_on nullptr\n");
        return coro_scheduler::schedule_operation{nullptr};
    }
}

template<typename Task_t>
static inline sync_task<void> schedule_on(coro_scheduler& scheduler, Task_t&& task)
{
    co_await schedule_on(scheduler);
    co_await std::forward<Task_t>(task);
    co_return;
}

static inline void sync_wait(coro_scheduler& scheduler)
{
    return scheduler.run_until_empty();
}

template<typename Task_t>
static inline void sync_wait(Task_t&& task)
{
    auto scheduler = get_current_scheduler();
    if(scheduler != nullptr)
    {
        schedule_on(*scheduler, std::forward<Task_t>(task));
        scheduler->run_until_empty();
    }
    else
    {
        coro_scheduler new_scheduler;
        schedule_on(new_scheduler, std::forward<Task_t>(task));
        new_scheduler.run_until_empty();
    }
    
}

#endif /* COROUTINE_HELP_H */
