#ifdef _WIN32
	#include <Windows.h>
#else
	#include <sys/time.h>
#endif

#include "boost/bind.hpp"
#include "boost/chrono.hpp"
#include "boost/chrono/ceil.hpp"
#include "boost/date_time.hpp"
#include "boost/thread/future.hpp"
#include "boost/thread/mutex.hpp"
#include "boost/thread/recursive_mutex.hpp"
#include "boost/thread/shared_lock_guard.hpp"
#ifdef USE_V2_SHARED_MUTEX
#include "boost/thread/v2/shared_mutex.hpp"
#else
#include "boost/thread/shared_mutex.hpp"
#endif
#include "boost/thread/thread.hpp"

#include <iomanip>
#ifdef CPP14_ENABLED
#include <future>
#include <mutex>
#include <shared_mutex>
#include <thread>
#endif

// These values are intentially:
//   more than 2 * 100 milliseconds
//   more than 2 * 100 milliseconds apart
//   not a multiple of 100 milliseconds
//   not a multiple of each other
//   don't sum or diff to a multiple of 100 milliseconds
const long long s_waitMs            = 580;
const long long s_shortJumpMs       = 230;
const long long s_longJumpMs        = 870; // causes additional, unavoidable failures when BOOST_THREAD_HAS_CONDATTR_SET_CLOCK_MONOTONIC is disabled
const long long s_sleepBeforeJumpMs = 110;

#ifdef _WIN32
const long long s_maxEarlyErrorMs =  10
                                  + 100; // Windows is unpredictable, especially in a VM, so allow extra time if the function returns early
const long long s_maxLateErrorMs  = 110  // due to polling, functions may not return for up to 100 milliseconds after they are supposed to
                                  + 100; // Windows is slow, especially in a VM, so allow extra time for the functions to return
#else
const long long s_maxEarlyErrorMs =  10;
const long long s_maxLateErrorMs  = 110; // due to polling, functions may not return for up to 100 milliseconds after they are supposed to
#endif

int g_numTestsRun    = 0;
int g_numTestsPassed = 0;
int g_numTestsFailed = 0;

//******************************************************************************

// A custom clock based off the system clock but with a different epoch.

namespace custom
{
    class custom_boost_clock
    {
    public:
        typedef boost::chrono::microseconds                   duration; // intentionally not nanoseconds
        typedef duration::rep                                 rep;
        typedef duration::period                              period;
        typedef boost::chrono::time_point<custom_boost_clock> time_point;
        static bool is_steady;

        static time_point now();
    };

    bool custom_boost_clock::is_steady = false;

    custom_boost_clock::time_point custom_boost_clock::now()
    {
        return time_point(boost::chrono::ceil<duration>(boost::chrono::system_clock::now().time_since_epoch()) - boost::chrono::hours(10 * 365 * 24));
    }

#ifdef CPP14_ENABLED
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
#endif
}

//******************************************************************************

template <typename MutexType = boost::mutex, typename TestType = boost::condition_variable>
struct BoostHelper
{
    typedef MutexType mutex;
    typedef TestType test_var; // may be condition variable or future

    typedef boost::lock_guard<MutexType> lock_guard;
    typedef boost::unique_lock<MutexType> unique_lock;

    typedef boost::chrono::milliseconds milliseconds;
    typedef boost::chrono::nanoseconds nanoseconds;

    typedef boost::chrono::system_clock system_clock;
    typedef boost::chrono::steady_clock steady_clock;
    typedef custom::custom_boost_clock custom_clock;

    typedef system_clock::time_point system_time_point;
    typedef steady_clock::time_point steady_time_point;
    typedef custom_clock::time_point custom_time_point;

    typedef boost::cv_status cv_status;
    typedef boost::future_status future_status;

    typedef boost::packaged_task<bool> packaged_task;

    typedef boost::thread thread;

    static const milliseconds dur;

    template <typename T>
    static void sleep_for(T d)
    {
        boost::this_thread::sleep_for(d);
    }

    template <typename T>
    static void sleep_for_no_int(T d)
    {
        boost::this_thread::no_interruption_point::sleep_for(d);
    }

    template <typename T>
    static void sleep_until(T t)
    {
        boost::this_thread::sleep_until(t);
    }

    template <typename T>
    static void sleep_until_no_int(T t)
    {
        boost::this_thread::no_interruption_point::sleep_until(t);
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
    static ToDuration duration_cast(const boost::chrono::duration<Rep, Period>& d)
    {
        return boost::chrono::duration_cast<ToDuration>(d);
    }

    static milliseconds zero()
    {
        return milliseconds(0);
    }
};

template <typename MutexType, typename TestType>
const typename BoostHelper<MutexType, TestType>::milliseconds
BoostHelper<MutexType, TestType>::dur = typename BoostHelper<MutexType, TestType>::milliseconds(s_waitMs);

#ifdef CPP14_ENABLED
template <typename MutexType = std::mutex, typename TestType = std::condition_variable>
struct StdHelper
{
    typedef MutexType mutex;
    typedef TestType test_var; // may be condition variable or future

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

    typedef std::thread thread;

    static const milliseconds dur;

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

template <typename MutexType, typename TestType>
const typename StdHelper<MutexType, TestType>::milliseconds
StdHelper<MutexType, TestType>::dur = typename StdHelper<MutexType, TestType>::milliseconds(s_waitMs);
#endif

//******************************************************************************

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

template <typename Helper>
void checkWaitTime(typename Helper::nanoseconds expected, typename Helper::nanoseconds actual, bool noTimeout)
{
    if (expected != Helper::zero() && expected < typename Helper::milliseconds(s_sleepBeforeJumpMs))
    {
        expected = typename Helper::milliseconds(s_sleepBeforeJumpMs);
    }

    typename Helper::milliseconds expectedMs = Helper::template duration_cast<typename Helper::milliseconds>(expected);
    typename Helper::milliseconds actualMs   = Helper::template duration_cast<typename Helper::milliseconds>(actual);

    std::cout << "Expected: " << std::setw(4) << expectedMs.count() << " ms"
              << ", Actual: " << std::setw(4) << actualMs.count() << " ms"
              << ", Returned: " << (noTimeout ? "no_timeout, "
                                              : "timeout,    ");

    if (expectedMs == Helper::zero())
    {
        std::cout << "FAILED: SKIPPED (test would lock up system if run)" << std::endl;
        g_numTestsFailed++;
    }
    else if (actual < expected - typename Helper::milliseconds(s_maxEarlyErrorMs))
    {
        std::cout << "FAILED: TOO SHORT" << std::endl;
        g_numTestsFailed++;
    }
    else if (actual > expected + typename Helper::milliseconds(s_maxLateErrorMs))
    {
        std::cout << "FAILED: TOO LONG" << std::endl;
        g_numTestsFailed++;
    }
    else if (noTimeout)
    {
        std::cout << "FAILED: RETURNED NO_TIMEOUT" << std::endl;
        g_numTestsFailed++;
    }
    else
    {
        std::cout << "Passed" << std::endl;
        g_numTestsPassed++;
    }

    g_numTestsRun++;
}

void sleepForLongTime()
{
#ifdef _WIN32
	Sleep(10000);
#else
    struct timespec ts = {5, 0};
    nanosleep(&ts, NULL);
#endif
}

bool returnFalse()
{
    return false;
}

//******************************************************************************

// Run the test in the current thread context.
template <typename Helper, void (*Function)(const long long)>
void runTestNoThread(const std::string name)
{
    std::cout << name << ":" << std::endl;

    {
        std::cout << "    While system clock remains stable:        ";
        Function(0);
    }

    {
        std::cout << "    While system clock jumps back (short):    ";
        typename Helper::thread t(boost::bind(changeSystemTime, -s_shortJumpMs));
        Function(-s_shortJumpMs);
        t.join();
    }

    {
        std::cout << "    While system clock jumps back (long):     ";
        typename Helper::thread t(boost::bind(changeSystemTime, -s_longJumpMs));
        Function(-s_longJumpMs);
        t.join();
    }

    {
        std::cout << "    While system clock jumps forward (short): ";
        typename Helper::thread t(boost::bind(changeSystemTime, s_shortJumpMs));
        Function(s_shortJumpMs);
        t.join();
    }

    {
        std::cout << "    While system clock jumps forward (long):  ";
        typename Helper::thread t(boost::bind(changeSystemTime, s_longJumpMs));
        Function(s_longJumpMs);
        t.join();
    }
}

//--------------------------------------

template <typename Helper, void (*Function)(const long long)>
void wrapInThread(const long long jumpMs)
{
    typename Helper::thread t(boost::bind(Function, jumpMs));
    t.join();
}

template <typename Helper, void (*Function)(typename Helper::mutex&, const long long)>
void wrapInThread(const long long jumpMs)
{
    typename Helper::mutex m;
    typename Helper::lock_guard g(m);
    typename Helper::thread t(boost::bind(Function, boost::ref(m), jumpMs));
    t.join();
}

template <typename Helper, void (*Function)(typename Helper::mutex&, const long long)>
void wrapInThreadShared(const long long jumpMs)
{
    typename Helper::mutex m;
    boost::shared_lock_guard<typename Helper::mutex> g(m);
    typename Helper::thread t(boost::bind(Function, boost::ref(m), jumpMs));
    t.join();
}

template <typename Helper, void (*Function)(typename Helper::mutex&, const long long)>
void wrapInThreadUpgrade(const long long jumpMs)
{
    typename Helper::mutex m;
    boost::upgrade_lock<typename Helper::mutex> g(m);
    typename Helper::thread t(boost::bind(Function, boost::ref(m), jumpMs));
    t.join();
}

//--------------------------------------

// Run the test in a separate thread.
template <typename Helper, void (*Function)(const long long)>
void runTest(const std::string name)
{
    runTestNoThread<Helper, wrapInThread<Helper, Function> >(name);
}

// Run the test in a separate thread. Pass a locked mutex to the function under test.
template <typename Helper, void (*Function)(typename Helper::mutex&, const long long)>
void runTest(const std::string name)
{
    runTestNoThread<Helper, wrapInThread<Helper, Function> >(name);
}

// Run the test in a separate thread. Pass a shared-locked mutex to the function under test.
template <typename Helper, void (*Function)(typename Helper::mutex&, const long long)>
void runTestShared(const std::string name)
{
    runTestNoThread<Helper, wrapInThreadShared<Helper, Function> >(name);
}

// Run the test in a separate thread. Pass an upgrade-locked mutex to the function under test.
template <typename Helper, void (*Function)(typename Helper::mutex&, const long long)>
void runTestUpgrade(const std::string name)
{
    runTestNoThread<Helper, wrapInThreadUpgrade<Helper, Function> >(name);
}

//******************************************************************************

// Test Sleep

template <typename Helper>
void testSleepFor(const long long jumpMs)
{
    typename Helper::steady_time_point before(Helper::steadyNow());
    Helper::sleep_for(Helper::dur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur, after - before, false);
}

template <typename Helper>
void testSleepUntilSteady(const long long jumpMs)
{
    typename Helper::steady_time_point before(Helper::steadyNow());
    Helper::sleep_until(Helper::steadyNow() + Helper::dur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur, after - before, false);
}

template <typename Helper>
void testSleepUntilSystem(const long long jumpMs)
{
    typename Helper::steady_time_point before(Helper::steadyNow());
    Helper::sleep_until(Helper::systemNow() + Helper::dur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur - typename Helper::milliseconds(jumpMs), after - before, false);
}

template <typename Helper>
void testSleepUntilCustom(const long long jumpMs)
{
    typename Helper::steady_time_point before(Helper::steadyNow());
    Helper::sleep_until(Helper::customNow() + Helper::dur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur - typename Helper::milliseconds(jumpMs), after - before, false);
}

//--------------------------------------

template <typename Helper>
void testSleepDur(const long long jumpMs)
{
#ifndef SKIP_DATETIME_FUNCTIONS
    typename Helper::steady_time_point before(Helper::steadyNow());
    boost::posix_time::milliseconds ptDur(boost::chrono::duration_cast<boost::chrono::milliseconds>(Helper::dur).count());
    boost::this_thread::sleep(ptDur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur, after - before, false);
#else
    checkWaitTime<Helper>(Helper::zero(), Helper::zero(), false);
#endif
}

template <typename Helper>
void testSleepSystem(const long long jumpMs)
{
#ifndef SKIP_DATETIME_FUNCTIONS
    typename Helper::steady_time_point before(Helper::steadyNow());
    boost::posix_time::ptime ptNow(boost::posix_time::microsec_clock::universal_time());
    boost::posix_time::milliseconds ptDur(boost::chrono::duration_cast<boost::chrono::milliseconds>(Helper::dur).count());
    boost::this_thread::sleep(ptNow + ptDur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur - typename Helper::milliseconds(jumpMs), after - before, false);
#else
    checkWaitTime<Helper>(Helper::zero(), Helper::zero(), false);
#endif
}

//--------------------------------------

template <typename Helper>
void testSleepStd(const std::string& name)
{
    std::cout << std::endl;
    runTest<Helper, testSleepFor        <Helper> >(name + "::this_thread::sleep_for()");
    runTest<Helper, testSleepUntilSteady<Helper> >(name + "::this_thread::sleep_until(), steady time");
    runTest<Helper, testSleepUntilSystem<Helper> >(name + "::this_thread::sleep_until(), system time");
    runTest<Helper, testSleepUntilCustom<Helper> >(name + "::this_thread::sleep_until(), custom time");
}

template <typename Helper>
void testSleepBoost(const std::string& name)
{
    testSleepStd<Helper>(name);

    // Boost-only functions
    runTest<Helper, testSleepDur   <Helper> >(name + "::this_thread::sleep(), posix duration");
    runTest<Helper, testSleepSystem<Helper> >(name + "::this_thread::sleep(), posix system time");
}

template <typename Helper>
void testSleepNoThreadStd(const std::string& name)
{
    std::cout << std::endl;
    runTestNoThread<Helper, testSleepFor        <Helper> >(name + "::this_thread::sleep_for(), no thread");
    runTestNoThread<Helper, testSleepUntilSteady<Helper> >(name + "::this_thread::sleep_until(), no thread, steady time");
    runTestNoThread<Helper, testSleepUntilSystem<Helper> >(name + "::this_thread::sleep_until(), no thread, system time");
    runTestNoThread<Helper, testSleepUntilCustom<Helper> >(name + "::this_thread::sleep_until(), no thread, custom time");
}

template <typename Helper>
void testSleepNoThreadBoost(const std::string& name)
{
    testSleepNoThreadStd<Helper>(name);

    // Boost-only functions
    runTestNoThread<Helper, testSleepDur   <Helper> >(name + "::this_thread::sleep(), no thread, posix duration");
    runTestNoThread<Helper, testSleepSystem<Helper> >(name + "::this_thread::sleep(), no thread, posix system time");
}

//******************************************************************************

// Test Sleep, No Interruption Point

template <typename Helper>
void testSleepForNoInt(const long long jumpMs)
{
    typename Helper::steady_time_point before(Helper::steadyNow());
    Helper::sleep_for_no_int(Helper::dur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur, after - before, false);
}

template <typename Helper>
void testSleepUntilNoIntSteady(const long long jumpMs)
{
    typename Helper::steady_time_point before(Helper::steadyNow());
    Helper::sleep_until_no_int(Helper::steadyNow() + Helper::dur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur, after - before, false);
}

template <typename Helper>
void testSleepUntilNoIntSystem(const long long jumpMs)
{
    typename Helper::steady_time_point before(Helper::steadyNow());
    Helper::sleep_until_no_int(Helper::systemNow() + Helper::dur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur - typename Helper::milliseconds(jumpMs), after - before, false);
}

template <typename Helper>
void testSleepUntilNoIntCustom(const long long jumpMs)
{
    typename Helper::steady_time_point before(Helper::steadyNow());
    Helper::sleep_until_no_int(Helper::customNow() + Helper::dur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur - typename Helper::milliseconds(jumpMs), after - before, false);
}

//--------------------------------------

// The following functions are not implemented on Linux.
#ifdef _WIN32

template <typename Helper>
void testSleepNoIntDur(const long long jumpMs)
{
#ifndef SKIP_DATETIME_FUNCTIONS
    typename Helper::steady_time_point before(Helper::steadyNow());
    boost::posix_time::milliseconds ptDur(boost::chrono::duration_cast<boost::chrono::milliseconds>(Helper::dur).count());
    boost::this_thread::no_interruption_point::sleep(ptDur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur, after - before, false);
#else
    checkWaitTime<Helper>(Helper::zero(), Helper::zero(), false);
#endif
}

template <typename Helper>
void testSleepNoIntSystem(const long long jumpMs)
{
#ifndef SKIP_DATETIME_FUNCTIONS
    typename Helper::steady_time_point before(Helper::steadyNow());
    boost::posix_time::ptime ptNow(boost::posix_time::microsec_clock::universal_time());
    boost::posix_time::milliseconds ptDur(boost::chrono::duration_cast<boost::chrono::milliseconds>(Helper::dur).count());
    boost::this_thread::no_interruption_point::sleep(ptNow + ptDur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur - typename Helper::milliseconds(jumpMs), after - before, false);
#else
    checkWaitTime<Helper>(Helper::zero(), Helper::zero(), false);
#endif
}

#endif

//--------------------------------------

// Only Boost supports no_interruption_point

template <typename Helper>
void testSleepNoIntBoost(const std::string& name)
{
    std::cout << std::endl;
    runTest<Helper, testSleepForNoInt        <Helper> >(name + "::this_thread::no_interruption_point::sleep_for()");
    runTest<Helper, testSleepUntilNoIntSteady<Helper> >(name + "::this_thread::no_interruption_point::sleep_until(), steady time");
    runTest<Helper, testSleepUntilNoIntSystem<Helper> >(name + "::this_thread::no_interruption_point::sleep_until(), system time");
    runTest<Helper, testSleepUntilNoIntCustom<Helper> >(name + "::this_thread::no_interruption_point::sleep_until(), custom time");

// The following functions are not implemented on Linux.
#ifdef _WIN32
    runTest<Helper, testSleepNoIntDur   <Helper> >(name + "::this_thread::no_interruption_point::sleep(), posix duration");
    runTest<Helper, testSleepNoIntSystem<Helper> >(name + "::this_thread::no_interruption_point::sleep(), posix system time");
#endif
}

template <typename Helper>
void testSleepNoThreadNoIntBoost(const std::string& name)
{
    std::cout << std::endl;
    runTestNoThread<Helper, testSleepForNoInt        <Helper> >(name + "::this_thread::no_interruption_point::sleep_for(), no thread");
    runTestNoThread<Helper, testSleepUntilNoIntSteady<Helper> >(name + "::this_thread::no_interruption_point::sleep_until(), no thread, steady time");
    runTestNoThread<Helper, testSleepUntilNoIntSystem<Helper> >(name + "::this_thread::no_interruption_point::sleep_until(), no thread, system time");
    runTestNoThread<Helper, testSleepUntilNoIntCustom<Helper> >(name + "::this_thread::no_interruption_point::sleep_until(), no thread, custom time");

// The following functions are not implemented on Linux.
#ifdef _WIN32
    runTestNoThread<Helper, testSleepNoIntDur   <Helper> >(name + "::this_thread::no_interruption_point::sleep(), no thread, posix duration");
    runTestNoThread<Helper, testSleepNoIntSystem<Helper> >(name + "::this_thread::no_interruption_point::sleep(), no thread, posix system time");
#endif
}

//******************************************************************************

// Test Try Join

template <typename Helper>
void testTryJoinFor(const long long jumpMs)
{
    typename Helper::steady_time_point before(Helper::steadyNow());
    typename Helper::thread t3(sleepForLongTime);
    bool noTimeout = t3.try_join_for(Helper::dur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur, after - before, noTimeout);
}

template <typename Helper>
void testTryJoinUntilSteady(const long long jumpMs)
{
    typename Helper::steady_time_point before(Helper::steadyNow());
    typename Helper::thread t3(sleepForLongTime);
    bool noTimeout = t3.try_join_until(Helper::steadyNow() + Helper::dur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur, after - before, noTimeout);
}

template <typename Helper>
void testTryJoinUntilSystem(const long long jumpMs)
{
    typename Helper::steady_time_point before(Helper::steadyNow());
    typename Helper::thread t3(sleepForLongTime);
    bool noTimeout = t3.try_join_until(Helper::systemNow() + Helper::dur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur - typename Helper::milliseconds(jumpMs), after - before, noTimeout);
}

template <typename Helper>
void testTryJoinUntilCustom(const long long jumpMs)
{
    typename Helper::steady_time_point before(Helper::steadyNow());
    typename Helper::thread t3(sleepForLongTime);
    bool noTimeout = t3.try_join_until(Helper::customNow() + Helper::dur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur - typename Helper::milliseconds(jumpMs), after - before, noTimeout);
}

//--------------------------------------

template <typename Helper>
void testTimedJoinDur(const long long jumpMs)
{
#ifndef SKIP_DATETIME_FUNCTIONS
    typename Helper::steady_time_point before(Helper::steadyNow());
    boost::posix_time::milliseconds ptDur(boost::chrono::duration_cast<boost::chrono::milliseconds>(Helper::dur).count());
    typename Helper::thread t3(sleepForLongTime);
    bool noTimeout = t3.timed_join(ptDur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur, after - before, noTimeout);
#else
    checkWaitTime<Helper>(Helper::zero(), Helper::zero(), false);
#endif
}

template <typename Helper>
void testTimedJoinSystem(const long long jumpMs)
{
#ifndef SKIP_DATETIME_FUNCTIONS
    typename Helper::steady_time_point before(Helper::steadyNow());
    boost::posix_time::ptime ptNow(boost::posix_time::microsec_clock::universal_time());
    boost::posix_time::milliseconds ptDur(boost::chrono::duration_cast<boost::chrono::milliseconds>(Helper::dur).count());
    typename Helper::thread t3(sleepForLongTime);
    bool noTimeout = t3.timed_join(ptNow + ptDur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur - typename Helper::milliseconds(jumpMs), after - before, noTimeout);
#else
    checkWaitTime<Helper>(Helper::zero(), Helper::zero(), false);
#endif
}

//--------------------------------------

// Only Boost supports timed try_join functions

template <typename Helper>
void testJoinBoost(const std::string& name)
{
    std::cout << std::endl;
    runTest<Helper, testTryJoinFor        <Helper> >(name + "::thread::try_join_for()");
    runTest<Helper, testTryJoinUntilSteady<Helper> >(name + "::thread::try_join_until(), steady time");
    runTest<Helper, testTryJoinUntilSystem<Helper> >(name + "::thread::try_join_until(), system time");
    runTest<Helper, testTryJoinUntilCustom<Helper> >(name + "::thread::try_join_until(), custom time");
    runTest<Helper, testTimedJoinDur      <Helper> >(name + "::thread::timed_join(), posix duration");
    runTest<Helper, testTimedJoinSystem   <Helper> >(name + "::thread::timed_join(), posix system time");
}

//******************************************************************************

// Test Condition Variable Wait

template <typename Helper>
void testCondVarWaitFor(const long long jumpMs)
{
    typename Helper::test_var cv;
    typename Helper::mutex m;
    typename Helper::unique_lock g(m);

    typename Helper::steady_time_point before(Helper::steadyNow());
    bool noTimeout = (cv.wait_for(g, Helper::dur) == Helper::cv_status::no_timeout);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur, after - before, noTimeout);
}

template <typename Helper>
void testCondVarWaitUntilSteady(const long long jumpMs)
{
    typename Helper::test_var cv;
    typename Helper::mutex m;
    typename Helper::unique_lock g(m);

    typename Helper::steady_time_point before(Helper::steadyNow());
    bool noTimeout = (cv.wait_until(g, Helper::steadyNow() + Helper::dur) == Helper::cv_status::no_timeout);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur, after - before, noTimeout);
}

template <typename Helper>
void testCondVarWaitUntilSystem(const long long jumpMs)
{
    typename Helper::test_var cv;
    typename Helper::mutex m;
    typename Helper::unique_lock g(m);

    typename Helper::steady_time_point before(Helper::steadyNow());
    bool noTimeout = (cv.wait_until(g, Helper::systemNow() + Helper::dur) == Helper::cv_status::no_timeout);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur - typename Helper::milliseconds(jumpMs), after - before, noTimeout);
}

template <typename Helper>
void testCondVarWaitUntilCustom(const long long jumpMs)
{
    typename Helper::test_var cv;
    typename Helper::mutex m;
    typename Helper::unique_lock g(m);

    typename Helper::steady_time_point before(Helper::steadyNow());
    bool noTimeout = (cv.wait_until(g, Helper::customNow() + Helper::dur) == Helper::cv_status::no_timeout);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur - typename Helper::milliseconds(jumpMs), after - before, noTimeout);
}

//--------------------------------------

template <typename Helper>
void testCondVarTimedWaitDur(const long long jumpMs)
{
#ifndef SKIP_DATETIME_FUNCTIONS
    typename Helper::test_var cv;
    typename Helper::mutex m;
    typename Helper::unique_lock g(m);

    typename Helper::steady_time_point before(Helper::steadyNow());
    boost::posix_time::milliseconds ptDur(boost::chrono::duration_cast<boost::chrono::milliseconds>(Helper::dur).count());
    bool noTimeout = cv.timed_wait(g, ptDur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur, after - before, noTimeout);
#else
    checkWaitTime<Helper>(Helper::zero(), Helper::zero(), false);
#endif
}

template <typename Helper>
void testCondVarTimedWaitSystem(const long long jumpMs)
{
#ifndef SKIP_DATETIME_FUNCTIONS
    typename Helper::test_var cv;
    typename Helper::mutex m;
    typename Helper::unique_lock g(m);

    typename Helper::steady_time_point before(Helper::steadyNow());
    boost::posix_time::ptime ptNow(boost::posix_time::microsec_clock::universal_time());
    boost::posix_time::milliseconds ptDur(boost::chrono::duration_cast<boost::chrono::milliseconds>(Helper::dur).count());
    bool noTimeout = cv.timed_wait(g, ptNow + ptDur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur - typename Helper::milliseconds(jumpMs), after - before, noTimeout);
#else
    checkWaitTime<Helper>(Helper::zero(), Helper::zero(), false);
#endif
}

//--------------------------------------

template <typename Helper>
void testCondVarStd(const std::string& name)
{
    std::cout << std::endl;
    runTest<Helper, testCondVarWaitFor        <Helper> >(name + "::wait_for()");
    runTest<Helper, testCondVarWaitUntilSteady<Helper> >(name + "::wait_until(), steady time");
    runTest<Helper, testCondVarWaitUntilSystem<Helper> >(name + "::wait_until(), system time");
    runTest<Helper, testCondVarWaitUntilCustom<Helper> >(name + "::wait_until(), custom time");
}

template <typename Helper>
void testCondVarBoost(const std::string& name)
{
    testCondVarStd<Helper>(name);

    // Boost-only functions
    runTest<Helper, testCondVarTimedWaitDur   <Helper> >(name + "::timed_wait(), posix duration");
    runTest<Helper, testCondVarTimedWaitSystem<Helper> >(name + "::timed_wait(), posix system time");
}

//******************************************************************************

// Test Condition Variable Wait with Predicate

template <typename Helper>
void testCondVarWaitForPred(const long long jumpMs)
{
    typename Helper::test_var cv;
    typename Helper::mutex m;
    typename Helper::unique_lock g(m);

    typename Helper::steady_time_point before(Helper::steadyNow());
    bool noTimeout = cv.wait_for(g, Helper::dur, returnFalse);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur, after - before, noTimeout);
}

template <typename Helper>
void testCondVarWaitUntilPredSteady(const long long jumpMs)
{
    typename Helper::test_var cv;
    typename Helper::mutex m;
    typename Helper::unique_lock g(m);

    typename Helper::steady_time_point before(Helper::steadyNow());
    bool noTimeout = cv.wait_until(g, Helper::steadyNow() + Helper::dur, returnFalse);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur, after - before, noTimeout);
}

template <typename Helper>
void testCondVarWaitUntilPredSystem(const long long jumpMs)
{
    typename Helper::test_var cv;
    typename Helper::mutex m;
    typename Helper::unique_lock g(m);

    typename Helper::steady_time_point before(Helper::steadyNow());
    bool noTimeout = cv.wait_until(g, Helper::systemNow() + Helper::dur, returnFalse);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur - typename Helper::milliseconds(jumpMs), after - before, noTimeout);
}

template <typename Helper>
void testCondVarWaitUntilPredCustom(const long long jumpMs)
{
    typename Helper::test_var cv;
    typename Helper::mutex m;
    typename Helper::unique_lock g(m);

    typename Helper::steady_time_point before(Helper::steadyNow());
    bool noTimeout = cv.wait_until(g, Helper::customNow() + Helper::dur, returnFalse);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur - typename Helper::milliseconds(jumpMs), after - before, noTimeout);
}

//--------------------------------------

template <typename Helper>
void testCondVarTimedWaitPredDur(const long long jumpMs)
{
#ifndef SKIP_DATETIME_FUNCTIONS
    typename Helper::test_var cv;
    typename Helper::mutex m;
    typename Helper::unique_lock g(m);

    typename Helper::steady_time_point before(Helper::steadyNow());
    boost::posix_time::milliseconds ptDur(boost::chrono::duration_cast<boost::chrono::milliseconds>(Helper::dur).count());
    bool noTimeout = cv.timed_wait(g, ptDur, returnFalse);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur, after - before, noTimeout);
#else
    checkWaitTime<Helper>(Helper::zero(), Helper::zero(), false);
#endif
}

template <typename Helper>
void testCondVarTimedWaitPredSystem(const long long jumpMs)
{
#ifndef SKIP_DATETIME_FUNCTIONS
    typename Helper::test_var cv;
    typename Helper::mutex m;
    typename Helper::unique_lock g(m);

    typename Helper::steady_time_point before(Helper::steadyNow());
    boost::posix_time::ptime ptNow(boost::posix_time::microsec_clock::universal_time());
    boost::posix_time::milliseconds ptDur(boost::chrono::duration_cast<boost::chrono::milliseconds>(Helper::dur).count());
    bool noTimeout = cv.timed_wait(g, ptNow + ptDur, returnFalse);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur - typename Helper::milliseconds(jumpMs), after - before, noTimeout);
#else
    checkWaitTime<Helper>(Helper::zero(), Helper::zero(), false);
#endif
}

//--------------------------------------

template <typename Helper>
void testCondVarPredStd(const std::string& name)
{
    std::cout << std::endl;
    runTest<Helper, testCondVarWaitForPred        <Helper> >(name + "::wait_for(), with predicate");
    runTest<Helper, testCondVarWaitUntilPredSteady<Helper> >(name + "::wait_until(), with predicate, steady time");
    runTest<Helper, testCondVarWaitUntilPredSystem<Helper> >(name + "::wait_until(), with predicate, system time");
    runTest<Helper, testCondVarWaitUntilPredCustom<Helper> >(name + "::wait_until(), with predicate, custom time");
}

template <typename Helper>
void testCondVarPredBoost(const std::string& name)
{
    testCondVarPredStd<Helper>(name);

    // Boost-only functions
    runTest<Helper, testCondVarTimedWaitPredDur   <Helper> >(name + "::timed_wait(), with predicate, posix duration");
    runTest<Helper, testCondVarTimedWaitPredSystem<Helper> >(name + "::timed_wait(), with predicate, posix system time");
}

//******************************************************************************

// Test Try Lock

template <typename Helper>
void testTryLockFor(typename Helper::mutex& m, const long long jumpMs)
{
    typename Helper::steady_time_point before(Helper::steadyNow());
    bool noTimeout = m.try_lock_for(Helper::dur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur, after - before, noTimeout);
}

template <typename Helper>
void testTryLockUntilSteady(typename Helper::mutex& m, const long long jumpMs)
{
    typename Helper::steady_time_point before(Helper::steadyNow());
    bool noTimeout = m.try_lock_until(Helper::steadyNow() + Helper::dur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur, after - before, noTimeout);
}

template <typename Helper>
void testTryLockUntilSystem(typename Helper::mutex& m, const long long jumpMs)
{
    typename Helper::steady_time_point before(Helper::steadyNow());
    bool noTimeout = m.try_lock_until(Helper::systemNow() + Helper::dur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur - typename Helper::milliseconds(jumpMs), after - before, noTimeout);
}

template <typename Helper>
void testTryLockUntilCustom(typename Helper::mutex& m, const long long jumpMs)
{
    typename Helper::steady_time_point before(Helper::steadyNow());
    bool noTimeout = m.try_lock_until(Helper::customNow() + Helper::dur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur - typename Helper::milliseconds(jumpMs), after - before, noTimeout);
}

//--------------------------------------

template <typename Helper>
void testTimedLockDur(typename Helper::mutex& m, const long long jumpMs)
{
#ifndef SKIP_DATETIME_FUNCTIONS
    typename Helper::steady_time_point before(Helper::steadyNow());
    boost::posix_time::milliseconds ptDur(boost::chrono::duration_cast<boost::chrono::milliseconds>(Helper::dur).count());
    bool noTimeout = m.timed_lock(ptDur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur, after - before, noTimeout);
#else
    checkWaitTime<Helper>(Helper::zero(), Helper::zero(), false);
#endif
}

template <typename Helper>
void testTimedLockSystem(typename Helper::mutex& m, const long long jumpMs)
{
#ifndef SKIP_DATETIME_FUNCTIONS
    typename Helper::steady_time_point before(Helper::steadyNow());
    boost::posix_time::ptime ptNow(boost::posix_time::microsec_clock::universal_time());
    boost::posix_time::milliseconds ptDur(boost::chrono::duration_cast<boost::chrono::milliseconds>(Helper::dur).count());
    bool noTimeout = m.timed_lock(ptNow + ptDur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur - typename Helper::milliseconds(jumpMs), after - before, noTimeout);
#else
    checkWaitTime<Helper>(Helper::zero(), Helper::zero(), false);
#endif
}

//--------------------------------------

template <typename Helper>
void testMutexStd(const std::string& name)
{
    std::cout << std::endl;
    runTest<Helper, testTryLockFor        <Helper> >(name + "::try_lock_for()");
    runTest<Helper, testTryLockUntilSteady<Helper> >(name + "::try_lock_until(), steady time");
    runTest<Helper, testTryLockUntilSystem<Helper> >(name + "::try_lock_until(), system time");
    runTest<Helper, testTryLockUntilCustom<Helper> >(name + "::try_lock_until(), custom time");
}

template <typename Helper>
void testMutexBoost(const std::string& name)
{
    testMutexStd<Helper>(name);

    // Boost-only functions
    runTest<Helper, testTimedLockDur   <Helper> >(name + "::timed_lock(), posix duration");
    runTest<Helper, testTimedLockSystem<Helper> >(name + "::timed_lock(), posix system time");
}

//******************************************************************************

// Test Try Lock Shared

template <typename Helper>
void testTryLockSharedFor(typename Helper::mutex& m, const long long jumpMs)
{
    typename Helper::steady_time_point before(Helper::steadyNow());
    bool noTimeout = m.try_lock_shared_for(Helper::dur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur, after - before, noTimeout);
}

template <typename Helper>
void testTryLockSharedUntilSteady(typename Helper::mutex& m, const long long jumpMs)
{
    typename Helper::steady_time_point before(Helper::steadyNow());
    bool noTimeout = m.try_lock_shared_until(Helper::steadyNow() + Helper::dur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur, after - before, noTimeout);
}

template <typename Helper>
void testTryLockSharedUntilSystem(typename Helper::mutex& m, const long long jumpMs)
{
    typename Helper::steady_time_point before(Helper::steadyNow());
    bool noTimeout = m.try_lock_shared_until(Helper::systemNow() + Helper::dur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur - typename Helper::milliseconds(jumpMs), after - before, noTimeout);
}

template <typename Helper>
void testTryLockSharedUntilCustom(typename Helper::mutex& m, const long long jumpMs)
{
    typename Helper::steady_time_point before(Helper::steadyNow());
    bool noTimeout = m.try_lock_shared_until(Helper::customNow() + Helper::dur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur - typename Helper::milliseconds(jumpMs), after - before, noTimeout);
}

//--------------------------------------

template <typename Helper>
void testTimedLockSharedDur(typename Helper::mutex& m, const long long jumpMs)
{
#ifndef SKIP_DATETIME_FUNCTIONS
    typename Helper::steady_time_point before(Helper::steadyNow());
    boost::posix_time::milliseconds ptDur(boost::chrono::duration_cast<boost::chrono::milliseconds>(Helper::dur).count());
    bool noTimeout = m.timed_lock_shared(ptDur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur, after - before, noTimeout);
#else
    checkWaitTime<Helper>(Helper::zero(), Helper::zero(), false);
#endif
}

template <typename Helper>
void testTimedLockSharedSystem(typename Helper::mutex& m, const long long jumpMs)
{
#ifndef SKIP_DATETIME_FUNCTIONS
    typename Helper::steady_time_point before(Helper::steadyNow());
    boost::posix_time::ptime ptNow(boost::posix_time::microsec_clock::universal_time());
    boost::posix_time::milliseconds ptDur(boost::chrono::duration_cast<boost::chrono::milliseconds>(Helper::dur).count());
    bool noTimeout = m.timed_lock_shared(ptNow + ptDur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur - typename Helper::milliseconds(jumpMs), after - before, noTimeout);
#else
    checkWaitTime<Helper>(Helper::zero(), Helper::zero(), false);
#endif
}

//--------------------------------------

template <typename Helper>
void testMutexSharedStd(const std::string& name)
{
    std::cout << std::endl;
    runTest<Helper, testTryLockSharedFor        <Helper> >(name + "::try_lock_shared_for()");
    runTest<Helper, testTryLockSharedUntilSteady<Helper> >(name + "::try_lock_shared_until(), steady time");
    runTest<Helper, testTryLockSharedUntilSystem<Helper> >(name + "::try_lock_shared_until(), system time");
    runTest<Helper, testTryLockSharedUntilCustom<Helper> >(name + "::try_lock_shared_until(), custom time");
}

template <typename Helper>
void testMutexSharedBoost(const std::string& name)
{
    testMutexSharedStd<Helper>(name);

    // Boost-only functions
    runTest<Helper, testTimedLockSharedDur   <Helper> >(name + "::timed_lock_shared(), posix duration");
    runTest<Helper, testTimedLockSharedSystem<Helper> >(name + "::timed_lock_shared(), posix system time");
}

//******************************************************************************

// Test Try Lock Upgrade

// The following functions are not implemented in the Windows version of shared_mutex.
#ifndef _WIN32

template <typename Helper>
void testTryLockUpgradeFor(typename Helper::mutex& m, const long long jumpMs)
{
    typename Helper::steady_time_point before(Helper::steadyNow());
    bool noTimeout = m.try_lock_upgrade_for(Helper::dur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur, after - before, noTimeout);
}

template <typename Helper>
void testTryLockUpgradeUntilSteady(typename Helper::mutex& m, const long long jumpMs)
{
    typename Helper::steady_time_point before(Helper::steadyNow());
    bool noTimeout = m.try_lock_upgrade_until(Helper::steadyNow() + Helper::dur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur, after - before, noTimeout);
}

template <typename Helper>
void testTryLockUpgradeUntilSystem(typename Helper::mutex& m, const long long jumpMs)
{
    typename Helper::steady_time_point before(Helper::steadyNow());
    bool noTimeout = m.try_lock_upgrade_until(Helper::systemNow() + Helper::dur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur - typename Helper::milliseconds(jumpMs), after - before, noTimeout);
}

template <typename Helper>
void testTryLockUpgradeUntilCustom(typename Helper::mutex& m, const long long jumpMs)
{
    typename Helper::steady_time_point before(Helper::steadyNow());
    bool noTimeout = m.try_lock_upgrade_until(Helper::customNow() + Helper::dur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur - typename Helper::milliseconds(jumpMs), after - before, noTimeout);
}

//--------------------------------------

template <typename Helper>
void testTimedLockUpgradeDur(typename Helper::mutex& m, const long long jumpMs)
{
#ifndef SKIP_DATETIME_FUNCTIONS
    typename Helper::steady_time_point before(Helper::steadyNow());
    boost::posix_time::milliseconds ptDur(boost::chrono::duration_cast<boost::chrono::milliseconds>(Helper::dur).count());
    bool noTimeout = m.timed_lock_upgrade(ptDur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur, after - before, noTimeout);
#else
    checkWaitTime<Helper>(Helper::zero(), Helper::zero(), false);
#endif
}

template <typename Helper>
void testTimedLockUpgradeSystem(typename Helper::mutex& m, const long long jumpMs)
{
#ifndef SKIP_DATETIME_FUNCTIONS
    typename Helper::steady_time_point before(Helper::steadyNow());
    boost::posix_time::ptime ptNow(boost::posix_time::microsec_clock::universal_time());
    boost::posix_time::milliseconds ptDur(boost::chrono::duration_cast<boost::chrono::milliseconds>(Helper::dur).count());
    bool noTimeout = m.timed_lock_upgrade(ptNow + ptDur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur - typename Helper::milliseconds(jumpMs), after - before, noTimeout);
#else
    checkWaitTime<Helper>(Helper::zero(), Helper::zero(), false);
#endif
}

//--------------------------------------

template <typename Helper>
void testTryUnlockSharedAndLockFor(typename Helper::mutex& m, const long long jumpMs)
{
    boost::shared_lock_guard<typename Helper::mutex> g(m);

    typename Helper::steady_time_point before(Helper::steadyNow());
    bool noTimeout = m.try_unlock_shared_and_lock_for(Helper::dur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur, after - before, noTimeout);
}

template <typename Helper>
void testTryUnlockSharedAndLockUntilSteady(typename Helper::mutex& m, const long long jumpMs)
{
    boost::shared_lock_guard<typename Helper::mutex> g(m);

    typename Helper::steady_time_point before(Helper::steadyNow());
    bool noTimeout = m.try_unlock_shared_and_lock_until(Helper::steadyNow() + Helper::dur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur, after - before, noTimeout);
}

template <typename Helper>
void testTryUnlockSharedAndLockUntilSystem(typename Helper::mutex& m, const long long jumpMs)
{
    boost::shared_lock_guard<typename Helper::mutex> g(m);

    typename Helper::steady_time_point before(Helper::steadyNow());
    bool noTimeout = m.try_unlock_shared_and_lock_until(Helper::systemNow() + Helper::dur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur - typename Helper::milliseconds(jumpMs), after - before, noTimeout);
}

template <typename Helper>
void testTryUnlockSharedAndLockUntilCustom(typename Helper::mutex& m, const long long jumpMs)
{
    boost::shared_lock_guard<typename Helper::mutex> g(m);

    typename Helper::steady_time_point before(Helper::steadyNow());
    bool noTimeout = m.try_unlock_shared_and_lock_until(Helper::customNow() + Helper::dur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur - typename Helper::milliseconds(jumpMs), after - before, noTimeout);
}

//--------------------------------------

template <typename Helper>
void testTryUnlockUpgradeAndLockFor(typename Helper::mutex& m, const long long jumpMs)
{
    boost::upgrade_lock<typename Helper::mutex> g(m);

    typename Helper::steady_time_point before(Helper::steadyNow());
    bool noTimeout = m.try_unlock_upgrade_and_lock_for(Helper::dur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur, after - before, noTimeout);
}

template <typename Helper>
void testTryUnlockUpgradeAndLockUntilSteady(typename Helper::mutex& m, const long long jumpMs)
{
    boost::upgrade_lock<typename Helper::mutex> g(m);

    typename Helper::steady_time_point before(Helper::steadyNow());
    bool noTimeout = m.try_unlock_upgrade_and_lock_until(Helper::steadyNow() + Helper::dur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur, after - before, noTimeout);
}

template <typename Helper>
void testTryUnlockUpgradeAndLockUntilSystem(typename Helper::mutex& m, const long long jumpMs)
{
    boost::upgrade_lock<typename Helper::mutex> g(m);

    typename Helper::steady_time_point before(Helper::steadyNow());
    bool noTimeout = m.try_unlock_upgrade_and_lock_until(Helper::systemNow() + Helper::dur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur - typename Helper::milliseconds(jumpMs), after - before, noTimeout);
}

template <typename Helper>
void testTryUnlockUpgradeAndLockUntilCustom(typename Helper::mutex& m, const long long jumpMs)
{
    boost::upgrade_lock<typename Helper::mutex> g(m);

    typename Helper::steady_time_point before(Helper::steadyNow());
    bool noTimeout = m.try_unlock_upgrade_and_lock_until(Helper::customNow() + Helper::dur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur - typename Helper::milliseconds(jumpMs), after - before, noTimeout);
}

//--------------------------------------

template <typename Helper>
void testTryUnlockSharedAndLockUpgradeFor(typename Helper::mutex& m, const long long jumpMs)
{
    boost::shared_lock_guard<typename Helper::mutex> g(m);

    typename Helper::steady_time_point before(Helper::steadyNow());
    bool noTimeout = m.try_unlock_shared_and_lock_upgrade_for(Helper::dur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur, after - before, noTimeout);
}

template <typename Helper>
void testTryUnlockSharedAndLockUpgradeUntilSteady(typename Helper::mutex& m, const long long jumpMs)
{
    boost::shared_lock_guard<typename Helper::mutex> g(m);

    typename Helper::steady_time_point before(Helper::steadyNow());
    bool noTimeout = m.try_unlock_shared_and_lock_upgrade_until(Helper::steadyNow() + Helper::dur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur, after - before, noTimeout);
}

template <typename Helper>
void testTryUnlockSharedAndLockUpgradeUntilSystem(typename Helper::mutex& m, const long long jumpMs)
{
    boost::shared_lock_guard<typename Helper::mutex> g(m);

    typename Helper::steady_time_point before(Helper::steadyNow());
    bool noTimeout = m.try_unlock_shared_and_lock_upgrade_until(Helper::systemNow() + Helper::dur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur - typename Helper::milliseconds(jumpMs), after - before, noTimeout);
}

template <typename Helper>
void testTryUnlockSharedAndLockUpgradeUntilCustom(typename Helper::mutex& m, const long long jumpMs)
{
    boost::shared_lock_guard<typename Helper::mutex> g(m);

    typename Helper::steady_time_point before(Helper::steadyNow());
    bool noTimeout = m.try_unlock_shared_and_lock_upgrade_until(Helper::customNow() + Helper::dur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur - typename Helper::milliseconds(jumpMs), after - before, noTimeout);
}

#endif

//--------------------------------------

// Only Boost supports upgrade mutexes

template <typename Helper>
void testMutexUpgradeBoost(const std::string& name)
{
// The following functions are not implemented in the Windows version of shared_mutex.
#ifndef _WIN32
    std::cout << std::endl;
    runTest<Helper, testTryLockUpgradeFor        <Helper> >(name + "::try_lock_upgrade_for()");
    runTest<Helper, testTryLockUpgradeUntilSteady<Helper> >(name + "::try_lock_upgrade_until(), steady time");
    runTest<Helper, testTryLockUpgradeUntilSystem<Helper> >(name + "::try_lock_upgrade_until(), system time");
    runTest<Helper, testTryLockUpgradeUntilCustom<Helper> >(name + "::try_lock_upgrade_until(), custom time");
    runTest<Helper, testTimedLockUpgradeDur      <Helper> >(name + "::timed_lock_upgrade(), posix duration");
    runTest<Helper, testTimedLockUpgradeSystem   <Helper> >(name + "::timed_lock_upgrade(), posix system time");

    std::cout << std::endl;
    runTestShared<Helper, testTryUnlockSharedAndLockFor        <Helper> >(name + "::try_unlock_shared_and_lock_for()");
    runTestShared<Helper, testTryUnlockSharedAndLockUntilSteady<Helper> >(name + "::try_unlock_shared_and_lock_until(), steady time");
    runTestShared<Helper, testTryUnlockSharedAndLockUntilSystem<Helper> >(name + "::try_unlock_shared_and_lock_until(), system time");
    runTestShared<Helper, testTryUnlockSharedAndLockUntilCustom<Helper> >(name + "::try_unlock_shared_and_lock_until(), custom time");

    std::cout << std::endl;
    runTestShared<Helper, testTryUnlockUpgradeAndLockFor        <Helper> >(name + "::try_unlock_upgrade_and_lock_for()");
    runTestShared<Helper, testTryUnlockUpgradeAndLockUntilSteady<Helper> >(name + "::try_unlock_upgrade_and_lock_until(), steady time");
    runTestShared<Helper, testTryUnlockUpgradeAndLockUntilSystem<Helper> >(name + "::try_unlock_upgrade_and_lock_until(), system time");
    runTestShared<Helper, testTryUnlockUpgradeAndLockUntilCustom<Helper> >(name + "::try_unlock_upgrade_and_lock_until(), custom time");

    std::cout << std::endl;
    runTestUpgrade<Helper, testTryUnlockSharedAndLockFor        <Helper> >(name + "::try_unlock_shared_and_lock_upgrade_for()");
    runTestUpgrade<Helper, testTryUnlockSharedAndLockUntilSteady<Helper> >(name + "::try_unlock_shared_and_lock_upgrade_until(), steady time");
    runTestUpgrade<Helper, testTryUnlockSharedAndLockUntilSystem<Helper> >(name + "::try_unlock_shared_and_lock_upgrade_until(), system time");
    runTestUpgrade<Helper, testTryUnlockSharedAndLockUntilCustom<Helper> >(name + "::try_unlock_shared_and_lock_upgrade_until(), custom time");
#endif
}

//******************************************************************************

// Test Future Wait

template <typename Helper>
void testFutureWaitFor(const long long jumpMs)
{
    typename Helper::packaged_task pt(returnFalse);
    typename Helper::test_var f = pt.get_future();

    typename Helper::steady_time_point before(Helper::steadyNow());
    bool noTimeout = (f.wait_for(Helper::dur) == Helper::future_status::ready);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur, after - before, noTimeout);
}

template <typename Helper>
void testFutureWaitUntilSteady(const long long jumpMs)
{
    typename Helper::packaged_task pt(returnFalse);
    typename Helper::test_var f = pt.get_future();

    typename Helper::steady_time_point before(Helper::steadyNow());
    bool noTimeout = (f.wait_until(Helper::steadyNow() + Helper::dur) == Helper::future_status::ready);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur, after - before, noTimeout);
}

template <typename Helper>
void testFutureWaitUntilSystem(const long long jumpMs)
{
    typename Helper::packaged_task pt(returnFalse);
    typename Helper::test_var f = pt.get_future();

    typename Helper::steady_time_point before(Helper::steadyNow());
    bool noTimeout = (f.wait_until(Helper::systemNow() + Helper::dur) == Helper::future_status::ready);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur - typename Helper::milliseconds(jumpMs), after - before, noTimeout);
}

template <typename Helper>
void testFutureWaitUntilCustom(const long long jumpMs)
{
    typename Helper::packaged_task pt(returnFalse);
    typename Helper::test_var f = pt.get_future();

    typename Helper::steady_time_point before(Helper::steadyNow());
    bool noTimeout = (f.wait_until(Helper::customNow() + Helper::dur) == Helper::future_status::ready);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur - typename Helper::milliseconds(jumpMs), after - before, noTimeout);
}

//--------------------------------------

template <typename Helper>
void testFutureTimedWaitDur(const long long jumpMs)
{
#ifndef SKIP_DATETIME_FUNCTIONS
    typename Helper::packaged_task pt(returnFalse);
    typename Helper::test_var f = pt.get_future();

    typename Helper::steady_time_point before(Helper::steadyNow());
    boost::posix_time::milliseconds ptDur(boost::chrono::duration_cast<boost::chrono::milliseconds>(Helper::dur).count());
    bool noTimeout = f.timed_wait(ptDur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur, after - before, noTimeout);
#else
    checkWaitTime<Helper>(Helper::zero(), Helper::zero(), false);
#endif
}

template <typename Helper>
void testFutureTimedWaitSystem(const long long jumpMs)
{
#ifndef SKIP_DATETIME_FUNCTIONS
    typename Helper::packaged_task pt(returnFalse);
    typename Helper::test_var f = pt.get_future();

    typename Helper::steady_time_point before(Helper::steadyNow());
    boost::posix_time::ptime ptNow(boost::posix_time::microsec_clock::universal_time());
    boost::posix_time::milliseconds ptDur(boost::chrono::duration_cast<boost::chrono::milliseconds>(Helper::dur).count());
    bool noTimeout = f.timed_wait_until(ptNow + ptDur);
    typename Helper::steady_time_point after(Helper::steadyNow());

    checkWaitTime<Helper>(Helper::dur - typename Helper::milliseconds(jumpMs), after - before, noTimeout);
#else
    checkWaitTime<Helper>(Helper::zero(), Helper::zero(), false);
#endif
}

//--------------------------------------

template <typename Helper>
void testFutureStd(const std::string& name)
{
    std::cout << std::endl;
    runTest<Helper, testFutureWaitFor        <Helper> >(name + "::wait_for()");
    runTest<Helper, testFutureWaitUntilSteady<Helper> >(name + "::wait_until(), steady time");
    runTest<Helper, testFutureWaitUntilSystem<Helper> >(name + "::wait_until(), system time");
    runTest<Helper, testFutureWaitUntilCustom<Helper> >(name + "::wait_until(), custom time");
}

template <typename Helper>
void testFutureBoost(const std::string& name)
{
    testFutureStd<Helper>(name);

    // Boost-only functions
    runTest<Helper, testFutureTimedWaitDur   <Helper> >(name + "::timed_wait(), posix duration");
    runTest<Helper, testFutureTimedWaitSystem<Helper> >(name + "::timed_wait_until(), posix system time");
}

//******************************************************************************

int main()
{
    std::cout << std::endl;
    std::cout << "NOTE: Run this test as root so it can change the system clock" << std::endl;
    std::cout << "NOTE: Disable NTP while this test is running" << std::endl;
    std::cout << std::endl;

    std::cout << "BOOST_HAS_PTHREAD_DELAY_NP:                    ";
#ifdef BOOST_HAS_PTHREAD_DELAY_NP
    std::cout << "YES" << std::endl;
#else
    std::cout << "NO" << std::endl;
#endif

    std::cout << "BOOST_HAS_NANOSLEEP:                           ";
#ifdef BOOST_HAS_NANOSLEEP
    std::cout << "YES" << std::endl;
#else
    std::cout << "NO" << std::endl;
#endif

    std::cout << "BOOST_THREAD_SLEEP_FOR_IS_STEADY:              ";
#ifdef BOOST_THREAD_SLEEP_FOR_IS_STEADY
    std::cout << "YES" << std::endl;
#else
    std::cout << "NO" << std::endl;
#endif

    std::cout << "CLOCK_MONOTONIC:                               ";
#ifdef CLOCK_MONOTONIC
    std::cout << "YES" << std::endl;
#else
    std::cout << "NO" << std::endl;
#endif

    std::cout << "BOOST_THREAD_HAS_CONDATTR_SET_CLOCK_MONOTONIC: ";
#ifdef BOOST_THREAD_HAS_CONDATTR_SET_CLOCK_MONOTONIC
    std::cout << "YES" << std::endl;
#else
    std::cout << "NO" << std::endl;
#endif

    std::cout << std::endl;
    std::cout << "Wait Time:              " << s_waitMs << " ms" << std::endl;
    std::cout << "Short Jump Time:        " << s_shortJumpMs << " ms" << std::endl;
    std::cout << "Long Jump Time:         " << s_longJumpMs << " ms" << std::endl;
    std::cout << "Sleep Before Jump Time: " << s_sleepBeforeJumpMs << " ms" << std::endl;
    std::cout << "Max Early Error:        " << s_maxEarlyErrorMs << " ms" << std::endl;
    std::cout << "Max Late Error:         " << s_maxLateErrorMs << " ms" << std::endl;

    testSleepBoost             <BoostHelper<                                           > >("boost");
    testSleepNoIntBoost        <BoostHelper<                                           > >("boost");
    testSleepNoThreadBoost     <BoostHelper<                                           > >("boost");
    testSleepNoThreadNoIntBoost<BoostHelper<                                           > >("boost");
    testJoinBoost              <BoostHelper<                                           > >("boost");
    testCondVarBoost           <BoostHelper<boost::mutex, boost::condition_variable    > >("boost::condition_variable");
    testCondVarPredBoost       <BoostHelper<boost::mutex, boost::condition_variable    > >("boost::condition_variable");
    testCondVarBoost           <BoostHelper<boost::mutex, boost::condition_variable_any> >("boost::condition_variable_any");
    testCondVarPredBoost       <BoostHelper<boost::mutex, boost::condition_variable_any> >("boost::condition_variable_any");
    testMutexBoost             <BoostHelper<boost::timed_mutex                         > >("boost::timed_mutex");
    testMutexBoost             <BoostHelper<boost::recursive_timed_mutex               > >("boost::recursive_timed_mutex");
    testMutexBoost             <BoostHelper<boost::shared_mutex                        > >("boost::shared_mutex"); // upgrade_mutex is a typedef of shared_mutex, so no need to test upgrade_mutex
    testMutexSharedBoost       <BoostHelper<boost::shared_mutex                        > >("boost::shared_mutex"); // upgrade_mutex is a typedef of shared_mutex, so no need to test upgrade_mutex
    testMutexUpgradeBoost      <BoostHelper<boost::shared_mutex                        > >("boost::shared_mutex"); // upgrade_mutex is a typedef of shared_mutex, so no need to test upgrade_mutex
    testFutureBoost            <BoostHelper<boost::mutex, boost::future<bool>          > >("boost::future");
    testFutureBoost            <BoostHelper<boost::mutex, boost::shared_future<bool>   > >("boost::shared_future");

#ifdef CPP14_ENABLED
//    testSleepStd        <StdHelper<                                       > >("std");
//    testSleepNoThreadStd<StdHelper<                                       > >("std");
//    testCondVarStd      <StdHelper<std::mutex, std::condition_variable    > >("std::condition_variable");
//    testCondVarPredStd  <StdHelper<std::mutex, std::condition_variable    > >("std::condition_variable");
//    testCondVarStd      <StdHelper<std::mutex, std::condition_variable_any> >("std::condition_variable_any");
//    testCondVarPredStd  <StdHelper<std::mutex, std::condition_variable_any> >("std::condition_variable_any");
//    testMutexStd        <StdHelper<std::timed_mutex                       > >("std::timed_mutex");
//    testMutexStd        <StdHelper<std::recursive_timed_mutex             > >("std::recursive_timed_mutex");
//    testMutexStd        <StdHelper<std::shared_timed_mutex                > >("std::shared_timed_mutex");
//    testMutexSharedStd  <StdHelper<std::shared_timed_mutex                > >("std::shared_timed_mutex");
//    testFutureStd       <StdHelper<std::mutex, std::future<bool>          > >("std::future");
//    testFutureStd       <StdHelper<std::mutex, std::shared_future<bool>   > >("std::shared_future");
#endif

    std::cout << std::endl;
    std::cout << "Number of Tests Run:    " << g_numTestsRun << std::endl;
    std::cout << "Number of Tests Passed: " << g_numTestsPassed << std::endl;
    std::cout << "Number of Tests Failed: " << g_numTestsFailed << std::endl;

    return 0;
}
