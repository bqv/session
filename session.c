#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <fcntl.h>
#include <pwd.h>

#include <sys/prctl.h>
#include <sys/stat.h>

#define E_ARGS 1
#define E_PIPE 2
#define E_USER 3

int main(int argc, char* argv[]) {
    if (argc < 2 || (strncmp(argv[1], "-h", 2) == 0 || (strncmp(argv[1], "--help", 6) == 0))) {
        printf("usage: %s SVDIR [SVC] [-- [COMMAND..]]\n", argv[0]);
        printf("       %s [-h | --help]\n", argv[0]);
        printf("  e.g  %s sway-svc -- swaymsg -- exec\n", argv[0]);
        printf("\n");
        printf("Where SVDIR is the service directory\n");
        printf(" (presumably `./service`)\n");
        printf("Where SVC is the special graphical session service\n");
        printf(" (defaults to `sway-svc`)\n");
        printf("The COMMAND may be a prefix to execute in the session context\n");
        printf(" (defaults to `swaymsg --socket /run/user/${uid}/sway-ipc.${uid}.${pid}.sock -- exec`,\n");
        printf("  ${uid} will be replaced with your UID,\n");
        printf("  ${pid} will be replaced with PROG's PID)\n");
        printf("The session manager will then be executed appropriately\n");
        exit(E_ARGS);
    }

    struct passwd *pw = getpwuid(getuid());
    if (!pw) { exit(E_USER); }

    char **prgv = &argv[1];
    char **cmdv = NULL;
    for (int i = 2; i <= argc; i++)
    {
        if (i == argc) cmdv = NULL;
        else if (strncmp(argv[i], "--", 2) == 0)
        {
            argv[i] = NULL;
            if (argv[i+1]) cmdv = &argv[i+1];
            break;
        }
    }

    chdir(pw->pw_dir);
    chdir(prgv[0] ? prgv[0] : "./service");

    char env_fifo[PATH_MAX+1] = {0};
    realpath("./env", env_fifo);
    char log_fifo[PATH_MAX+1] = {0};
    realpath("./uncaught-logs/fifo", log_fifo);

    prctl(PR_SET_CHILD_SUBREAPER, 1);

    mkfifo(env_fifo, 0644);
    mkfifo(log_fifo, 0644);

    if (fork() == 0) {
        char *args[] = {
            "s6-supervise", "uncaught-logs", NULL
        };
        execvp(args[0], args);
    }

    freopen(log_fifo, "w", stdout);
    pid_t pid = fork();

    if (pid == 0) {
        close(0);
        dup2(1, 2);
        // fdholder?

        char *args[] = {
            "s6-supervise", prgv[1] ? prgv[1] : "sway-srv", NULL
        };
        execvp(args[0], args);
    }

    FILE *pipe = popen("execlineb -s0 /dev/stdin", "w");
    if (!pipe) {
        perror("Error opening execlineb");
        kill(pid, SIGTERM);
        exit(E_PIPE);
    }

    fprintf(pipe, "define pid %d", pid);
    fputs(" backtick -E uid { id -u }", pipe);
    fputs(" loopwhilex -n foreground { sleep 1 }", pipe);
    fputs(" swaymsg --socket /run/user/${uid}/sway-ipc.${uid}.${pid}.sock -- exec", pipe);
    fprintf(pipe, " redirfd -w 1 %s /usr/bin/env SESSION_PID=${pid}", env_fifo);
    if (pclose(pipe) == -1) {
        perror("Error closing execlineb");
        exit(E_PIPE);
    }

    char *args[] = {
        "redirfd", "-r", "0", env_fifo,
        "envfile", "-",
        "redirfd", "-r", "0", "/dev/null",
        "fdmove", "-c", "2", "1",
        "s6-svscan", NULL
    };
    execvp(args[0], args);

    return 0;
}
