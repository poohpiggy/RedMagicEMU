/* Vulkan bridge transport handshake. This does not advertise GPU capability. */
#ifndef RME_VULKANBRIDGE_PROTOCOL_H
#define RME_VULKANBRIDGE_PROTOCOL_H

#include <stdint.h>

/* Both functions perform one handshake on an already-connected stream socket.
 * The caller must hold fd exclusively for the call: no concurrent I/O, close,
 * or reuse. The caller owns fd and must close it after any error or rejection;
 * neither function changes fd flags. timeout_ms is one overall monotonic
 * deadline in the range 1..60000, and version must be nonzero.
 *
 * Return 0 when protocol versions match, 1 when the peer rejects the version,
 * -1 on I/O, EOF or timeout, and -2 for invalid arguments or malformed frames.
 * A successful handshake is neither authentication, Vulkan capability, nor a
 * guarantee that the connection stays alive. The caller must enforce the
 * connection state and reject repeated handshakes or premature commands. */
int vkb_client_hello(int fd, uint32_t version, int timeout_ms);
int vkb_server_hello(int fd, uint32_t version, int timeout_ms);

#endif
