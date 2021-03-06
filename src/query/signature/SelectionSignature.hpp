#pragma once

#include "query/parser/Common.hpp"
#include "query/util/Predicate.hpp"
#include <memory>
#include <vector>
#include <string>

namespace dbi {

namespace qopt { class GlobalRegister; class Predicate; }

class SelectionSignature {
public:
   /// Create signature using the supplied signature of the underlying operator and the predicate
   SelectionSignature(std::unique_ptr<qopt::Predicate> predicate, qopt::GlobalRegister& golbalRegister);

   bool fullfillsPredicates() const;

   void dump(std::ostream& os) const;

private:
   std::unique_ptr<qopt::Predicate> predicate;
};

}
