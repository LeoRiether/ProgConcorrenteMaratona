#ifndef ALARME_MODULE
#define ALARME_MODULE

#include <unistd.h>
#include <time.h>
#include <stdlib.h>
#include <pthread.h>

#include "cond.h"
#include "deque.h"

#define MAX_TIMERS 64
int next_timer_id;
bool alarme_cancelado[MAX_TIMERS];
pthread_mutex_t next_timer_lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct alarme_arg {
	int delay;
	int id;
	cond_t* c;
	deque_t* d;
	void* d_element;
} alarme_arg;

void* alarme_thread(void* arg) {
	alarme_arg* a = (alarme_arg*)arg;

	sleep(a->delay);

	m_lock(&a->c->lock);
	if (!alarme_cancelado[a->id]) {
		// Coloca o elemento no deque
		if (a->d) d_push_back(a->d, a->d_element);

		pthread_cond_signal(&a->c->cond);
	}
	m_unlock(&a->c->lock);

	free(arg);
	pthread_exit(0);
}

// Cria uma thread que dorme por `delay` segundos, e logo depois dá um signal
// na variável de condição dada.
// Além disso, se for dado um deque_t* d não nulo, antes do signal o elemento `d_element`
// será colocado no deque.
// O alarme pode ser cancelado ao setar `alarme_cancelado[id] = true`, tomando cuidado para estar
// de posse do lock c->lock, de modo que não haja condição de corrida.
// A função retorna o `id` do alarme gerado.
int alarme(int delay, cond_t* c, deque_t* d, void* d_element) {

	// Cria os argumentos da thread de alarme
	alarme_arg* arg = (alarme_arg*)malloc(sizeof(alarme_arg));
	*arg = (alarme_arg) {
		.delay = delay,
		.c = c,
		.d = d,
		.d_element = d_element
	};

	m_lock(&next_timer_lock);
		int id = next_timer_id++;
		arg->id = id;
		if (next_timer_id >= MAX_TIMERS) // os ids sempre estão em [0..MAX_TIMERS)
			next_timer_id -= MAX_TIMERS;
		alarme_cancelado[id] = false;
	m_unlock(&next_timer_lock);

	// Cria a thread
	pthread_t handle;
	pthread_create(&handle, NULL, alarme_thread, (void*)arg);

	return id;
}

#endif
