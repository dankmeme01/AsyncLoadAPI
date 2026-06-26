#include <AsyncLoad/assert.hpp>
#include <Geode/utils/terminate.hpp>

namespace AsyncLoad {

[[noreturn]] void _assertionFail(std::string_view what, std::string_view file, int line) {
    geode::utils::terminate(fmt::format("Assertion failed ({}) at {}:{}", what, file, line));
}

}
