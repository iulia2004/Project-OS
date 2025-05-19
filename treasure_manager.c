#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#include <dirent.h>
#include <errno.h>

#define TREASURE_FILE "treasures.dat"
#define LOG_FILE "logged_hunt"

typedef struct {
    int id;
    char username[20];
    float latitude;
    float longitude;
    char clue[50];
    int value;
} Treasure;

void log_operation(const char *hunt_id, const char *operation) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", hunt_id, LOG_FILE);

    int f = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (f < 0) {
        perror("Failed to open log file");
        return;
    }

    dprintf(f, "%s\n", operation);

    close(f);

    char linkname[256];
    snprintf(linkname, sizeof(linkname), "logged_hunt-%s", hunt_id);
    unlink(linkname);
    symlink(path, linkname); 
}

void add(const char *hunt_id) {
    mkdir(hunt_id, 0755);

    char path[256];
    snprintf(path, sizeof(path), "%s/%s", hunt_id, TREASURE_FILE);

    int f = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (f < 0) {
        perror("Failed to open file");
        return;
    }

    Treasure t;
    printf("Treasure ID: ");
    scanf("%d", &t.id);
    printf("Username: ");
    scanf("%s", t.username);
    printf("Latitude: ");
    scanf("%f", &t.latitude);
    printf("Longitude: ");
    scanf("%f", &t.longitude);
    printf("Clue: ");
    getchar();
    fgets(t.clue, 50, stdin);
    t.clue[strcspn(t.clue, "\n")] = '\0';
    printf("Value: ");
    scanf("%d", &t.value);

    write(f, &t, sizeof(Treasure));
    close(f);

    log_operation(hunt_id, "Added treasure");
    printf("Treasure added.\n");
}                    

void list(const char *hunt_id) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", hunt_id, TREASURE_FILE);

    struct stat st;
    if (stat(path, &st) != 0) {
        perror("Failed to get treasure file info");
        return;
    }

    printf("Hunt: %s\n", hunt_id);
    printf("File size: %ld bytes\n", st.st_size);
    printf("Last modified: %s", ctime(&st.st_mtime));

    int f = open(path, O_RDONLY);
    if (f < 0) {
        perror("Failed to open file");
        return;
    }

    Treasure t;
    while (read(f, &t, sizeof(Treasure)) == sizeof(Treasure)) {
        printf("Treasure ID: %d\n", t.id);
        printf("User name: %s\n", t.username);
        printf("Coordinates: (%.2f, %.2f)\n", t.latitude, t.longitude);
        printf("Clue: %s\n", t.clue);
        printf("Value: %d\n", t.value);
        printf("\n");
    }
    close(f);
}

void view(const char *hunt_id, int id) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", hunt_id, TREASURE_FILE);

    int f = open(path, O_RDONLY);
    if (f < 0) {
        perror("Failed to open file");
        return;
    }

    Treasure t;
    while (read(f, &t, sizeof(Treasure)) == sizeof(Treasure)) {
        if (t.id == id) {
            printf("Treasure ID: %d\n", t.id);
            printf("User name: %s\n", t.username);
            printf("Coordinates: (%.2f, %.2f)\n", t.latitude, t.longitude);
            printf("Clue: %s\n", t.clue);
            printf("Value: %d\n", t.value);
            printf("\n");
            close(f);
            return;
        }
    }

    printf("Treasure with ID %d not found.\n", id);
    close(f);
}


void remove_treasure(const char *hunt_id, int id) {
    char path[256], temp_path[256];
    snprintf(path, sizeof(path), "%s/%s", hunt_id, TREASURE_FILE);
    snprintf(temp_path, sizeof(temp_path), "%s/temp.dat", hunt_id);

    int f = open(path, O_RDONLY);
    int temp_f = open(temp_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (f < 0 || temp_f < 0) {
        perror("Failed to open file");
        return;
    }

    int found = 0;
    Treasure t;
    while (read(f, &t, sizeof(Treasure)) == sizeof(Treasure)) {
        if (t.id == id) {
            found = 1;
            continue;
        }
        write(temp_f, &t, sizeof(Treasure));
    }

    close(f);
    close(temp_f);

    rename(temp_path, path);

    if (found) {
        log_operation(hunt_id, "Removed treasure");
        printf("Treasure removed.\n");
    } else {
        printf("Treasure not found.\n");
    }
}

void remove_hunt(const char *hunt_id) {
    char path[256];

    snprintf(path, sizeof(path), "%s/%s", hunt_id, TREASURE_FILE);
    unlink(path);

    snprintf(path, sizeof(path), "%s/%s", hunt_id, LOG_FILE);
    unlink(path);

    rmdir(hunt_id);

    char linkname[256];
    snprintf(linkname, sizeof(linkname), "logged_hunt-%s", hunt_id);
    unlink(linkname); 

    printf("Hunt removed.\n");
}

void print_menu() {
    printf("Treasure Manager\n");
    printf("Commands:\n");
    printf("1. --add <hunt_id>\n");
    printf("2. --list <hunt_id>\n");
    printf("3. --view <hunt_id> <id>\n");
    printf("4. --remove_treasure <hunt_id> <id>\n");
    printf("5. --remove_hunt <hunt_id>\n");
}

int main(int argc, char *argv[]) {

    if (argc < 3) {
        print_menu();
        return 1;
    }

    const char *cmd = argv[1];
    const char *hunt_id = argv[2]; 

    if (strcmp(cmd, "--add") == 0) {
        add(hunt_id);
    } else if (strcmp(cmd, "--list") == 0) {
        list(hunt_id);
    } else if (strcmp(cmd, "--view") == 0 && argc == 4) {
        view(hunt_id, atoi(argv[3])); 
    } else if (strcmp(cmd, "--remove_treasure") == 0 && argc == 4) {
        remove_treasure(hunt_id, atoi(argv[3]));
    } else if (strcmp(cmd, "--remove_hunt") == 0) {
        remove_hunt(hunt_id);
    } else {
        fprintf(stderr, "Invalid command\n");
        print_menu();
        return 1;
    }

    return 0;
}
