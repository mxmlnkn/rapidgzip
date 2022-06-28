#pragma once

#include <condition_variable>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "JoiningThread.hpp"


/**
 * Function evaluations can be given to a ThreadPool instance,
 * which assigns the evaluation to one of its threads to be evaluated in parallel.
 */
class ThreadPool
{
private:
    /**
     * A small type-erasure function wrapper for non-copyable function objects with no function arguments.
     *
     * std::function<void()> won't work to wrap std::packaged_task
     * because the former requires a copy-constructible object, which the latter is not.
     * @see http://www.open-std.org/jtc1/sc22/wg21/docs/lwg-defects.html#1287
     *
     * By upcasting from a templated specialized derived class to this base class, the template type is "erased".
     * However, @ref BaseFunctor can't be directly used with very much correctly because the default
     * operations like copy, move and so on won't work correctly with the members in the derived class when it
     * is called from the base class type.
     * For this reason, this FunctionWrapper creates a unique_ptr of @ref BaseFunctor.
     */
    class PackagedTaskWrapper
    {
    private:
        struct BaseFunctor
        {
            virtual void
            operator()() = 0;

            virtual ~BaseFunctor() = default;
        };

        template<class T_Functor>
        struct SpecializedFunctor :
            BaseFunctor
        {
            explicit
            SpecializedFunctor( T_Functor&& functor ) :
                m_functor( std::move( functor ) )
            {}

            void
            operator()() override
            {
                m_functor();
            }

        private:
            T_Functor m_functor;
        };

    public:
        template<class T_Functor>
        explicit
        PackagedTaskWrapper( T_Functor&& functor ) :
            m_impl( std::make_unique<SpecializedFunctor<T_Functor> >( std::move( functor ) ) )
        {}

        void
        operator()()
        {
            ( *m_impl )();
        }

    private:
        std::unique_ptr<BaseFunctor> m_impl;
    };

public:
    explicit
    ThreadPool( size_t nThreads = std::thread::hardware_concurrency() )
    {
        for ( size_t i = 0; i < nThreads; ++i ) {
            m_threads.emplace_back( JoiningThread( &ThreadPool::workerMain, this ) );
        }
    }

    ~ThreadPool()
    {
        stop();
    }

    void
    stop()
    {
        {
            std::lock_guard lock( m_mutex );
            m_threadPoolRunning = false;
            m_pingWorkers.notify_all();
        }
        m_threads.clear();
    }

    /**
     * Any function taking no arguments and returning any argument may be submitted to be executed.
     * The returned future can be used to access the result when it is really needed.
     */
    template<class T_Functor>
    std::future<decltype( std::declval<T_Functor>()() )>
    submitTask( T_Functor task )
    {
        std::lock_guard lock( m_mutex );

        /* Use a packaged task, which abstracts handling the return type and makes the task return void. */
        using ReturnType = decltype( std::declval<T_Functor>()() );
        std::packaged_task<ReturnType()> packagedTask{ task };
        auto resultFuture = packagedTask.get_future();
        m_tasks.emplace_back( std::move( packagedTask ) );

        m_pingWorkers.notify_one();

        return resultFuture;
    }

    [[nodiscard]] size_t
    size() const
    {
        return m_threads.size();
    }

    [[nodiscard]] size_t
    unprocessedTasksCount() const
    {
        std::lock_guard lock( m_mutex );
        return m_tasks.size();
    }

private:
    void
    workerMain()
    {
        while ( m_threadPoolRunning )
        {
            std::unique_lock<std::mutex> tasksLock( m_mutex );
            m_pingWorkers.wait( tasksLock, [this] () { return !m_tasks.empty() || !m_threadPoolRunning; } );

            if ( !m_threadPoolRunning ) {
                break;
            }

            if ( !m_tasks.empty() ) {
                auto task = std::move( m_tasks.front() );
                m_tasks.pop_front();
                tasksLock.unlock();
                task();
            }
        }
    }

private:
    std::atomic<bool> m_threadPoolRunning = true;
    std::deque<PackagedTaskWrapper> m_tasks;
    /** necessary for m_tasks AND m_pingWorkers or else the notify_all might go unnoticed! */
    mutable std::mutex m_mutex;
    std::condition_variable m_pingWorkers;

    /**
     * Should come last so that it's lifetime is the shortest, i.e., there is no danger for the other
     * members to not yet be constructed or be already destructed while a task is still running.
     */
    std::vector<JoiningThread> m_threads;
};
