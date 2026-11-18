#include "principal_scope.h"
#include <memory>
extern "C" rme::PrincipalScope* rme_test_scope_begin(const std::shared_ptr<const rme::Principal>* principal) {
    return new rme::PrincipalScope(*principal);
}
extern "C" void rme_test_scope_end(rme::PrincipalScope* scope) { delete scope; }
