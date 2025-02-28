#ifndef CLIENT_H
#define CLIENT_H

#define _GNU_SOURCE // soluzione per errore implicit declaration of signal.h

#include "common/common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <ctype.h>
#include <errno.h>

void client_run(int sockfd);

#endif // CLIENT_H
