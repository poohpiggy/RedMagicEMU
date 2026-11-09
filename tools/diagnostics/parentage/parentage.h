#ifndef RME_PARENTAGE_DIAGNOSTIC_H
#define RME_PARENTAGE_DIAGNOSTIC_H

#include <sys/types.h>

/* Computer-only diagnostic: verifies observed physical parentage at one point
 * in time. sender_pid must come from kernel SCM_CREDENTIALS. It does not issue
 * identities, verify Guest UIDs or authorize any operation. */
int rme_observe_parentage(int parent_fd, pid_t parent_pid, pid_t sender_pid,
                          int child_fd, pid_t child_pid);

#endif
