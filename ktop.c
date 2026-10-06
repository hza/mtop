// ktop - minimal top: top 16 processes by CPU, arrows move, Backspace kills.
#include <errno.h>
#include <libproc.h>
#include <mach/mach_time.h>
#include <ncurses.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>
#include <time.h>
#include <unistd.h>

#define TOPN 16
#define MAXP 8192

typedef struct {
    pid_t pid;
    uint64_t cpu;   // cumulative cpu ns
    double pct;
    char name[64];
} proc_t;

static proc_t cur[MAXP], prev[MAXP];
static int ncur, nprev;
static uint64_t prev_wall;
static mach_timebase_info_data_t tb;

static uint64_t now_ns(void) {
    return clock_gettime_nsec_np(CLOCK_MONOTONIC);
}

static int by_pid(const void *a, const void *b) {
    return ((const proc_t *)a)->pid - ((const proc_t *)b)->pid;
}

static int by_cpu(const void *a, const void *b) {
    double d = ((const proc_t *)b)->pct - ((const proc_t *)a)->pct;
    return d > 0 ? 1 : d < 0 ? -1 : by_pid(a, b);
}

static void sample(void) {
    static pid_t pids[MAXP];
    int n = proc_listallpids(pids, sizeof pids);
    uint64_t wall = now_ns();
    double dt = prev_wall ? (double)(wall - prev_wall) : 0;

    memcpy(prev, cur, sizeof(proc_t) * ncur);
    nprev = ncur;
    qsort(prev, nprev, sizeof(proc_t), by_pid);

    ncur = 0;
    for (int i = 0; i < n && ncur < MAXP; i++) {
        struct proc_taskinfo ti;
        if (pids[i] <= 0) continue;
        if (proc_pidinfo(pids[i], PROC_PIDTASKINFO, 0, &ti, sizeof ti) != sizeof ti) continue;
        proc_t *p = &cur[ncur++];
        p->pid = pids[i];
        p->cpu = (ti.pti_total_user + ti.pti_total_system) * tb.numer / tb.denom;
        p->pct = 0;
        if (proc_name(p->pid, p->name, sizeof p->name) <= 0)
            snprintf(p->name, sizeof p->name, "?");
        proc_t key = {.pid = p->pid};
        proc_t *q = bsearch(&key, prev, nprev, sizeof(proc_t), by_pid);
        if (q && dt > 0 && p->cpu >= q->cpu)
            p->pct = (p->cpu - q->cpu) * 100.0 / dt;
    }
    prev_wall = wall;
    qsort(cur, ncur, sizeof(proc_t), by_cpu);
}

static void cmdline(pid_t pid, char *out, size_t outsz) {
    int mib[3] = {CTL_KERN, KERN_PROCARGS2, pid};
    static char buf[1 << 18];
    size_t sz = sizeof buf;
    out[0] = 0;
    if (sysctl(mib, 3, buf, &sz, NULL, 0) < 0 || sz < sizeof(int)) {
        snprintf(out, outsz, "(command line unavailable)");
        return;
    }
    int argc;
    memcpy(&argc, buf, sizeof argc);
    char *p = buf + sizeof argc, *end = buf + sz;
    while (p < end && *p) p++;   // exec path
    while (p < end && !*p) p++;  // padding
    size_t len = 0;
    for (int i = 0; i < argc && p < end; i++) {
        size_t l = strnlen(p, end - p);
        if (len && len + 1 < outsz) out[len++] = ' ';
        size_t c = l < outsz - 1 - len ? l : outsz - 1 - len;
        memcpy(out + len, p, c);
        len += c;
        p += l + 1;
    }
    out[len] = 0;
}

int main(void) {
    mach_timebase_info(&tb);
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    timeout(250);

    pid_t sel_pid = -1;
    int sel = 0;
    char status[256] = "";
    char cmd[4096];
    uint64_t last = 0;

    for (;;) {
        if (now_ns() - last >= 1000000000ULL) {
            sample();
            last = now_ns();
        }
        int n = ncur < TOPN ? ncur : TOPN;
        for (int i = 0; i < n; i++)
            if (cur[i].pid == sel_pid) { sel = i; break; }
        if (sel >= n) sel = n - 1;
        if (sel < 0) sel = 0;
        sel_pid = n ? cur[sel].pid : -1;

        erase();
        attron(A_BOLD);
        mvprintw(0, 0, "ktop  up/down move  backspace kill  q quit");
        mvprintw(1, 0, "  %-7s %6s  %s", "PID", "CPU%", "NAME");
        attroff(A_BOLD);
        int y = 2;
        for (int i = 0; i < n; i++, y++) {
            if (i == sel) attron(A_REVERSE);
            mvprintw(y, 0, "%c %-7d %6.1f  %-*.*s", i == sel ? '>' : ' ', cur[i].pid,
                     cur[i].pct, COLS > 18 ? COLS - 18 : 0, COLS > 18 ? COLS - 18 : 0,
                     cur[i].name);
            if (i == sel) {
                attroff(A_REVERSE);
                cmdline(cur[i].pid, cmd, sizeof cmd);
                attron(A_DIM);
                mvprintw(++y, 10, "%.*s", COLS > 10 ? COLS - 10 : 0, cmd);
                attroff(A_DIM);
            }
        }
        mvprintw(y + 1, 0, "%.*s", COLS, status);
        refresh();

        int ch = getch();
        if (ch == 'q' || ch == 'Q') break;
        if ((ch == KEY_UP || ch == 'k') && sel > 0) sel_pid = cur[--sel].pid;
        if ((ch == KEY_DOWN || ch == 'j') && sel < n - 1) sel_pid = cur[++sel].pid;
        if ((ch == KEY_BACKSPACE || ch == 127 || ch == 8) && n) {
            if (kill(sel_pid, SIGTERM) == 0)
                snprintf(status, sizeof status, "killed %d (%s)", sel_pid, cur[sel].name);
            else
                snprintf(status, sizeof status, "kill %d: %s", sel_pid, strerror(errno));
            last = 0;  // resample now
        }
    }
    endwin();
    return 0;
}
