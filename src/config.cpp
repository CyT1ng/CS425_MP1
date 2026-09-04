#include "mp1/config.hpp"

namespace mp1 {

// The contract -- what is tolerated, what is rejected, and the shape of the
// error text -- lives above the declaration in config.hpp. Only implementation
// notes belong here.
//
// Read line by line, tracking a 1-based line number for the error messages,
// and keep the ids seen so far so a duplicate is caught against the line that
// first claimed it. Clear `out` up front: the "empty on failure" guarantee is
// easiest to keep if nothing is appended until a line has fully validated.
bool LoadMachines(const std::string& path, std::vector<Machine>& out,
                  std::string* err) {
    (void)path; (void)out;
    if (err) *err = "LoadMachines not implemented";
    return false;  // TODO
}

// Kept trivial on purpose -- see config.hpp for why this is a function at all
// rather than a format string repeated in mp1d, mp1gen, and the tests.
std::string LogFileName(int id) {
    return "machine." + std::to_string(id) + ".log";
}

}  // namespace mp1
