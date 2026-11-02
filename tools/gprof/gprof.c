// gprof <pid> [secs] [tidfilter-name]: samples a qemu-arm guest process. For each thread: ptrace interrupt,
// host regs; x19 is TCG's env (CPUARMState, regs[15] at +60). Prints host-side region (code cache / libqemu / other
// lib) histogram and the top guest PCs (by 64-byte bucket).
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <elf.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <asm/ptrace.h>

struct reg { unsigned long lo, hi; char name[160]; };
static struct reg R[4096]; static int nr;
static void maps(int pid) {
    char p[64]; snprintf(p, sizeof p, "/proc/%d/maps", pid);
    FILE *f = fopen(p, "r"); char l[512];
    while (f && fgets(l, sizeof l, f) && nr < 4096) {
        unsigned long a, b, off; char perm[8], nm[256] = "";
        if (sscanf(l, "%lx-%lx %7s %lx %*s %*s %255[^\n]", &a, &b, perm, &off, nm) < 4) continue;
        if (perm[2] != 'x') continue;
        R[nr].lo = a; R[nr].hi = b; const char *s = strrchr(nm, '/'); snprintf(R[nr].name, 160, "%s", s ? s + 1 : (nm[0] ? nm : "[anon-x]")); nr++;
    }
    if (f) fclose(f);
}
static const char *where(unsigned long pc) { for (int i = 0; i < nr; i++) if (pc >= R[i].lo && pc < R[i].hi) return R[i].name; return "?"; }
struct cnt { unsigned long k; int n; char tag[48]; };
static struct cnt H[200000]; static int nh;
static void add(unsigned long k, const char *tag) {
    for (int i = 0; i < nh; i++) if (H[i].k == k && !strcmp(H[i].tag, tag)) { H[i].n++; return; }
    if (nh < 200000) { H[nh].k = k; H[nh].n = 1; snprintf(H[nh].tag, 48, "%s", tag); nh++; }
}
#include <signal.h>
static void sig(int t, const char *n, int sg) {
    siginfo_t si; char tag[48]; unsigned long a = 0;
    if (ptrace(PTRACE_GETSIGINFO, t, 0, &si) == 0) a = (unsigned long)si.si_addr;
    struct user_pt_regs rr; struct iovec io2 = { &rr, sizeof rr }; const char *w = "?"; unsigned long off = 0;
    if (ptrace(PTRACE_GETREGSET, t, (void *)NT_PRSTATUS, &io2) == 0) { w = where(rr.pc); for (int i = 0; i < nr; i++) if (rr.pc >= R[i].lo && rr.pc < R[i].hi) off = rr.pc - R[i].lo; }
    if (sg == SIGSEGV) { long g = ptrace(PTRACE_PEEKDATA, t, (void *)(rr.regs[19] + 60), 0); snprintf(tag, 48, "SEGV %lx gpc", a); add((unsigned)g, tag); return; }
    snprintf(tag, 48, "%s:SIG%d", n, sg); add(sg == SIGSYS ? (unsigned long)*(int *)((char *)&si + 24) : sg == SIGSEGV || sg == SIGBUS ? a & ~4095ul : 0, tag);
}
static int cmp(const void *a, const void *b) { return ((const struct cnt *)b)->n - ((const struct cnt *)a)->n; }
int main(int c, char **v) {
    int pid = atoi(v[1]);
    if (!pid) { for (int w = 0; !pid && w < 600; w++) { DIR *pd = opendir("/proc"); struct dirent *pe;
        while (pd && (pe = readdir(pd))) { int q = atoi(pe->d_name); if (!q) continue; char cp[64], b[32] = ""; snprintf(cp, 64, "/proc/%d/comm", q);
            FILE *f = fopen(cp, "r"); if (f) { if (fgets(b, 32, f)) b[strcspn(b, "\n")] = 0; fclose(f); } if (!strcmp(b, v[1])) { pid = q; break; } }
        if (pd) closedir(pd); if (!pid) usleep(200000); }
      fprintf(stderr, "pid %d\n", pid); sleep(c > 4 ? atoi(v[4]) : 7); } double secs = c > 2 ? atof(v[2]) : 3; const char *flt = c > 3 ? v[3] : 0;
    maps(pid);
    int tids[256], nt = 0; char tn[256][20];
    char p[64]; snprintf(p, sizeof p, "/proc/%d/task", pid);
    DIR *d = opendir(p); struct dirent *e;
    while (d && (e = readdir(d)) && nt < 256) {
        if (e->d_name[0] == '.') continue;
        int t = atoi(e->d_name); char cp[96]; snprintf(cp, sizeof cp, "/proc/%d/task/%d/comm", pid, t);
        FILE *f = fopen(cp, "r"); tn[nt][0] = 0; if (f) { if (fgets(tn[nt], 20, f)) tn[nt][strcspn(tn[nt], "\n")] = 0; fclose(f); }
        if (flt && !strstr(tn[nt], flt)) continue;
        if (ptrace(PTRACE_SEIZE, t, 0, 0) == 0) tids[nt++] = t; else fprintf(stderr, "seize %d: %s\n", t, strerror(errno));
    }
    if (d) closedir(d);
    struct timespec t0, now; clock_gettime(CLOCK_MONOTONIC, &t0);
    int samples = 0, gc = 0, hostq = 0, other = 0;
    for (;;) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        if ((now.tv_sec - t0.tv_sec) + (now.tv_nsec - t0.tv_nsec) * 1e-9 > secs) break;
        for (int i = 0; i < nt; i++) {
            int t = tids[i];
            char sp[96]; snprintf(sp, sizeof sp, "/proc/%d/task/%d/stat", pid, t);
            FILE *f = fopen(sp, "r"); char st = '?'; if (f) { char b[512]; if (fgets(b, sizeof b, f)) { char *q = strrchr(b, ')'); if (q) st = q[2]; } fclose(f); }
            if (st == 't') { int s0; if (waitpid(t, &s0, __WALL | WNOHANG) > 0 && WIFSTOPPED(s0)) { if (!(s0 >> 16)) sig(t, tn[i], WSTOPSIG(s0)); ptrace(PTRACE_CONT, t, 0, (s0 >> 16) ? 0 : WSTOPSIG(s0)); } }
            char stt[2] = { st, 0 }; if (st != 'R') { char tg[48]; snprintf(tg, 48, "%s:state-%s", tn[i], stt); add(0, tg); continue; }
            if (ptrace(PTRACE_INTERRUPT, t, 0, 0)) continue;
            int s; if (waitpid(t, &s, __WALL) < 0) continue;
            while (WIFSTOPPED(s) && (s >> 16) != PTRACE_EVENT_STOP) { sig(t, tn[i], WSTOPSIG(s)); ptrace(PTRACE_CONT, t, 0, WSTOPSIG(s)); if (waitpid(t, &s, __WALL) < 0) break; }
            struct user_pt_regs r; struct iovec io = { &r, sizeof r };
            if (ptrace(PTRACE_GETREGSET, t, (void *)NT_PRSTATUS, &io) == 0) {
                samples++;
                const char *w = where(r.pc);
                errno = 0; long g = ptrace(PTRACE_PEEKDATA, t, (void *)(r.regs[19] + 60), 0);
                char tag[48];
                if (w[0] == '[' || !strcmp(w, "?")) { gc++; snprintf(tag, 48, "%s:tcg", tn[i]); if (!errno) add((unsigned long)(unsigned)g & ~63ul, tag); }
                else if (strstr(w, "qemu")) { hostq++; snprintf(tag, 48, "%s:qemu", tn[i]); add(r.pc & ~255ul, tag); if (!errno) { char t2[48]; snprintf(t2, 48, "%s:gpc@qemu", tn[i]); add((unsigned long)(unsigned)g & ~63ul, t2); } }
                else { other++; snprintf(tag, 48, "%s:%s", tn[i], w); add(0, tag); }
            }
            ptrace(PTRACE_CONT, t, 0, 0);
        }
        usleep(500);
    }
    for (int i = 0; i < nt; i++) ptrace(PTRACE_DETACH, tids[i], 0, 0);
    printf("samples %d: tcg %d, libqemu %d, other %d\n", samples, gc, hostq, other);
    qsort(H, nh, sizeof *H, cmp);
    unsigned long qlo = 0; for (int i = 0; i < nr; i++) if (strstr(R[i].name, "qemu")) { qlo = R[i].lo; break; }
    for (int i = 0; i < nh && i < 60; i++)
        printf("%5d %-34s %#lx\n", H[i].n, H[i].tag, strstr(H[i].tag, ":qemu") ? H[i].k - qlo : H[i].k);
    return 0;
}
