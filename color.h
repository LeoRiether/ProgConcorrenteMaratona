#ifndef COLOR_MODULE
#define COLOR_MODULE

#ifdef COLOR
void red() { printf("\033[91m"); }
void green() { printf("\033[92m"); }
void yellow() { printf("\033[93m"); }
void cyan() { printf("\033[96m"); }
void reset() { printf("\033[0m"); }
#else
void red() {}
void green() {}
void yellow() {}
void cyan() {}
void reset() {}
#endif

#endif
