#include "uruntime.h"
#include <signal.h>

static volatile sig_atomic_t signal_received = 0;

static void signal_handler(int sig) {
    signal_received = sig;
}

void signals_handler(pid_t pid, const char *mount_point, bool killpid, bool selfexit) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigfillset(&sa.sa_mask);

    int sigs[] = {SIGINT, SIGTERM, SIGQUIT, SIGHUP, SIGUSR1, SIGUSR2};
    for (size_t i = 0; i < sizeof(sigs) / sizeof(sigs[0]); i++)
        sigaction(sigs[i], &sa, NULL);

    while (true) {
        // Poll every 500ms instead of using pause() to avoid race conditions
        usleep(500000);

        if (!signal_received) continue;
        int sig = signal_received;
        signal_received = 0;

        bool handled = false;
        for (size_t i = 0; i < sizeof(sigs) / sizeof(sigs[0]); i++) {
            if (sig == sigs[i]) { handled = true; break; }
        }
        if (!handled) continue;

        if (killpid && pid > 0) {
            kill(pid, sig);
        } else {
            try_unmount(pid, mount_point);
        }

        if (selfexit) {
            // Reset signals to default before exiting
            for (size_t i = 0; i < sizeof(sigs) / sizeof(sigs[0]); i++)
                signal(sigs[i], SIG_DFL);
            _exit(0);
        }

        if (!killpid) break;
    }
}
