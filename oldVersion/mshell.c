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
char buforGlowny[2 * MAX_LINE_LENGTH + 10];    // buffor glowny
char buforParsera[MAX_LINE_LENGTH + 10];       // buffor do parsowania
int start, end;
int koniecKomendy;                             // koniec komendy
int przesuwanieBufora;                         // przesuwanie buffera
int pozycjaBufor;                              // pozycja aktualna w bufferze
int pozycjaParser;                             // pozycja aktualna w bufferzr do parsowania
int koniecLinii;                               // koniec linii
int koniecOstatniejLinii;                      // koniec ostatniej linii
int liniaZaDluga;                              // linia za dluga
struct stat typCzytania;                       // sprawdzanie skad przychodza dane

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

// wykonywanie komendy
void execute() {
    line *ln = NULL;
    int pipeNumber = 0;
    int lineSize = 0;

    fprintf(stdout, "%s\n", buforParsera);

    if ((ln = parseline(buforParsera)) == NULL) {
        SYNTAX();
        return;
    }

    if(buforParsera[0] == '\n') {
        return;
    }

    if(buforParsera[0] == '#') {
        return;
    }

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

// znajdowanie nowej linii
void findNewLine() {
    int i = 0;
    for (i = start; buforGlowny[i] != 0; i++) {
        if (buforGlowny[i] == '\n') {
            koniecLinii = i;
            return;
        }
    }
    if (i > 0)
        koniecLinii = i - 1;
    else koniecLinii = 0;
}

// przesuwanie buffera w lewo
void shiftBufferLeft() {
    if (koniecKomendy) {
        koniecKomendy = 0;
        start = end = koniecLinii = 0;
        koniecOstatniejLinii = -1;
        pozycjaBufor = pozycjaParser = 0;
        memset(buforParsera, 0, MAX_LINE_LENGTH + 10);
        memset(buforGlowny, 0, 2 * MAX_LINE_LENGTH + 10);
        return;
    }

    FOR(i, koniecOstatniejLinii + 1, end) {
        buforGlowny[i - (koniecOstatniejLinii + 1)] = buforGlowny[i];
        buforParsera[i - (koniecOstatniejLinii + 1)] = buforGlowny[i];
    }
    FOR(i, end, 2 * MAX_LINE_LENGTH + 9) {
        buforGlowny[i] = 0;
    }
    FOR(i, end - koniecOstatniejLinii, MAX_LINE_LENGTH + 9) {
        buforParsera[i] = 0;
    }
    end = end - (koniecOstatniejLinii + 1);
    if (buforGlowny[0] == 0) {
        pozycjaBufor = pozycjaParser = 0;
        start = koniecLinii = end = 0;
        koniecOstatniejLinii = -1;
    } else {
        pozycjaBufor = pozycjaParser = end + 1;
        start = koniecLinii = end + 1;
        koniecOstatniejLinii = -1;
    }
}

// pobieranie nowej linii
void getLines() {
    ssize_t k = 0;
    if (pozycjaBufor == end) {
        if (buforGlowny[pozycjaBufor] == '\n') {
            koniecKomendy = 1;
        }
        if (przesuwanieBufora) {
            przesuwanieBufora = 0;
            shiftBufferLeft();
        }
        if (end == 0) {
            if (buforGlowny[0] == 0) {
                k = read(0, buforGlowny, MAX_LINE_LENGTH + 1);
                pozycjaParser = pozycjaBufor = 0;
                start = koniecLinii = 0;
                koniecOstatniejLinii = -1;
                end = k - 1;
            } else {
                k = read(0, buforGlowny + 1, MAX_LINE_LENGTH + 1);
                pozycjaParser = pozycjaBufor = 1;
                start = koniecLinii = 1;
                koniecOstatniejLinii = -1;
                end = k;
            }
        } else if (end > 0) {
            k = read(0, buforGlowny + end + 1, MAX_LINE_LENGTH + 1);
            pozycjaBufor = end + 1;
            pozycjaParser = end + 1;
            start = koniecLinii = end + 1;
            end += k;
            koniecOstatniejLinii = -1;
        }
        if (k == -1) {
            exit(1);
        }

        if (k == 0) {
            if (strlen(buforParsera)) {
                buforParsera[pozycjaParser] = 0;
                if (!liniaZaDluga)
                    execute();
            }
            if (S_ISCHR(typCzytania.st_mode))
                write(1, "\n", 1);

            exit(0);
        }
    }
    while (pozycjaBufor <= end) {
        findNewLine();
        if (koniecLinii - (koniecOstatniejLinii + 1) > MAX_LINE_LENGTH) {
            int p;
            FOR(p, start, end) {
                buforGlowny[p - start] = buforGlowny[p];
            }
            FOR(p, 0, MAX_LINE_LENGTH + 9) {
                buforParsera[p] = 0;
            }
            FOR(p, end + 1, 2 * MAX_LINE_LENGTH + 9) {
                buforGlowny[p] = 0;
            }

            liniaZaDluga = 1;
            koniecLinii = koniecLinii - start;
            start = pozycjaBufor = pozycjaParser = 0;
            end = k - 1;
            koniecOstatniejLinii = -1;
        }
        for (pozycjaBufor = start; pozycjaBufor <= koniecLinii; ++pozycjaBufor, ++pozycjaParser) {
            buforParsera[pozycjaParser] = buforGlowny[pozycjaBufor];
            if (pozycjaBufor == end) {
                if (buforGlowny[pozycjaBufor] == '\n') {
                    buforParsera[pozycjaParser] = 0;
                    koniecOstatniejLinii = end;
                    przesuwanieBufora = 1;
                    if (start != koniecLinii) {
                        if (!liniaZaDluga)
                            execute();
                    }
                    koniecKomendy = 1;
                    memset(buforParsera, 0, MAX_LINE_LENGTH + 10);
                    pozycjaParser = 0;
                    if (liniaZaDluga) {
                        pozycjaBufor = 0;
                        memset(buforParsera, 0, MAX_LINE_LENGTH + 10);
                        memset(buforGlowny, 0, 2 * MAX_LINE_LENGTH + 10);
                        koniecOstatniejLinii = -1;
                        koniecLinii = end = start = 0;
                        przesuwanieBufora = 0;
                        koniecKomendy = 0;
                        liniaZaDluga = 0;
                    }
                }
                return;
            } else if (pozycjaBufor == koniecLinii) {
                if (buforGlowny[pozycjaBufor] == '\n') {
                    buforParsera[pozycjaParser] = 0;
                    koniecOstatniejLinii = pozycjaBufor;

                    if (start != koniecLinii) {
                        przesuwanieBufora = 1;
                        if (!liniaZaDluga)
                            execute();
                    }

                    pozycjaBufor++;
                    start = pozycjaBufor;
                    koniecLinii = start;
                    memset(buforParsera, 0, MAX_LINE_LENGTH + 10);
                    pozycjaParser = 0;

                    if (liniaZaDluga) liniaZaDluga = 0;

                    break;
                }
            }
        }
    }
}

//  petla główna
int main(int argc, char *argv[]) {
    przesuwanieBufora = liniaZaDluga = koniecKomendy = start = end = koniecLinii = pozycjaBufor = pozycjaParser = 0;
    koniecOstatniejLinii = -1;
    if ((fstat(0, &typCzytania)) == -1) exit(1);

    while (1) {
        if (S_ISCHR(typCzytania.st_mode) && (!liniaZaDluga)) {
            write(1, PROMPT_STR, 2);
        }
        getLines();
    }
}
