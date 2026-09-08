#pragma once
//
// Minimal test harness. The spec allows googletest/junit or "tests in a raw
// manner (function calls)" -- this is the raw route with just enough structure
// to get named cases, counted failures, and a meaningful exit code, and with no
// dependency to install on the VMs.
//
// This file is the FRAMEWORK. The tests themselves go in test_*.cpp.
//
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace mp1test {

struct Registry {
    struct Case { std::string name; void (*fn)(); };
    static std::vector<Case>& cases() { static std::vector<Case> c; return c; }
    static int& failures() { static int f = 0; return f; }
    static int& checks()   { static int c = 0; return c; }
    static std::string& current() { static std::string n; return n; }
};

struct Registrar {
    Registrar(const char* name, void (*fn)()) {
        Registry::cases().push_back({name, fn});
    }
};

// Renders a value for a failure message. "3 vs 4" is the difference between a
// failure you can act on and one you have to reproduce under a debugger.
template <typename T>
std::string Show(const T& value) {
    std::ostringstream out;
    out << value;
    return out.str();
}

// Turns a hang into a loud failure.
//
// A distributed suite has more ways to hang than to fail: a pipe nobody drains,
// a read with no timeout, a daemon that never answers. A suite that hangs tells
// you nothing and blocks the whole run, so every case runs under a deadline and
// the process exits, naming the case, if it is missed.
class Watchdog {
public:
    Watchdog(std::chrono::seconds limit, std::string name)
        : name_(std::move(name)) {
        worker_ = std::thread([this, limit] {
            std::unique_lock<std::mutex> lock(mutex_);
            if (!finished_.wait_for(lock, limit, [this] { return done_; })) {
                std::printf("  FAIL %s: still running after %lld s -- "
                            "aborting the suite\n",
                            name_.c_str(), static_cast<long long>(limit.count()));
                std::fflush(stdout);
                std::_Exit(2);
            }
        });
    }

    ~Watchdog() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            done_ = true;
        }
        finished_.notify_all();
        worker_.join();
    }

    Watchdog(const Watchdog&)            = delete;
    Watchdog& operator=(const Watchdog&) = delete;

private:
    std::string             name_;
    std::mutex              mutex_;
    std::condition_variable finished_;
    bool                    done_ = false;
    std::thread             worker_;
};

#define TEST(name)                                                       \
    static void name();                                                  \
    static ::mp1test::Registrar reg_##name(#name, name);                 \
    static void name()

#define CHECK(cond)                                                      \
    do {                                                                 \
        ++::mp1test::Registry::checks();                                 \
        if (!(cond)) {                                                   \
            ++::mp1test::Registry::failures();                           \
            std::printf("  FAIL %s:%d: CHECK(%s)\n",                     \
                        __FILE__, __LINE__, #cond);                      \
        }                                                                \
    } while (0)

#define CHECK_EQ(a, b)                                                   \
    do {                                                                 \
        ++::mp1test::Registry::checks();                                 \
        const auto va = (a); const auto vb = (b);                        \
        if (!(va == vb)) {                                               \
            ++::mp1test::Registry::failures();                           \
            std::printf("  FAIL %s:%d: %s == %s  (%s vs %s)\n",          \
                        __FILE__, __LINE__, #a, #b,                      \
                        ::mp1test::Show(va).c_str(),                     \
                        ::mp1test::Show(vb).c_str());                    \
        }                                                                \
    } while (0)

// Like CHECK, but ABANDONS the case, and prints `detail` -- an error string
// from whatever just failed. For preconditions: there is nothing to learn from
// a hundred assertions about a cluster that never started, and the cascade
// buries the one failure that matters. Usable only in a TEST body; it returns.
#define REQUIRE(cond, detail)                                            \
    do {                                                                 \
        ++::mp1test::Registry::checks();                                 \
        if (!(cond)) {                                                   \
            ++::mp1test::Registry::failures();                           \
            std::printf("  FAIL %s:%d: REQUIRE(%s): %s\n",               \
                        __FILE__, __LINE__, #cond,                       \
                        ::mp1test::Show(detail).c_str());                \
            return;                                                      \
        }                                                                \
    } while (0)

int RunAll();

}  // namespace mp1test
