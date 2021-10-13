#ifndef DEQUE_MODULE
#define DEQUE_MODULE

// Esse módulo não é muito relevante com relação ao objetivo do trabalho em geral.
// Aqui apenas defino uma estrutura de dados deque, que é utilizada em outros módulos.
// O deque não possui nenhum tipo de mecanismo de exclusão mútua, e portanto deve-se tomar
// cuidado ao utilizá-lo em mais de uma thread.

#include <stdio.h>
#include <stdlib.h>

// Double ended queue
// Implementada como um buffer circular, só porque não gosto de listas encadeadas.
typedef struct deque_t {
	void** buffer; // buffer é um array de void*
	int cap; // capacidade/tamanho do buffer
	int len;
	int front, back;
} deque_t;

void d_init(deque_t* d) {
	*d = (deque_t) {
		.buffer = NULL,
		.cap = 0,
		.len = 0,
		.front = 0,
		.back = 0
	};
}

void* d_pop_back(deque_t* d);

void d_destroy(deque_t* d) {
	// Retiramos todos os elementos antes, para evitar memory leak
	while (d->len > 0) {
		void* x = d_pop_back(d);
		free(x);
	}

	if (d->buffer) free(d->buffer);
	d_init(d);
}

// Dobra a capacidade do buffer
void __d_double_cap(deque_t* d) {
	int new_cap = d->cap * 2;
	void** new_buffer = (void**)malloc(sizeof(void*) * new_cap);

	// Copiamos o conteúdo do buffer anterior para o novo
	int j = 0;
	for (int i = d->front; i != d->back; i = (i+1) % d->cap)
		new_buffer[j++] = d->buffer[i];
	new_buffer[j++] = d->buffer[d->back];

	if (d->buffer) free(d->buffer);

	d->buffer = new_buffer;
	d->cap = new_cap;
	d->front = 0;
	d->back = j-1;
}

void __d_push_first(deque_t* d, void* x) {
	d->buffer = (void**)malloc(sizeof(void*));
	d->cap = 1;
	d->front = d->back = 0;
	d->buffer[0] = x;
}

void d_push_back(deque_t* d, void* x) {
	d->len++;
	if (d->cap == 0) {
		__d_push_first(d, x);
		return;
	}
	if (d->len == 1) {
		d->front = d->back = 0;
		d->buffer[0] = x;
		return;
	}

	if ((d->back + 1) % d->cap == d->front)
		__d_double_cap(d);

	d->back = (d->back + 1) % d->cap;
	d->buffer[d->back] = x;
}

void d_push_front(deque_t* d, void* x) {
	d->len++;
	if (d->cap == 0) {
		__d_push_first(d, x);
		return;
	}
	if (d->len == 1) {
		d->front = d->back = 0;
		d->buffer[0] = x;
		return;
	}

	if ((d->front - 1 + d->cap) % d->cap == d->back)
		__d_double_cap(d);

	d->front = (d->front - 1 + d->cap) % d->cap;
	d->buffer[d->front] = x;
}

// Retorna o elemento mais recente do deque, ou NULL se ele está vazio
void* d_pop_back(deque_t* d) {
	if (d->len == 0) return NULL;
	d->len--;

	void* ret = d->buffer[d->back];
	d->back = (d->back - 1 + d->cap) % d->cap;
	return ret;
}

// Retorna o elemento mais antigo do deque, ou NULL se ele está vazio
void* d_pop_front(deque_t* d) {
	if (d->len == 0) return NULL;
	d->len--;

	void* ret = d->buffer[d->front];
	d->front = (d->front + 1) % d->cap;
	return ret;
}

//
// Funcões utilizadas apenas para testar a implementação do deque
//

#ifdef TEST
deque_t global_d;

void __print_stats() {
	printf("(cap=%d, front=%d, back=%d)\n",
			global_d.cap, global_d.front, global_d.back);
}

void __pushb(int x) {
	int* arg = (int*)malloc(sizeof(int));
	*arg = x;
	d_push_back(&global_d, arg);
	printf("pushed back %d ", x);
	__print_stats();
}

void __pushf(int x) {
	int* arg = (int*)malloc(sizeof(int));
	*arg = x;
	d_push_front(&global_d, arg);
	printf("pushed front %d \n", x);
	__print_stats();
}

void __popb() {
	int* x = (int*)d_pop_back(&global_d);
	if (x == NULL) printf("popped back <null> ");
	else printf("popped back %d ", *x);

	__print_stats();

	if (x) free(x);
}

void __popf() {
	int* x = (int*)d_pop_front(&global_d);
	if (x == NULL) printf("popped front <null> ");
	else printf("popped front %d ", *x);

	__print_stats();

	if (x) free(x);
}
#endif

#endif
