#include <stdio.h>
#include <stdlib.h>
#include <time.h>

// Function to generate a random alphanumeric character
char get_random_char() {
    int rand_num = rand() % 62;
    if (rand_num < 10) {
        return '0' + rand_num; // Numbers (0-9)
    } else if (rand_num < 36) {
        return 'A' + (rand_num - 10); // Uppercase letters (A-Z)
    } else {
        return 'a' + (rand_num - 36); // Lowercase letters (a-z)
    }
}

// Function to generate a random string of a given length
void generate_random_string(char *str, int length) {
    for (int i = 0; i < length; i++) {
        str[i] = get_random_char();
    }
    str[length] = '\0'; // Null terminator
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <number_of_lines> <output_file>\n", argv[0]);
        return 1;
    }

    int num_lines = atoi(argv[1]);
    const char *output_file = argv[2];
    
    FILE *file = fopen(output_file, "w");
    if (!file) {
        perror("Failed to open output file");
        return 1;
    }

    srand(time(NULL)); // Seed the random number generator

    for (int i = 0; i < num_lines; i++) {
        int length = rand() % 10000 + 1000;
        char *random_string = malloc(length + 1); // +1 for the null terminator
        if (!random_string) {
            perror("Failed to allocate memory");
            fclose(file);
            return 1;
        }

        generate_random_string(random_string, length);
        fprintf(file, "%s", random_string); // Add a null terminator and newline
        fputc('\0', file);
        fputc('\n', file);
        free(random_string);
    }

    fclose(file);
    printf("Generated %d lines of random null-terminated strings in %s\n", num_lines, output_file);

    return 0;
}

