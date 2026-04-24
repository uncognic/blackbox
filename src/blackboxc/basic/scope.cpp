//
// Created by User on 2026-04-21.
//

#include "scope.hpp"

namespace basic {
std::vector<std::string> Scope::global_names() const {
    return global_name_order_;
}

std::vector<std::pair<uint32_t, std::string>> Scope::global_symbols() const {
    std::vector<std::pair<uint32_t, std::string>> out;
    out.reserve(global_name_order_.size());

    for (const auto& v : vars_) {
        if (v.is_global) {
            out.emplace_back(v.slot, v.name);
        }
    }
    return out;
}

Scope::Scope(uint32_t* global_counter, std::string namespace_prefix)
    : ns_prefix_(std::move(namespace_prefix)), global_counter_(global_counter) {
}

std::string Scope::mangle(const std::string& name) const {
    if (ns_prefix_.empty()) {
        return name;
    }
    return ns_prefix_ + "__" + name;
}

uint32_t Scope::alloc_global_slot() {
    if (global_counter_) {
        return (*global_counter_)++;
    }
    return next_slot_++;
}

Variable* Scope::find(const std::string& name) {
    // try mangled name first
    if (!ns_prefix_.empty()) {
        std::string mangled = mangle(name);
        for (auto& v : vars_) {
            if (v.name == mangled) {
                return &v;
            }
        }
    }
    // try plain name
    for (auto& v : vars_) {
        if (v.name == name) {
            return &v;
        }
    }
    // walk up to parent scope
    if (parent_) {
        return parent_->find(name);
    }
    return nullptr;
}

Variable* Scope::add_int(const std::string& name, bool is_global) {
    std::string mangled = mangle(name);
    if (Variable* existing = find(mangled)) {
        return existing;
    }

    Variable v;
    v.name = mangled;
    v.type = VarType::Int;
    v.is_global = is_global;
    v.slot = is_global ? alloc_global_slot() : alloc_local_slot();
    if (is_global) {
        global_name_order_.push_back(mangled);
    }
    vars_.push_back(std::move(v));
    return &vars_.back();
}
Variable* Scope::add_str(const std::string& name, const std::string& data_name, bool is_const,
                         bool is_global) {
    std::string mangled = mangle(name);
    if (Variable* existing = find(mangled)) {
        return existing;
    }

    Variable v;
    v.name = mangled;
    v.type = VarType::Str;
    v.is_const = is_const;
    v.is_global = is_global;
    v.data_name = data_name;
    v.slot = is_global ? alloc_global_slot() : alloc_local_slot();
    if (is_global) {
        global_name_order_.push_back(mangled);
    }
    vars_.push_back(std::move(v));
    return &vars_.back();
}
Variable* Scope::add_ref(const std::string& name) {
    std::string mangled = mangle(name);
    if (Variable* existing = find(mangled)) {
        return existing;
    }

    Variable v;
    v.name = mangled;
    v.type = VarType::Int;
    v.is_ref = true;
    v.slot = alloc_local_slot();
    vars_.push_back(std::move(v));
    return &vars_.back();
}
} // namespace basic