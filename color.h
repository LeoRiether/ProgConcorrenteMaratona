#ifndef COLOR_MODULE
#define COLOR_MODULE

#include "cond.h"
#include <pthread.h>

// Lock para uma thread não ficar com a cor do print de outra
pthread_mutex_t color_lock;
pthread_mutexattr_t color_lock_attr;

void color_init() {
	pthread_mutexattr_init(&color_lock_attr);
	pthread_mutexattr_settype(&color_lock_attr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&color_lock, &color_lock_attr);
}

#ifdef COLOR
void color_begin() { m_lock(&color_lock); }
void red() {
	m_lock(&color_lock);
	printf("\033[91m");
}
void green() {
	m_lock(&color_lock);
	printf("\033[92m");
}
void yellow() {
	m_lock(&color_lock);
	printf("\033[93m");
}
void cyan() {
	m_lock(&color_lock);
	printf("\033[96m");
}
void yellow_bg() {
	m_lock(&color_lock);
	printf("\033[43m\033[30m");
}
void green_bg() {
	m_lock(&color_lock);
	printf("\033[102m\033[30m");
}
void red_bg() {
	m_lock(&color_lock);
	printf("\033[101m\033[30m");
}
void cyan_bg() {
	m_lock(&color_lock);
	printf("\033[46m\033[30m");
}
void reset() {
	printf("\033[0m");
	m_unlock(&color_lock);
}
#else
void color_begin() { m_lock(&color_lock); }
void red() { m_lock(&color_lock); }
void green() { m_lock(&color_lock); }
void yellow() { m_lock(&color_lock); }
void cyan() { m_lock(&color_lock); }
void yellow_bg() { m_lock(&color_lock); }
void green_bg() { m_lock(&color_lock); }
void red_bg() { m_lock(&color_lock); }
void cyan_bg() { m_lock(&color_lock); }
void reset() { m_unlock(&color_lock); }
#endif

#endif
