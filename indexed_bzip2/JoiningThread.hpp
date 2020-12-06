#pragma once

#include <thread>
#include <utility>


/**
 * Similar to the planned C++20 std::jthread, this class joins in the destructor.
 */
class JoiningThread
{
public:
    template<class... T_Args>
    explicit
    JoiningThread( T_Args&&... args ) :
        m_thread( std::forward<T_Args>( args )... )
    {}

    JoiningThread( JoiningThread&& ) = default;
    JoiningThread( const JoiningThread& ) = delete;
    JoiningThread& operator=( JoiningThread&& ) = default;
    JoiningThread& operator=( const JoiningThread& ) = delete;

    ~JoiningThread()
    {
        if ( m_thread.joinable() ) {
            m_thread.join();
        }
    }

    std::thread::id
    get_id() const noexcept
    {
        return m_thread.get_id();
    }

    void
    join()
    {
        m_thread.join();
    }

private:
    std::thread m_thread;
};

