#pragma once

#include "../blackboxc/asm_util.hpp"
#include <string>
#include <vector>

namespace blackbox::tools {

// expands a single macro invocation line into output lines
bool expand_invocation(
    std::string_view                              line,
    const std::vector<bbxc::asm_helpers::Macro>& macros,
    std::vector<std::string>&                     out,
    unsigned long&                                expand_id,
    int                                           depth = 0);

} // namespace blackbox::tools