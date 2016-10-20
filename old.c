#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include "config.h"
#include "siparse.h"
#include "utils.h"
#include "errno.h"

int n;
line * ln;
command *com;
char *buforTemp;
char bufor[1000000];

// wypisywanie znaku zachęty w wypadku wczytywania danych z terminala 
void wypiszPrompt() {
	write(1, PROMPT_STR, 2);
}

// sprawdza czy podana linia nie jest dluzsza niz maksymalna dlugosc jaka moze obsluzyc parser
int sprawdzDlugoscLini(char b[MAX_LINE_LENGTH + 1]) {
	if(strlen(bufor) > MAX_LINE_LENGTH) {
		fprintf(stderr, SYNTAX_ERROR_STR);
		fprintf(stderr, "\n");
		return 0;
	}
	return 1;
}

// jezeli nie powiodlo sie wykonanie polecenia sprawdza z jakiego powodu
void sprawdzTypBledu(command *c) {
	switch(errno) {
		case ENOENT:
			strcat(c->argv[0], ": no such file or directory\n");
			break;
		case EACCES:
			strcat(c->argv[0], ": permission denied\n");
			break;
		default:
			strcat(c->argv[0], ": exec error\n");
			break;
	}
}

// wykonanie forka i wykonanie polecenia w procesie potomnym
void wykonaj(command *c) {
	pid_t pid;
	int status;
	
	if((pid = fork()) < 0) {
		// nie udalo sie zforkowac
		exit(1);
	} else if(pid == 0) {
		// proces dziecka
		if(execvp(c->argv[0], c->argv) < 0) {
			sprawdzTypBledu(c);
			fprintf(stderr, c->argv[0]);
			exit(EXEC_FAILURE);
		}
	} else {
		// proces rodzica
		while (wait(&status) != pid);
	}
}

// glowna czesc kodu w wypadku gdy uzytkownik podaje polecenia przez terminal
void readFromTerminal() {
	wypiszPrompt();
	
	while((n = read(0, bufor, MAX_LINE_LENGTH)) > 0) {
		bufor[n] = 0;
		if(!sprawdzDlugoscLini(bufor)) continue;
		ln = parseline(bufor);
		com = pickfirstcommand(ln);

		wykonaj(com);
		wypiszPrompt();
	}
}

// glowna czesc kodu gdy uzytkownik wczytuje dane z pliku
void readFromFile() {
	while((n = read(0, bufor, 1000000)) > 0) {
		int k = 0;
		while(k < n) {
			int start = k;
			while(k < n && bufor[k] != '\n' && bufor[k] != 0) {
				k++;
			}
			
			if(bufor[k] == '\n' || bufor[k] == 0) {
				// od pozycji start do pozycji k jest aktualna cala linia
				buforTemp = malloc((k - start + 1) * sizeof(char));
				int i; 
				for(i = start; i < k; i++) {
					buforTemp[i - start] = bufor[i];
				}
				buforTemp[k-start] = 0;
			
				ln = parseline(buforTemp);
				com = pickfirstcommand(ln);

				wykonaj(com);
				free(buforTemp);
			} else {
				// skonczyl sie bufor ale konca lini jeszcze nie bylo 
				// od pozycji start do koniec jest nie cala linia trzeba doczytac reszte
				fprintf(stderr, "NASTAPIL BLAD");
			}
			k++;
		}
	}
}

int main(int argc, char *argv[]) {
	struct stat statBuffer;
	fstat(0, &statBuffer);
	
	if (S_ISCHR(statBuffer.st_mode) != 0) {
		readFromTerminal();
	} else {
		readFromFile();
	}
}
