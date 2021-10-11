#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "cond.h"
#include "deque.h"
#include "alarme.h"

// Quantos times participam da competição
#define TIMES 1

// Quantos competidores existem por time
#define SZ_TIME 3

#define N (TIMES * SZ_TIME)

typedef enum tipo_evento_t {
	Acordar, Scoreboard
} tipo_evento_t;

typedef struct evento_t {
	tipo_evento_t tipo;
	void* payload;
} evento_t;

cond_t sinal[N];
evento_t evento[N]; //! devia ser uma fila ou stack de eventos
pthread_barrier_t comeco_da_prova;

void* competidor(void*);

int main(int argv, char* argc[]) {

	pthread_barrier_init(&comeco_da_prova, 0, N);

	pthread_t competidores[N];
	for (int i = 0; i < N; i++) {
		c_init(&sinal[i]);
	}

	for (int team = 0; team < TIMES; team++) {
		for (int c = 0; c < SZ_TIME; c++) {
			int id = team * SZ_TIME + c;

			int *arg = malloc(sizeof(int));
			*arg = id;
			pthread_create(&competidores[id], NULL, &competidor, (void *)arg);
		}
	}

	for (int i = 0; i < N; i++) {
		pthread_join(competidores[i], NULL);
	}

	return 0;
}

void* competidor(void* arg) {
	int id = *(int*)arg;
	printf("Competidor %d entrou na sala\n", id);

	// Os competidores esperam até que todos estejam prontos para começar a prova
	// ao mesmo tempo
	pthread_barrier_wait(&comeco_da_prova);

	m_lock(&sinal[id].lock);
	alarme(2, &sinal[id]);
	printf("%d esperando o alarme...\n", id);
	pthread_cond_wait(&sinal[id].cond, &sinal[id].lock);
	printf("%d recebeu o alarme!\n", id);
	m_unlock(&sinal[id].lock);

	printf("Competidor %d saiu da sala\n", id);

	free(arg);
	pthread_exit(0);
}
