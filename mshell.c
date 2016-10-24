#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include "include/siparse.h"
#include "include/config.h"
#include "include/utils.h"
#include "include/builtins.h"

int i;
char bG[2 * MAX_LINE_LENGTH + 10];   // buffer glowny
char bP[MAX_LINE_LENGTH + 10];       // buffor do parsowania
int start, end;
int kK;                              // koniec komendy
int shiftB;                          // przesuwanie buffera
int pozB;                            // pozycja aktualna w bufferze
int pozBP;                           // pozycja aktualna w bufferzr do parsowania
int kL;                              // koniec linii
int kOL;                             // koniec ostatniej linii
int lZD;                             // linia za dluga
struct stat typCzytania;             // sprawdzanie skad przychodza dane

// wykonywanie komendy
void execute() {
    line *ln = NULL;
    command *com = NULL;
    ln = parseline(bP);
    if ((ln == NULL) || (*(ln->pipelines) == NULL) || (**(ln->pipelines) == NULL)) {
        fprintf(stderr, "%s\n", SYNTAX_ERROR_STR);
        return;
    }

    com = pickfirstcommand(ln);
    if ((com == NULL) || (com->argv == NULL) || (com->argv[0] == NULL)) {
        if (bP[0] != '#') {
            fprintf(stderr, "%s\n", SYNTAX_ERROR_STR);
        }
    }

    int p = 0, typKomendy = 0;
    while (p < 6) {
        if (com->argv[0] != NULL && strcmp(builtins_table[p].name, com->argv[0]) == 0) {
            typKomendy = 1;
            break;
        }
        p++;
    }

    if (typKomendy == 1) {
        int result = builtins_table[p].fun(com->argv);
    } else {
        pid_t pid;

        if ((pid = fork()) == -1) {
            exit(1);
        } else if (pid == 0) {
            if (execvp(com->argv[0], com->argv) == -1) {
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
                exit(EXEC_FAILURE);
            }
        } else if (waitpid(pid, NULL, 0) == -1) {
            exit(1);
        }
    }
}

// znajdowanie nowej linii
void findNewLine() {
    int i = 0;
    for (i = start; bG[i] != 0; ++i) {
        if (bG[i] == '\n') {
            kL = i;
            return;
        }
    }
    if (i > 0)
        kL = i - 1;
    else kL = 0;
}

// przesuwanie buffera w lewo
void shiftBufferLeft() {
    if (kK) {
        kK = 0;
        start = end = kL = 0;
        kOL = -1;
        pozB = pozBP = 0;
        memset(bP, 0, MAX_LINE_LENGTH + 10);
        memset(bG, 0, 2 * MAX_LINE_LENGTH + 10);
        return;
    }

    for (i = kOL + 1; i <= end; ++i) {
        bG[i - (kOL + 1)] = bG[i];
        bP[i - (kOL + 1)] = bG[i];
    }
    for (i = end - kOL; i < 2 * MAX_LINE_LENGTH + 10; ++i)
        bG[i] = 0;
    for (i = end - kOL; i < MAX_LINE_LENGTH + 10; ++i)
        bP[i] = 0;
    end = end - (kOL + 1);
    if (bG[0] == 0) {
        pozB = pozBP = 0;
        start = kL = end = 0;
        kOL = -1;
    } else {
        pozB = pozBP = end + 1;
        start = kL = end + 1;
        kOL = -1;
    }
}

// pobieranie nowej linii
void getLines() {
    int k = 0;
    if (pozB == end) {
        if (bG[pozB] == '\n') {
            kK = 1;
        }
        if (shiftB) {
            shiftB = 0;
            shiftBufferLeft();
        }
        if (end == 0) {
            if (bG[0] == 0) {
                k = read(0, bG, MAX_LINE_LENGTH + 1);
                pozBP = pozB = 0;
                start = kL = 0;
                kOL = -1;
                end = k - 1;
            } else {
                k = read(0, bG + 1, MAX_LINE_LENGTH + 1);
                pozBP = pozB = 1;
                start = kL = 1;
                kOL = -1;
                end = k;
            }
        } else if (end > 0) {
            k = read(0, bG + end + 1, MAX_LINE_LENGTH + 1);
            pozB = end + 1;
            pozBP = end + 1;
            start = kL = end + 1;
            end += k;
            kOL = -1;
        }
        if (k == -1) {
            exit(1);
        }

        if (k == 0) {
            if (strlen(bP)) {
                bP[pozBP] = 0;
                if (!lZD)
                    execute();
            }
            if (S_ISCHR(typCzytania.st_mode))
                write(1, "\n", 1);

            exit(0);
        }
    }
    while (pozB <= end) {
        findNewLine();
        if (kL - (kOL + 1) > MAX_LINE_LENGTH) {
            int p;
            for (p = start; p <= end; ++p)
                bG[p - start] = bG[p];

            for (p = 0; p < MAX_LINE_LENGTH + 10; ++p)
                bP[p] = 0;
            for (p = end + 1; p < 2 * MAX_LINE_LENGTH + 10; ++p)
                bG[p] = 0;

            lZD = 1;
            kL = kL - start;
            start = pozB = pozBP = 0;
            end = k - 1;
            kOL = -1;
        }
        for (pozB = start; pozB <= kL; ++pozB, ++pozBP) {
            bP[pozBP] = bG[pozB];
            if (pozB == end) {
                if (bG[pozB] == '\n') {
                    bP[pozBP] = 0;
                    kOL = end;
                    shiftB = 1;
                    if (start != kL) {
                        if (!lZD)
                            execute();
                    }
                    kK = 1;
                    memset(bP, 0, MAX_LINE_LENGTH + 10);
                    pozBP = 0;
                    if (lZD) {
                        pozB = 0;
                        memset(bP, 0, MAX_LINE_LENGTH + 10);
                        memset(bG, 0, 2 * MAX_LINE_LENGTH + 10);
                        kOL = -1;
                        kL = end = start = 0;
                        shiftB = 0;
                        kK = 0;
                        lZD = 0;
                    }
                }
                return;
            } else if (pozB == kL) {
                if (bG[pozB] == '\n') {
                    bP[pozBP] = 0;
                    kOL = pozB;

                    if (start != kL) {
                        shiftB = 1;
                        if (!lZD)
                            execute();
                    }

                    pozB++;
                    start = pozB;
                    kL = start;
                    memset(bP, 0, MAX_LINE_LENGTH + 10);
                    pozBP = 0;

                    if (lZD) lZD = 0;

                    break;
                }
            }
        }
    }
}

//  petla główna
int main(int argc, char *argv[]) {
    shiftB = lZD = kK = start = end = kL = pozB = pozBP = 0;
    kOL = -1;
    if ((fstat(0, &typCzytania)) == -1) exit(1);

    while (1) {
        if (S_ISCHR(typCzytania.st_mode) && (!lZD)) {
            write(1, PROMPT_STR, 2);
        }
        getLines();
    }
}
