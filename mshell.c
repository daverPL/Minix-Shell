#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "include/siparse.h"
#include "include/config.h"
#include "include/builtins.h"
#include "include/utils.h"

#define FOR(i, a, b) for((i) = (a); (i) <= (b); (i)=(i)+1)
#define FOR_D(i, a, b) for((i) = (a); (i) >= (b); (i) = (i) - 1)
#define SYNTAX() fprintf(stderr, "%s\n", SYNTAX_ERROR_STR)
#define READ_END 0
#define WRITE_END 1

int i;
char buforGlowny[4 * MAX_LINE_LENGTH + 10];
char buforParsera[4 * MAX_LINE_LENGTH + 10];
int rozmiarGlowny = 0;
int rozmiarParser = 0;
int pozycjaGlowny = 0;
int koniecParser = 0;
int liniaZaDluga;
struct stat typCzytania;

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
    int pipeNumber = 0;
    int lineSize = 0;

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

    // printparsedline(ln);

    while ((ln->pipelines[lineSize]) != NULL) {
        lineSize++;
    }

    FOR(pipeNumber, 0, lineSize - 1) {
        int pipeSize = 0;
        while (ln->pipelines[pipeNumber][pipeSize] != NULL) {
            pipeSize++;
        }

        int pozycja = 0;
        FOR(pozycja, 0, pipeSize - 1) {
            if (ln->pipelines[pipeNumber][pozycja]->argv[0] == NULL && pipeSize > 1) {
                SYNTAX();
                return;
            }
        }
    }

    FOR(pipeNumber, 0, lineSize - 1) {
        int comNumber = 0;
        int pipeSize = 0;
        while (ln->pipelines[pipeNumber][pipeSize] != NULL) {
            pipeSize++;
        }

        int pipes[pipeSize][2];

        FOR(comNumber, 0, pipeSize - 1) {
            command *com = ln->pipelines[pipeNumber][comNumber];
            if (pipeSize == 1) {
                if (komendaWbudowana(com) == 1) {
                    continue;
                }
            }

            pid_t pid;

            if (pipeSize - 1 != 0) {
                pipe(pipes[comNumber]);
            }

            if ((pid = fork()) == -1) {
                exit(1);
            } else if (pid == 0) {
                int liczbaPrzekierowan = 0;

                while (com->redirs[liczbaPrzekierowan] != NULL) {
                    liczbaPrzekierowan++;
                }


                if (pipeSize - 1 != 0) {
                    if (comNumber == 0) {
                        close(pipes[comNumber][READ_END]);

                        close(STDOUT_FILENO);
                        dup2(pipes[comNumber][WRITE_END], STDOUT_FILENO);
                        close(pipes[comNumber][WRITE_END]);
                    }
                    if (comNumber > 0 && comNumber < pipeSize - 1) {
                        close(pipes[comNumber - 1][WRITE_END]);

                        close(STDIN_FILENO);
                        dup2(pipes[comNumber - 1][READ_END], STDIN_FILENO);
                        close(pipes[comNumber - 1][READ_END]);

                        close(STDOUT_FILENO);
                        dup2(pipes[comNumber][WRITE_END], STDOUT_FILENO);
                        close(pipes[comNumber][WRITE_END]);

                        close(pipes[comNumber][READ_END]);
                    }
                    if (comNumber == pipeSize - 1) {
                        close(pipes[comNumber - 1][WRITE_END]);
                        close(pipes[comNumber][WRITE_END]);

                        close(STDIN_FILENO);
                        dup2(pipes[comNumber - 1][READ_END], STDIN_FILENO);
                        close(pipes[comNumber - 1][READ_END]);
                    }
                }

                przekierowaniaWejscia(liczbaPrzekierowan, com);

                if (execvp(com->argv[0], com->argv) == -1) {
                    statusExec(com);
                    exit(EXEC_FAILURE);
                }
            } else {
                while (wait(NULL) != -1);
                close(pipes[comNumber][WRITE_END]);
                if (comNumber >= 2) {
                    close(pipes[comNumber - 2][READ_END]);
                    close(pipes[comNumber - 2][WRITE_END]);
                }
            }
        }

        FOR(comNumber, 0, pipeSize - 1) {
            close(pipes[comNumber][READ_END]);
            close(pipes[comNumber][WRITE_END]);
        }
    }
}

void readline() {
    liniaZaDluga = 0;

    while (1) {
        if (koniecParser == 1) {
            execute();
            memset(buforParsera, 0, rozmiarParser + 10);
            rozmiarParser = 0;
            koniecParser = 0;
            break;
        } else {
            if (rozmiarGlowny == 0) {
                pozycjaGlowny = 0;
                rozmiarGlowny = read(0, buforGlowny, 2 * MAX_LINE_LENGTH);
            }

            if (rozmiarGlowny == 0) {
                exit(0);
            }
            if (rozmiarGlowny == -1) {
                exit(1);
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
                koniecParser = 0;
            }
        }

        if (rozmiarParser >= MAX_LINE_LENGTH) {
            liniaZaDluga = 1;
            memset(buforParsera, 0, rozmiarParser + 10);
            rozmiarParser = 0;
            koniecParser = 0;
            SYNTAX();
        }
    }
}


int main(int argc, char *argv[]) {
    if ((fstat(0, &typCzytania)) == -1) exit(1);

    while (1) {
        if (S_ISCHR(typCzytania.st_mode) && (!liniaZaDluga)) {
            write(1, PROMPT_STR, 2);
        }

        readline();
    }
}
