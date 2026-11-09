#ifndef RME_FAKE_VIBRATION_GUEST_H
#define RME_FAKE_VIBRATION_GUEST_H

#include <stdint.h>

/* Test-only sender: does not register a HAL or authenticate a Guest UID. */
struct fake_guest { int fd; uint64_t epoch, sequence; int stopped; };
int fake_guest_on(struct fake_guest *guest, uint32_t duration_ms);
int fake_guest_off(struct fake_guest *guest);
void fake_guest_stop(struct fake_guest *guest);

#endif
