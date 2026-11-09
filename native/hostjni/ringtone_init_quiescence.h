#ifndef RME_RINGTONE_INIT_QUIESCENCE_H
#define RME_RINGTONE_INIT_QUIESCENCE_H

/*
 * Only for an otherwise dedicated first-initialization process. The caller
 * must durably mark its instance dirty before enabling this facility, close
 * every Guest creation/input gate before waiting, and independently wait for
 * every Java-managed Process and host-side writer. Entry and creation gates
 * must be serialized by the caller. These functions never kill or reap a
 * child. An unrecognized result MUST be treated as uncertain. A process death
 * can bypass any return value; the caller must preserve dirty state.
 */
enum rme_init_quiescence {
    RME_INIT_EMPTY = 0,
    RME_INIT_BUSY = 1,
    RME_INIT_TIMEOUT = 2,
    RME_INIT_INTERRUPTED = 3,
    RME_INIT_UNSUPPORTED = 4,
    RME_INIT_ERROR = 5,
};

/* Requires a new, dedicated process with no children and no prior subreaper. */
enum rme_init_quiescence rme_init_subreaper_begin(void);

/* Observe all child types across the process's threads, without reaping. */
enum rme_init_quiescence rme_init_children_now(void);

/* Requires successful begin. At most 600000 ms; not a stop request or proof
 * of host-thread quiescence. Unreaped zombies cause a timeout. */
enum rme_init_quiescence rme_init_await_empty(unsigned int timeout_ms);

#endif
