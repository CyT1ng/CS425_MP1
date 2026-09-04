#include "test_framework.hpp"

namespace mp1test {

int RunAll() {
    std::printf("running %zu test cases\n\n", Registry::cases().size());
    for (auto& c : Registry::cases()) {
        Registry::current() = c.name;
        int before = Registry::failures();
        std::printf("[ RUN  ] %s\n", c.name.c_str());
        c.fn();
        bool ok = (Registry::failures() == before);
        std::printf("[ %s ] %s\n", ok ? "  OK  " : "FAILED", c.name.c_str());
    }
    std::printf("\n%d checks, %d failures\n",
                Registry::checks(), Registry::failures());
    return Registry::failures() == 0 ? 0 : 1;
}

}  // namespace mp1test

int main() { return mp1test::RunAll(); }
