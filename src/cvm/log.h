#ifndef LOG_H
#define LOG_H


#include <stdio.h>
#include <stdarg.h>


#ifdef DEBUG
#define dlog(...) printf(__VA_ARGS__)
#else // DEBUG
#define dlog(...) while(0) printf(__VA_ARGS__)
#endif // DEBUG


#define log(...) printf(__VA_ARGS__)


#endif // LOG_H
