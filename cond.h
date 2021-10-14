#ifndef COND_MODULE
#define COND_MODULE

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

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
#define m_trylock pthread_mutex_trylock
#define m_unlock pthread_mutex_unlock

#endif
