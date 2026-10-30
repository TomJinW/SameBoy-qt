#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <malloc.h>

typedef intptr_t ssize_t;
#define alloca _alloca
int vasprintf(char **out, const char *format, va_list args);
ssize_t getline(char **line, size_t *capacity, FILE *file);
