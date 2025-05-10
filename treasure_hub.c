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

#define CMD_FILE "monitor_command.txt"
#define RESPONSE_FILE "monitor_response.txt"
#define TREASURE_FILE "treasures.dat"

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

void sigchld_handler(int sig) {
    int status;
    waitpid(monitor_pid, &status, 0);
    printf("Monitor process exited with status %d\n", WEXITSTATUS(status));
    monitor_pid = -1;
    monitor_running = 0;
}

void list_treasures(const char *hunt_id, int out_fd) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", hunt_id, TREASURE_FILE);

    int f = open(path, O_RDONLY);
    if (f < 0) {
        dprintf(out_fd, "Failed to open %s\n", path);
        return;
    }

    Treasure t;
    while (read(f, &t, sizeof(Treasure)) == sizeof(Treasure)) {
        dprintf(out_fd, "Treasure ID: %d\n", t.id);
        dprintf(out_fd, "User name: %s\n", t.username);
        dprintf(out_fd, "Coordinates: (%.2f, %.2f)\n", t.latitude, t.longitude);
        dprintf(out_fd, "Clue: %s\n", t.clue);
        dprintf(out_fd, "Value: %d\n\n", t.value);
    }
    close(f);
}

void view_treasure(const char *hunt_id, int tid, int out_fd) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", hunt_id, TREASURE_FILE);
    int f = open(path, O_RDONLY);
    if (f < 0) {
        dprintf(out_fd, "Failed to open %s\n", path);
        return;
    }

    Treasure t;
    while (read(f, &t, sizeof(Treasure)) == sizeof(Treasure)) {
        if (t.id == tid) {
            dprintf(out_fd, "Treasure ID: %d\n", t.id);
            dprintf(out_fd, "User name: %s\n", t.username);
            dprintf(out_fd, "Coordinates: (%.2f, %.2f)\n", t.latitude, t.longitude);
            dprintf(out_fd, "Clue: %s\n", t.clue);
            dprintf(out_fd, "Value: %d\n", t.value);
            close(f);
            return;
        }
    }
    dprintf(out_fd, "Treasure with ID %d not found.\n", tid);
    close(f);
}

void list_hunts(int out_fd) {
    DIR *d = opendir(".");
    struct dirent *entry;
    if (!d) return;

    while ((entry = readdir(d))) {
        if (entry->d_type == DT_DIR && entry->d_name[0] != '.') {
            char path[256];
            snprintf(path, sizeof(path), "%s/%s", entry->d_name, TREASURE_FILE);
            int f = open(path, O_RDONLY);
            int count = 0;
            Treasure t;
            while (f >= 0 && read(f, &t, sizeof(Treasure)) == sizeof(Treasure)) {
                count++;
            }
            if (f >= 0) close(f);

            if (count > 0) {
                dprintf(out_fd, "Hunt: %s, Treasures: %d\n", entry->d_name, count);
            }
        }
    }
    closedir(d);
}

void start_monitor() {
    if (monitor_running) {
        printf("Monitor already running.\n");
        return;
    }

    monitor_pid = fork();
    if (monitor_pid < 0) {
        perror("fork");
        exit(1);
    } else if (monitor_pid == 0) {
        struct sigaction sa_usr1, sa_usr2;
        sigemptyset(&sa_usr1.sa_mask);
        sigemptyset(&sa_usr2.sa_mask);
        sa_usr1.sa_flags = 0;
        sa_usr2.sa_flags = 0;

        volatile sig_atomic_t action_needed = 0;
        volatile sig_atomic_t should_exit = 0;

        void handle_usr1(int sig) { action_needed = 1; }
        void handle_usr2(int sig) { should_exit = 1; }

        sa_usr1.sa_handler = handle_usr1;
        sa_usr2.sa_handler = handle_usr2;
        sigaction(SIGUSR1, &sa_usr1, NULL);
        sigaction(SIGUSR2, &sa_usr2, NULL);

        while (!should_exit) {
            pause();
            if (action_needed) {
                action_needed = 0;

                int cmd_fd = open(CMD_FILE, O_RDONLY);
                if (cmd_fd < 0) continue;
                char buf[256];
                ssize_t n = read(cmd_fd, buf, sizeof(buf) - 1);
                if (n <= 0) { close(cmd_fd); continue; }
                buf[n] = '\0';
                close(cmd_fd);

                int out_fd = open(RESPONSE_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (out_fd < 0) continue;

                if (strncmp(buf, "list_treasures", 15) == 0) {
                    char hunt_id[50];
                    if (sscanf(buf, "list_treasures %s", hunt_id) == 1) {
                        list_treasures(hunt_id, out_fd);
                    } else {
                        dprintf(out_fd, "Error: Missing hunt ID.\n");
                    }
                } else if (strncmp(buf, "view_treasure", 14) == 0) {
                    char hunt[100];
                    int tid;
                    if (sscanf(buf + 15, "%s %d", hunt, &tid) == 2)
                        view_treasure(hunt, tid, out_fd);
                } else if (strcmp(buf, "list_hunts") == 0) {
                    list_hunts(out_fd);
                } else {
                    dprintf(out_fd, "Unknown command\n");
                }

                close(out_fd);
            }
        }
        usleep(500000);
        exit(0);
    } else {
        monitor_running = 1;
        struct sigaction sa;
        sa.sa_handler = sigchld_handler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(SIGCHLD, &sa, NULL);
        printf("Monitor started (PID: %d)\n", monitor_pid);
    }
}

void send_command(const char *cmd) {
    if (!monitor_running) {
        printf("Monitor not running.\n");
        return;
    }

    int fd = open(CMD_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror("open CMD_FILE");
        return;
    }
    write(fd, cmd, strlen(cmd));
    close(fd);

    kill(monitor_pid, SIGUSR1);
    usleep(300000);

    fd = open(RESPONSE_FILE, O_RDONLY);
    if (fd < 0) {
        perror("open RESPONSE_FILE");
        return;
    }
    char buf[512];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        printf("%s", buf);
    }
    close(fd);
}

void stop_monitor() {
    if (!monitor_running) {
        printf("Monitor not running.\n");
        return;
    }
    kill(monitor_pid, SIGUSR2);
    printf("Sent stop signal to monitor.\n");
}

int main() {
    char input[100];

    while (1) {
        printf("treasure_hub> ");
        fflush(stdout);

        if (!fgets(input, sizeof(input), stdin)) break;
        input[strcspn(input, "\n")] = '\0';  
        
        if (strcmp(input, "start_monitor") == 0) {
            start_monitor();
        } else if (strncmp(input, "list_treasures", 15) == 0) {
            char hunt_id[50];
            if (sscanf(input, "list_treasures %s", hunt_id) == 1) {
                printf("Parsed hunt ID: '%s'\n", hunt_id);
                char cmd[150];
                snprintf(cmd, sizeof(cmd), "list_treasures %s", hunt_id);
                send_command(cmd);
            } else {
                printf("Error: Missing hunt ID.\n");
            }
        } else if (strncmp(input, "list_hunts", 10) == 0 ||
                   strncmp(input, "view_treasure", 14) == 0) {
            send_command(input);
        } else if (strcmp(input, "stop_monitor") == 0) {
            stop_monitor();
        } else if (strcmp(input, "exit") == 0) {
            if (monitor_running) {
                printf("Error: Monitor is still running. Use stop_monitor first.\n");
            } else {
                break;
            }
        } else {
            printf("Unknown command.\n");
        }
    }

    return 0;
}
