#pragma once

#include <atomic>

#if defined(_WIN32)
//---------------------------------------------------------
// Semaphore (Windows)
//---------------------------------------------------------

#include <windows.h>
#undef min
#undef max

class Semaphore
{
private:
	HANDLE m_hSema;

	Semaphore(const Semaphore& other) = delete;
	Semaphore& operator=(const Semaphore& other) = delete;

public:
	Semaphore(int initialCount = 0)
	{
		m_hSema = CreateSemaphore(NULL, initialCount, MAXLONG, NULL);
	}

	~Semaphore()
	{
		CloseHandle(m_hSema);
	}

	void wait()
	{
		WaitForSingleObject(m_hSema, INFINITE);
	}

	void signal(int count = 1)
	{
		ReleaseSemaphore(m_hSema, count, NULL);
	}
};


#elif defined(__MACH__)
//---------------------------------------------------------
// Semaphore (Apple iOS and OSX)
// Can't use POSIX semaphores due to http://lists.apple.com/archives/darwin-kernel/2009/Apr/msg00010.html
//---------------------------------------------------------

#include <mach/mach.h>

class Semaphore
{
private:
	semaphore_t m_sema;

	Semaphore(const Semaphore& other) = delete;
	Semaphore& operator=(const Semaphore& other) = delete;

public:
	Semaphore(int initialCount = 0)
	{
		assert(initialCount >= 0);
		semaphore_create(mach_task_self(), &m_sema, SYNC_POLICY_FIFO, initialCount);
	}

	~Semaphore()
	{
		semaphore_destroy(mach_task_self(), m_sema);
	}

	void wait()
	{
		semaphore_wait(m_sema);
	}

	void signal()
	{
		semaphore_signal(m_sema);
	}

	void signal(int count)
	{
		while (count-- > 0)
		{
			semaphore_signal(m_sema);
		}
	}
};


#elif defined(__unix__)
//---------------------------------------------------------
// Semaphore (POSIX, Linux)
//---------------------------------------------------------

#include <semaphore.h>

class Semaphore
{
private:
	sem_t m_sema;

	Semaphore(const Semaphore& other) = delete;
	Semaphore& operator=(const Semaphore& other) = delete;

public:
	Semaphore(int initialCount = 0)
	{
		sem_init(&m_sema, 0, initialCount);
	}

	~Semaphore()
	{
		sem_destroy(&m_sema);
	}

	void wait()
	{
		// http://stackoverflow.com/questions/2013181/gdb-causes-sem-wait-to-fail-with-eintr-error
		int rc;
		do
		{
			rc = sem_wait(&m_sema);
		} while (rc == -1 && errno == EINTR);
	}

	void signal()
	{
		sem_post(&m_sema);
	}

	void signal(int count)
	{
		while (count-- > 0)
		{
			sem_post(&m_sema);
		}
	}
};


#else

#error Unsupported platform!

#endif

class LightweightSemaphore
{
private:
	std::atomic<int> m_count;
	Semaphore m_sema;

	void waitWithPartialSpinning()
	{
		int oldCount;
		// Is there a better way to set the initial spin count?
		// If we lower it to 1000, testBenaphore becomes 15x slower on my Core i7-5930K Windows PC,
		// as threads start hitting the kernel semaphore.
		int spin = 10000;
		while (spin--)
		{
			oldCount = m_count.load(std::memory_order_relaxed);
			if ((oldCount > 0) && m_count.compare_exchange_strong(oldCount, oldCount - 1, std::memory_order_acquire))
				return;
			std::atomic_signal_fence(std::memory_order_acquire);     // Prevent the compiler from collapsing the loop.
		}
		oldCount = m_count.fetch_sub(1, std::memory_order_acquire);
		if (oldCount <= 0)
		{
			m_sema.wait();
		}
	}

public:
	LightweightSemaphore(int initialCount = 0) : m_count(initialCount)
	{
	}

	bool tryWait()
	{
		int oldCount = m_count.load(std::memory_order_relaxed);
		return (oldCount > 0 && m_count.compare_exchange_strong(oldCount, oldCount - 1, std::memory_order_acquire));
	}

	void wait()
	{
		if (!tryWait())
			waitWithPartialSpinning();
	}

	void signal(const int count = 1)
	{
		const int old_count = m_count.fetch_add(count, std::memory_order_release);
		const int to_release = -old_count < count ? -old_count : count;
		if (to_release > 0)
		{
			m_sema.signal(to_release);
		}
	}
};

class auto_reset_event {
	// m_status == 1: Event object is signaled.
	// m_status == 0: Event object is reset and no threads are waiting.
	// m_status == -N: Event object is reset and N threads are waiting.
	std::atomic<int> m_status_;
	LightweightSemaphore m_sema_;

public:
	auto_reset_event() : m_status_(0) {
	}

	void signal(const int count = 1) {
		int old_status = m_status_.load(std::memory_order_relaxed);
		for (;;) {    // Increment m_status atomically via CAS loop.
		
			const int new_status = old_status < 1 ? old_status + 1 : 1;
			
			if (m_status_.compare_exchange_weak(old_status, new_status, std::memory_order_release, std::memory_order_relaxed))
				break;
			// The compare-exchange failed, likely because another thread changed m_status.
			// oldStatus has been updated. Retry the CAS loop.
		}

		if (old_status < 0)
			m_sema_.signal(count);    // Release one waiting thread.
	}

	void wait() {
		const int old_status = m_status_.fetch_sub(1, std::memory_order_acquire);

		if (old_status < 1) {
			m_sema_.wait();
		}
	}
};