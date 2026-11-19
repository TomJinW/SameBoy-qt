#pragma once
/* Some MinGW-w64 releases (e.g. Debian's GCC 14) do not declare getline() for -std=gnu11 builds. */
#include <stdio.h>
#include <sys/types.h>
ssize_t getline(char **line, size_t *capacity, FILE *file);
