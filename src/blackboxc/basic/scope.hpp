//
// Created by User on 2026-04-21.
//

#ifndef BLACKBOX_SCOPE_HPP
#define BLACKBOX_SCOPE_HPP
#include "types.hpp"
#include <optional>
#include <string>
#include <vector>

namespace basic {

class Scope {
  public:
    explicit Scope(uint32_t* global_counter = nullptr, std::string namespace_prefix = {});

    Variable* find(const std::string& name);

    // alloc
    Variable* add_int(const std::string& name, bool is_global = false);
    Variable* add_str(const std::string& name, const std::string& data_name, bool is_const,
                      bool is_global = false);
    Variable* add_ref(const std::string& name);

    // slots
    uint32_t next_local_slot() const { return next_slot_; }
    uint32_t alloc_local_slot() { return next_slot_++; }
    void set_slot_start(uint32_t s) { next_slot_ = s; }

    // scope
    void set_parent(Scope* parent) { parent_ = parent; }
    Scope* parent() const { return parent_; }

    // namespaceprefix
    const std::string& ns_prefix() const { return ns_prefix_; }

  private:
    std::string ns_prefix_;
    std::vector<Variable> vars_;
    uint32_t next_slot_ = 0;
    uint32_t* global_counter_ = nullptr;
    Scope* parent_ = nullptr;

    std::string mangle(const std::string& name) const;
    uint32_t alloc_global_slot();
};
} // namespace basic
#endif // BLACKBOX_SCOPE_HPP
