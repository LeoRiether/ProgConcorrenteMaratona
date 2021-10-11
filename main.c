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

// gera um número aleatório em [low, high]
static inline int rand_int(int low, int high) {
	return rand() % (high - low + 1) + low;
}

static inline long long pair(int x, int y) {
	return ((long long)x << 32ll) | y;
}

typedef enum tipo_evento_t {
	Alarme, Placar
} tipo_evento_t;

typedef struct evento_t {
	tipo_evento_t tipo;
	int payload;
} evento_t;

typedef enum competidor_st {
	Pensando, AchouSolucao, Escrevendo, OlhandoPlacar,
} competidor_st;

competidor_st state[N]; // estado dos competidores
cond_t sinal[N];
deque_t eventos[N]; // eventos[i] = fila de eventos do i-ésimo competidor
					// para acessar eventos[i], deve-se estar de posse do lock sinal[i].lock
pthread_barrier_t comeco_da_prova;
pthread_mutex_t computador;

void* competidor(void*);

int main(int argv, char* argc[]) {

	srand(time(NULL));

	// Inicializa as variáveis
	pthread_t competidores[N];
	pthread_barrier_init(&comeco_da_prova, 0, N);
	pthread_mutex_init(&computador, 0);
	for (int i = 0; i < N; i++) {
		c_init(&sinal[i]);
		d_init(&eventos[i]);
		state[i] = Pensando;
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

	// Destrói as variáveis
	pthread_barrier_destroy(&comeco_da_prova);
	pthread_mutex_destroy(&computador);
	for (int i = 0; i < N; i++) {
		c_destroy(&sinal[i]);
		d_destroy(&eventos[i]);
	}

	return 0;
}

int acordar_em(int delay, int id) {
	// Colocamos um alarme para daqui a `delay` segundos.
	// Passado esse tempo, será dado um signal em `sinal[id]` e
	// será colocado o elemento `evt` no deque `eventos[i]`.
	evento_t* evt = (evento_t*)malloc(sizeof(evento_t));
	*evt = (evento_t) {
		.tipo = Alarme,
		.payload = 0,
	};
	int id_alarme = alarme(delay, &sinal[id], &eventos[id], evt);
	return id_alarme;
}

void achou_solucao(int id);
void olhar_placar(int id);

// Implementação do comportamento de um competidor
void* competidor(void* arg) {
	int id = *(int*)arg;
	printf("Competidor %d entrou na sala\n", id);

	// Os competidores esperam até que todos estejam prontos para começar a prova
	// ao mesmo tempo
	pthread_barrier_wait(&comeco_da_prova);

	for (;;) {
		acao_t prox_acao = rand() % 2;

		printf("%d está pensando em um problema\n", id);
		sleep(rand_int(4, 7));

		switch (prox_acao) {
			case AchouSolucao:
				printf("%d encontrou a solução para um problema!\n", id);
				achou_solucao(id);
				break;
			case OlharPlacar:
				printf("%d quer olhar o placar\n", id);
				olhar_placar(id);
				break;
			default:
				ensure(false, "prox_acao inválida");
		}
	}

	printf("Competidor %d saiu da sala\n", id);

	free(arg);
	pthread_exit(0);
}

// O competidor `id` encontrou a solução de um problema, e agora quer ficar de posse do computador
// para escrever o código.
void achou_solucao(int id) {
	if (pthread_mutex_trylock(&computador) == 0) {
		printf("%d está de posse do computador, escrevendo uma solução\n", id);
		return;
	}

	int id_alarme = acordar_em(2, id);

	printf("%d está resolvendo uma questão...\n", id);

	m_lock(&sinal[id].lock);
		for (;;) {
			// Esperamos um sinal
			pthread_cond_wait(&sinal[id].cond, &sinal[id].lock);

			// Pegamos o primeiro evento do deque
			void* evt_pointer = d_pop_front(&eventos[id]);
			evento_t evt = *(evento_t*)evt_pointer;
			free(evt_pointer);

			// E tratamos o evento de acordo
			if (evt.tipo == Alarme) {
				// O competidor acabou de escrever o código da questão
				// submit();
				// libera_pc()
				break;
			} else if (evt.tipo == Placar) {

			}
		}

		// TODO: tomar cuidado com outros eventos que podem ter aparecido e ainda estão no deque

		printf("%d terminou de escrever sua solução\n", id);
	m_unlock(&sinal[id].lock);
}

void olhar_placar(int id) {
	printf("olhar_placar ainda não foi implementado\n");
}
