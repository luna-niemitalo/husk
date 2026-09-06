#include "canon_policy.hpp"

#include <stdexcept>

namespace husk::canon {

void enforcePartialFailurePolicy(const m2::Model& model, const char* field, PartialFailurePolicy policy) {
    if (policy == PartialFailurePolicy::Tolerant) return;
    for (const auto& f : model.parseFailures) {
        if (f.field == field) throw std::runtime_error(f.what);
    }
}

}  // namespace husk::canon
