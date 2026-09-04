#include "mp1/config.hpp"

namespace mp1 {

bool LoadMachines(const std::string& path, std::vector<Machine>& out,
                  std::string* err) {
    (void)path; (void)out;
    if (err) *err = "LoadMachines not implemented";
    return false;  // TODO
}

std::string LogFileName(int id) {
    return "machine." + std::to_string(id) + ".log";
}

}  // namespace mp1
