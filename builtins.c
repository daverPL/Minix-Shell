#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <memory.h>
#include <signal.h>

#include "include/builtins.h"

int exitE(char *[]);

int echo(char *[]);

int cd(char *[]);

int killK(char *[]);

int ls(char *[]);

builtin_pair builtins_table[] = {
        {"exit",  &exitE},
        {"lecho", &echo},
        {"lcd",   &cd},
        {"cd",    &cd},
        {"lkill", &killK},
        {"lls",   &ls},
        {NULL, NULL}
};

int
echo(char *argv[]) {
    int i = 1;
    if (argv[i]) printf("%s", argv[i++]);
    while (argv[i])
        printf(" %s", argv[i++]);

    printf("\n");
    fflush(stdout);
    return 0;
}


int
exitE(char *argv[]) {
    if (argv[1] != NULL) {
        fprintf(stderr, "Builtin %s error.\n", argv[0]);
        return BUILTIN_ERROR;
    }
    exit(0);
}

int
cd(char *argv[]) {

    int ch;
    if (argv[1] == NULL) {
        ch = chdir(getenv("HOME"));
        if (ch == -1) {
            fprintf(stderr, "Builtin %s error.\n", argv[0]);
            return BUILTIN_ERROR;
        }
        return 0;
    }

    if (argv[2] != NULL) {
        fprintf(stderr, "Builtin %s error.\n", argv[0]);
        return BUILTIN_ERROR;
    }

    ch = chdir(argv[1]);
    if (ch == -1) {
        fprintf(stderr, "Builtin %s error.\n", argv[0]);
        return BUILTIN_ERROR;
    }
    return 0;
}

int
killK(char *argv[]) {
    if ((argv[1] == NULL) || (argv[3] != NULL)) {
        fprintf(stderr, "Builtin %s error.\n", argv[0]);
        return BUILTIN_ERROR;
    }

    if (argv[2] == NULL) {
        int p = 1, pid = 0, i = 0;
        int k = strlen(argv[1]) - 1;
        for (i = k; i >= 0; i--) {
            if (i == 0 && argv[1][i] == '-') {
                pid = -pid;
                break;
            }
            int c = argv[1][i] - '0';
            if ((c < 0) || (c > 9)) {
                fprintf(stderr, "Builtin %s error.\n", argv[0]);
                return BUILTIN_ERROR;
            }
            pid += c * p;
            pid *= 10;
        }

        if (kill(pid, SIGTERM) == -1) {
            fprintf(stderr, "Builtin %s error.\n", argv[0]);
            return BUILTIN_ERROR;
        }
        return 0;
    }

    int p = 1, pid = 0, signal = 0, i = 0;
    int k = strlen(argv[1]) - 1;

    for (i = k; i >= 0; i--) {
        if (i == 0) {
            if (argv[1][i] == '-') {
                break;
            } else {
                fprintf(stderr, "Builtin %s error.\n", argv[0]);
                return BUILTIN_ERROR;
            }
        }
        int c = argv[1][i] - '0';
        if ((c < 0) || (c > 9)) {
            fprintf(stderr, "Builtin %s error.\n", argv[0]);
            return BUILTIN_ERROR;
        }
        signal += c * p;
        p *= 10;
    }

    p = 1;
    i = 0;
    k = strlen(argv[2]) - 1;

    for (i = k; i >= 0; i--) {
        int c = argv[2][i] - '0';
        if ((c < 0) || (c > 9)) {
            fprintf(stderr, "Builtin %s error.\n", argv[0]);
            return BUILTIN_ERROR;
        }
        pid += c * p;
        p *= 10;
    }

    if (kill(pid, signal) == -1) {
        fprintf(stderr, "Builtin %s error.\n", argv[0]);
        return BUILTIN_ERROR;
    }

    return 0;
}

int
ls(char *argv[]) {
    if (argv[1] != NULL) {
        fprintf(stderr, "Builtin %s error.\n", argv[0]);
        return BUILTIN_ERROR;
    }

    DIR *dir = opendir(".");
    struct dirent *dp;

    if (dir == NULL) {
        fprintf(stderr, "Builtin %s error.\n", argv[0]);
        return BUILTIN_ERROR;
    }

    while ((dp = readdir(dir))) {
        if (dp == NULL) {
            fprintf(stderr, "Builtin %s error.\n", argv[0]);
            return BUILTIN_ERROR;
        }
        if (dp->d_name[0] != '.') {
            puts(dp->d_name);
        }
    }

    closedir(dir);

    return 0;
}