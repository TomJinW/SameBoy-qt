#include "windows_compat.h"
#include <stdlib.h>

int vasprintf(char **out, const char *format, va_list args)
{
    va_list copy;
    va_copy(copy, args);
    int size = _vscprintf(format, copy);
    va_end(copy);
    *out = size < 0 ? NULL : malloc((size_t)size + 1);
    if (!*out) return -1;
    return vsnprintf(*out, (size_t)size + 1, format, args);
}

ssize_t getline(char **line, size_t *capacity, FILE *file)
{
    size_t length = 0;
    int byte;
    while ((byte = fgetc(file)) != EOF) {
        if (!*line || length + 1 >= *capacity) {
            size_t size = *capacity ? *capacity * 2 : 128;
            char *grown = realloc(*line, size);
            if (!grown) return -1;
            *line = grown; *capacity = size;
        }
        (*line)[length++] = (char)byte;
        if (byte == '\n') break;
    }
    if (!length) return -1;
    (*line)[length] = 0;
    return (ssize_t)length;
}
