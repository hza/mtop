// mtop - minimal top: top processes (fits terminal height), arrows move, k kills, space pauses.
#include <ctype.h>
#include <errno.h>
#include <libproc.h>
#include <locale.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <ncurses.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/sysctl.h>
#include <time.h>
#include <unistd.h>

#define HEAD_ROWS 4    // header, blank, column titles, separator
#define DETAIL_ROWS 16 // separator, CMD/ARGS/CWD, filter, status, help
#define MAXP 8192

typedef struct {
    pid_t pid;
    uint64_t cpu;   // cumulative cpu ns
    uint64_t rss;
    double pct;
    char name[64];
} proc_t;

static proc_t cur[MAXP], prev[MAXP], view[MAXP];
static int ncur, nprev, nview;
static uint64_t prev_wall;
static mach_timebase_info_data_t tb;

static uint64_t mem_total, mem_used;
static double cpu_total;
static double load[3];
static int sort_mem;
static const int intervals[] = {250, 500, 1000, 2000, 3000, 5000, 10000, 30000, 60000};  // ms
static int ival = 2;
static int paused;
#define NIVAL (int)(sizeof intervals / sizeof *intervals)
static char flt[64];

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

static int by_mem(const void *a, const void *b) {
    uint64_t x = ((const proc_t *)a)->rss, y = ((const proc_t *)b)->rss;
    return x < y ? 1 : x > y ? -1 : by_pid(a, b);
}

static void sample_system(void) {
    static uint64_t prev_ticks[CPU_STATE_MAX];
    host_cpu_load_info_data_t cl;
    mach_msg_type_number_t cnt = HOST_CPU_LOAD_INFO_COUNT;
    if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO, (host_info_t)&cl, &cnt) ==
        KERN_SUCCESS) {
        uint64_t d[CPU_STATE_MAX], all = 0;
        for (int i = 0; i < CPU_STATE_MAX; i++) {
            d[i] = cl.cpu_ticks[i] - prev_ticks[i];
            prev_ticks[i] = cl.cpu_ticks[i];
            all += d[i];
        }
        cpu_total = all ? 100.0 * (all - d[CPU_STATE_IDLE]) / all : 0;
    }

    size_t sz = sizeof mem_total;
    sysctlbyname("hw.memsize", &mem_total, &sz, NULL, 0);
    vm_statistics64_data_t vm;
    cnt = HOST_VM_INFO64_COUNT;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64, (host_info64_t)&vm, &cnt) ==
        KERN_SUCCESS) {
        // Activity Monitor style: app + wired + compressed
        uint64_t pages = vm.internal_page_count - vm.purgeable_count + vm.wire_count +
                         vm.compressor_page_count;
        mem_used = pages * vm_kernel_page_size;
    }
    getloadavg(load, 3);
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
        p->rss = ti.pti_resident_size;
        p->pct = 0;
        if (proc_name(p->pid, p->name, sizeof p->name) <= 0)
            snprintf(p->name, sizeof p->name, "?");
        proc_t key = {.pid = p->pid};
        proc_t *q = bsearch(&key, prev, nprev, sizeof(proc_t), by_pid);
        if (q && dt > 0 && p->cpu >= q->cpu)
            p->pct = (p->cpu - q->cpu) * 100.0 / dt;
    }
    prev_wall = wall;
    sample_system();
}

static void build_view(void) {
    nview = 0;
    for (int i = 0; i < ncur; i++)
        if (!flt[0] || strcasestr(cur[i].name, flt)) view[nview++] = cur[i];
    qsort(view, nview, sizeof(proc_t), sort_mem ? by_mem : by_cpu);
}

// argv[0] -> cmd, argv[1..] -> args
static void cmdline(pid_t pid, char *cmd, char *args, size_t outsz) {
    int mib[3] = {CTL_KERN, KERN_PROCARGS2, pid};
    static char buf[1 << 18];
    size_t sz = sizeof buf;
    cmd[0] = args[0] = 0;
    if (sysctl(mib, 3, buf, &sz, NULL, 0) < 0 || sz < sizeof(int)) {
        snprintf(cmd, outsz, "(unavailable)");
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
        if (i == 0) {
            snprintf(cmd, outsz, "%.*s", (int)l, p);
        } else {
            if (len && len + 1 < outsz) args[len++] = ' ';
            size_t c = l < outsz - 1 - len ? l : outsz - 1 - len;
            memcpy(args + len, p, c);
            len += c;
        }
        p += l + 1;
    }
    args[len] = 0;
}

static void cwd_of(pid_t pid, char *out, size_t outsz) {
    struct proc_vnodepathinfo vi;
    if (proc_pidinfo(pid, PROC_PIDVNODEPATHINFO, 0, &vi, sizeof vi) == sizeof vi)
        snprintf(out, outsz, "%s", vi.pvi_cdir.vip_path);
    else
        snprintf(out, outsz, "(unavailable)");
}

// Prints label + value word-wrapped under a 7-col indent; returns rows used.
static int field(int y, const char *label, const char *val) {
    int w = COLS > 8 ? COLS - 7 : 1, rows = 0;
    if (y >= LINES - 3) return 1;
    attron(A_BOLD);
    mvprintw(y, 2, "%-5s", label);
    attroff(A_BOLD);
    do {
        int len = strlen(val);
        int take = len;
        if (len > w) {
            take = w;
            for (int i = w; i > w / 2; i--)
                if (val[i] == ' ') { take = i; break; }
        }
        if (y + rows < LINES - 3) mvprintw(y + rows, 7, "%.*s", take, val);
        val += take;
        while (*val == ' ') val++;
        rows++;
    } while (*val);
    return rows;
}

static const char *human(uint64_t b, char *out, size_t sz) {
    if (b >= 1ULL << 30) snprintf(out, sz, "%.1fG", b / 1073741824.0);
    else if (b >= 1ULL << 20) snprintf(out, sz, "%.1fM", b / 1048576.0);
    else snprintf(out, sz, "%lluK", (unsigned long long)(b >> 10));
    return out;
}

static void hline_at(int y) {
    move(y, 0);
    for (int i = 0; i < COLS; i++) addstr("─");
}

int main(void) {
    setlocale(LC_ALL, "");
    mach_timebase_info(&tb);
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    set_escdelay(25);
    curs_set(0);
    timeout(250);

    pid_t sel_pid = -1;
    int sel = 0, editing = 0;
    char status[256] = "";
    char cmd[4096], args[4096], cwd[PROC_PIDPATHINFO_MAXSIZE];
    uint64_t last = 0;

    for (;;) {
        if (!last || (!paused && now_ns() - last >= intervals[ival] * 1000000ULL)) {
            sample();
            last = now_ns();
        }
        build_view();
        int topn = LINES - HEAD_ROWS - DETAIL_ROWS;
        if (topn < 1) topn = 1;
        int n = nview < topn ? nview : topn;
        for (int i = 0; i < nview; i++)
            if (view[i].pid == sel_pid) {
                // keep the selected process visible: pin it to the last row
                if (i >= n) { view[n - 1] = view[i]; i = n - 1; }
                sel = i;
                break;
            }
        if (sel >= n) sel = n - 1;
        if (sel < 0) sel = 0;
        sel_pid = n ? view[sel].pid : -1;

        erase();
        mvprintw(0, 0, "🔥 CPU %.0f%% | 🧠 MEM %.1fG/%.0fG | 📈 LOAD %.1f %.1f %.1f | ⏱️  REFRESH %gs%s", cpu_total,
                 mem_used / 1073741824.0, mem_total / 1073741824.0, load[0], load[1],
                 load[2], intervals[ival] / 1000.0, paused ? " ⏸️  PAUSED" : "");
        attron(A_BOLD);
        mvprintw(2, 0, "  %-7s %6s %8s  %s", "PID", "CPU%", "MEM", "NAME");
        attroff(A_BOLD);
        hline_at(3);
        char mem[16];
        int nw = COLS > 28 ? COLS - 28 : 0;
        for (int i = 0; i < n; i++) {
            if (i == sel) attron(A_REVERSE);
            mvprintw(4 + i, 0, "%s %-7d %6.1f %8s  %-*.*s", i == sel ? "▶" : " ",
                     view[i].pid, view[i].pct, human(view[i].rss, mem, sizeof mem), nw, nw,
                     view[i].name);
            if (i == sel) attroff(A_REVERSE);
        }
        int y = HEAD_ROWS + topn;
        hline_at(y);
        if (n) {
            cmdline(sel_pid, cmd, args, sizeof cmd);
            cwd_of(sel_pid, cwd, sizeof cwd);
        } else {
            cmd[0] = args[0] = cwd[0] = 0;
        }
        y++;
        y += field(y, "CMD", cmd);
        y += field(y, "ARGS", args);
        y += field(y, "CWD", cwd);
        if (editing || flt[0])
            mvprintw(LINES - 3, 0, "filter: %s%s", flt, editing ? "_" : "");
        mvprintw(LINES - 2, 0, "%.*s", COLS, status);
        mvprintw(LINES - 1, 0, "[←/→] refresh time  [space] pause  [k] kill  [s] sort cpu  [m] sort mem  [/] filter  [q] quit");
        refresh();

        int ch = getch();
        if (ch == ERR) continue;
        if (editing) {
            size_t l = strlen(flt);
            if (ch == '\n' || ch == KEY_ENTER) editing = 0;
            else if (ch == 27) { flt[0] = 0; editing = 0; }
            else if ((ch == KEY_BACKSPACE || ch == 127 || ch == 8) && l) flt[l - 1] = 0;
            else if (ch < 128 && isprint(ch)) {
                if (l < sizeof flt - 1) { flt[l] = ch; flt[l + 1] = 0; }
            }
            if (ch != KEY_UP && ch != KEY_DOWN && ch != KEY_LEFT && ch != KEY_RIGHT) continue;
        }
        if (ch == 'q' || ch == 'Q') break;
        if (ch == KEY_UP && sel > 0) sel_pid = view[--sel].pid;
        if (ch == KEY_DOWN && sel < n - 1) sel_pid = view[++sel].pid;
        if (ch == KEY_LEFT && ival > 0) ival--;
        if (ch == KEY_RIGHT && ival < NIVAL - 1) ival++;
        if (ch == 's') sort_mem = 0;
        if (ch == 'm') sort_mem = 1;
        if (ch == '/') editing = 1;
        if (ch == 27) flt[0] = 0;
        if (ch == ' ') paused = !paused;
        if (ch == 'k' && n) {
            if (kill(sel_pid, SIGTERM) == 0)
                snprintf(status, sizeof status, "killed %d (%s)", sel_pid, view[sel].name);
            else
                snprintf(status, sizeof status, "kill %d: %s", sel_pid, strerror(errno));
            last = 0;  // resample now
        }
    }
    endwin();
    return 0;
}
