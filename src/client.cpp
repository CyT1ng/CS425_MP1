#include "mp1/client.hpp"

namespace mp1 {

const char* StatusText(MachineStatus status) {
    (void)status;
    // TODO: one short label per status, for the summary table.
    return "?";
}

MachineResult QueryOne(const Machine& machine,
                       const std::vector<std::string>& grep_args,
                       const QueryOptions& opts) {
    (void)machine; (void)grep_args; (void)opts;
    // TODO: Connect -> SendRequest -> RecvFrame until the END frame.
    // Record this machine's own latency around the whole exchange.
    //
    // Never let a failure escape this function. A dead machine is a normal
    // outcome that belongs in `status`, not an exception that aborts the query
    // -- that is the fault-tolerance requirement, expressed as a return type.
    return MachineResult{};
}

QuerySummary RunQuery(const std::vector<Machine>& machines,
                      const std::vector<std::string>& grep_args,
                      const QueryOptions& opts) {
    (void)machines; (void)grep_args; (void)opts;
    // TODO: t0, one std::async(std::launch::async, ...) per machine, collect
    // every future, t1. See client.hpp for why launch::async is mandatory.
    return QuerySummary{};
}

void PrintSummary(const QuerySummary& summary, const QueryOptions& opts) {
    (void)summary; (void)opts;
    // TODO: the per-machine table, then the totals line.
}

}  // namespace mp1
