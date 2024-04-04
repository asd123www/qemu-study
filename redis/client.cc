#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h> // for non-blocking
#include <unistd.h>
#include <iostream>
#include <random>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <strings.h>
#include <arpa/inet.h>
#include <fstream>

#define WARMUP 1000
#define TOTAL_ROUND 30000

const int KEY_LEN_MIN = 100;
const int KEY_LEN_MAX = 101;
const int VAL_LEN_MIN = 200;
const int VAL_LEN_MAX = 201;
const int MAX_LEN = 10000;



char set_buffer[MAX_LEN];
char get_buffer[MAX_LEN];
char buff[MAX_LEN];

long long ti[TOTAL_ROUND * 2 + 5];
long long lat[TOTAL_ROUND * 2 + 5];


int main() {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serv_addr;

    // Set server address
    bzero(&serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(12345); // Default Redis port
    serv_addr.sin_addr.s_addr = inet_addr("10.10.1.100"); // Set Redis server IP

    // Connect the socket to the server
    int ret = connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    if (ret < 0) {
        printf("connect failed!\n");
        exit(0);
    }


    int key_len = rand() % (KEY_LEN_MAX - KEY_LEN_MIN) + KEY_LEN_MIN;
    int val_len = rand() % (VAL_LEN_MAX - VAL_LEN_MIN) + VAL_LEN_MIN;
    std::string key(key_len, 'a');
    std::string value(val_len, 'b');
    std::string set_req = "*3\r\n$3\r\nSET\r\n$" + std::to_string(key.length()) + "\r\n" + key + "\r\n$" + std::to_string(value.length()) + "\r\n" + value + "\r\n";
    std::string get_req = "*2\r\n$3\r\nGET\r\n$" + std::to_string(key.length()) + "\r\n" + key + "\r\n";

    // set
    int set_len = set_req.length();
    std::copy(set_req.begin(), set_req.end(), set_buffer);
    set_buffer[set_len] = '\0';
    // get
    int get_len = get_req.length();
    char *get_buffer = new char[MAX_LEN];
    std::copy(get_req.begin(), get_req.end(), get_buffer);
    get_buffer[get_len] = '\0';


    struct timespec offset, start, end;
    clock_gettime(CLOCK_MONOTONIC, &offset);
    for (int i = 0; i < TOTAL_ROUND; ++i) {
        // set 
        clock_gettime(CLOCK_MONOTONIC, &start);
        uint32_t set_write_len = write(sockfd, set_buffer, set_len);
        if (set_write_len != set_len) {
            puts("Set wrong");
            fflush(stdout);
            exit(1);
        }
        uint32_t set_read_len = 0;
        while (1) {
            // '+OK\r\n'
            set_read_len += read(sockfd, buff, MAX_LEN);
            if (set_read_len == 5) break;   
        }
        clock_gettime(CLOCK_MONOTONIC, &end);
        if (i > WARMUP) {
            ti[++ti[0]] = start.tv_sec * 1000000000LL + start.tv_nsec;
            lat[++lat[0]] = end.tv_sec * 1000000000LL + end.tv_nsec - start.tv_sec * 1000000000LL - start.tv_nsec;
        }

        // get
        clock_gettime(CLOCK_MONOTONIC, &start);
        uint32_t get_write_len = write(sockfd, get_buffer, get_len);
        if (get_write_len != get_len) {
            puts("Get wrong");
            fflush(stdout);
            exit(1);
        }
        uint32_t get_read_len = 0;
        while (1) {
            // '$' + str(len(value)).encode('utf-8') + b'\r\n' + value.encode('utf-8') + b'\r\n'
            get_read_len += read(sockfd, buff, MAX_LEN);
            if (get_read_len == 1 + 3 + 2 + 2 + val_len) break;   
        }
        clock_gettime(CLOCK_MONOTONIC, &end);
        if (i > WARMUP) {
            ti[++ti[0]] = start.tv_sec * 1000000000LL + start.tv_nsec;
            lat[++lat[0]] = end.tv_sec * 1000000000LL + end.tv_nsec - start.tv_sec * 1000000000LL - start.tv_nsec;
        }
    }

    close(sockfd);

    std::ofstream file("example.txt");
    for (int i = 1; i <= ti[0]; ++i) file << ti[i] - offset.tv_sec * 1000000000LL - offset.tv_nsec << " " << lat[i] << std::endl;
    return 0;
}