#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

// Quantos times participam da competição
#define TIMES 1

// Quantos competidores existem por time
#define SZ_TIME 3

#define N (TIMES * SZ_TIME)

void ensure(bool cond, const char* msg_erro) {
	if (!cond) {
		fprintf(stderr, "%s\n", msg_erro);
		exit(1);
	}
}

typedef struct cond_t {
	pthread_mutex_t lock;
	pthread_cond_t cond;
} cond_t;

// Inicializa uma variável cond_t
void c_init(cond_t* c) {
	ensure(pthread_mutex_init(&c->lock, 0) == 0, "Erro ao inicializar um lock");
	ensure(pthread_cond_init(&c->cond, 0) == 0, "Erro ao inicializar uma convar");
}

// Destrói uma variável cond_t
void c_destroy(cond_t* c) {
	pthread_mutex_destroy(&c->lock);
	pthread_cond_destroy(&c->cond);
}

// "pthread_mutex_lock" é um nome muito longo...
#define m_lock pthread_mutex_lock
#define m_unlock pthread_mutex_unlock

typedef enum tipo_evento_t {
	Acordar, Scoreboard
} tipo_evento_t;

typedef struct evento_t {
	tipo_evento_t tipo;
	void* payload;
} evento_t;

typedef struct fila_t {
	void* head;
	struct fila_t* next;
} fila_t;

//
// Timers
//
#define MAX_TIMERS 64
int next_timer_id;
bool cancelado[MAX_TIMERS];
pthread_mutex_t next_timer_lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct alarme_arg {
	int delay;
	int id;
	cond_t* c;
} alarme_arg;

void* alarme_thread(void* arg) {
	alarme_arg* a = (alarme_arg*)arg;

	sleep(a->delay);

	m_lock(&a->c->lock);
	if (!cancelado[a->id])
		pthread_cond_broadcast(&a->c->cond);
	m_unlock(&a->c->lock);

	free(arg);
	pthread_exit(0);
}

// Cria uma thread que dorme por `delay` segundos, e logo depois dá um broadcast
// na variável do condição dada. O alarme pode ser cancelado 
int alarme(int delay, cond_t* c) {

	// Cria os argumentos da thread de alarme
	alarme_arg* arg = malloc(sizeof(alarme_arg));
	arg->delay = delay;
	arg->c = c;

	m_lock(&next_timer_lock);
	int id = next_timer_id++;
	arg->id = id;
	if (next_timer_id >= MAX_TIMERS) // os ids sempre estão em [0..MAX_TIMERS)
		next_timer_id -= MAX_TIMERS;
	m_unlock(&next_timer_lock);

	// Cria a thread
	pthread_t handle;
	pthread_create(&handle, NULL, alarme_thread, (void*)arg);

	return id;
}

//
//
//
cond_t sinal[N];
evento_t evento[N]; //! devia ser uma fila ou stack de eventos
pthread_barrier_t comeco_da_prova;

void* competidor(void*);

int main(int argv, char* argc[]) {

	pthread_barrier_init(&comeco_da_prova, 0, N);

	pthread_t competidores[N];
	int ids[N];
	for (int tim = 0; tim < TIMES; tim++) {
		for (int c = 0; c < SZ_TIME; c++) {
			int id = tim * SZ_TIME + c;
			ids[id] = id;
			c_init(&sinal[id]);
			pthread_create(&competidores[id], NULL, &competidor, (void*)&ids[id]);
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
	pthread_barrier_wait(&comeco_da_prova);

	m_lock(&sinal[id].lock);
	alarme(2, &sinal[id]);
	printf("%d esperando o alarme...\n", id);
	pthread_cond_wait(&sinal[id].cond, &sinal[id].lock);
	printf("%d recebeu o alarme!\n", id);
	m_unlock(&sinal[id].lock);

	printf("Competidor %d saiu da sala\n", id);
	pthread_exit(0);
}
