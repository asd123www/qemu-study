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
    char commandname[2048];
    snprintf(commandname, 2048, "%s", command);
    return system(commandname);
}

int execute_wrapper_process(char *command) {
    pid_t pid = vfork();
    if (pid == -1) {
        // Fork failed
        perror("fork");
        exit(-1);
    } else if (pid == 0) {
        char *argv[] = {"sh", "-c", (char *)command, NULL};
        execvp("sh", argv);

        puts("Spawned process quited.");fflush(stdout);
        // If execvp returns, it must have failed
        exit(-1);
    } else {
        return 0;
    }
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

#define IP_LEN 64
#define PORT_LEN 20
#define DATA_LEN 10240

int connfd, srcfd, dstfd;

char src_ip[IP_LEN], dst_ip[IP_LEN], backup_ip[IP_LEN], vm_ip[IP_LEN];
char migration_port[PORT_LEN], src_control_port[PORT_LEN], dst_control_port[PORT_LEN], backup_control_port[PORT_LEN];
char buff[DATA_LEN];
struct timespec start, end;
char bench_script[256], cpu_num[256], memory_size[256], output_file[256];

char startString[15] = "start migration";
char endString[15] =   "ended migration";



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


// ------------------------------ QEMU's migration control ------------------------------

void qemu_src_main() {
    printf("Hello from the source!\n");
    FILE *pid_file = fopen("./src_controller.pid", "w");
    if (pid_file) {
        fprintf(pid_file, "%d", getpid());
        fclose(pid_file);
    }

    char instr[2048];
    sprintf(instr, "sudo bash scripts/src_boot.sh %s %s %s > %s", bench_script, cpu_num, memory_size, output_file);
    execute_wrapper_process(instr);

    // build connection with `backup`.
    printf("addr:  %s:%s\n", src_ip, src_control_port);
    connfd = listen_wrapper(src_ip, src_control_port);

    bzero(buff, DATA_LEN);
    read_from_file(connfd, sizeof("qemu_migrate"), buff);
    assert(strcmp(buff, "qemu_migrate") == 0);

    sprintf(instr, "echo \"migrate -d tcp:%s:%s\" | sudo socat stdio unix-connect:qemu-monitor-migration-src", dst_ip, migration_port);
    assert(execute_wrapper(instr) == 0);
}

void signal_handler_dst(int signal) {
    if (signal == SIGUSR1) {
        printf("dst: Received SIGUSR1 signal\n");
        uint32_t write_len = write(connfd, endString, sizeof(endString));
        assert(write_len == sizeof(endString));
    }
}
void qemu_dst_main() {
    printf("Hello from the dest!\n");
    FILE *pid_file = fopen("./dst_controller.pid", "w");
    if (pid_file) {
        fprintf(pid_file, "%d", getpid());
        fclose(pid_file);
    }

    // start dest VM.
    char instr[2048];
    sprintf(instr, "sudo bash scripts/dst_boot.sh %s %s > %s", cpu_num, memory_size, output_file);
    puts("Starting dest VM!"); fflush(stdout);
    printf("%s\n", instr);
    execute_wrapper_process(instr);

    // migrate_incoming tcp:10.10.1.1:4444
    sprintf(instr, "echo \"migrate_incoming tcp:%s:%s\" | sudo socat stdio unix-connect:qemu-monitor-migration-dst", dst_ip, migration_port);
    printf("%s\n", instr);
    while (1) {
        int ret = execute_wrapper(instr);
        if (!ret) break;
    }

    // build connection with `backup`.
    printf("addr:  %s:%s\n", dst_ip, dst_control_port);
    connfd = listen_wrapper(dst_ip, dst_control_port);

    // if (signal(SIGUSR1, signal_handler_dst) == SIG_ERR) {
    //     printf("An error occurred while setting a signal handler.\n"); fflush(stdout);
    //     exit(-1);
    // }

    while (1) {
        sleep(1);
    }
}


void qemu_backup_main() {
    printf("Hello from the backup!\n");
    printf("src:  %s:%s\n", src_ip, src_control_port);
    printf("dst:  %s:%s\n", dst_ip, dst_control_port);fflush(stdout);
    srcfd = connect_wrapper(src_ip, src_control_port);
    dstfd = connect_wrapper(dst_ip, dst_control_port);
    char data[100] = {0};

    write_to_file(srcfd, "qemu_migrate");
}


// ------------------------------ Shared memory migration control. ------------------------------

void signal_handler_shm_src(int signal) {
    if (signal == SIGUSR1) {
        // printf("shm_src: VM image is complete!\n");
        write_to_file(connfd, "complete_vm_image");
        clock_gettime(CLOCK_MONOTONIC, &end);
        execute_wrapper("echo \"q\" | sudo socat stdio unix-connect:qemu-monitor-migration-src");
        printf("\n\nsrc complete VM image durtion: %lld ns\n", end.tv_sec * 1000000000LL + end.tv_nsec - start.tv_sec * 1000000000LL - start.tv_nsec);
        puts("Quit src.");
        exit(-1);
    }
}
void shm_src_main() {
    printf("Hello from the shm_source!\n");
    FILE *pid_file = fopen("./src_controller.pid", "w");
    if (pid_file) {
        fprintf(pid_file, "%d", getpid());
        fclose(pid_file);
    }

    execute_wrapper("sudo rm /dev/shm/my_shared_memory");

    char instr[2048];
    sprintf(instr, "sudo bash scripts/src_boot.sh %s %s %s > %s", bench_script, cpu_num, memory_size, output_file);
    execute_wrapper_process(instr);

    // build connection with `backup`.
    printf("addr:  %s:%s\n", src_ip, src_control_port);
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
    int ret = execute_wrapper("echo \"shm_migrate_switchover\" | sudo socat stdio unix-connect:qemu-monitor-migration-src");
    printf("Connect result: %d\n", ret);fflush(stdout);

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
    printf("Hello from the shm_destination!\n");
    FILE *pid_file = fopen("./dst_controller.pid", "w");
    if (pid_file) {
        fprintf(pid_file, "%d", getpid());
        fclose(pid_file);
    }

    // build connection with `backup`.
    printf("addr:  %s:%s\n", dst_ip, dst_control_port);
    connfd = listen_wrapper(dst_ip, dst_control_port);

    if (signal(SIGUSR1, signal_handler_shm_dst) == SIG_ERR) {
        printf("An error occurred while setting a signal handler.\n"); fflush(stdout);
        exit(-1);
    }

    bzero(buff, DATA_LEN);

    read_from_file(connfd, sizeof("start_target_vm"), buff);
    assert(strcmp(buff, "start_target_vm") == 0);
    char instr[2048];
    sprintf(instr, "sudo bash scripts/dst_boot.sh %s %s > %s", cpu_num, memory_size, output_file);
    puts("Starting dest VM!"); fflush(stdout);
    execute_wrapper_process(instr);

    read_from_file(connfd, sizeof("shm_load_vm_image"), buff);
    assert(strcmp(buff, "shm_load_vm_image") == 0);
    clock_gettime(CLOCK_MONOTONIC, &start);

    while (1) {
        int ret = execute_wrapper("echo \"migrate_incoming_shm /my_shared_memory 10\" | sudo socat stdio unix-connect:qemu-monitor-migration-dst");
        if (!ret) break;
        // usleep(10000);
    }

    while (1) {
        sleep(1);
    }
}

// simulate a control plane.
void shm_backup_main() {
    printf("Hello from the shm_backup!\n");
    printf("src:  %s:%s\n", src_ip, src_control_port);
    printf("dst:  %s:%s\n", dst_ip, dst_control_port);fflush(stdout);
    srcfd = connect_wrapper(src_ip, src_control_port);
    dstfd = connect_wrapper(dst_ip, dst_control_port);
    char data[100] = {0};

    write_to_file(srcfd, "shm_migrate");
    // Zezhou: this is a little bit tricky.
    //    So if we don't know who is the destination, then we can't start the target VM.
    //    But can't we start a target VM in all machines? Because OS does lazy allocation.
    //    Actually there is no resouce allocated.
    sleep(1);
    write_to_file(dstfd, "start_target_vm");
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
    printf("Migration mode: %s\n", argv[1]);
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
            if (argc < 7) {
                printf("Usage: %s shm src\n", argv[0]);
                printf("          <benchmark script: e.g. `scripts/vm-boot/boot_vm.exp`> <# of vCPU: e.g. `4`>\n");
                printf("          <memory size: e.g. `8G`> <output file name: e.g. `vm_round1`>\n");
                return 1;
            }
            printf("benchmark script: %s\n", argv[3]);
            printf("# of vCPU: %s\n", argv[4]);
            printf("Memory size: %s\n", argv[5]);
            printf("Output file: %s\n", argv[6]);
            strcpy(bench_script, argv[3]);
            strcpy(cpu_num, argv[4]);
            strcpy(memory_size, argv[5]);
            strcpy(output_file, argv[6]);
            shm_src_main();
        } else if (strcmp(argv[2], "dst") == 0) {
            if (argc < 6) {
                printf("Usage: %s shm dst\n", argv[0]);
                printf("          <# of vCPU: e.g. `4`> <memory size: e.g. `8G`> <output file name: e.g. `vm_round1`>\n");
                return 1;
            }
            printf("# of vCPU: %s\n", argv[3]);
            printf("Memory size: %s\n", argv[4]);
            printf("Output file: %s\n", argv[5]);
            strcpy(cpu_num, argv[3]);
            strcpy(memory_size, argv[4]);
            strcpy(output_file, argv[5]);
            shm_dst_main();
        } else {
            assert(strcmp(argv[2], "backup") == 0);
            shm_backup_main();
        }
    } else {
        assert(strcmp(argv[1], "normal") == 0);
        if (strcmp(argv[2], "src") == 0) {
            if (argc < 7) {
                printf("Usage: %s shm src\n", argv[0]);
                printf("          <benchmark script: e.g. `scripts/vm-boot/boot_vm.exp`> <# of vCPU: e.g. `4`>\n");
                printf("          <memory size: e.g. `8G`> <output file name: e.g. `vm_round1`>\n");
                return 1;
            }
            printf("benchmark script: %s\n", argv[3]);
            printf("# of vCPU: %s\n", argv[4]);
            printf("Memory size: %s\n", argv[5]);
            printf("Output file: %s\n", argv[6]);
            strcpy(bench_script, argv[3]);
            strcpy(cpu_num, argv[4]);
            strcpy(memory_size, argv[5]);
            strcpy(output_file, argv[6]);
            qemu_src_main();
        } else if (strcmp(argv[2], "dst") == 0) {
            if (argc < 6) {
                printf("Usage: %s shm dst\n", argv[0]);
                printf("          <# of vCPU: e.g. `4`> <memory size: e.g. `8G`> <output file name: e.g. `vm_round1`>\n");
                return 1;
            }
            printf("# of vCPU: %s\n", argv[3]);
            printf("Memory size: %s\n", argv[4]);
            printf("Output file: %s\n", argv[5]);
            strcpy(cpu_num, argv[3]);
            strcpy(memory_size, argv[4]);
            strcpy(output_file, argv[5]);
            qemu_dst_main();
        } else {
            assert(strcmp(argv[2], "backup") == 0);
            qemu_backup_main();
        }
    }

    return 0;
}