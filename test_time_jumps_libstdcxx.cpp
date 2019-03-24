#ifdef _WIN32
	#include <windows.h>
#else
	#include <sys/time.h>
#endif

#include <functional>
#include <future>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <shared_mutex>
#include <thread>

/******************************************************************************/

/*
 * Summary:
 *
 * This code tests the behavior of time-related functions in the presence of
 * system clock changes (jumps). It requires root/Administrator privileges in
 * order to run because it changes the system clock. NTP should also be disabled
 * while running this code so that NTP can't change the system clock.
 *
 * Each function to be tested is executed five times. The amount of time the
 * function waits before returning is measured against the amount of time the
 * function was expected to wait. If the difference exceeds a threshold value
 * (defined below) then the test fails.
 *
 * The following values are intentially:
 * - more than 200 milliseconds
 * - more than 200 milliseconds apart
 * - not a multiple of 100 milliseconds
 * - not a multiple of each other
 * - don't sum or diff to a multiple of 100 milliseconds
 */
const long long s_waitMs            = 580;
const long long s_shortJumpMs       = 230;
const long long s_longJumpMs        = 870;
const long long s_sleepBeforeJumpMs = 110;

#ifdef _WIN32
const long long s_maxEarlyErrorMs =  10
                                  + 100; // Windows is unpredictable, especially in a VM, so allow extra time if the function returns early
const long long s_maxLateErrorMs  = 110  // due to polling, functions may not return for up to 100 milliseconds after they are supposed to
                                  + 100; // Windows is slow, especially in a VM, so allow extra time for the functions to return
#else
const long long s_maxEarlyErrorMs =  10;
const long long s_maxLateErrorMs  = 110; // Due to polling, functions may not return for up to 100 milliseconds after they are supposed to
#endif

int g_numTestsRun    = 0;
int g_numTestsPassed = 0;
int g_numTestsFailed = 0;

/******************************************************************************/

// A custom clock based off the system clock but with a different epoch.

namespace custom
{
    class custom_std_clock
    {
    public:
        typedef std::chrono::microseconds                 duration; // intentionally not nanoseconds
        typedef duration::rep                             rep;
        typedef duration::period                          period;
        typedef std::chrono::time_point<custom_std_clock> time_point;
        static bool is_steady;

        static time_point now();
    };

    bool custom_std_clock::is_steady = false;

    custom_std_clock::time_point custom_std_clock::now()
    {
        return time_point(std::chrono::duration_cast<duration>(std::chrono::system_clock::now().time_since_epoch()) - std::chrono::hours(10 * 365 * 24));
    }
}

/******************************************************************************/

template <typename MutexType = std::mutex, typename CondType = std::condition_variable>
struct StdHelper
{
    typedef MutexType mutex;
    typedef CondType cond;

    typedef std::lock_guard<MutexType> lock_guard;
    typedef std::unique_lock<MutexType> unique_lock;

    typedef std::chrono::milliseconds milliseconds;
    typedef std::chrono::nanoseconds nanoseconds;

    typedef std::chrono::system_clock system_clock;
    typedef std::chrono::steady_clock steady_clock;
    typedef custom::custom_std_clock custom_clock;

    typedef system_clock::time_point system_time_point;
    typedef steady_clock::time_point steady_time_point;
    typedef custom_clock::time_point custom_time_point;

    typedef std::cv_status cv_status;
    typedef std::future_status future_status;

    typedef std::packaged_task<bool()> packaged_task;
    typedef std::future<bool> future;
    typedef std::shared_future<bool> shared_future;

    typedef std::thread thread;

    static const milliseconds waitDur;

    template <typename T>
    static void sleep_for(T d)
    {
        std::this_thread::sleep_for(d);
    }

    template <typename T>
    static void sleep_until(T t)
    {
        std::this_thread::sleep_until(t);
    }

    static system_time_point systemNow()
    {
        return system_clock::now();
    }

    static steady_time_point steadyNow()
    {
        return steady_clock::now();
    }

    static custom_time_point customNow()
    {
        return custom_clock::now();
    }

    template <class ToDuration, class Rep, class Period>
    static ToDuration duration_cast(const std::chrono::duration<Rep, Period>& d)
    {
        return std::chrono::duration_cast<ToDuration>(d);
    }

    static milliseconds zero()
    {
        return milliseconds(0);
    }
};

template <typename MutexType, typename CondType>
const typename StdHelper<MutexType, CondType>::milliseconds
StdHelper<MutexType, CondType>::waitDur = typename StdHelper<MutexType, CondType>::milliseconds(s_waitMs);

/******************************************************************************/

#ifdef _WIN32

void changeSystemTime(long long changeMs)
{
	Sleep(s_sleepBeforeJumpMs);

	SYSTEMTIME systemTime;
	GetSystemTime(&systemTime);

    FILETIME fileTime;
    if (!SystemTimeToFileTime(&systemTime, &fileTime))
    {
        std::cout << "ERROR: Couldn't convert system time to file time" << std::endl;
    }

    ULARGE_INTEGER largeInt;
    largeInt.LowPart  = fileTime.dwLowDateTime;
    largeInt.HighPart = fileTime.dwHighDateTime;
    largeInt.QuadPart += changeMs * 10000;
    fileTime.dwLowDateTime  = largeInt.LowPart;
    fileTime.dwHighDateTime = largeInt.HighPart;

    if (!FileTimeToSystemTime(&fileTime, &systemTime))
    {
        std::cout << "ERROR: Couldn't convert file time to system time" << std::endl;
    }

    if (!SetSystemTime(&systemTime))
    {
        std::cout << "ERROR: Couldn't set system time" << std::endl;
    }
}

#else

void changeSystemTime(long long changeMs)
{
    struct timespec sleepTs;
    sleepTs.tv_sec  = (s_sleepBeforeJumpMs / 1000);
    sleepTs.tv_nsec = (s_sleepBeforeJumpMs % 1000) * 1000000;
    nanosleep(&sleepTs, NULL);

    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0)
    {
        std::cout << "ERROR: Couldn't get system time" << std::endl;
    }

    changeMs += tv.tv_sec  * 1000;
    changeMs += tv.tv_usec / 1000;
    tv.tv_sec  = (changeMs / 1000);
    tv.tv_usec = (changeMs % 1000) * 1000;

    if (settimeofday(&tv, NULL) != 0)
    {
        std::cout << "ERROR: Couldn't set system time" << std::endl;
    }
}

#endif

enum RcEnum
{
    e_no_timeout,
    e_timeout,
    e_failed_bad,
    e_failed_good,
    e_succeeded_bad,
    e_succeeded_good,
    e_ready_bad,
    e_not_ready_good,
    e_na
};

template <typename Helper>
void checkWaitTime(typename Helper::nanoseconds expected, typename Helper::nanoseconds actual, RcEnum rc)
{
    if (expected != Helper::zero() && expected < typename Helper::milliseconds(s_sleepBeforeJumpMs))
    {
        expected = typename Helper::milliseconds(s_sleepBeforeJumpMs);
    }

    typename Helper::milliseconds expectedMs = Helper::template duration_cast<typename Helper::milliseconds>(expected);
    typename Helper::milliseconds actualMs   = Helper::template duration_cast<typename Helper::milliseconds>(actual);

    std::cout << "Expected: " << std::setw(4) << expectedMs.count() << " ms"
              << ", Actual: " << std::setw(4) << actualMs.count() << " ms"
              << ", Returned: ";
    switch (rc)
    {
        case e_no_timeout     : std::cout << "no_timeout, "; break;
        case e_timeout        : std::cout << "timeout,    "; break;
        case e_failed_bad     : std::cout << "failed,     "; break;
        case e_failed_good    : std::cout << "failed,     "; break;
        case e_succeeded_bad  : std::cout << "succeeded,  "; break;
        case e_succeeded_good : std::cout << "succeeded,  "; break;
        case e_ready_bad      : std::cout << "ready,      "; break;
        case e_not_ready_good : std::cout << "not_ready,  "; break;
        default               : std::cout << "N/A,        "; break;
    }

    if (expectedMs == Helper::zero())
    {
        std::cout << "FAILED: SKIPPED (test would lock up if run)";
        g_numTestsFailed++;
    }
    else if (actual < expected - typename Helper::milliseconds(s_maxEarlyErrorMs))
    {
        std::cout << "FAILED: TOO SHORT";
        if (rc == e_timeout) // bad
        {
            std::cout << ", RETURNED TIMEOUT";
        }
        else if (rc == e_failed_bad)
        {
            std::cout << ", RETURNED FAILED";
        }
        else if (rc == e_succeeded_bad)
        {
            std::cout << ", RETURNED SUCCEEDED";
        }
        else if (rc == e_ready_bad)
        {
            std::cout << ", RETURNED READY";
        }
        g_numTestsFailed++;
    }
    else if (actual > expected + typename Helper::milliseconds(s_maxLateErrorMs))
    {
        std::cout << "FAILED: TOO LONG";
        if (rc == e_no_timeout) // bad
        {
            std::cout << ", RETURNED NO_TIMEOUT";
        }
        else if (rc == e_failed_bad)
        {
            std::cout << ", RETURNED FAILED";
        }
        else if (rc == e_succeeded_bad)
        {
            std::cout << ", RETURNED SUCCEEDED";
        }
        else if (rc == e_ready_bad)
        {
            std::cout << ", RETURNED READY";
        }
        g_numTestsFailed++;
    }
    else if (rc == e_no_timeout) // bad
    {
        std::cout << "FAILED: RETURNED NO_TIMEOUT";
        g_numTestsFailed++;
    }
    else if (rc == e_failed_bad)
    {
        std::cout << "FAILED: RETURNED FAILED";
        g_numTestsFailed++;
    }
    else if (rc == e_succeeded_bad)
    {
        std::cout << "FAILED: RETURNED SUCCEEDED";
        g_numTestsFailed++;
    }
    else if (rc == e_ready_bad)
    {
        std::cout << "FAILED: RETURNED READY";
        g_numTestsFailed++;
    }
    else
    {
        std::cout << "Passed";
        g_numTestsPassed++;
    }
    std::cout << std::endl;

    g_numTestsRun++;
}

bool returnFalse()
{
    return false;
}

/******************************************************************************/

// Run the test in the context provided, which may be the current thread or a separate thread.
template <typename Helper, typename Context, typename Function>
void runTestInContext(Context context, Function func, const std::string name)
{
    std::cout << name << ":" << std::endl;

    {
        std::cout << "    While system clock remains stable:        ";
        context(func, 0);
    }

    {
        std::cout << "    While system clock jumps back (short):    ";
        typename Helper::thread t(std::bind(changeSystemTime, -s_shortJumpMs));
        context(func, -s_shortJumpMs);
        t.join();
    }

    {
        std::cout << "    While system clock jumps back (long):     ";
        typename Helper::thread t(std::bind(changeSystemTime, -s_longJumpMs));
        context(func, -s_longJumpMs);
        t.join();
    }

    {
        std::cout << "    While system clock jumps forward (short): ";
        typename Helper::thread t(std::bind(changeSystemTime, s_shortJumpMs));
        context(func, s_shortJumpMs);
        t.join();
    }

    {
        std::cout << "    While system clock jumps forward (long):  ";
        typename Helper::thread t(std::bind(changeSystemTime, s_longJumpMs));
        context(func, s_longJumpMs);
        t.join();
    }
}

//--------------------------------------

template <typename Helper, typename Function>
void noThreadContext(Function func, const long long jumpMs)
{
    func(jumpMs);
}

template <typename Helper, typename Function>
void threadContextWithNone(Function func, const long long jumpMs)
{
    typename Helper::thread t(std::bind(func, jumpMs));
    t.join();
}

template <typename Helper, typename Function>
void threadContextWithUnique(Function func, const long long jumpMs)
{
    typename Helper::mutex m;
    typename Helper::lock_guard g(m);
    typename Helper::thread t(std::bind(func, std::ref(m), jumpMs));
    t.join();
}

//--------------------------------------

// Run the test in the current thread.
template <typename Helper, typename Function>
void runTest(Function func, const std::string name)
{
    runTestInContext<Helper>(noThreadContext<Helper, Function>, func, name);
}

// Run the test in a separate thread.
template <typename Helper, typename Function>
void runTestWithNone(Function func, const std::string name)
{
    runTestInContext<Helper>(threadContextWithNone<Helper, Function>, func, name);
}

// Run the test in a separate thread. Pass a locked mutex to the function under test.
template <typename Helper, typename Function>
void runTestWithUnique(Function func, const std::string name)
{
    runTestInContext<Helper>(threadContextWithUnique<Helper, Function>, func, name);
}

/******************************************************************************/

// Test Sleep

template <typename Helper>
void testSleepFor(const long long jumpMs)
{
    typename Helper::steady_time_point before(Helper::steadyNow());
    Helper::sleep_for(Helper::waitDur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::waitDur, after - before, e_na);
}

template <typename Helper>
void testSleepUntilSteady(const long long jumpMs)
{
    typename Helper::steady_time_point before(Helper::steadyNow());
    Helper::sleep_until(Helper::steadyNow() + Helper::waitDur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::waitDur, after - before, e_na);
}

template <typename Helper>
void testSleepUntilSystem(const long long jumpMs)
{
    typename Helper::steady_time_point before(Helper::steadyNow());
    Helper::sleep_until(Helper::systemNow() + Helper::waitDur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::waitDur - typename Helper::milliseconds(jumpMs), after - before, e_na);
}

template <typename Helper>
void testSleepUntilCustom(const long long jumpMs)
{
    typename Helper::steady_time_point before(Helper::steadyNow());
    Helper::sleep_until(Helper::customNow() + Helper::waitDur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::waitDur - typename Helper::milliseconds(jumpMs), after - before, e_na);
}

//--------------------------------------

template <typename Helper>
void testSleepStd(const std::string& name)
{
    std::cout << std::endl;
    runTestWithNone<Helper>(testSleepFor        <Helper>, name + "::this_thread::sleep_for()");
    runTestWithNone<Helper>(testSleepUntilSteady<Helper>, name + "::this_thread::sleep_until(), steady time");
    runTestWithNone<Helper>(testSleepUntilSystem<Helper>, name + "::this_thread::sleep_until(), system time");
    runTestWithNone<Helper>(testSleepUntilCustom<Helper>, name + "::this_thread::sleep_until(), custom time");
}

template <typename Helper>
void testSleepNoThreadStd(const std::string& name)
{
    std::cout << std::endl;
    runTest<Helper>(testSleepFor        <Helper>, name + "::this_thread::sleep_for(), no thread");
    runTest<Helper>(testSleepUntilSteady<Helper>, name + "::this_thread::sleep_until(), no thread, steady time");
    runTest<Helper>(testSleepUntilSystem<Helper>, name + "::this_thread::sleep_until(), no thread, system time");
    runTest<Helper>(testSleepUntilCustom<Helper>, name + "::this_thread::sleep_until(), no thread, custom time");
}

/******************************************************************************/

// Test Condition Variable Wait

template <typename Helper>
void testCondVarWaitFor(const long long jumpMs)
{
    typename Helper::cond cv;
    typename Helper::mutex m;
    typename Helper::unique_lock g(m);

    typename Helper::steady_time_point before(Helper::steadyNow());
    bool noTimeout = (cv.wait_for(g, Helper::waitDur) == Helper::cv_status::no_timeout);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::waitDur, after - before, noTimeout ? e_no_timeout : e_timeout);
}

template <typename Helper>
void testCondVarWaitUntilSteady(const long long jumpMs)
{
    typename Helper::cond cv;
    typename Helper::mutex m;
    typename Helper::unique_lock g(m);

    typename Helper::steady_time_point before(Helper::steadyNow());
    bool noTimeout = (cv.wait_until(g, Helper::steadyNow() + Helper::waitDur) == Helper::cv_status::no_timeout);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::waitDur, after - before, noTimeout ? e_no_timeout : e_timeout);
}

template <typename Helper>
void testCondVarWaitUntilSystem(const long long jumpMs)
{
    typename Helper::cond cv;
    typename Helper::mutex m;
    typename Helper::unique_lock g(m);

    typename Helper::steady_time_point before(Helper::steadyNow());
    bool noTimeout = (cv.wait_until(g, Helper::systemNow() + Helper::waitDur) == Helper::cv_status::no_timeout);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::waitDur - typename Helper::milliseconds(jumpMs), after - before, noTimeout ? e_no_timeout : e_timeout);
}

template <typename Helper>
void testCondVarWaitUntilCustom(const long long jumpMs)
{
    typename Helper::cond cv;
    typename Helper::mutex m;
    typename Helper::unique_lock g(m);

    typename Helper::steady_time_point before(Helper::steadyNow());
    bool noTimeout = (cv.wait_until(g, Helper::customNow() + Helper::waitDur) == Helper::cv_status::no_timeout);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::waitDur - typename Helper::milliseconds(jumpMs), after - before, noTimeout ? e_no_timeout : e_timeout);
}

//--------------------------------------

template <typename Helper>
void testCondVarStd(const std::string& name)
{
    std::cout << std::endl;
    runTestWithNone<Helper>(testCondVarWaitFor        <Helper>, name + "::wait_for()");
    runTestWithNone<Helper>(testCondVarWaitUntilSteady<Helper>, name + "::wait_until(), steady time");
    runTestWithNone<Helper>(testCondVarWaitUntilSystem<Helper>, name + "::wait_until(), system time");
    runTestWithNone<Helper>(testCondVarWaitUntilCustom<Helper>, name + "::wait_until(), custom time");
}

/******************************************************************************/

// Test Condition Variable Wait with Predicate

template <typename Helper>
void testCondVarWaitForPred(const long long jumpMs)
{
    typename Helper::cond cv;
    typename Helper::mutex m;
    typename Helper::unique_lock g(m);

    typename Helper::steady_time_point before(Helper::steadyNow());
    bool noTimeout = cv.wait_for(g, Helper::waitDur, returnFalse);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::waitDur, after - before, noTimeout ? e_no_timeout : e_timeout);
}

template <typename Helper>
void testCondVarWaitUntilPredSteady(const long long jumpMs)
{
    typename Helper::cond cv;
    typename Helper::mutex m;
    typename Helper::unique_lock g(m);

    typename Helper::steady_time_point before(Helper::steadyNow());
    bool noTimeout = cv.wait_until(g, Helper::steadyNow() + Helper::waitDur, returnFalse);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::waitDur, after - before, noTimeout ? e_no_timeout : e_timeout);
}

template <typename Helper>
void testCondVarWaitUntilPredSystem(const long long jumpMs)
{
    typename Helper::cond cv;
    typename Helper::mutex m;
    typename Helper::unique_lock g(m);

    typename Helper::steady_time_point before(Helper::steadyNow());
    bool noTimeout = cv.wait_until(g, Helper::systemNow() + Helper::waitDur, returnFalse);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::waitDur - typename Helper::milliseconds(jumpMs), after - before, noTimeout ? e_no_timeout : e_timeout);
}

template <typename Helper>
void testCondVarWaitUntilPredCustom(const long long jumpMs)
{
    typename Helper::cond cv;
    typename Helper::mutex m;
    typename Helper::unique_lock g(m);

    typename Helper::steady_time_point before(Helper::steadyNow());
    bool noTimeout = cv.wait_until(g, Helper::customNow() + Helper::waitDur, returnFalse);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::waitDur - typename Helper::milliseconds(jumpMs), after - before, noTimeout ? e_no_timeout : e_timeout);
}

//--------------------------------------

template <typename Helper>
void testCondVarPredStd(const std::string& name)
{
    std::cout << std::endl;
    runTestWithNone<Helper>(testCondVarWaitForPred        <Helper>, name + "::wait_for(), with predicate");
    runTestWithNone<Helper>(testCondVarWaitUntilPredSteady<Helper>, name + "::wait_until(), with predicate, steady time");
    runTestWithNone<Helper>(testCondVarWaitUntilPredSystem<Helper>, name + "::wait_until(), with predicate, system time");
    runTestWithNone<Helper>(testCondVarWaitUntilPredCustom<Helper>, name + "::wait_until(), with predicate, custom time");
}

/******************************************************************************/

// Test Try Lock

template <typename Helper>
void testTryLockFor(typename Helper::mutex& m, const long long jumpMs)
{
    typename Helper::steady_time_point before(Helper::steadyNow());
    bool succeeded = m.try_lock_for(Helper::waitDur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::waitDur, after - before, succeeded ? e_succeeded_bad : e_failed_good);
}

template <typename Helper>
void testTryLockUntilSteady(typename Helper::mutex& m, const long long jumpMs)
{
    typename Helper::steady_time_point before(Helper::steadyNow());
    bool succeeded = m.try_lock_until(Helper::steadyNow() + Helper::waitDur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::waitDur, after - before, succeeded ? e_succeeded_bad : e_failed_good);
}

template <typename Helper>
void testTryLockUntilSystem(typename Helper::mutex& m, const long long jumpMs)
{
    typename Helper::steady_time_point before(Helper::steadyNow());
    bool succeeded = m.try_lock_until(Helper::systemNow() + Helper::waitDur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::waitDur - typename Helper::milliseconds(jumpMs), after - before, succeeded ? e_succeeded_bad : e_failed_good);
}

template <typename Helper>
void testTryLockUntilCustom(typename Helper::mutex& m, const long long jumpMs)
{
    typename Helper::steady_time_point before(Helper::steadyNow());
    bool succeeded = m.try_lock_until(Helper::customNow() + Helper::waitDur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::waitDur - typename Helper::milliseconds(jumpMs), after - before, succeeded ? e_succeeded_bad : e_failed_good);
}

//--------------------------------------

template <typename Helper>
void testMutexStd(const std::string& name)
{
    std::cout << std::endl;
    runTestWithUnique<Helper>(testTryLockFor        <Helper>, name + "::try_lock_for()");
    runTestWithUnique<Helper>(testTryLockUntilSteady<Helper>, name + "::try_lock_until(), steady time");
    runTestWithUnique<Helper>(testTryLockUntilSystem<Helper>, name + "::try_lock_until(), system time");
    runTestWithUnique<Helper>(testTryLockUntilCustom<Helper>, name + "::try_lock_until(), custom time");
}

/******************************************************************************/

// Test Try Lock Shared

template <typename Helper>
void testTryLockSharedFor(typename Helper::mutex& m, const long long jumpMs)
{
    typename Helper::steady_time_point before(Helper::steadyNow());
    bool succeeded = m.try_lock_shared_for(Helper::waitDur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::waitDur, after - before, succeeded ? e_succeeded_bad : e_failed_good);
}

template <typename Helper>
void testTryLockSharedUntilSteady(typename Helper::mutex& m, const long long jumpMs)
{
    typename Helper::steady_time_point before(Helper::steadyNow());
    bool succeeded = m.try_lock_shared_until(Helper::steadyNow() + Helper::waitDur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::waitDur, after - before, succeeded ? e_succeeded_bad : e_failed_good);
}

template <typename Helper>
void testTryLockSharedUntilSystem(typename Helper::mutex& m, const long long jumpMs)
{
    typename Helper::steady_time_point before(Helper::steadyNow());
    bool succeeded = m.try_lock_shared_until(Helper::systemNow() + Helper::waitDur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::waitDur - typename Helper::milliseconds(jumpMs), after - before, succeeded ? e_succeeded_bad : e_failed_good);
}

template <typename Helper>
void testTryLockSharedUntilCustom(typename Helper::mutex& m, const long long jumpMs)
{
    typename Helper::steady_time_point before(Helper::steadyNow());
    bool succeeded = m.try_lock_shared_until(Helper::customNow() + Helper::waitDur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::waitDur - typename Helper::milliseconds(jumpMs), after - before, succeeded ? e_succeeded_bad : e_failed_good);
}

//--------------------------------------

template <typename Helper>
void testMutexSharedStd(const std::string& name)
{
    std::cout << std::endl;
    runTestWithUnique<Helper>(testTryLockSharedFor        <Helper>, name + "::try_lock_shared_for()");
    runTestWithUnique<Helper>(testTryLockSharedUntilSteady<Helper>, name + "::try_lock_shared_until(), steady time");
    runTestWithUnique<Helper>(testTryLockSharedUntilSystem<Helper>, name + "::try_lock_shared_until(), system time");
    runTestWithUnique<Helper>(testTryLockSharedUntilCustom<Helper>, name + "::try_lock_shared_until(), custom time");
}

/******************************************************************************/

// Test Future Wait

template <typename Helper>
void testFutureWaitFor(const long long jumpMs)
{
    typename Helper::packaged_task pt(returnFalse);
    typename Helper::future f = pt.get_future();

    typename Helper::steady_time_point before(Helper::steadyNow());
    bool timeout = (f.wait_for(Helper::waitDur) == Helper::future_status::timeout);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::waitDur, after - before, timeout ? e_timeout : e_no_timeout);
}

template <typename Helper>
void testFutureWaitUntilSteady(const long long jumpMs)
{
    typename Helper::packaged_task pt(returnFalse);
    typename Helper::future f = pt.get_future();

    typename Helper::steady_time_point before(Helper::steadyNow());
    bool timeout = (f.wait_until(Helper::steadyNow() + Helper::waitDur) == Helper::future_status::timeout);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::waitDur, after - before, timeout ? e_timeout : e_no_timeout);
}

template <typename Helper>
void testFutureWaitUntilSystem(const long long jumpMs)
{
    typename Helper::packaged_task pt(returnFalse);
    typename Helper::future f = pt.get_future();

    typename Helper::steady_time_point before(Helper::steadyNow());
    bool timeout = (f.wait_until(Helper::systemNow() + Helper::waitDur) == Helper::future_status::timeout);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::waitDur - typename Helper::milliseconds(jumpMs), after - before, timeout ? e_timeout : e_no_timeout);
}

template <typename Helper>
void testFutureWaitUntilCustom(const long long jumpMs)
{
    typename Helper::packaged_task pt(returnFalse);
    typename Helper::future f = pt.get_future();

    typename Helper::steady_time_point before(Helper::steadyNow());
    bool timeout = (f.wait_until(Helper::customNow() + Helper::waitDur) == Helper::future_status::timeout);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::waitDur - typename Helper::milliseconds(jumpMs), after - before, timeout ? e_timeout : e_no_timeout);
}

//--------------------------------------

template <typename Helper>
void testFutureStd(const std::string& name)
{
    std::cout << std::endl;
    runTestWithNone<Helper>(testFutureWaitFor        <Helper>, name + "::wait_for()");
    runTestWithNone<Helper>(testFutureWaitUntilSteady<Helper>, name + "::wait_until(), steady time");
    runTestWithNone<Helper>(testFutureWaitUntilSystem<Helper>, name + "::wait_until(), system time");
    runTestWithNone<Helper>(testFutureWaitUntilCustom<Helper>, name + "::wait_until(), custom time");
}

/******************************************************************************/

// Test Shared Future Wait

template <typename Helper>
void testSharedFutureWaitFor(const long long jumpMs)
{
    typename Helper::packaged_task pt(returnFalse);
    typename Helper::future f = pt.get_future();
    typename Helper::shared_future sf = std::move(f);

    typename Helper::steady_time_point before(Helper::steadyNow());
    bool timeout = (sf.wait_for(Helper::waitDur) == Helper::future_status::timeout);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::waitDur, after - before, timeout ? e_timeout : e_no_timeout);
}

template <typename Helper>
void testSharedFutureWaitUntilSteady(const long long jumpMs)
{
    typename Helper::packaged_task pt(returnFalse);
    typename Helper::future f = pt.get_future();
    typename Helper::shared_future sf = std::move(f);

    typename Helper::steady_time_point before(Helper::steadyNow());
    bool timeout = (sf.wait_until(Helper::steadyNow() + Helper::waitDur) == Helper::future_status::timeout);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::waitDur, after - before, timeout ? e_timeout : e_no_timeout);
}

template <typename Helper>
void testSharedFutureWaitUntilSystem(const long long jumpMs)
{
    typename Helper::packaged_task pt(returnFalse);
    typename Helper::future f = pt.get_future();
    typename Helper::shared_future sf = std::move(f);

    typename Helper::steady_time_point before(Helper::steadyNow());
    bool timeout = (sf.wait_until(Helper::systemNow() + Helper::waitDur) == Helper::future_status::timeout);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::waitDur - typename Helper::milliseconds(jumpMs), after - before, timeout ? e_timeout : e_no_timeout);
}

template <typename Helper>
void testSharedFutureWaitUntilCustom(const long long jumpMs)
{
    typename Helper::packaged_task pt(returnFalse);
    typename Helper::future f = pt.get_future();
    typename Helper::shared_future sf = std::move(f);

    typename Helper::steady_time_point before(Helper::steadyNow());
    bool timeout = (sf.wait_until(Helper::customNow() + Helper::waitDur) == Helper::future_status::timeout);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::waitDur - typename Helper::milliseconds(jumpMs), after - before, timeout ? e_timeout : e_no_timeout);
}

//--------------------------------------

template <typename Helper>
void testSharedFutureStd(const std::string& name)
{
    std::cout << std::endl;
    runTestWithNone<Helper>(testSharedFutureWaitFor        <Helper>, name + "::wait_for()");
    runTestWithNone<Helper>(testSharedFutureWaitUntilSteady<Helper>, name + "::wait_until(), steady time");
    runTestWithNone<Helper>(testSharedFutureWaitUntilSystem<Helper>, name + "::wait_until(), system time");
    runTestWithNone<Helper>(testSharedFutureWaitUntilCustom<Helper>, name + "::wait_until(), custom time");
}

/******************************************************************************/

int main()
{
    std::cout << std::endl;
    std::cout << "INFO: This test requires root/Administrator privileges in order to change the system clock." << std::endl;
    std::cout << "INFO: Disable NTP while running this test to prevent NTP from changing the system clock." << std::endl;
    std::cout << std::endl;

    std::cout << std::endl;
    std::cout << "Wait Time:              " << s_waitMs            << " ms" << std::endl;
    std::cout << "Short Jump Time:        " << s_shortJumpMs       << " ms" << std::endl;
    std::cout << "Long Jump Time:         " << s_longJumpMs        << " ms" << std::endl;
    std::cout << "Sleep Before Jump Time: " << s_sleepBeforeJumpMs << " ms" << std::endl;
    std::cout << "Max Early Error:        " << s_maxEarlyErrorMs   << " ms" << std::endl;
    std::cout << "Max Late Error:         " << s_maxLateErrorMs    << " ms" << std::endl;

    testSleepStd        <StdHelper<                                       > >("std");
    testSleepNoThreadStd<StdHelper<                                       > >("std");
    testCondVarStd      <StdHelper<std::mutex, std::condition_variable    > >("std::condition_variable");
    testCondVarPredStd  <StdHelper<std::mutex, std::condition_variable    > >("std::condition_variable");
    testCondVarStd      <StdHelper<std::mutex, std::condition_variable_any> >("std::condition_variable_any");
    testCondVarPredStd  <StdHelper<std::mutex, std::condition_variable_any> >("std::condition_variable_any");
    testMutexStd        <StdHelper<std::timed_mutex                       > >("std::timed_mutex");
    testMutexStd        <StdHelper<std::recursive_timed_mutex             > >("std::recursive_timed_mutex");
    testMutexStd        <StdHelper<std::shared_timed_mutex                > >("std::shared_timed_mutex");
    testMutexSharedStd  <StdHelper<std::shared_timed_mutex                > >("std::shared_timed_mutex");
    testFutureStd       <StdHelper<                                       > >("std::future");
    testSharedFutureStd <StdHelper<                                       > >("std::shared_future");

    std::cout << std::endl;
    std::cout << "Number of Tests Run:    " << g_numTestsRun << std::endl;
    std::cout << "Number of Tests Passed: " << g_numTestsPassed << std::endl;
    std::cout << "Number of Tests Failed: " << g_numTestsFailed << std::endl;

    return 0;
}
