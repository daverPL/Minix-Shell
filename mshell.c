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
#define SYNTAX() fprintf(stderr, "%s\n", SYNTAX_ERROR_STR) // wypisywanie syntax error
#define READ_END 0 // koncowka pipa odpowiadajaca za czytanie z niego
#define WRITE_END 1 // koncowka pipa odpowiadajaca za pisanie do niego
#define MAX_ENDED_PROCESSES 500 // makysmalna liczba procesow z tla ktore moja struktura przetrzymuje
#define MAX_BUFOR 4000010 // maksymalny rozmiar bufora glownego do ktorego zczytuje standardowe wejscie

char buforGlowny[MAX_BUFOR]; // aktualni odczytane wejscie
char buforParsera[MAX_BUFOR]; // poprawna linia az do \n
int bezPrompta = 0; // czy wypisywac prompt czy nie
int rozmiarGlowny = 0; // liczba znakow odczytanych z wejscia
int rozmiarParser = 0; // liczba znakow w poprawnej linii podanej do parsera
int pozycjaGlowny = 0; // pozycja do ktorej podalismy znaki parserowi juz
int koniecParser = 0; // informacja czy juz w buforze parsera mamy cala linie tzn pojawil sie \n
int liniaZaDluga = 0; // informacja czy podana do bufora parsera nie jest za dluga

int wypisujZakonczone; // w zaleznosci od tryby albo wypisuje albo nie statusy procesow zakonczonych w tle
int liniaDoWykonaniaTlo = 0; // informacja czy podana linia ma byc wykonywana w tle - 1 czy normalnie - 0
int statusyZakonczonychTlo[MAX_ENDED_PROCESSES][2]; // struktura do przetrzymywania informacji o statusach zakonczonych procesow w tle
volatile int liczbaZakonczonychTlo = 0; // liczba procesow w tle ktore sie zakonczyly
volatile int ileJeszczeDzialaNormalnie = 0; // liczba procesow dzialajacych normalnie ktore jeszcze dzialaja
volatile int pidyWykonywanychNormalnie[MAX_ENDED_PROCESSES]; // pidy procesow wykonywanych normalnie
volatile int ileProcesowLiniaNormalna = 0; // liczba procesow w lini normalnej tj na ile procesow zakonczonych potrzeba czekac

struct stat typCzytania; // sluzy do odczytania informacji czy mamy standardowe wejscie czy z pliku etc
struct sigaction sigINT; // struktura do obslugi sygnalow sigINT
struct sigaction sigCHLD; // struktura do obslugi sygnalu sigCHLD

int i;

void chldHandler(int sig_nb) {
    // piszemy tutaj handler dla sygnalow z tla
    // najpierw zapisujemy jakie bylo errno przed wejsciem do handlera
    int savedErrno = errno;
    int pid = 0;
    int status;
    // czekamy na ktorys proces dziecka => -1
    // whohang zeby wyjsc natychmiast gdy zadne dziecko nie wejdzie
    while ((pid = waitpid(-1, &status, WNOHANG))) {
        // pid jest -1 czyli mamy jakis blad
        if (pid == -1) {
            // blad taki ze proces o numerze pid nie istnieje albo nie jest dzieckiem procesu czekajacego
            if (errno == ECHILD) {
                break;
            } else {
                exit(1);
            }
        } else {
            // proces sie zakonczyl jakos poprawnie
            // to sprawdzam czy byl normalny czy z tla
            // zwrocony pid to pid dziecka ktory sie zakonczyl
            int answer = 0;
            FOR(i, 0, ileProcesowLiniaNormalna - 1) {
                if (pidyWykonywanychNormalnie[i] == pid) {
                    answer = 1;
                }
            }
            // jak byl normalny to zmniejszam licznik normalnych
            // a jak byl z tla to wtedy dopisuje jego status do tablicy wynikow procesow
            // zakonczonych z tla
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
    // przywracamy poprzednie errno sprzed obslugi sygnalu
    errno = savedErrno;
}

int statusOtwarciaPliku(command *com) {
    // jezeli nie udalo sie otworzyc pliku, do ktorego mielismy przekierowanie to za pomoca errno sprawdzamy sobie
    // jaki typ bledu nastapil i wywalamy na stderr stosowny komunikat
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
    // jezeli nie udalo sie wykonac execvp to patrzymy jaki blad nastapil i wywalamy stosowny komunikat na stderr
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
    // tutaj sprawdzamy czy komenda jest jedna z komend wbudowanych typu ls cd etc i wykonujemy je
    // zwraca 1 jak komenda byla wbudowana i udalo sie bez bledu wykonac oraz 0 w przeciwnym przypadku
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
    // no tutaj mamy powiedzione ze w komendzie com mamy l znakow przekierowania
    // iterujemy sie po tych przekierowaniach i dla kazdego robimy co trzeba
    FOR(i, 0, l - 1) {
        int f;
        // jak bylo wejscie <
        if (IS_RIN(((com->redirs)[i])->flags)) {
            // otwieram ten plik w trybie tylko do odczytu
            if ((f = open((com->redirs[i])->filename, O_RDONLY, S_IRWXU | S_IRWXG | S_IRWXO)) == -1) {
                statusOtwarciaPliku(com);
            }
            // jak sie uda to zamykam standardowe wejscie
            if (close(STDIN_FILENO) == -1) exit(1);
            // duplikujemy deskryptor z otwartym plikiem na standardowe wejscie
            if (dup2(f, STDIN_FILENO) == -1) exit(1);
            // a jak juz skopiowalismy to zamykam deskryptor f
            if (close(f) == -1) exit(1);
        }

        if (IS_ROUT(((com->redirs)[i])->flags)) {
            // otwieram plik do zapisu, a jak go nie ma to go tworzymy
            if ((f = open(((com->redirs)[i])->filename, O_WRONLY | O_CREAT | O_TRUNC, S_IRWXU | S_IRWXG | S_IRWXO)) ==
                -1) {
                statusOtwarciaPliku(com);
            }
            // jak sie udalo bez bledow do tego momentu to zamykam standardowe wyjscie
            if (close(STDOUT_FILENO) == -1) exit(1);
            // duplikuje deskryptor z plikiem na standardowe wyjscie
            if (dup2(f, STDOUT_FILENO) == -1) exit(1);
            // zamykam ten deskryptor z plikiem, bo juz skopiowany na standardowe wyjscie
            if (close(f) == -1) exit(1);

        } else if (IS_RAPPEND(((com->redirs)[i])->flags)) {
            // bylo przekierowanie bledow to otwieram plik do zapisu, jak nie ma to tworze
            if ((f = open(((com->redirs)[i])->filename, O_WRONLY | O_CREAT | O_APPEND, S_IRWXU | S_IRWXG | S_IRWXO)) ==
                -1) {
                statusOtwarciaPliku(com);
            }
            // zamykam standardowe wyjscie
            if (close(STDOUT_FILENO) == -1) exit(1);
            // duplikuje deskryptor otwartego pliku na standardowe wyjscie
            if (dup2(f, STDOUT_FILENO) == -1) exit(1);
            // zamykam deskryptor od pliku bo juz skopiowany na standardowe wyjscie
            if (close(f) == -1) exit(1);
        }
    }
}

void execute() {
    // wykonywanie konkretnej lini konczacej sie \n
    line *ln = NULL;
    int aktualnyPipe = 0;
    int liczbaPipow = 0;

    buforParsera[rozmiarParser] = 0;

    // jezeli parser jest pusty albo linia jest komentarzem bo zaczyna sie od # to wywalam od razu
    if (rozmiarParser == 0 || rozmiarParser == 1 || buforParsera[0] == '\n' || buforParsera[0] == '#') {
        return;
    }

    // uzywam parsera do sparsowania lini z buforaparsera, jak sie nie powiodlo to wywalam syntax error
    if ((ln = parseline(buforParsera)) == NULL) {
        SYNTAX();
        return;
    }

    // sprawdzam czy linia ma byc wykonywana w tle czy nie na podstawie flagi ln->flags
    liniaDoWykonaniaTlo = ln->flags;
    wypisujZakonczone = 1 - ln->flags;

    // licze ile w linii jest pipelinow oddzielonych ;
    while ((ln->pipelines[liczbaPipow]) != NULL) {
        liczbaPipow++;
    }

    // dla kazdego pipelina
    FOR(aktualnyPipe, 0, liczbaPipow - 1) {
        // licze ile ma komend w srodku
        int liczbaKomend = 0;
        while (ln->pipelines[aktualnyPipe][liczbaKomend] != NULL) {
            liczbaKomend++;
        }

        // oraz czy nie ma pustych komeda, bo wtedy zgodnie z specyfikacja mamy wywalic blad
        int pozycja = 0;
        FOR(pozycja, 0, liczbaKomend - 1) {
            if (ln->pipelines[aktualnyPipe][pozycja]->argv[0] == NULL && liczbaKomend > 1) {
                SYNTAX();
                return;
            }
        }
    }

    // wszystko poszlo pomyslnie wiec dla kazdego pipelina
    FOR(aktualnyPipe, 0, liczbaPipow - 1) {
        int aktualnaKomenda = 0;
        int liczbaKomend = 0;

        ileJeszczeDzialaNormalnie = 0;
        ileProcesowLiniaNormalna = 0;

        // licze sobie ile komend jest w aktualnym pipie
        while (ln->pipelines[aktualnyPipe][liczbaKomend] != NULL) {
            liczbaKomend++;
        }

        // jezeli komenda jest jedna to patrze czy jest wbudowana i jak jest to tam ja robie i koniec
        if (liczbaKomend == 1) {
            if (komendaWbudowana(ln->pipelines[aktualnyPipe][0]) == 1) {
                continue;
            }
        }

        // taka nieco duza tablica pipow do komunikacji miedzy kolejnymi komendami z pipelina
        int pipes[liczbaKomend][2];

        // struktura do opisu sygnalu
        sigset_t newMask;
        // robimy pusty zbiór sygnałów
        sigemptyset(&newMask);
        // dodajemy do zbioru sygnałów sygnał SIGCHILD
        sigaddset(&newMask, SIGCHLD);
        // zmieniamy zestaw aktualnie blokowanych sygnalow
        // zmienna how  ustawiona na SIG_BLOCK zeby dodaj zbior sygnalow z newMask do sygnalow blokowanych
        sigprocmask(SIG_BLOCK, &newMask, NULL);

        // dla kazdej komendy w linii
        FOR(aktualnaKomenda, 0, liczbaKomend - 1) {
            command *com = ln->pipelines[aktualnyPipe][aktualnaKomenda];

            pid_t pid;

            // probuje stworzyc pipa dla tej komendy
            if (pipe(pipes[aktualnaKomenda]) == -1) exit(1);

            // jezeli ta komenda jest juz dalszą to wczesniejszej ktora juz z pipa nie korzysta moge sprobowac go zamknac
            if (aktualnaKomenda >= 2) {
                if (close(pipes[aktualnaKomenda - 2][READ_END]) == -1) exit(1);
                if (close(pipes[aktualnaKomenda - 2][WRITE_END]) == -1) exit(1);
            }

            // forkuje sie
            if ((pid = fork()) == -1) {
                // fork sie nie powiodl
                exit(1);
            } else if (pid == 0) {
                // fork sie powiodl i jestem teraz w procesie dziecka
                int liczbaPrzekierowan = 0;

                // dziecko zlicza sobie ile ma przekierowan w linii
                while (com->redirs[liczbaPrzekierowan] != NULL) {
                    liczbaPrzekierowan++;
                }

                // w procesie dziecka odblokowuje sygnały w masce newMask czyli SIGCHLD
                sigprocmask(SIG_UNBLOCK, &newMask, NULL);
                // wyczyszczenie zbioru sygnalow
                sigemptyset(&sigINT.sa_mask);
                // sygnal bedzie obslugiwany w sposob  domyslny zdefiniowany w systemie
                sigINT.sa_handler = SIG_DFL;
                // zmieniamy sposob obslugi sygnalu SIGINT na te zapisana w sigINT
                sigaction(SIGINT, &sigINT, NULL);

                // zeruje zbior sygnalow
                sigemptyset(&sigCHLD.sa_mask);
                // ustawiam obsluge domyslna systemowa dla sygnalu sigCHLD
                sigCHLD.sa_handler = SIG_DFL;
                // zmieniam sposob obslugi sygnalu SIGCHILD dla tego procesu
                sigaction(SIGCHLD, &sigCHLD, NULL);

                // jezeli linia miala byc wykonana w tle to probuje
                if (liniaDoWykonaniaTlo == 1) {
                    // stworzyc nowy rod dla tych z tla zeby sigINT ich nie zabil
                    if (setsid() == -1) exit(1);
                }

                // zamykam sobie koncowke czytajaca pipa ktorego bede uzywal
                if (close(pipes[aktualnaKomenda][READ_END]) == -1) exit(1);

                // dla wszystkich komend poza pierwsza
                if (aktualnaKomenda > 0) {
                    // zamykam poprzedniej koncowke piszaca do pipa
                    if (close(pipes[aktualnaKomenda - 1][WRITE_END]) == -1) exit(1);
                    // sobie zamykam standardowe wejscie
                    if (close(STDIN_FILENO) == -1) exit(1);
                    // podpinam koncowke czytajaca pipa poprzedniego procesu do mojego standardowego wejscia zeby mi podawal wejscie
                    if (dup2(pipes[aktualnaKomenda - 1][READ_END], STDIN_FILENO) == -1) exit(1);
                    // a jak juz podpialem na moje standardowe wejscie deskryptor czytajacy poprzedniego pipa to moge tego pipa zamknac bo nikt go nie uzyje
                    if (close(pipes[aktualnaKomenda - 1][READ_END]) == -1) exit(1);
                }
                // no a jak to nie jest komenda ostatnia w linii to wtedy
                if (aktualnaKomenda < liczbaKomend - 1) {
                    // zamykam jej standardowe wyjscie
                    if (close(STDOUT_FILENO) == -1) exit(1);
                    // duplikuje deskryptor do pipa do ktorego ona ma pisac na standardowe wyjscie
                    if (dup2(pipes[aktualnaKomenda][WRITE_END], STDOUT_FILENO) == -1) exit(1);
                    // zaraz pocztm moge zamknac koncowke piszaca w pipie bo juz skopiowane na standfardowe wyjscie
                    if (close(pipes[aktualnaKomenda][WRITE_END]) == -1) exit(1);
                }
                // a jezeli jest to ostatnia komenda
                // to zamykam tylko pipa i od niej chce zeby rzeczywiscie pisala na standardowe wyjscie
                if (aktualnaKomenda == liczbaKomend - 1) {
                    if (close(pipes[aktualnaKomenda][WRITE_END]) == -1) exit(1);
                }

                // tutaj w tej funkcji sie robia te wszystkie przekierowania ktore dana komenda dostala ze ma miec
                przekierowaniaWejscia(liczbaPrzekierowan, com);

                // probujemy wykonac komende
                if (execvp(com->argv[0], com->argv) == -1) {
                    // jak sie niue powiedzie to wywalamy blad
                    statusExec(com);
                    exit(EXEC_FAILURE);
                }
            } else {
                // teraz jestesmy w procesie ojca
                // jezeli linie nie ma byc wykonywana w tle
                if (liniaDoWykonaniaTlo == 0) {
                    // to zwiekszam licznik liczby procesow na wynik ktorych musze czekac bo dzialaja normalnie
                    ileJeszczeDzialaNormalnie++;
                    // zapisuje takze pid dziecka wykonywanego normlanie do tablicy pidow
                    pidyWykonywanychNormalnie[ileProcesowLiniaNormalna] = pid;
                    // no i zwiekszam indeks pod ktorym zapisuje
                    ileProcesowLiniaNormalna++;
                }

                // no musze tutaj posprzatac pipy po dziecku jak juz skonczylo swoja zabawe
                if (aktualnaKomenda == liczbaKomend - 1) {
                    if (aktualnaKomenda > 0) {
                        if (close(pipes[aktualnaKomenda - 1][READ_END]) == -1) exit(1);
                        if (close(pipes[aktualnaKomenda - 1][WRITE_END]) == -1) exit(1);
                    }
                    if (close(pipes[aktualnaKomenda][READ_END]) == -1) exit(1);
                    if (close(pipes[aktualnaKomenda][WRITE_END]) == -1) exit(1);

                    // robimy nowa maske
                    sigset_t mask;
                    // czyscimy zbior jej sygnalow
                    sigemptyset(&mask);
                    // dodajemy jej wszystkie sygnaly
                    sigfillset(&mask);
                    // usuwamy z maski sygnal SIGCHLD
                    sigdelset(&mask, SIGCHLD);

                    // jak juz sie okaze ze nasz proces sie skonczyl to czekamy na reszte procesow z przodu iel ich tam jest
                    // az sie wszystkie zakoncza poprawnie
                    while (ileJeszczeDzialaNormalnie > 0) {
                        // tymczasowo zastepujemy maske sygnalow podana w mask
                        // proces zostanie zawieszony do momentu kiedy nadejdzie odblokowany sygnal
                        // po obsludze sygnalu ponownie jest ustawiana maska sprzed wywolania sigsuspend
                        sigsuspend(&mask);
                    }

                    // przywroc maske sygnalow do tej sprzed calosci zabawy
                    sigprocmask(SIG_UNBLOCK, &newMask, NULL);

                    // jezeli linia byla wykonywana w tle to powypisuje te z tla ktore zdazyly sie zakonczyc
                    if (liniaDoWykonaniaTlo == 1)
                        wypisujZakonczone = 1;
                }
            }
        }
    }
}

void readline() {
    // flaga trzymajaca info czy linia jest za dluga czy nie
    liniaZaDluga = 0;
    int flaga = 0;

    while (1) {
        // jezeli w parserze jest linia zakonczona znakiem konca linii too wtedy ja wykonaj
        if (koniecParser == 1) {
            execute();
            // po wykonaniu czyszcze buforParsera i zeruje co nalezy
            memset(buforParsera, 0, 2 * rozmiarParser + 10);
            rozmiarParser = 0;
            koniecParser = 0;
            break;
        } else {
            // jezeli nie ma w parserze znaku konca linii
            if (rozmiarGlowny == 0) {
                A:
                // jezeli w buforze glownym nie mamy nic to czytamy ile mozna
                pozycjaGlowny = 0;
                rozmiarGlowny = read(0, buforGlowny, MAX_BUFOR / 2);
            }

            // ewentualna obsluga bledow

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

            // sprawdzam sobie czy w buforze glownym przyszedl jakis koniec linii
            int jestKoniec = 0;

            FOR(i, pozycjaGlowny, rozmiarGlowny - 1) {
                if (buforGlowny[i] == '\n') {
                    jestKoniec = 1;
                    break;
                }
            }

            if (jestKoniec == 1) {
                // jezeli przyszedl to ten kawalek bufora do tego konca linii podaje parserowi
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
                // a gdy w buforze glownym zostalo juz wzglednie malo znakow to przesuwam to co jest w buforze na jego poczatek
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
                flaga = 0;
            } else {
                // a jak w buforze glownym nie dostalismy konca linii to znaczy ze caly bufor glowny mozna wrzucic do parsera
                // nic sie nie stanie a my oczekujemy na kolejny odczyt reada ktory byc moze poda nam nowy koniec linii
                FOR(i, pozycjaGlowny, rozmiarGlowny - 1) {
                    buforParsera[rozmiarParser] = buforGlowny[i];
                    rozmiarParser++;
                }
                // zeruje co trzeba
                memset(buforGlowny, 0, 2 * rozmiarGlowny + 10);
                rozmiarGlowny = 0;
                pozycjaGlowny = 0;
                rozmiarGlowny = read(0, buforGlowny, MAX_BUFOR / 2);
                koniecParser = 0;
                flaga = 1;
            }
        }

        // gdyby przypadkiem do parsera zostala podana za duza linia to mamy problem
        // nalezy wiec wyrzucic syntax error i doczytac z buforu glownego az do \n bowiemy ze ta linia az do \n jest za dluga
        if (rozmiarParser >= MAX_LINE_LENGTH) {
            liniaZaDluga = 1;
            memset(buforParsera, 0, 2 * rozmiarParser + 10);
            if(flaga == 1) goto A;
            rozmiarParser = 0;
            koniecParser = 0;
            SYNTAX();
        }
    }
}

int main(int argc, char *argv[]) {
    // to oznacza ze sygnal bedzie ignorowany bo taka funkcje obslugi podalismy
    sigINT.sa_handler = SIG_IGN;
    // nadzoruje obsluge sygnalu przez jadro
    sigINT.sa_flags = 0;
    // tutaj zaznaczam ze podczas procesu oblsugi sygnalu ktoremu podamy sigINT zadne syganly nie maja byc blokowane
    sigemptyset(&sigINT.sa_mask);
    // to oznacza ze sygnal SIGINT chcemy obsluzyc tak zeby go ignorowac
    sigaction(SIGINT, &sigINT, NULL);

    // dla sygnalu sigCHLD definiujemy sposob obslugi w naszej funkcji napisanej wyzej
    sigCHLD.sa_handler = chldHandler;
    // nadzoruje obsluge sygnalu przez jadro
    sigCHLD.sa_flags = SA_RESTART;
    // podobnie zaznaczamy ze podczas obslugi ma nie blokowac sygnalow
    sigemptyset(&sigCHLD.sa_mask);
    // dodajemy dla sygnalu SIGCHLD nasz sposob jego obslugi zdefiniowany w handlerze
    sigaction(SIGCHLD, &sigCHLD, NULL);

    // tutaj patrze w jaki sposob sa podawne dane czy z standardowego wejscia czy z pliku etc
    if ((fstat(0, &typCzytania)) == -1) exit(1);

    while (1) {
        if (S_ISCHR(typCzytania.st_mode) && (!liniaZaDluga)) {
            if (wypisujZakonczone) {
                // jezeli mamy jakies procesy w tle zakonczone to
                wypisujZakonczone = 0;
                // robimy nowa maske dla sygnalow
                sigset_t newMask;
                // robimy ja pusta na poczatek
                sigemptyset(&newMask);
                // dodajemy sygnal sigCHLD
                sigaddset(&newMask, SIGCHLD);
                // blokujemy go
                sigprocmask(SIG_BLOCK, &newMask, NULL);
                // dla kazdego zakonczonego procesu
                FOR(i, 0, liczbaZakonczonychTlo - 1) {
                    int childPid = statusyZakonczonychTlo[i][0];
                    int exitStatus = statusyZakonczonychTlo[i][1];
                    // biore jego pid i status i wypisuje stosowny komunikat
                    if (WIFEXITED(exitStatus)) {
                        printf("Background process %d terminated. (exited with status %d)\n", childPid,
                               WEXITSTATUS(exitStatus));
                    } else if (WIFSIGNALED(exitStatus)) {
                        printf("Background process %d terminated. (killed by signal %d)\n", childPid,
                               WTERMSIG(exitStatus));
                    }

                    fflush(stdout);
                }
                // odblokowujemy go
                sigprocmask(SIG_UNBLOCK, &newMask, NULL);
                // zerujemy liczbe procesow w tle ktore sie zakonczyly
                liczbaZakonczonychTlo = 0;
                ileProcesowLiniaNormalna = 0;
            }

            // wypisz prompta jak go powinien wypisac
            if (bezPrompta == 0) {
                write(1, PROMPT_STR, 2);
            }
        }
        bezPrompta = 0;
        readline();
    }
}