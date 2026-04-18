//
// Created by User on 2026-04-18.
//

#ifndef BLACKBOX_FAULT_HPP
#define BLACKBOX_FAULT_HPP

#include <cstdint>
#include <string>

enum class FaultType : uint8_t {
    PermRead,
    PermWrite,
    BadSyscall,
    Priv,
    DivZero,
    OOB,
    EnvVarNotFound,
    Count // not a real fault
};

inline std::string_view fault_name(FaultType type) {
    switch (type) {
        case FaultType::PermRead:
            return "PERM_READ";
        case FaultType::PermWrite:
            return "PERM_WRITE";
        case FaultType::BadSyscall:
            return "BAD_SYSCALL";
        case FaultType::Priv:
            return "PRIV";
        case FaultType::DivZero:
            return "DIV_ZERO";
        case FaultType::OOB:
            return "OOB";
        case FaultType::EnvVarNotFound:
            return "ENV_VAR_NOT_FOUND";
        default:
            return "UNKNOWN";
    }
}
struct VMFault {
    FaultType type;
    std::string name;
    size_t program_counter;
};
#endif // BLACKBOX_FAULT_HPP
