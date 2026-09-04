#pragma once
//
// Minimal test harness. The spec allows googletest/junit or "tests in a raw
// manner (function calls)" -- this is the raw route with just enough structure
// to get named cases, counted failures, and a meaningful exit code, and with no
// dependency to install on the VMs.
//
// This file is the FRAMEWORK. The tests themselves go in test_*.cpp.
//
#include <cstdio>
#include <string>
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
        auto va = (a); auto vb = (b);                                    \
        if (!(va == vb)) {                                               \
            ++::mp1test::Registry::failures();                           \
            std::printf("  FAIL %s:%d: %s == %s\n",                      \
                        __FILE__, __LINE__, #a, #b);                     \
        }                                                                \
    } while (0)

int RunAll();

}  // namespace mp1test
