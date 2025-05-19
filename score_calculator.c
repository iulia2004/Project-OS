#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#define TREASURE_FILE "treasures.dat"

typedef struct {
    int id;
    char username[20];
    float latitude;
    float longitude;
    char clue[50];
    int value;
} Treasure;

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <hunt_id>\n", argv[0]);
        return 1;
    }

    char path[256];
    snprintf(path, sizeof(path), "%s/%s", argv[1], TREASURE_FILE);
    int f = open(path, O_RDONLY);
    if (f < 0) {
        perror("open");
        return 1;
    }

    Treasure t;
    struct {
        char name[20];
        int total;
    } users[100];

    int user_count = 0;

    while (read(f, &t, sizeof(Treasure)) == sizeof(Treasure)) {
        int found = 0;
        for (int i = 0; i < user_count; i++) {
            if (strcmp(users[i].name, t.username) == 0) {
                users[i].total += t.value;
                found = 1;
                break;
            }
        }
        if (!found) {
            strcpy(users[user_count].name, t.username);
            users[user_count].total = t.value;
            user_count++;
        }
    }
    close(f);

    for (int i = 0; i < user_count; i++) {
        printf("%s: %d\n", users[i].name, users[i].total);
    }

    return 0;
}
