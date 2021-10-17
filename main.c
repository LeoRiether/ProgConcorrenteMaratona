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
#define TIMES 3

// Quantos competidores existem por time
#define SZ_TIME 3

// Quantos competidores no total
#define N (TIMES * SZ_TIME)

// Quantos competidores podem ficar na sala do coffee break simultaneamente
#define MAX_COFFEE 3

// gera um número aleatório em [low, high]
static inline int rand_int(int low, int high) {
	return rand() % (high - low + 1) + low;
}

// Para propósitos de debugging (aka deixar rodando por bastante tempo e ver se deu deadlock (sei que não é assim que se prova a corretude de um algoritmo concorrente))
void print_time() {
	time_t timer;
	struct tm* tm_info;
	timer = time(NULL);
	tm_info = localtime(&timer);
	char buffer[9];
	strftime(buffer, 9, "%H:%M:%S", tm_info);
	puts(buffer);
}

//--------------------------------------------------------------
// Definições de variáveis dos competidores e juíz automático
//--------------------------------------------------------------
const char* evento2str[] = { "Alarme", "PermissaoComputador" };
typedef enum evento_t {
	Alarme, PermissaoComputador
} evento_t;

int team[N]; // team[i] = id do time do i-ésimo competidor

cond_t sinal[N];
deque_t eventos[N]; // eventos[i] = fila de eventos do i-ésimo competidor
					// para acessar eventos[i], deve-se estar de posse do lock sinal[i].lock

int alarme_id[N]; // alarme_id[i] = id do alarme associado ao competidor `i`

pthread_mutex_t quer_computador[TIMES]; // lock para que apenas uma pessoa tente entrar no computador por vez
bool alguem_no_pc[TIMES]; // alguem_no_pc[i] = existe algum competidor do time `i` usando o PC?
deque_t esperando_pc[TIMES]; // fila de pessoas de um time esperando para usar o PC.
							 // para acessar o deque, deve-se estar de posse do lock quer_computador[time]

sem_t coffee_break; // Quantas pessoas ainda podem entrar no coffee break

deque_t fila_juiz; // Fila de submissões que devem ser julgadas
cond_t cond_juiz; // Variável de condição associada à fila_juiz

pthread_barrier_t comeco_da_prova;

void* competidor(void*);
void* juiz();

//--------------------------------------------------------------
// Inicialização e destruição das variáveis
//--------------------------------------------------------------
void init() {
	color_init();
	pthread_barrier_init(&comeco_da_prova, 0, N + 1); // N competidores em 1 juíz
	sem_init(&coffee_break, 0, MAX_COFFEE);
	for (int i = 0; i < TIMES; i++) {
		alguem_no_pc[i] = 0;
		pthread_mutex_init(&quer_computador[i], 0);
		d_init(&esperando_pc[i]);
	}
	for (int i = 0; i < N; i++) {
		c_init(&sinal[i]);
		d_init(&eventos[i]);
	}

	c_init(&cond_juiz);
	d_init(&fila_juiz);
}

void destroy() {
	pthread_barrier_destroy(&comeco_da_prova);
	sem_destroy(&coffee_break);
	for (int i = 0; i < TIMES; i++) {
		pthread_mutex_destroy(&quer_computador[i]);
		d_destroy(&esperando_pc[i]);
	}
	for (int i = 0; i < N; i++) {
		c_destroy(&sinal[i]);
		d_destroy(&eventos[i]);
	}
	c_destroy(&cond_juiz);
	d_destroy(&fila_juiz);
}

//--------------------------------------------------------------
// main
//--------------------------------------------------------------
int main() {

	srand(time(NULL));

	init();

	pthread_t competidores[N];
	pthread_t tjuiz;
	pthread_create(&tjuiz, NULL, &juiz, NULL);
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
	*evt = Alarme;
	int id_alarme = alarme(delay, &sinal[id], &eventos[id], evt);
	return id_alarme;
}

//--------------------------------------------------------------
// Comportamento dos competidores
//--------------------------------------------------------------

void competidor_init(int id);
void achou_solucao(int id);
void escreve_codigo(int id);
void entrar_no_coffee_break(int id);

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

		printf("[time %d] O competidor %d recebeu um evento do tipo %s\n",
				team[id], id, evento2str[evt]);

		// print_time();

		if (evt == Alarme && rand_int(0, 9) <= 2) {
			 // 20% de chance do competidor ir para o coffee break em vez de solucionar uma questão
			entrar_no_coffee_break(id);
		} else if (evt == Alarme) {
			achou_solucao(id);
		} else if (evt == PermissaoComputador) {
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

	// Envia o código ao juíz
	m_lock(&cond_juiz.lock);
		int* team_id = (int*)malloc(sizeof(int));
		*team_id = t;
		d_push_back(&fila_juiz, (void*)team_id);
		pthread_cond_signal(&cond_juiz.cond);
	m_unlock(&cond_juiz.lock);

	// Libera o computador do time `t`
	m_lock(&quer_computador[t]);
		if (esperando_pc[t].len == 0) {
			// Ninguém mais quer usar o PC
			alguem_no_pc[t] = false;
			printf("[time %d] o computador foi liberado\n", t);
		} else {
			// Uma pessoa da fila `esperando_pc[t]` quer escrever uma solução
			void* ptr = d_pop_front(&esperando_pc[t]);
			int outro = *(int*)ptr;
			free(ptr);

			if (outro == id) {
				green(); printf("[time %d] %d já tem outra solução pronta!\n", t, id); reset();
				m_unlock(&quer_computador[t]);
				escreve_codigo(id);
				return;
			}

			yellow(); printf("[time %d] avisando a %d que ele tem permissão para usar o PC\n", t, outro); reset();

			m_lock(&sinal[outro].lock);
				// Coloca um evento do tipo "PermissaoComputador" na fila de eventos de `outro`
				evento_t* evt = (evento_t*)malloc(sizeof(evento_t));
				*evt = PermissaoComputador;
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
	if (!alguem_no_pc[t]) {
		// Conseguimos pegar o computador
		alguem_no_pc[t] = true;
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

void entrar_no_coffee_break(int id) {
	sem_wait(&coffee_break);

	color_begin();
	printf("[time %d] %d está no ", team[id], id);
	yellow_bg(); printf("coffee break"); reset();
	printf("...\n");
	reset();

	sleep(rand_int(3, 7));

	color_begin();
	printf("[time %d] %d voltou do ", team[id], id);
	yellow_bg(); printf("coffee break"); reset();
	printf("\n");
	reset();

	sem_post(&coffee_break);
}

//--------------------------------------------------------------
// Comportamento do juíz automático
//--------------------------------------------------------------

void* juiz() {
	color_begin();
	green_bg(); printf("Juíz pronto"); reset();
	printf("\n");
	reset();

	pthread_barrier_wait(&comeco_da_prova);

	for (;;) {
		// Esperamos até que a fila de submissões não esteja vazia
		m_lock(&cond_juiz.lock);
		while (fila_juiz.len == 0)
			pthread_cond_wait(&cond_juiz.cond, &cond_juiz.lock);

		void* ptr = d_pop_front(&fila_juiz);
		int time_id = *(int*)ptr;
		free(ptr);

		m_unlock(&cond_juiz.lock);

		// E julgamos a submissão do time `time_id`
		sleep(rand_int(0, 10));

		static const char* vereditos[] = { "AC", "TLE", "WA" };
		static void (*bgcolor[])() = { &green_bg, &cyan_bg, &red_bg };

		int v = rand_int(0, 2); // índice do veretido

		// Impressão do resultado
		color_begin();
		cyan_bg(); printf("[juiz]"); reset();
		printf(" o time %d recebeu um ", time_id);
		bgcolor[v](); printf("%s", vereditos[v]); reset();
		printf("!\n");
		reset();
	}
}
