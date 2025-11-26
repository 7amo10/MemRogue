#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    printf("[Example] Starting allocation sequence\n");

    char *buffer = malloc(64);
    if (!buffer) {
        fprintf(stderr, "[Example] malloc failed\n");
        return EXIT_FAILURE;
    }
    strcpy(buffer, "MemRogue");

    buffer = realloc(buffer, 128);
    if (!buffer) {
        fprintf(stderr, "[Example] realloc failed\n");
        return EXIT_FAILURE;
    }

    int *numbers = calloc(16, sizeof(int));
    if (!numbers) {
        fprintf(stderr, "[Example] calloc failed\n");
        free(buffer);
        return EXIT_FAILURE;
    }

    for (int i = 0; i < 16; ++i) {
        numbers[i] = i * 2;
    }

    free(numbers);
    free(buffer);

    printf("[Example] Allocation sequence complete\n");
    return EXIT_SUCCESS;
}
