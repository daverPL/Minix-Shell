#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <memory.h>
#include <signal.h>

#include "include/builtins.h"

#define FOR(i, a, b) for((i) = (a); (i) <= (b); (i)=(i)+1)
#define FOR_D(i, a, b) for((i) = (a); (i) >= (b); (i) = (i) - 1)
#define SYNTAX() fprintf(stderr, "%s\n", SYNTAX_ERROR_STR)
#define BUILD_ERROR(x) fprintf(stderr, "Builtin %s error.\n", x);

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

int echo(char *argv[]) {
    // no poprostu wypisuje wszystkie argumenty na ekran
    int i = 1;
    if (argv[i]) {
        write(1, argv[i], strlen(argv[i]));
        i++;
    }
    while (argv[i]) {
        write(1, argv[i], strlen(argv[i]));
        i++;
    }
    write(1, "\n", 1);
    return 0;
}


int exitE(char *argv[]) {
    // exit jak exit
    if (argv[1] != NULL) {
        BUILD_ERROR(argv[0]);
        return BUILTIN_ERROR;
    }
    exit(0);
}

int cd(char *argv[]) {
    // robimy komende cd
    if (argv[1] == NULL) {
        // w momencie gdy jest samo cd bez argumentow to idziemy do katalogu domowego uzytkownika
        if (chdir(getenv("HOME")) == -1) {
            BUILD_ERROR(argv[0]);
            return BUILTIN_ERROR;
        }
        return 0;
    }

    if (argv[2] != NULL) {
        // jak jest cd i miejsce gdzie i jeszcze cos dalej to wywal blad
        BUILD_ERROR(argv[0]);
        return BUILTIN_ERROR;
    }

    // a jak komenda wyglada na wporzadku to poprostu przechodze do tamtego foldera gdzie mi kazano
    if (chdir(argv[1]) == -1) {
        BUILD_ERROR(argv[0]);
        return BUILTIN_ERROR;
    }
    return 0;
}

int killK(char *argv[]) {
    // jak komenda ma niepoprawny format to wywalam
    if ((argv[1] == NULL) || (argv[3] != NULL)) {
        BUILD_ERROR(argv[0]);
        return BUILTIN_ERROR;
    }

    // jezeli jest tylko lkill pid i tyle to parsuje ten pid do liczby i idpalam kill z domyslnym
    // wedle specyfikacji sygnalem sigterm
    if (argv[2] == NULL) {
        int p = 1, pid = 0, i = 0;
        int k = strlen(argv[1]) - 1;
        FOR_D(i, k, 0) {
            if (i == 0 && argv[1][i] == '-') {
                pid = -pid;
                break;
            }
            int c = argv[1][i] - '0';
            if ((c < 0) || (c > 9)) {
                BUILD_ERROR(argv[0]);
                return BUILTIN_ERROR;
            }
            pid += c * p;
            pid *= 10;
        }

        if (kill(pid, SIGTERM) == -1) {
            BUILD_ERROR(argv[0]);
            return BUILTIN_ERROR;
        }
        return 0;
    }

    // a jak jest podane co to za sygnal to parsuje wszystko co pisze
    int p = 1, pid = 0, signal = 0, i = 0;
    int k = strlen(argv[1]) - 1;

    for (i = k; i >= 0; i--) {
        if (i == 0) {
            if (argv[1][i] == '-') {
                break;
            } else {
                BUILD_ERROR(argv[0]);
                return BUILTIN_ERROR;
            }
        }
        int c = argv[1][i] - '0';
        if ((c < 0) || (c > 9)) {
            BUILD_ERROR(argv[0]);
            return BUILTIN_ERROR;
        }
        signal += c * p;
        p *= 10;
    }

    p = 1;
    k = strlen(argv[2]) - 1;
    FOR_D(i, k, 0) {
        int c = argv[2][i] - '0';
        if ((c < 0) || (c > 9)) {
            BUILD_ERROR(argv[0]);
            return BUILTIN_ERROR;
        }
        pid += c * p;
        p *= 10;
    }
    // a jak wszystkow  porzadku sparsuje to wysylam podany sygnal do podanego pidu
    if (kill(pid, signal) == -1) {
        BUILD_ERROR(argv[0]);
        return BUILTIN_ERROR;
    }

    return 0;
}

int ls(char *argv[]) {
    if (argv[1] != NULL) {
        BUILD_ERROR(argv[0]);
        return BUILTIN_ERROR;
    }
    // jak komenda w porzadku podana to otwieram katalog do odpowiedniej struktury
    DIR *dir = opendir(".");
    struct dirent *dp;

    if (dir == NULL) {
        BUILD_ERROR(argv[0]);
        return BUILTIN_ERROR;
    }

    // no i iteruje sie po plikach i wypisuje co trzeba jak nie zaczyna sie nazwa pliku etc od kropki
    while ((dp = readdir(dir))) {
        if (dp == NULL) {
            BUILD_ERROR(argv[0]);
            return BUILTIN_ERROR;
        }
        if (dp->d_name[0] != '.') {
            write(1, dp->d_name, strlen(dp->d_name));
            write(1, "\n", 1);
        }
    }

    closedir(dir);

    return 0;
}