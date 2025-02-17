PROGETTO DI LABORATORI II

### descrizione
sviluppato da Tae Hwan Kim, t.kim1@studenti.unipi.it
il progetto e' sviluppato su un ambiente unix


### struttura del file 
progetto_lab_2_kim/
├── src/
│   ├── server/
│   │   ├── server_main.c           
│   │   ├── server_paroliere.c      
│   │   ├── dictionary.c           
│   │   └── matrix.c              
│   └── client/
│       ├── client_main.c          
│       └── client_paroliere.c      
├── resources/
│   └── dictionary.txt             
├── Makefile                        
└── README.md                      


### istruzioni di compliazione
per compilare sia il server che il client, nello specifico:
il comando 
- **`make`**:
    compila e genera due eseguibili:
    1. **`paroliere_srv`** -> eseguibile del server
    2. **`paroliere_cl`** -> eseguibile del client

- **`make clean`**:
    rimuove tutti i file oggetto (`.o`) e gli eseguibili (`paroliere_srv` e `paroliere_cl`),
    cosi da permettere di ripartire con una compilazione "pulita"

se si vuole compilare due separatamente, per server:
    - make paroliere_srv
analogamente per client:
    - make paroliere_cl

### considerazioni importanti
- assicurarsi di aver installato un compilatore C(gcc) e librerie utilizzate nel progetto
- i file di testo (dictionary.txt, matrix.txt(opzionale)) devono trovarsi nella cartella `resources/` 
  oppure in un percorso specificato a riga di comando

### esempi di esecuzione
in questa sezione, vengono mostrati degli esempi concreti di come avviare client/server.

### avvio di server
per il corretto avvio del server e' necessario i seguenti argomenti:
./paroliere_srv <nome_server> <porta>

a cui si possono aggiungere parametri opzionali
- `--matrici <file>`: specifica un file con matrici predefinite (in `resources/matrix.txt`)
- `--durata <minuti>`: imposta la durata di ciascuna partita (default: 3 minuti)
- `--seed <rnd_sedd>`: imposta un seed personalizzato per la generazione pseudocausale della matrice
- `--diz <file>`: specifica il file di dizionario (default : `resources/dictionary.txt`)
- `--disconnetti-dopo <minuti>`: imposta il tempo di inattivita' dopo il quale un client viene disconnesso (default: 3 minuti)

un esempio completo potrebbe essere:
./paroliere_srv MyServerName 9000 --matrici resources/matrix.txt --durata 5 --seed 12345 --diz resources/dictionary.txt --disconnetti-dopo 2

il server rispondera' con:
[SERVER] In ascolto sulla porta 9000

e all'arrivo di un client, inviera' un messaggio di benvenuto con il nome del server

### avvio di client
per connettersi al server avviato, lanciare:
./paroliere_cl MyserverName 9000

dove:
- `MyServerName` è lo stesso nome passato al server (o un alias nel file `/etc/hosts` che risolva all’indirizzo IP della macchina server).  
- `9000` è la porta corrispondente.

Su localhost, è possibile usare `127.0.0.1` o `localhost` al posto di `MyServerName`, se il server gira sulla stessa macchina:
./paroliere_cl localhost 9000


Appena connesso, il client riceverà un eventuale messaggio di OK di benvenuto, e mostrerà il prompt:
[PROMPT PAROLIERE]-->

dopo questo, si possono digitare i comandi 

### comandi client
in questa sezione, si specificano i comandi supportati dal client

una volta avviato il client, apparira' il prompt:
[PROMPT PAROLIERE]-->

Da qui si possono digitare i seguenti comandi:

1. **`aiuto`**  
   Mostra l’elenco dei comandi disponibili.

2. **`registra utente <nome>`**  
   Registra un nuovo utente sul server, inviando un messaggio di tipo `MSG_REGISTRA_UTENTE`.
   - Se il nome è già in uso, il server risponde con `MSG_ERR`.
   - Se la registrazione va a buon fine, il server risponde con `MSG_OK`.

3. **`login utente <nome>`**  
   Effettua il login con un nome utente già registrato in precedenza (e non cancellato).

4. **`cancella_registrazione`**  
   Invia il messaggio `MSG_CANCELLA_UTENTE` al server, cancellando l’utente corrente dal server (lo username diventa vuoto).

5. **`matrice`**  
   Richiede la matrice di gioco corrente al server (messaggio `MSG_MATRICE`).
   - Se la partita è in corso, il server risponde con le 16 lettere che compongono la matrice.
   - Se la partita non è in corso, restituisce un messaggio con il tempo mancante all’inizio di una nuova partita (nel codice esempio, può mandare `MSG_TEMPO_ATTESA`).

6. **`p <parola>`**  
   Invia una parola al server per la verifica e il calcolo del punteggio (`MSG_PAROLA`).
   - Se la parola è valida (presente nel dizionario, formabile dalla matrice) e non è già stata usata dallo stesso utente in questa partita, il server risponde con `MSG_PUNTI_PAROLA` e il punteggio relativo.
   - Se la parola era già stata proposta, il punteggio è 0.
   - Se la parola non è valida, il server risponde con `MSG_ERR`.

7. **`fine`**  
   Termina la sessione client, chiudendo la connessione al server. Il client esce.

#### Esempio di Interazione
Di seguito un esempio di sequenza di comandi e risposte:

[PROMPT PAROLIERE]--> aiuto Comandi disponibili: aiuto registra utente <nome> login utente <nome> cancella_registrazione matrice p <parola> fine

[PROMPT PAROLIERE]--> registra utente Mario [SERVER] OK: <-- Il server conferma la registrazione di Mario

[PROMPT PAROLIERE]--> matrice [SERVER] MATRICE: A D Qu C ... <-- Il server restituisce le 16 celle se la partita è in corso

[PROMPT PAROLIERE]--> p cadi [SERVER] PUNTI PAROLA: 4

[PROMPT PAROLIERE]--> fine Client terminato.

