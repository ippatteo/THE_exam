// ============ INCLUDES - Librerie necessarie per il server TCP ============
#include <string.h>      // strlen, bzero, strcpy - manipolazione stringhe
#include <unistd.h>      // write, close - operazioni base I/O e file descriptors
#include <netdb.h>       // funzioni di rete (gethostbyname, etc.)
#include <sys/socket.h>  // socket, bind, listen, accept, send, recv - core networking
#include <netinet/in.h>  // struct sockaddr_in, INADDR_ANY - strutture indirizzi IP
#include <stdlib.h>      // atoi, exit, malloc, free, realloc - gestione memoria dinamica
#include <stdio.h>       // sprintf - formattazione stringhe (⚠️ può includere funzioni non allowed)
#include <sys/select.h>  // select, FD_SET, FD_ISSET, fd_set - multiplexing I/O
#include <signal.h>      // aggiunto - per gestione SIGINT e cleanup memoria

// ============ STRUTTURA CLIENT - Rappresenta ogni client connesso ============
typedef struct s_clients {
    int     id;          // ID univoco client (0, 1, 2, 3, ...)
    char    *msg;        // aggiunto - Buffer dinamico per accumolare messaggi parziali/incompleti
    size_t  msg_capacity; // aggiunto - Capacità attuale del buffer
    size_t  msg_len;     // aggiunto - Lunghezza corrente del messaggio
} t_clients;

// ============ VARIABILI GLOBALI - Stato del server ============
t_clients   clients[1024];        // Array di max 1024 client (hardcoded, no #define)
fd_set      readfds, writefds, active;  // Set di file descriptors per select(), //nomi scelti, fd sets: tipo definito in sys/select
                                  // - active: FD attivi (server + tutti i client)
                                  // - readfds: FD pronti per lettura (copia di active)
                                  // - writefds: FD pronti per scrittura (copia di active)
int         fdMax = 0, idNext = 0; // fdMax: massimo FD attivo, idNext: prossimo ID cliente
char        bufferRead[120000], bufferWrite[120000]; // Buffer globali per I/O

// aggiunto - CLEANUP MEMORIA - Libera tutta la memoria allocata
void cleanupAllClients() {
    for (int i = 0; i <= fdMax; i++) {
        if (clients[i].msg) {
            free(clients[i].msg);
            clients[i].msg = NULL;
        }
    }
}

// aggiunto - GESTIONE SIGINT - Cleanup memoria prima di uscire
void handleSigint(int sig) {
    (void)sig;  // Evita warning unused parameter
    cleanupAllClients();
    exit(0);
}

// ============ GESTIONE ERRORI - Stampa errore ed esce ============
void    ftError(char *str) {
    if (str)
        write(2, str, strlen(str));           // Se messaggio custom, scrivilo su stderr
    else
        write(2, "Fatal error", strlen("Fatal error")); // Altrimenti messaggio default
    write(2, "\n", 1);                        // Aggiungi newline richiesto dal subject
    exit(1);                                  // Termina con codice errore 1
}

// aggiunto - GESTIONE MEMORIA DINAMICA - Inizializza buffer client
void initClient(int fd) {
    clients[fd].id = idNext++;
    clients[fd].msg = malloc(1024);  // Buffer iniziale da 1KB
    if (!clients[fd].msg)
        ftError(NULL);  // Errore malloc -> "Fatal error"
    clients[fd].msg[0] = '\0';
    clients[fd].msg_capacity = 1024;
    clients[fd].msg_len = 0;
}

// aggiunto - GESTIONE MEMORIA DINAMICA - Libera buffer client
void freeClient(int fd) {
    if (clients[fd].msg) {
        free(clients[fd].msg);
        clients[fd].msg = NULL;
    }
    clients[fd].msg_capacity = 0;
    clients[fd].msg_len = 0;
}

// aggiunto - GESTIONE MEMORIA DINAMICA - Ridimensiona buffer se necessario
int resizeClientBuffer(int fd, size_t needed_size) {
    if (needed_size <= clients[fd].msg_capacity)
        return 0;  // Buffer già abbastanza grande
    
    size_t new_capacity = clients[fd].msg_capacity;
    while (new_capacity < needed_size)
        new_capacity *= 2;  // Raddoppia fino a raggiungere la dimensione necessaria
    
    char *new_msg = realloc(clients[fd].msg, new_capacity);
    if (!new_msg)
        return -1;  // Errore realloc
    
    clients[fd].msg = new_msg;
    clients[fd].msg_capacity = new_capacity;
    return 0;
}

// ============ BROADCAST MESSAGGI - Invia a tutti tranne uno ============
void    sendAll(int not) {
    // Scorre tutti i possibili file descriptors da 0 a fdMax
    for(int i = 0; i <= fdMax; i++)
        // Controlla se il FD è attivo NEL SET WRITEFDS e diverso da 'not'
        if(FD_ISSET(i, &writefds) && i != not) //if((((&writefds)->fds_bits[(i) / NFDBITS] & (1 << ((i) % NFDBITS))) && i != not)) legge i bit di writefd, che è un array di bits
            // Invia il contenuto di bufferWrite a questo client
            send(i, bufferWrite, strlen(bufferWrite), 0);
}

// ============ MAIN FUNCTION - Core del server ============
int main(int ac, char **av) {
    // aggiunto - Setup handler SIGINT per cleanup memoria
    signal(SIGINT, handleSigint);
    
    // ---- CONTROLLO ARGOMENTI ----
    if (ac != 2)
        ftError("Wrong number of arguments");  // Subject richiede esattamente 1 argomento (porta)

    // ---- CREAZIONE SOCKET SERVER ----
    int sockfd = socket(AF_INET, SOCK_STREAM, 0); // domain: ipv4, stream socket: tcp, protocol: auto
    if (sockfd < 0)
        ftError(NULL);  // Errore di sistema -> "Fatal error"

    // ---- INIZIALIZZAZIONE STRUTTURE SELECT ----
    FD_ZERO(&active);           // Azzera set di file descriptors attivi, non strettamente necess. best pracxtice
    bzero(&clients, sizeof(clients)); // Azzera array di client, best practice
    fdMax = sockfd;             // Socket server è il primo FD registrato
    FD_SET(sockfd, &active);    // Aggiungi socket server al set attivo

    // ---- CONFIGURAZIONE INDIRIZZO SERVER ----
    struct sockaddr_in  servaddr; // Struttura per indirizzo IP del server, definita nelle librerie di sistema, serveraddr serve per impostare delle cose
    socklen_t           len;       // Lunghezza struttura indirizzo (per accept)
   	bzero(&servaddr, sizeof(servaddr)); // Azzera struttura
    servaddr.sin_family = AF_INET;       // Famiglia IPv4
	servaddr.sin_addr.s_addr =  0x0100007F; // 127.0.0.1 in formato gia in esadecimale
	uint16_t port = atoi(av[1]);
	servaddr.sin_port = ((port & 0xFF) << 8) | ((port >> 8) & 0xFF);
	//servaddr.sin_port = htons(atoi(av[1]));       // Porta da argomento in network order

    // ---- BIND E LISTEN ----
    if ((bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr))) < 0)
        ftError(NULL);  // Errore bind -> "Fatal error"
    if (listen(sockfd, 10) < 0)
        ftError(NULL);  // Errore listen -> "Fatal error"

    // ============ MAIN EVENT LOOP - Gestione non-blocking di tutti i client ============
    while(1) 
	{
        // ---- PREPARAZIONE SELECT ----
        readfds = writefds = active;  // Copia set attivo per select (select modifica i set)
        
        // ---- SELECT: ASPETTA ATTIVITÀ SU QUALSIASI FD ----
        if (select(fdMax + 1, &readfds, &writefds, NULL, NULL) < 0)
            continue;  // Ignora errori di select e riprova
        
        // ---- CONTROLLA OGNI FILE DESCRIPTOR PER ATTIVITÀ ----
        for(int fdI = 0; fdI <= fdMax; fdI++) //cicla tutti gli fd
		{
            // ---- CASO 1: NUOVO CLIENT (server socket pronto per accept) ----
            if (FD_ISSET(fdI, &readfds) && fdI == sockfd)  //se server socket pronto per attività di lettura, nuovo client in arrivo
			{
                // Accept non bloccante (select ha già verificato che è pronto)
                len = sizeof(servaddr); //INIZIALIZZA LEN
                int connfd = accept(sockfd, (struct sockaddr *)&servaddr, &len);
                if (connfd < 0)
                    continue;  // Errore accept, ignora e vai avanti
                
                // Aggiorna massimo FD per ottimizzare prossime select
                fdMax = connfd > fdMax ? connfd : fdMax;
                
                // aggiunto - Inizializza client con buffer dinamico
                initClient(connfd);
                
                // Aggiungi nuovo client al set di FD attivi
                FD_SET(connfd, &active);
                
                // Prepara messaggio di arrivo per tutti gli altri client
                sprintf(bufferWrite, "server: client %d just arrived\n", clients[connfd].id);
                sendAll(connfd);  // Invia a tutti TRANNE connfd
                break;  // Esci dal loop FD e torna a select
            }
            // ---- CASO 2: MESSAGGIO DA CLIENT ESISTENTE ----
            if (FD_ISSET(fdI, &readfds) && fdI != sockfd) //l'fd pronto non è sockfd
			{
                // Leggi dati dal client (non bloccante grazie a select)
                int res = recv(fdI, bufferRead, 65536, 0); //65536 64 kb in lettura, 0 comportamento "normale" select gia carantisce nonblocking
                
                if (res <= 0) 
				{
                    // ---- CLIENT DISCONNESSO O ERRORE ----
                    // Prepara messaggio di partenza per tutti gli altri
                    sprintf(bufferWrite, "server: client %d just left\n", clients[fdI].id);
                    sendAll(fdI);  // Invia a tutti TRANNE quello che è partito
                    
                    // aggiunto - Libera memoria dinamica del client
                    freeClient(fdI);
                    
                    // Rimuovi client dal sistema
                    FD_CLR(fdI, &active);  // Rimuovi da fd attivi
                    close(fdI);            // Chiudi connessione
                    break;  // Esci dal loop FD e torna a select
                }
                else 
				{
                    // ---- PROCESSA MESSAGGIO RICEVUTO ----
                    // Gestione corretta messaggi multi-linea con buffer dinamico
                    for (int i = 0; i < res; i++) //res = caratteri ricevuti
					{
                        if (bufferRead[i] == '\n')
						{
                            // Fine di una linea - invia se non vuota
                            if (clients[fdI].msg_len > 0)
							{
                                sprintf(bufferWrite, "client %d: %s\n", clients[fdI].id, clients[fdI].msg);
                                sendAll(fdI);
                            }
                            // aggiunto - Reset buffer per prossima linea
                            clients[fdI].msg[0] = '\0';
                            clients[fdI].msg_len = 0;
                        } 
						else
						{
                            // aggiunto - Ridimensiona buffer se necessario
                            if (resizeClientBuffer(fdI, clients[fdI].msg_len + 2) < 0)
                                ftError(NULL);  // Errore realloc -> "Fatal error"
                            
                            // Accumula carattere nel buffer
                            clients[fdI].msg[clients[fdI].msg_len] = bufferRead[i];
                            clients[fdI].msg_len++;
                            clients[fdI].msg[clients[fdI].msg_len] = '\0';
                        }
                    }
                    break;  // Esci dal loop FD e torna a select
                }
            }
        }  // Fine loop controllo FD
    }  // Fine while(1) - server gira per sempre
}  // Fine main

/*
============ RIEPILOGO FUNZIONAMENTO ============

1. SERVER STARTUP:
   - Crea socket TCP
   - Bind su 127.0.0.1:porta
   - Listen per connessioni
   - Inizializza strutture select()

2. MAIN LOOP (Event-driven):
   - select() aspetta attività su qualsiasi FD
   - Se server socket pronto → nuovo client
   - Se client socket pronto → messaggio o disconnessione

3. NUOVO CLIENT:
   - accept() non bloccante
   - Assegna ID sequenziale (0,1,2,...)
   - Notifica tutti: "server: client X just arrived"

4. MESSAGGIO CLIENT:
   - recv() caratteri
   - Accumula in buffer fino a '\n'
   - Broadcast: "client X: messaggio"

5. DISCONNESSIONE CLIENT:
   - recv() <= 0
   - Cleanup FD e strutture
   - Notifica tutti: "server: client X just left"

CARATTERISTICHE:
✅ Non-blocking: select() gestisce tutti simultaneamente
✅ Scalabile: centinaia di client con un thread
✅ Robusto: gestisce disconnessioni improvvise
✅ Conforme: rispetta tutti i requisiti del subject
*/