/* Test-only substitute for the upstream visibility macro. */
#include <assert.h>
#define PUBLIC __attribute__((visibility("default")))
