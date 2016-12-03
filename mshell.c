#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include "include/siparse.h"
#include "include/config.h"
#include "include/builtins.h"
#include "include/utils.h"

#define FOR(i, a, b) for((i) = (a); (i) <= (b); (i)=(i)+1)
#define FOR_D(i, a, b) for((i) = (a); (i) >= (b); (i) = (i) - 1)
#define SYNTAX() fprintf(stderr, "%s\n", SYNTAX_ERROR_STR)
#define READ_END 0
#define WRITE_END 1
#define MAX_ENDED_PROCESSES 1000
#define MAX_BUFOR 4000010


char buforGlowny[MAX_BUFOR];
char buforParsera[MAX_BUFOR];
int bezPrompta = 0;
int rozmiarGlowny = 0;
int rozmiarParser = 0;
int pozycjaGlowny = 0;
int koniecParser = 0;
int liniaZaDluga = 0;

int wypisujZakonczone;
int liniaDoWykonaniaTlo = 0;
int statusyZakonczonychTlo[MAX_ENDED_PROCESSES][2];
volatile int liczbaZakonczonychTlo = 0;
volatile int ileJeszczeDzialaNormalnie = 0;
volatile int pidyWykonywanychNormalnie[MAX_ENDED_PROCESSES];
volatile int ileProcesowLiniaNormalna = 0;

struct stat typCzytania;
struct sigaction sigInt;
struct sigaction sigChld;

int i;

void chldHandler(int sig_nb) {
    int savedErrno = errno;
    int pid = 0;
    int status;
    while ((pid = waitpid(-1, &status, WNOHANG))) {
        if (pid == -1) {
            if (errno == ECHILD) {
                break;
            } else {
                exit(1);
            }
        } else {
            int answer = 0;
            FOR(i, 0, ileProcesowLiniaNormalna - 1) {
                if (pidyWykonywanychNormalnie[i] == pid) {
                    answer = 1;
                }
            }
            if (answer == 1) {
                ileJeszczeDzialaNormalnie--;
            } else {
                if (liczbaZakonczonychTlo < MAX_ENDED_PROCESSES) {
                    statusyZakonczonychTlo[liczbaZakonczonychTlo][0] = pid;
                    statusyZakonczonychTlo[liczbaZakonczonychTlo][1] = status;
                    liczbaZakonczonychTlo++;
                }
            }
        }
    }
    errno = savedErrno;
}


int statusOtwarciaPliku(command *com) {
    switch (errno) {
        case EACCES:
            fprintf(stderr, "%s: permission denied\n", ((com->redirs)[i])->filename);
            break;
        case ENOENT:
            fprintf(stderr, "%s: no such file or directory\n", ((com->redirs)[i])->filename);
            break;
    }
    exit(1);
}

void statusExec(command *com) {
    switch (errno) {
        case ENOENT:
            fprintf(stderr, "%s: no such file or directory\n", com->argv[0]);
            break;
        case EACCES:
            fprintf(stderr, "%s: permission denied\n", com->argv[0]);
            break;
        default:
            fprintf(stderr, "%s: exec error\n", com->argv[0]);
            break;
    }
}

int komendaWbudowana(command *com) {
    int p = 0, typKomendy = 0;
    while (p < 6) {
        if (com->argv[0] != NULL && strcmp(builtins_table[p].name, com->argv[0]) == 0) {
            typKomendy = 1;
            break;
        }
        p++;
    }

    if (typKomendy == 1) {
        builtins_table[p].fun(com->argv);
        return 1;
    }

    return 0;
}

void przekierowaniaWejscia(int l, command *com) {
    FOR(i, 0, l - 1) {
        if (IS_RIN(((com->redirs)[i])->flags)) {
            int f = open((com->redirs[i])->filename, O_RDONLY, S_IRWXU | S_IRWXG | S_IRWXO);

            if (f == -1) {
                statusOtwarciaPliku(com);
            }

            close(STDIN_FILENO);
            dup2(f, STDIN_FILENO);
            close(f);
        }

        if (IS_ROUT(((com->redirs)[i])->flags)) {
            int f = open(((com->redirs)[i])->filename, O_WRONLY | O_CREAT | O_TRUNC,
                         S_IRWXU | S_IRWXG | S_IRWXO);

            if (f == -1) {
                statusOtwarciaPliku(com);
            }

            close(STDOUT_FILENO);
            dup2(f, STDOUT_FILENO);
            close(f);
        } else if (IS_RAPPEND(((com->redirs)[i])->flags)) {
            int f = open(((com->redirs)[i])->filename, O_WRONLY | O_CREAT | O_APPEND,
                         S_IRWXU | S_IRWXG | S_IRWXO);

            if (f == -1) {
                statusOtwarciaPliku(com);
            }

            close(STDOUT_FILENO);
            dup2(f, STDOUT_FILENO);
            close(f);
        }
    }
}

void execute() {
    line *ln = NULL;
    int aktualnyPipe = 0;
    int liczbaPipow = 0;

    buforParsera[rozmiarParser] = 0;

    if (rozmiarParser == 0 || rozmiarParser == 1) {
        return;
    }

    if (buforParsera[0] == '\n' || buforParsera[0] == '#') {
        return;
    }

    if ((ln = parseline(buforParsera)) == NULL) {
        SYNTAX();
        return;
    }

    if (ln->flags) {
        liniaDoWykonaniaTlo = 1;
        wypisujZakonczone = 0;
    } else {
        wypisujZakonczone = 1;
        liniaDoWykonaniaTlo = 0;
    }

    while ((ln->pipelines[liczbaPipow]) != NULL) {
        liczbaPipow++;
    }

    FOR(aktualnyPipe, 0, liczbaPipow - 1) {
        int liczbaKomend = 0;
        while (ln->pipelines[aktualnyPipe][liczbaKomend] != NULL) {
            liczbaKomend++;
        }

        int pozycja = 0;
        FOR(pozycja, 0, liczbaKomend - 1) {
            if (ln->pipelines[aktualnyPipe][pozycja]->argv[0] == NULL && liczbaKomend > 1) {
                SYNTAX();
                return;
            }
        }
    }

    FOR(aktualnyPipe, 0, liczbaPipow - 1) {
        int aktualnaKomenda = 0;
        int liczbaKomend = 0;

        ileJeszczeDzialaNormalnie = 0;
        ileProcesowLiniaNormalna = 0;

        while (ln->pipelines[aktualnyPipe][liczbaKomend] != NULL) {
            liczbaKomend++;
        }

        if (liczbaKomend == 1) {
            if (komendaWbudowana(ln->pipelines[aktualnyPipe][0]) == 1) {
                continue;
            }
        }

        int pipes[liczbaKomend][2];
        sigset_t newMask;
        sigemptyset(&newMask);
        sigaddset(&newMask, SIGCHLD);
        sigprocmask(SIG_BLOCK, &newMask, NULL);

        FOR(aktualnaKomenda, 0, liczbaKomend - 1) {
            command *com = ln->pipelines[aktualnyPipe][aktualnaKomenda];

            pid_t pid;

            int c = pipe(pipes[aktualnaKomenda]);
            if (c == -1) {
                exit(1);
            }

            if (aktualnaKomenda >= 2) {
                int c1 = close(pipes[aktualnaKomenda - 2][READ_END]);
                int c2 = close(pipes[aktualnaKomenda - 2][WRITE_END]);
                if ((c1 == -1) || (c2 == -1))
                    exit(1);
            }

            if ((pid = fork()) == -1) {
                exit(1);
            } else if (pid == 0) {
                int liczbaPrzekierowan = 0;

                while (com->redirs[liczbaPrzekierowan] != NULL) {
                    liczbaPrzekierowan++;
                }

                sigprocmask(SIG_UNBLOCK, &newMask, NULL);
                sigemptyset(&sigInt.sa_mask);
                sigInt.sa_handler = SIG_DFL;
                sigaction(SIGINT, &sigInt, NULL);

                sigemptyset(&sigChld.sa_mask);
                sigInt.sa_handler = SIG_DFL;
                sigaction(SIGCHLD, &sigChld, NULL);

                if (liniaDoWykonaniaTlo == 1) {
                    int group = setsid();
                    if (group == -1)
                        exit(1);
                }

                if (close(pipes[aktualnaKomenda][READ_END]) == -1) {
                    exit(1);
                }

                if (aktualnaKomenda > 0) {
                    if (close(pipes[aktualnaKomenda - 1][WRITE_END]) == -1) exit(1);
                    if (close(STDIN_FILENO) == -1) exit(1);
                    if (dup2(pipes[aktualnaKomenda - 1][READ_END], STDIN_FILENO) == -1) exit(1);
                    if (close(pipes[aktualnaKomenda - 1][READ_END]) == -1) exit(1);
                }

                if (aktualnaKomenda < liczbaKomend - 1) {
                    if (close(STDOUT_FILENO) == -1) exit(1);
                    if (dup2(pipes[aktualnaKomenda][WRITE_END], STDOUT_FILENO) == -1) exit(1);
                    if (close(pipes[aktualnaKomenda][WRITE_END]) == -1) exit(1);
                }
                if (aktualnaKomenda == liczbaKomend - 1) {
                    if (close(pipes[aktualnaKomenda][WRITE_END]) == -1) exit(1);
                }

                przekierowaniaWejscia(liczbaPrzekierowan, com);

                if (execvp(com->argv[0], com->argv) == -1) {
                    statusExec(com);
                    exit(EXEC_FAILURE);
                }
            } else {
                if (liniaDoWykonaniaTlo == 0) {
                    ileJeszczeDzialaNormalnie++;
                    pidyWykonywanychNormalnie[ileProcesowLiniaNormalna] = pid;
                    ileProcesowLiniaNormalna++;
                }

                if (aktualnaKomenda == liczbaKomend - 1) {
                    if (aktualnaKomenda > 0) {
                        if (close(pipes[aktualnaKomenda - 1][READ_END]) == -1) exit(1);
                        if (close(pipes[aktualnaKomenda - 1][WRITE_END]) == -1) exit(1);
                    }
                    if (close(pipes[aktualnaKomenda][READ_END]) == -1) exit(1);
                    if (close(pipes[aktualnaKomenda][WRITE_END]) == -1) exit(1);

                    sigset_t mask;
                    sigemptyset(&mask);
                    sigfillset(&mask);
                    sigdelset(&mask, SIGCHLD);

                    while (ileJeszczeDzialaNormalnie > 0) {
                        sigsuspend(&mask);
                    }
                    sigprocmask(SIG_UNBLOCK, &newMask, NULL);
                    if (liniaDoWykonaniaTlo >= 1)
                        wypisujZakonczone = 1;
                }
            }
        }

        FOR(aktualnaKomenda, 0, liczbaKomend - 1) {
            close(pipes[aktualnaKomenda][READ_END]);
            close(pipes[aktualnaKomenda][WRITE_END]);
        }
    }
}

void readline() {
    liniaZaDluga = 0;

    while (1) {
        if (koniecParser == 1) {
            execute();
            memset(buforParsera, 0, 2 * rozmiarParser + 10);
            rozmiarParser = 0;
            koniecParser = 0;
            break;
        } else {
            if (rozmiarGlowny == 0) {
                pozycjaGlowny = 0;
                rozmiarGlowny = read(0, buforGlowny, 2 * 1000000);
            }

            if (rozmiarGlowny == 0) {
                exit(0);
            }

            if (rozmiarGlowny == -1) {
                if (errno == EINTR) {
                    bezPrompta = 1;
                    return;
                } else {
                    exit(1);
                }
            }

            int jestKoniec = 0;

            FOR(i, pozycjaGlowny, rozmiarGlowny - 1) {
                if (buforGlowny[i] == '\n') {
                    jestKoniec = 1;
                    break;
                }
            }

            if (jestKoniec == 1) {
                FOR(i, pozycjaGlowny, rozmiarGlowny - 1) {
                    if (buforGlowny[i] != '\n') {
                        buforParsera[rozmiarParser] = buforGlowny[i];
                        buforGlowny[i] = 0;
                        rozmiarParser++;
                    } else {
                        break;
                    }
                }
                pozycjaGlowny = i + 1;

                if (1000 * pozycjaGlowny > 999 * rozmiarGlowny) {
                    int start = i + 1;
                    int where = 0;

                    while (start < rozmiarGlowny) {
                        buforGlowny[where] = buforGlowny[start];
                        buforGlowny[start] = 0;
                        start++;
                        where++;
                    }

                    rozmiarGlowny = where;
                    pozycjaGlowny = 0;
                }
                koniecParser = 1;
            } else {
                FOR(i, pozycjaGlowny, rozmiarGlowny - 1) {
                    buforParsera[rozmiarParser] = buforGlowny[i];
                    rozmiarParser++;
                }
                memset(buforGlowny, 0, 2 * rozmiarGlowny + 10);
                rozmiarGlowny = 0;
                pozycjaGlowny = 0;
                rozmiarGlowny = read(0, buforGlowny, 2 * 1000000);
                koniecParser = 0;
            }
        }

        if (rozmiarParser >= MAX_LINE_LENGTH) {
            liniaZaDluga = 1;
            memset(buforParsera, 0, 2 * rozmiarParser + 10);
            rozmiarParser = 0;
            koniecParser = 0;
            SYNTAX();
        }
    }
}

int main(int argc, char *argv[]) {
    sigInt.sa_handler = SIG_IGN;
    sigInt.sa_flags = 0;
    sigemptyset(&sigInt.sa_mask);
    sigaction(SIGINT, &sigInt, NULL);

    sigChld.sa_handler = chldHandler;
    sigChld.sa_flags = 0;
    sigemptyset(&sigChld.sa_mask);
    sigaction(SIGCHLD, &sigChld, NULL);

    if ((fstat(0, &typCzytania)) == -1) exit(1);

    while (1) {
        if (S_ISCHR(typCzytania.st_mode) && (!liniaZaDluga)) {
            if (wypisujZakonczone) {
                wypisujZakonczone = 0;
                sigset_t newMask;
                sigemptyset(&newMask);
                sigaddset(&newMask, SIGCHLD);
                sigprocmask(SIG_BLOCK, &newMask, NULL);
                FOR(i, 0, liczbaZakonczonychTlo - 1) {
                    int childPid = statusyZakonczonychTlo[i][0];
                    int exitStatus = statusyZakonczonychTlo[i][1];
                    if (WIFEXITED(exitStatus)) {
                        printf("Background process %d terminated. (exited with status %d)\n", childPid,
                               WEXITSTATUS(exitStatus));
                    } else if (WIFSIGNALED(exitStatus)) {
                        printf("Background process %d terminated. (killed by signal %d)\n", childPid,
                               WTERMSIG(exitStatus));
                    }

                    fflush(stdout);
                }
                sigprocmask(SIG_UNBLOCK, &newMask, NULL);
                liczbaZakonczonychTlo = 0;
                ileProcesowLiniaNormalna = 0;
            }

            if (bezPrompta == 0) {
                write(1, PROMPT_STR, 2);
            }
        }
        bezPrompta = 0;
        readline();
    }
}
