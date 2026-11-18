#include "principal_scope.h"
#include <cassert>
#include <memory>

// Deliberately model an unrelated same-thread channel creation that occurs
// inside the ambient scope without establishing its own nested scope. This is
// a counterexample to using the current carrier as channel authorization.
static std::shared_ptr<const rme::Principal> unrelated_create() {
    return rme::PrincipalScope::take();
}
int main() {
    auto principal = std::make_shared<const rme::Principal>();
    rme::PrincipalScope scope(principal);
    auto unintended = unrelated_create();
    auto intended = rme::PrincipalScope::take();
    assert(unintended == principal && !intended);
}
