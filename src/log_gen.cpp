#include "mp1/log_gen.hpp"

namespace mp1 {

uint64_t ExpectedCounts::TotalFor(const std::string& token) const {
    (void)token;
    return 0;  // TODO
}

bool GenerateLog(const LogSpec& spec, const std::string& out_path,
                 ExpectedCounts& expected, std::string* err) {
    (void)spec; (void)out_path; (void)expected;
    if (err) *err = "GenerateLog not implemented";
    return false;  // TODO -- see log_gen.hpp for the determinism requirements
}

}  // namespace mp1
