#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

// ! Descomentar essa linha para ter output colorido!
// Fica mais fácil de identificar os tipos de eventos
// #define COLOR

#include "color.h"
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


// Para propósitos de debugging (aka deixar rodando por bastante tempo e ver se deu deadlock)
void print_time() {
	time_t timer;
	struct tm* tm_info;
	timer = time(NULL);
	tm_info = localtime(&timer);
	char buffer[9];
	strftime(buffer, 9, "%H:%M:%S", tm_info);
	puts(buffer);
}

const char* evento2str[] = { "Alarme", "PermissaoComputador" };
typedef enum tipo_evento_t {
	Alarme, PermissaoComputador
} tipo_evento_t;

typedef struct evento_t {
	tipo_evento_t tipo;
	int payload;
} evento_t;

int team[N]; // team[i] = id do time do i-ésimo competidor

cond_t sinal[N];
deque_t eventos[N]; // eventos[i] = fila de eventos do i-ésimo competidor
					// para acessar eventos[i], deve-se estar de posse do lock sinal[i].lock
int alarme_id[N];

pthread_mutex_t quer_computador[TIMES]; // lock para que apenas uma pessoa tente entrar no computador por vez
pthread_mutex_t computador[TIMES]; // computador[i] = lock de acesso ao computador do i-ésimo time
deque_t esperando_pc[TIMES]; // fila de pessoas de um time esperando para usar o PC.
							 // para acessar o deque, deve-se estar de posse do lock quer_computador[time]

pthread_barrier_t comeco_da_prova;

void* competidor(void*);

void init() {
	pthread_barrier_init(&comeco_da_prova, 0, N);
	for (int i = 0; i < TIMES; i++) {
		pthread_mutex_init(&computador[i], 0);
		pthread_mutex_init(&quer_computador[i], 0);
		d_init(&esperando_pc[i]);
	}
	for (int i = 0; i < N; i++) {
		c_init(&sinal[i]);
		d_init(&eventos[i]);
	}
}

void destroy() {
	pthread_barrier_destroy(&comeco_da_prova);
	for (int i = 0; i < TIMES; i++) {
		pthread_mutex_destroy(&computador[i]);
		pthread_mutex_destroy(&quer_computador[i]);
		d_destroy(&esperando_pc[i]);
	}
	for (int i = 0; i < N; i++) {
		c_destroy(&sinal[i]);
		d_destroy(&eventos[i]);
	}
}

int main(int argv, char* argc[]) {

	srand(time(NULL));

	init();

	pthread_t competidores[N];
	for (int t = 0; t < TIMES; t++) {
		for (int c = 0; c < SZ_TIME; c++) {
			int id = t * SZ_TIME + c;

			team[id] = t;

			int *arg = malloc(sizeof(int));
			*arg = id;
			pthread_create(&competidores[id], NULL, &competidor, (void *)arg);
		}
	}

	for (int i = 0; i < N; i++) {
		pthread_join(competidores[i], NULL);
	}

	destroy();

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

void competidor_init(int id);
void achou_solucao(int id);
void escreve_codigo(int id);
void olhar_placar(int id);

// Implementação do comportamento de um competidor
void* competidor(void* arg) {
	int id = *(int*)arg;
	free(arg);
	printf("Competidor %d entrou na sala\n", id);

	// Os competidores esperam até que todos estejam prontos para começar a prova
	// ao mesmo tempo
	pthread_barrier_wait(&comeco_da_prova);

	competidor_init(id);

	for (;;) {
		m_lock(&sinal[id].lock);
			// Esperamos um sinal
			while (eventos[id].len == 0)
				pthread_cond_wait(&sinal[id].cond, &sinal[id].lock);

			// E pegamos o primeiro evento do deque
			void* evt_pointer = d_pop_front(&eventos[id]);
			evento_t evt = *(evento_t*)evt_pointer; // copia (!) o evento
			free(evt_pointer);
		m_unlock(&sinal[id].lock);

		printf("[time %d] O competidor %d recebeu um evento %s (payload=%d)\n",
				team[id], id, evento2str[evt.tipo], evt.payload);

		print_time();

		if (evt.tipo == Alarme && rand_int(0, 9) <= 2)
			olhar_placar(id);
		else if (evt.tipo == Alarme)
			achou_solucao(id);
		else if (evt.tipo == PermissaoComputador) {
			// cancela o alarme, porque o competidor vai parar de pensar em outro problema
			// para escrever código
			m_lock(&sinal[id].lock);
			alarme_cancelado[alarme_id[id]] = true;
			m_unlock(&sinal[id].lock);
			escreve_codigo(id);
		}

		// Volta a pensar em um problema
		alarme_id[id] = acordar_em(rand_int(4, 11), id);
	}

	printf("Competidor %d saiu da sala\n", id);

	pthread_exit(0);
}

void competidor_init(int id) {
	// Competidor começa a pensar em uma questão
	alarme_id[id] = acordar_em(rand_int(4, 11), id);
}

// O competidor *já tem permissão exclusiva de uso do computador* e vai escrever
// o código de solução
void escreve_codigo(int id) {
	int t = team[id];

	green(); printf("[time %d] %d está de posse do computador e escrevendo código\n", t, id); reset();

	sleep(rand_int(8, 14));

	cyan(); printf("[time %d] %d acabou de escrever a solução e vai submetê-la no juíz\n", t, id); reset();

	// TODO: fila de submissões

	// Libera o computador do time `t`
	m_lock(&quer_computador[t]);
		if (esperando_pc[t].len == 0) {
			// Ninguém mais quer usar o PC
			m_unlock(&computador[t]);
			printf("[time %d] o computador foi liberado\n", t);
		} else {
			// Uma pessoa da fila `esperando_pc[t]` quer escrever uma solução
			void* ptr = d_pop_front(&esperando_pc[t]);
			int outro = *(int*)ptr;
			free(ptr);

			if (outro == id) {
				green(); printf("[time %d] %d vai escrever outra solução e já está no computador\n", t, id); reset();
				m_unlock(&quer_computador[t]);
				escreve_codigo(id);
				return;
			}

			yellow(); printf("[time %d] avisando a %d que ele tem permissão para usar o PC\n", t, outro); reset();

			m_lock(&sinal[outro].lock);
				// Coloca um evento do tipo "PermissaoComputador" na fila de eventos de `outro`
				evento_t* evt = (evento_t*)malloc(sizeof(evento_t));
				*evt = (evento_t) {
					.tipo = PermissaoComputador,
					.payload = 0,
				};
				d_push_back(&eventos[outro], (void*)evt);

				// E avisa que há um evento
				pthread_cond_signal(&sinal[outro].cond);
			m_unlock(&sinal[outro].lock);
		}
	m_unlock(&quer_computador[t]);
}

// O competidor `id` achou a solução de um problema, e agora vai tentar
// ficar de posse do computador, para escrever a solução
void achou_solucao(int id) {
	int t = team[id];
	m_lock(&quer_computador[t]);
	if (m_trylock(&computador[t]) == 0) {
		// Conseguimos pegar o computador
		m_unlock(&quer_computador[t]);
		escreve_codigo(id);
	} else {
		// Outro membro do time já está usando o computador.
		// Temos que colocar nosso nome na fila de pessoas que querem utilizar o PC
		red(); printf("[time %d] %d não conseguiu acesso ao PC, mas colocou o nome na fila\n", t, id); reset();
		int* me = (int*)malloc(sizeof(int));
		*me = id;
		d_push_back(&esperando_pc[t], (void*)me);
		m_unlock(&quer_computador[t]);
	}
}

void olhar_placar(int id) {
	printf("Competidor %d resolveu ir olhar o placar, mas acaba de lembrar que essa feature ainda não foi implementada!\n", id);
}
