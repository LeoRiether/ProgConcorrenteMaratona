#include <pthread.h>
#include <semaphore.h>
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

const char* evento2str[] = { "Alarme", "Placar" };
typedef enum tipo_evento_t {
	Alarme, Placar
} tipo_evento_t;

typedef struct evento_t {
	tipo_evento_t tipo;
	int payload;
} evento_t;

typedef enum competidor_st {
	Pensando, Escrevendo, OlhandoPlacar,
} competidor_st;

int team[N]; // team[i] = id do time do i-ésimo competidor
competidor_st state[N]; // estado dos competidores
sem_t permissao_placar[N]; // semáforo que indica se o competidor pode usar o computador para olhar o placar
sem_t volta_para_pc[N]; // indica se o competidor pode *voltar* a usar o computador, caso ele tenha dado
					    // permissão para outro olhar o placar

cond_t sinal[N];
deque_t eventos[N]; // eventos[i] = fila de eventos do i-ésimo competidor
					// para acessar eventos[i], deve-se estar de posse do lock sinal[i].lock
int alarme_id[N];

pthread_mutex_t computador[TIMES]; // computador[i] = lock de acesso ao computador do i-ésimo time
pthread_mutex_t quer_computador[TIMES]; // lock para que apenas uma pessoa tente entrar no computador por vez
int no_computador[TIMES]; // qual é o id do competidor que está de posse do computador?

pthread_barrier_t comeco_da_prova;

void* competidor(void*);

int main(int argv, char* argc[]) {

	srand(time(NULL));

	// Inicializa as variáveis
	pthread_t competidores[N];
	pthread_barrier_init(&comeco_da_prova, 0, N);
	for (int i = 0; i < TIMES; i++) {
		pthread_mutex_init(&computador[i], 0);
		pthread_mutex_init(&quer_computador[i], 0);
	}
	for (int i = 0; i < N; i++) {
		c_init(&sinal[i]);
		d_init(&eventos[i]);
		sem_init(&permissao_placar[i], 0, 0);
		sem_init(&volta_para_pc[i], 0, 0);
		state[i] = Pensando;
	}

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

	// Destrói as variáveis
	pthread_barrier_destroy(&comeco_da_prova);
	for (int i = 0; i < TIMES; i++) {
		pthread_mutex_destroy(&computador[i]);
		pthread_mutex_destroy(&quer_computador[i]);
	}
	for (int i = 0; i < N; i++) {
		c_destroy(&sinal[i]);
		d_destroy(&eventos[i]);
		sem_destroy(&permissao_placar[i]);
		sem_destroy(&volta_para_pc[i]);
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

void competidor_init(int id);
competidor_st pensando_handler(int id, evento_t*);
competidor_st escrevendo_handler(int id, evento_t*);
competidor_st olhando_placar_handler(int id, evento_t*);

// Implementação do comportamento de um competidor
void* competidor(void* arg) {
	int id = *(int*)arg;
	printf("Competidor %d entrou na sala\n", id);

	// Os competidores esperam até que todos estejam prontos para começar a prova
	// ao mesmo tempo
	pthread_barrier_wait(&comeco_da_prova);

	competidor_init(id);

	m_lock(&sinal[id].lock);
		for (;;) {
			// Esperamos um sinal
			while (eventos[id].len == 0)
				pthread_cond_wait(&sinal[id].cond, &sinal[id].lock);

			// Pegamos o primeiro evento do deque
			void* evt_pointer = d_pop_front(&eventos[id]);
			evento_t evt = *(evento_t*)evt_pointer;
			free(evt_pointer);

			printf("[time %d] O competidor %d recebeu um evento %s (payload=%d)\n",
					team[id], id, evento2str[evt.tipo], evt.payload);

			// E tratamos o evento de acordo com o estado atual do competidor,
			// atualizando o estado de acordo com o valor de retorno do handler
			competidor_st st = state[id];
			if (st == Pensando)
				state[id] = pensando_handler(id, &evt);
			else if (st == Escrevendo)
				state[id] = escrevendo_handler(id, &evt);
			else if (st == OlhandoPlacar)
				state[id] = olhando_placar_handler(id, &evt);
			else
				ensure(false, "Estado inexistente");
		}

		// TODO: tomar cuidado com outros eventos que podem ter aparecido e ainda estão no deque

	m_unlock(&sinal[id].lock);

	printf("Competidor %d saiu da sala\n", id);

	free(arg);
	pthread_exit(0);
}

void competidor_init(int id) {
	// Competidor começa a pensar em uma questão
	// TODO: ele pode querer olhar o placar tbm
	alarme_id[id] = acordar_em(rand_int(4, 7), id);
}

competidor_st pensando_handler(int id, evento_t* evt) {
	competidor_st new_st = Pensando;

	if (evt->tipo == Alarme) {
		// Achou a solução de um problema!
		m_lock(&quer_computador[team[id]]);
		if (m_trylock(&computador[team[id]]) == 0) {
			// Conseguimos pegar o computador
			printf("[time %d] %d está usando o computador\n", team[id], id);
			no_computador[team[id]] = id; // a pessoa `id` está no computador do time `team[id]`

			// daqui a `rand` segundos ela acaba de escrever a solução
			alarme_id[id] = acordar_em(rand_int(10, 30), id);

			new_st = Escrevendo;
		} else {
			// O competidor `no_computador[team[id]]` já está de posse do computador
			// TODO:
			printf("[time %d] competidor %d não conseguiu pegar o PC, too bad\n",
					team[id], id);
			alarme_id[id] = acordar_em(rand_int(4, 7), id);
		}
		m_unlock(&quer_computador[team[id]]);
	}

	return new_st;
}

competidor_st escrevendo_handler(int id, evento_t* evt) {
	competidor_st new_st = Escrevendo;

	if (evt->tipo == Alarme) {
		// Terminou de escrever a solução

		// Libera o computador do time e avisa aos que tem solução pronta
		// TODO: avisar aos membros do time que tem solução pronta
		m_lock(&quer_computador[team[id]]);
		m_unlock(&computador[team[id]]);
		m_unlock(&quer_computador[team[id]]);

		new_st = Pensando;
	} else if (evt->tipo == Placar) {
		// Cancelamos o evento de "terminar de escrever o código", por enquanto
		alarme_cancelado[alarme_id[id]] = true;

		// id do competidor que quer olhar o placar
		int outro = evt->payload;

		sem_post(&permissao_placar[outro]);
		printf("[team %d] %d esperando para voltar a escrever o código...\n", team[id], id);
		m_unlock(&sinal[id].lock); // não é necessário ficar com esse lock
		sem_wait(&volta_para_pc[id]);
		m_lock(&sinal[id].lock);

		// Cria outro alarme para indicar quando vamos terminar de escrever a solução.
		//
		// obs: o tempo é escolhido aleatoriamente, o que significa que o competidor pode demorar
		// mais tempo para escrever o código, caso vários membros do time fiquem querendo olhar
		// o placar (talvez ele até mesmo nunca termine o código...).
		// Isso é feito para fins de simplificação do código, mas nós poderíamos ter uma variável
		// do tipo `time_t` para cada competidor, dizendo quando ele começou a escrever o código.
		alarme_id[id] = acordar_em(rand_int(10, 30), id);

		new_st = Escrevendo; // continuamos a escrever a solução
	}

	return new_st;
}

competidor_st olhando_placar_handler(int id, evento_t* evt) {

	return OlhandoPlacar;
}
