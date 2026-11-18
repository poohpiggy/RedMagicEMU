#include "principal_scope.h"
#include <memory>
extern "C" std::shared_ptr<const rme::Principal> rme_test_scope_consume() {
    return rme::PrincipalScope::take();
}
