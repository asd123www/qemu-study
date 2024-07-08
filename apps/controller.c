#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <assert.h>
#include <netdb.h>
#include <time.h>
#include <sys/types.h>
#include <signal.h>



#define MAX_LINE_LENGTH 255

int execute_wrapper(char *command) {
    char commandname[256];
    snprintf(commandname, 256, "%s", command);
    return system(commandname);
}

void get_config_value(const char *key, char *value) {
    FILE* file = fopen("./config.txt", "r");
    if (file == NULL) {
        puts("The file doesn't exist."); fflush(stdout);
        exit(-1);
    }
    char line[MAX_LINE_LENGTH];

    while (fgets(line, MAX_LINE_LENGTH, file)) {
        if (strncmp(line, key, strlen(key)) == 0) {
            strcpy(value, strchr(line, '=') + 1);
            // Remove newline character
            value[strcspn(value, "\n")] = 0;

            break;
        }
    }
    fclose(file);
}

int listen_wrapper(char *addr, char *port) {
    int sockfd, connfd, len;
    struct sockaddr_in servaddr, cli;
   
    // socket create and verification
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        printf("socket creation failed...\n");
        exit(0);
    }
    bzero(&servaddr, sizeof(servaddr));
   
    // assign IP, PORT
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = inet_addr(addr);
    servaddr.sin_port = htons(atoi(port));
   
    // Binding newly created socket to given IP and verification
    if ((bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr))) != 0) {
        printf("socket bind failed...\n");
        exit(0);
    }
   
    // Now server is ready to listen and verification
    if ((listen(sockfd, 15)) != 0) {
        printf("Listen failed...\n");
        exit(0);
    }
    len = sizeof(cli);
   
    // Accept the data packet from client and verification.
    connfd = accept(sockfd, (struct sockaddr *)&cli, &len);
    if (connfd < 0) {
        printf("server accept failed...\n");
        exit(0);
    }

    return connfd;
}

int connect_wrapper(char *addr, char *port) {
    int sockfd;
    struct sockaddr_in server_addr;

    // Create a socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        printf("socket creation failed...\n");
        exit(0);
    }

    // Specify the server address
    bzero(&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET; // Address family
    // printf("%s:%s\n", addr, port);
    server_addr.sin_port = htons(atoi(port)); // Port number, converted to network byte order
    server_addr.sin_addr.s_addr = inet_addr(addr); // Server IP address

    // Connect the socket to the server
    int ret = connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (ret < 0) {
        printf("connect failed!\n");
        exit(0);
    }

    return sockfd;
}

/* asd123www: start & end migration logic.
 * -------------------------------------------
 * For `src`: 
 *     Listen on the controller port and build connection with `backup`.
 *     Waiting for the `start migration` command from `backup`.
 *     Upon receiving, issue `migrate` command in VM monitor.
 *     WARNING: we don't check the existence of the VM.
 * -------------------------------------------
 * For `dst`:
 *     
 * -------------------------------------------
 * For `backup`:
 * 
 * -------------------------------------------
 */

#define IP_LEN 16
#define PORT_LEN 20
#define DATA_LEN 1024

int connfd, srcfd, dstfd;

char src_ip[IP_LEN], dst_ip[IP_LEN], backup_ip[IP_LEN], vm_ip[IP_LEN];
char migration_port[PORT_LEN], src_control_port[PORT_LEN], dst_control_port[PORT_LEN], backup_control_port[PORT_LEN];
char buff[DATA_LEN];
struct timespec start, end;

char startString[15] = "start migration";
char endString[15] =   "ended migration";

void src_main() {
    printf("Hello form the source!\n");

    // build connection with `backup`.
    connfd = listen_wrapper(src_ip, src_control_port);

    bzero(buff, DATA_LEN);
    while (1) {
        uint32_t read_len = read(connfd, buff, sizeof(buff));
        if (!read_len) continue;
        if (read_len != strlen(startString)) {
            puts("asd123www: start migration is wrong."); fflush(stdout);
            exit(-1);
        }

        assert(strcmp(buff, startString) == 0);

        // `sudo bash start_migration.sh`
        int ret = execute_wrapper("sudo bash ./start_migration.sh");
        if (ret) {
            puts("asd123www: start migration is wrong."); fflush(stdout);
            exit(-1);
        }

        break;
    }
}

void signal_handler_dst(int signal) {
    if (signal == SIGUSR1) {
        printf("dst: Received SIGUSR1 signal\n");
        uint32_t write_len = write(connfd, endString, sizeof(endString));
        assert(write_len == sizeof(endString));
    }
}

void dst_main() {
    printf("Hello form the dest!\n");

    // write pid to a file for signal.
    FILE *pid_file = fopen("./controller.pid", "w");
    if (pid_file) {
        fprintf(pid_file, "%d", getpid());
        fclose(pid_file);
    }

    connfd = listen_wrapper(dst_ip, dst_control_port);

    if (signal(SIGUSR1, signal_handler_dst) == SIG_ERR) {
        printf("An error occurred while setting a signal handler.\n"); fflush(stdout);
        exit(-1);
    }

    printf("dst: Waiting for SIGUSR1 signal\n");

    // keeps the program alive
    while(1) {
        sleep(1);
    }
}


void signal_handler_backup(int signal) {
    if (signal == SIGUSR1) {
        printf("backup: Received SIGUSR1 signal\n");
        sleep(3);

        struct timespec start, end;
        clock_gettime(CLOCK_MONOTONIC, &start);

        // issue migration command.
        uint32_t write_len = write(srcfd, startString, sizeof(startString));
        assert(write_len == sizeof(startString));

        // wait for completion signal.
        bzero(buff, DATA_LEN);
        uint32_t read_len = read(dstfd, buff, sizeof(buff));
        assert(read_len == sizeof(endString));
        assert(strcmp(endString, buff) == 0);

        clock_gettime(CLOCK_MONOTONIC, &end);

        printf("start: %lld ns\n", start.tv_sec * 1000000000LL + start.tv_nsec);
        printf("end: %lld ns\n", end.tv_sec * 1000000000LL + end.tv_nsec);
        printf("durtion: %lld ns\n", end.tv_sec * 1000000000LL + end.tv_nsec - start.tv_sec * 1000000000LL - start.tv_nsec);

        close(srcfd);
        close(dstfd);
        
        exit(-1);
    }
}

void backup_main() {
    printf("Hello form the backup!\n");
    srcfd = connect_wrapper(src_ip, src_control_port);
    dstfd = connect_wrapper(dst_ip, dst_control_port);

    // write pid to a file for signal.
    FILE *pid_file = fopen("./controller.pid", "w");
    if (pid_file) {
        fprintf(pid_file, "%d", getpid());
        fclose(pid_file);
    }

    if (signal(SIGUSR1, signal_handler_backup) == SIG_ERR) {
        printf("An error occurred while setting a signal handler.\n"); fflush(stdout);
        exit(-1);
    }

    printf("backup: Waiting for SIGUSR1 signal\n");

    // keeps the program alive
    while(1) {
        sleep(1);
    }
}


void write_to_file(int fd, char *data) {
    uint32_t write_len = write(fd, data, strlen(data));
    assert(write_len == strlen(data));
}
void read_from_file(int fd, uint32_t len, char *data) {
    uint32_t read_len = 0;
    while (1) {
        read_len = read(fd, data, len);
        if (read_len) break;
    }
    assert(read_len == len - 1);
}

void signal_handler_shm_src(int signal) {
    if (signal == SIGUSR1) {
        // printf("shm_src: VM image is complete!\n");
        write_to_file(connfd, "complete_vm_image");
        clock_gettime(CLOCK_MONOTONIC, &end);
        printf("\n\nsrc complete VM image durtion: %lld ns\n", end.tv_sec * 1000000000LL + end.tv_nsec - start.tv_sec * 1000000000LL - start.tv_nsec);
        execute_wrapper("echo \"q\" | sudo socat stdio unix-connect:qemu-monitor-migration-src");  
        puts("Quit src.");fflush(stdout);
        exit(-1);
    }
}
void shm_src_main() {
    printf("Hello form the shm_source!\n");
    FILE *pid_file = fopen("./src_controller.pid", "w");
    if (pid_file) {
        fprintf(pid_file, "%d", getpid());
        fclose(pid_file);
    }

    execute_wrapper("sudo rm /dev/shm/my_shared_memory");

    // build connection with `backup`.
    connfd = listen_wrapper(src_ip, src_control_port);

    bzero(buff, DATA_LEN);
    read_from_file(connfd, sizeof("shm_migrate"), buff);
    assert(strcmp(buff, "shm_migrate") == 0);
    execute_wrapper("echo \"shm_migrate /my_shared_memory 10\" | sudo socat stdio unix-connect:qemu-monitor-migration-src");

    if (signal(SIGUSR1, signal_handler_shm_src) == SIG_ERR) {
        printf("An error occurred while setting a signal handler.\n"); fflush(stdout);
        exit(-1);
    }

    read_from_file(connfd, sizeof("shm_migrate_switchover"), buff);
    assert(strcmp(buff, "shm_migrate_switchover") == 0);
    clock_gettime(CLOCK_MONOTONIC, &start);
    execute_wrapper("echo \"shm_migrate_switchover\" | sudo socat stdio unix-connect:qemu-monitor-migration-src");

    while(1) {
        sleep(1);
    }
}

void signal_handler_shm_dst(int signal) {
    if (signal == SIGUSR1) {
        // printf("shm_dst: Loaded VM from shm!\n");
        write_to_file(connfd, "switchover_finished");
        clock_gettime(CLOCK_MONOTONIC, &end);
        printf("\n\ndst load VM image durtion: %lld ns\n", end.tv_sec * 1000000000LL + end.tv_nsec - start.tv_sec * 1000000000LL - start.tv_nsec);
        puts("Quit dst.");fflush(stdout);
        exit(-1);
    }
}
void shm_dst_main() {
    printf("Hello form the shm_destination!\n");
    FILE *pid_file = fopen("./dst_controller.pid", "w");
    if (pid_file) {
        fprintf(pid_file, "%d", getpid());
        fclose(pid_file);
    }

    // build connection with `backup`.
    connfd = listen_wrapper(dst_ip, dst_control_port);

    if (signal(SIGUSR1, signal_handler_shm_dst) == SIG_ERR) {
        printf("An error occurred while setting a signal handler.\n"); fflush(stdout);
        exit(-1);
    }

    bzero(buff, DATA_LEN);
    read_from_file(connfd, sizeof("shm_load_vm_image"), buff);
    assert(strcmp(buff, "shm_load_vm_image") == 0);
    clock_gettime(CLOCK_MONOTONIC, &start);
    execute_wrapper("echo \"migrate_incoming_shm /my_shared_memory 10\" | sudo socat stdio unix-connect:qemu-monitor-migration-dst");

    while (1) {
        sleep(1);
    }
}

// simulate a control plane.
void shm_backup_main() {
    printf("Hello form the shm_backup!\n");
    srcfd = connect_wrapper(src_ip, src_control_port);
    dstfd = connect_wrapper(dst_ip, dst_control_port);
    char data[100] = {0};

    write_to_file(srcfd, "shm_migrate");
    sleep(40);
    clock_gettime(CLOCK_MONOTONIC, &start);

    write_to_file(srcfd, "shm_migrate_switchover");
    memset(data, 0, sizeof(data));
    read_from_file(srcfd, sizeof("complete_vm_image"), data);
    assert(strcmp(data, "complete_vm_image") == 0);


    clock_gettime(CLOCK_MONOTONIC, &end);
    printf("durtion: %lld ns\n", end.tv_sec * 1000000000LL + end.tv_nsec - start.tv_sec * 1000000000LL - start.tv_nsec);

    write_to_file(dstfd, "shm_load_vm_image");
    memset(data, 0, sizeof(data));
    read_from_file(dstfd, sizeof("switchover_finished"), data);
    assert(strcmp(data, "switchover_finished") == 0);

    clock_gettime(CLOCK_MONOTONIC, &end);
    printf("durtion: %lld ns\n", end.tv_sec * 1000000000LL + end.tv_nsec - start.tv_sec * 1000000000LL - start.tv_nsec);
}

int main(int argc, char *argv[]) {
    // read machine name: `src`, `dst`, or `client`.
    if (argc < 3) {
        printf("Usage: %s <migration mode: `shm`, or `normal`> <machine type: `src`, `dst`, or `backup`>\n", argv[0]);
        return 1;
    }
    printf("Migration Mode: %s\n", argv[1]);
    printf("Machine type: %s\n", argv[2]);


    // read the config file.
    get_config_value("SRC_IP", src_ip);
    get_config_value("DST_IP", dst_ip);
    get_config_value("BACKUP_IP", backup_ip);
    get_config_value("VM_IP", vm_ip);
    get_config_value("MIGRATION_PORT", migration_port);
    get_config_value("SRC_CONTROL_PORT", src_control_port);
    get_config_value("DST_CONTROL_PORT", dst_control_port);
    get_config_value("BACKUP_CONTROL_PORT", backup_control_port);

    // migration mode.
    if (strcmp(argv[1], "shm") == 0) {
        if (strcmp(argv[2], "src") == 0) {
            shm_src_main();
        } else if (strcmp(argv[2], "dst") == 0) {
            shm_dst_main();
        } else {
            assert(strcmp(argv[2], "backup") == 0);
            shm_backup_main();
        }

    } else {
        assert(strcmp(argv[1], "normal") == 0);
        if (strcmp(argv[2], "src") == 0) {
            src_main();
        } else if (strcmp(argv[2], "dst") == 0) {
            dst_main();
        } else {
            assert(strcmp(argv[2], "backup") == 0);
            backup_main();
        }
    }


    return 0;
}