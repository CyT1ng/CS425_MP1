#include "test_framework.hpp"

#include "mp1/net.hpp"

namespace mp1test {
namespace {

// Per-case deadline. Well above the slowest honest test (the 300,000-line
// cluster), and far below "I left it running over lunch".
constexpr auto kCaseTimeLimit = std::chrono::seconds(120);

}  // namespace

int RunAll() {
    std::printf("running %zu test cases\n\n", Registry::cases().size());
    for (auto& c : Registry::cases()) {
        Registry::current() = c.name;
        int before = Registry::failures();
        std::printf("[ RUN  ] %s\n", c.name.c_str());
        std::fflush(stdout);  // so an aborted run still shows what it was in
        {
            Watchdog deadline(kCaseTimeLimit, c.name);
            c.fn();
        }
        bool ok = (Registry::failures() == before);
        std::printf("[ %s ] %s\n", ok ? "  OK  " : "FAILED", c.name.c_str());
    }
    std::printf("\n%d checks, %d failures\n",
                Registry::checks(), Registry::failures());
    return Registry::failures() == 0 ? 0 : 1;
}

}  // namespace mp1test

int main() {
    // The suite plays both sides: it runs real clients AND stands in for
    // daemons that die mid-answer. Writing to a peer that has already hung up
    // would otherwise kill this process outright, and a suite that dies is much
    // harder to read than a suite that fails.
    mp1::IgnoreSigpipe();
    return mp1test::RunAll();
}
