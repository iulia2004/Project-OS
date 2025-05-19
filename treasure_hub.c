#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>

#define TREASURE_FILE "treasures.dat"
#define MAX_BUF 4096

typedef struct {
    int id;
    char username[20];
    float latitude;
    float longitude;
    char clue[50];
    int value;
} Treasure;

pid_t monitor_pid = -1;
int monitor_running = 0;

int to_monitor_pipe[2]; 
int from_monitor_pipe[2];

int list_hunts(char *outbuf, size_t outsize) {
    DIR *d = opendir(".");
    struct dirent *entry;
    
    if (!d) 
        return -1;

    size_t len = 0;

    while ((entry = readdir(d))) {
        if (entry->d_type == DT_DIR && entry->d_name[0] != '.') {
            char path[512];
            snprintf(path, sizeof(path), "%s/%s", entry->d_name, TREASURE_FILE);
           
            int f = open(path, O_RDONLY);
            int count = 0;
            Treasure t;

            while (f >= 0 && read(f, &t, sizeof(Treasure)) == sizeof(Treasure)) 
                count++;
            
            close(f);

            if (count > 0) {
                len += snprintf(outbuf + len, outsize - len, "Hunt: %s, Treasures: %d\n", entry->d_name, count);
                if (len >= outsize) 
                    break;
            }
        }
    }
    closedir(d);
    if (len == 0) {
        return snprintf(outbuf, outsize, "No hunts found.\n");
    }
    return len;
}


int list_treasures(const char *hunt_id, char *outbuf, size_t outsize) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", hunt_id, TREASURE_FILE);

    int f = open(path, O_RDONLY);

    if (f < 0) 
        return snprintf(outbuf, outsize, "Failed to open %s\n", path);

    size_t len = 0;
    Treasure t;

    while (read(f, &t, sizeof(Treasure)) == sizeof(Treasure)) {
        len += snprintf(outbuf + len, outsize - len, "Treasure ID: %d\nUser name: %s\nCoordinates: (%.2f, %.2f)\nClue: %s\nValue: %d\n\n", t.id, t.username, t.latitude, t.longitude, t.clue, t.value);
        if (len >= outsize) break;
    }
    close(f);
    return len;
}

int view_treasure(const char *hunt_id, int tid, char *outbuf, size_t outsize) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", hunt_id, TREASURE_FILE);
    
    int f = open(path, O_RDONLY);
    
    if (f < 0)
        return snprintf(outbuf, outsize, "Failed to open %s\n", path);

    size_t len = 0;
    Treasure t;

    while (read(f, &t, sizeof(Treasure)) == sizeof(Treasure)) {
        if (t.id == tid) {
            len = snprintf(outbuf, outsize,
                "Treasure ID: %d\nUser name: %s\nCoordinates: (%.2f, %.2f)\nClue: %s\nValue: %d\n", t.id, t.username, t.latitude, t.longitude, t.clue, t.value);
            close(f);
            return len;
        }
    }
    close(f);
    return snprintf(outbuf, outsize, "Treasure with ID %d not found.\n", tid);
}

void monitor_loop() {
    close(to_monitor_pipe[1]);
    close(from_monitor_pipe[0]); 

    char buf[MAX_BUF];

    while (1) {
        ssize_t n = read(to_monitor_pipe[0], buf, sizeof(buf) - 1);
        if (n <= 0) {
            break;
        }
        buf[n] = '\0';

        char cmd[50], arg1[50], arg2[50];
        int num_args = sscanf(buf, "%49s %49s %49s", cmd, arg1, arg2);

        char outbuf[MAX_BUF];
        int outlen = 0;

        if (strcmp(cmd, "list_hunts") == 0) {
            outlen = list_hunts(outbuf, sizeof(outbuf));

        } else if (strcmp(cmd, "list_treasures") == 0 && num_args >= 2) {
            outlen = list_treasures(arg1, outbuf, sizeof(outbuf));

        } else if (strcmp(cmd, "view_treasure") == 0 && num_args >= 3) {
            int tid = atoi(arg2);
            outlen = view_treasure(arg1, tid, outbuf, sizeof(outbuf));

        } else {
            outlen = snprintf(outbuf, sizeof(outbuf), "Unknown command\n");
        }

        write(from_monitor_pipe[1], outbuf, outlen);
    }

    close(to_monitor_pipe[0]);
    close(from_monitor_pipe[1]);
    exit(0);
}

void start_monitor() {
    if (monitor_running) {
        printf("Monitor already running.\n");
        return;
    }

    if (pipe(to_monitor_pipe) == -1 || pipe(from_monitor_pipe) == -1) {
        perror("pipe");
        exit(1);
    }

    monitor_pid = fork();
    if (monitor_pid < 0) {
        perror("fork");
        exit(1);
    } 
    else if (monitor_pid == 0) {
        monitor_loop();
    } 
    else {
        close(to_monitor_pipe[0]);
        close(from_monitor_pipe[1]); 

        monitor_running = 1;
        printf("Monitor started (PID: %d)\n", monitor_pid);
    }
}

void send_command(const char *cmd) {
    if (!monitor_running) {
        printf("Monitor not running.\n");
        return;
    }

    write(to_monitor_pipe[1], cmd, strlen(cmd));
    write(to_monitor_pipe[1], "\n", 1); 

    char buf[MAX_BUF];
    ssize_t n = read(from_monitor_pipe[0], buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        printf("%s", buf);
    }
}


void stop_monitor() {
    if (!monitor_running) {
        printf("Monitor not running.\n");
        return;
    }

    kill(monitor_pid, SIGTERM); 
    waitpid(monitor_pid, NULL, 0);

    close(to_monitor_pipe[1]);
    close(from_monitor_pipe[0]);

    monitor_running = 0;
    monitor_pid = -1;
    printf("Monitor stopped.\n");
}

void calculate_score_for_hunt(const char *hunt_id) {
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("pipe");
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return;
    }

    if (pid == 0) {
        close(pipefd[0]); 
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);

        execl("./score_calculator", "score_calculator", hunt_id, NULL);
        perror("execl");
        exit(1);
    } else {
        close(pipefd[1]);

        char buffer[512];
        ssize_t n;
        printf("Scores for hunt %s:\n", hunt_id);
        while ((n = read(pipefd[0], buffer, sizeof(buffer)-1)) > 0) {
            buffer[n] = '\0';
            printf("%s", buffer);
        }
        close(pipefd[0]);
        waitpid(pid, NULL, 0);
        printf("\n");
    }
}

void calculate_score() {
    DIR *d = opendir(".");

    if (!d) {
        perror("opendir");
        return;
    }

    struct dirent *entry;

    while ((entry = readdir(d)))
        if (entry->d_type == DT_DIR && entry->d_name[0] != '.')
            calculate_score_for_hunt(entry->d_name);

    closedir(d);
}

void print_menu() {
    printf("Treasure Hub\n");
    printf("Commands:\n");
    printf("1. start_monitor\n");
    printf("2. list_hunts\n");
    printf("3. list_treasures <hunt_id>\n");
    printf("4. view_treasure <hunt_id> <treasure_id>\n");
    printf("5. calculate_score\n");
    printf("6. stop_monitor\n");
    printf("7. exit\n\n");
}

int main() {
    char input[100];

    while (1) {
        print_menu();
        fflush(stdout);

        if (!fgets(input, sizeof(input), stdin)) break;
        input[strcspn(input, "\n")] = '\0'; 

        char *cmd = strtok(input, " ");
        if (!cmd) continue;

        if (strcmp(cmd, "start_monitor") == 0) {
            start_monitor();
        } 
        else if (strcmp(cmd, "list_hunts") == 0) {
            send_command("list_hunts");
        }
        else if (strcmp(cmd, "list_treasures") == 0) {
            char *hunt_id = strtok(NULL, " ");
            if (!hunt_id) {
                printf("Error: Missing hunt ID.\n");
                continue;
            }
            char cmd_buf[150];
            snprintf(cmd_buf, sizeof(cmd_buf), "list_treasures %s", hunt_id);
            send_command(cmd_buf);
        } 
        else if (strcmp(cmd, "view_treasure") == 0) {
            char *hunt_id = strtok(NULL, " ");
            char *tid_str = strtok(NULL, " ");
            if (!hunt_id || !tid_str) {
                printf("Error: Missing hunt ID or treasure ID.\n");
                continue;
            }
            char cmd_buf[150];
            snprintf(cmd_buf, sizeof(cmd_buf), "view_treasure %s %s", hunt_id, tid_str);
            send_command(cmd_buf);

        } 
        else if (strcmp(cmd, "stop_monitor") == 0) {
            stop_monitor();
        } 
        else if (strcmp(cmd, "calculate_score") == 0) {
            calculate_score();
        }
        else if (strcmp(cmd, "exit") == 0) {
            if (monitor_running) {
                printf("Error: Monitor is still running. Use stop_monitor first.\n");
            } 
            else {
                break;
            }

        } 
        else {
            printf("Unknown command.\n");
        }
    }

    return 0;
}
