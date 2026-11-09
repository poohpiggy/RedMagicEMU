/* Experimental preload only. The launcher must authorize Guest requests,
 * pass trusted parameters, and supervise the connection. This constructor
 * verifies neither application permission nor actual Android linker behavior. */
#include "qti_session.h"
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

static struct rme_qti_session session;

static int decimal(const char *text, uint64_t maximum, uint64_t *out) {
    if (!text || !*text) return 0;
    uint64_t value = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        if (*p < '0' || *p > '9') return 0;
        unsigned digit = *p - '0';
        if (value > (maximum - digit) / 10) return 0;
        value = value * 10 + digit;
    }
    *out = value;
    return 1;
}

__attribute__((constructor)) static void rme_qti_autoload(void) {
    const char *name = getenv("RME_VIBRATION_SOCKET");
    const char *pid_text = getenv("RME_VIBRATION_HOST_PID");
    const char *uid_text = getenv("RME_VIBRATION_HOST_UID");
    const char *epoch_text = getenv("RME_VIBRATION_EPOCH");
    uint64_t pid, uid, epoch;
    if (!name && !pid_text && !uid_text && !epoch_text) return;
    if (!name || !decimal(pid_text, INT_MAX, &pid) || pid == 0 ||
        !decimal(uid_text, UINT32_MAX, &uid) || !decimal(epoch_text, INT64_MAX, &epoch) || epoch == 0) {
        static const char error[] = "vibration: invalid session parameters\n";
        (void)write(STDERR_FILENO, error, sizeof(error) - 1);
        return;
    }
    /* On failure the unbound shim continues rejecting all requests. */
    if (rme_qti_session_start(&session, name, (pid_t)pid, (uid_t)uid, epoch) != 0) {
        static const char error[] = "vibration: session start failed\n";
        (void)write(STDERR_FILENO, error, sizeof(error) - 1);
    }
}

__attribute__((destructor)) static void rme_qti_autounload(void) {
    if (session.active && rme_qti_session_close(&session) != 0) {
        static const char error[] = "vibration: session close failed\n";
        (void)write(STDERR_FILENO, error, sizeof(error) - 1);
    }
}
