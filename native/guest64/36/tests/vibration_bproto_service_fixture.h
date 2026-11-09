#ifndef RME_TEST_VIBRATION_BPROTO_SERVICE_FIXTURE_H
#define RME_TEST_VIBRATION_BPROTO_SERVICE_FIXTURE_H

#include "../vibration_bproto_client.h"
#include "../vibration_binder_contract.h"
#include "../vibration_service_registration.h"

/* Offline-only synthetic service. Not a product entry point or authorization. */
int rme_test_vib_service(struct rme_bproto_client *,
                         const struct rme_vib_add_service_input *,
                         const struct rme_vib_ops *, int stop_fd);
#endif
