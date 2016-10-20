#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <dirent.h>

#include "include/builtins.h"

int exitE(char *[]);

int echo(char *[]);

int cd(char *[]);

int kill(char *[]);

int ls(char *[]);

builtin_pair builtins_table[] = {
        {"exit",  &exitE},
        {"lecho", &echo},
        {"lcd",   &cd},
        {"cd",    &cd},
        {"lkill", &kill},
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
kill(char *argv[]) {
    if ((argv[1] == NULL) || (argv[3] != NULL)) {
        fprintf(stderr, "Builtin %s error.\n", argv[0]);
        return BUILTIN_ERROR;
    }

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