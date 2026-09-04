#include "harness.hpp"

namespace mp1test {

bool Cluster::Start(int n, uint16_t base_port, std::string* err) {
    (void)n; (void)base_port;
    if (err) *err = "Cluster::Start not implemented";
    return false;  // TODO
}

bool Cluster::Kill(int machine_id, std::string* err) {
    (void)machine_id;
    if (err) *err = "Cluster::Kill not implemented";
    return false;  // TODO
}

void Cluster::StopAll() {
    // TODO
}

}  // namespace mp1test
