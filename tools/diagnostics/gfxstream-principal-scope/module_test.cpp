#include "principal_scope.h"
#include <cassert>
#include <memory>
extern "C" rme::PrincipalScope* rme_test_scope_begin(const std::shared_ptr<const rme::Principal>*);
extern "C" void rme_test_scope_end(rme::PrincipalScope*);
extern "C" std::shared_ptr<const rme::Principal> rme_test_scope_consume();
int main() {
    std::shared_ptr<const rme::Principal> principal = std::make_shared<const rme::Principal>();
    auto* scope = rme_test_scope_begin(&principal);
    assert(rme_test_scope_consume() == principal);
    assert(!rme_test_scope_consume());
    rme_test_scope_end(scope);
    assert(!rme_test_scope_consume());
}
