#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define GIGA 1000000000L


// Structure to represent a job message
typedef struct __attribute__((__packed__)) {
    uint32_t client_id;       // ID of the client sending the job (process ID)
    uint16_t job_id;          // Number of the job in that client (starts at 0)
    uint32_t execution_time;  // Execution time (in nanoseconds) of the job
} job_message;

double randexp(double lambda) {
    /**
     * @brief Generate a random number from an exponential distribution with parameter lambda. 
     * @note This function supplied in the PA1 instructions.
     * @param lambda exponential distribution parameter.
     * @return a random number from an exponential distribution with parameter lambda.
     */
    double u = rand() / ((double)RAND_MAX + 1.0);
    return -log(1.0 - u) / lambda;
}

int send_job_message(int socket_fd, struct sockaddr_in *server_addr, uint32_t client_id, uint16_t job_id, uint32_t execution_time) {
    /**
     * @brief Send a job message to the server.
     * @param socket_fd the file descriptor of the UDP socket to send the job message to the server.
     * @param server_addr the address of the server to send the job message to.
     * @param client_id the ID of the client sending the job (process ID) to include in the job message.
     * @param job_id the number of the job in that client (starts at 0).
     * @param execution_time the execution time (in nanoseconds) of the job to send in the message.
     * @return 0 on success, non-zero on failure.
     */

    job_message job_message;
    job_message.client_id = htonl(client_id);
    job_message.job_id = htons(job_id);
    job_message.execution_time = htonl(execution_time);

    // Send the job message to the server
    ssize_t bytes_sent = sendto(socket_fd, &job_message, sizeof(job_message), 0, (struct sockaddr *)server_addr, sizeof(*server_addr));
    if (bytes_sent == -1) {
        perror("sendto");
        return 1;
    }
    
    return 0;
} 


int execute_all_jobs(int num_jobs, double lambda, double mu, int socket_fd, struct sockaddr_in *server_addr) {
    /**
     * @brief Generate and execute num_jobs jobs.
     * @param num_jobs the number of jobs to generate and execute.
     * @param lambda exponential distribution parameter for sleep time.
     * @param mu exponential distribution parameter for execution time.
     * @param socket_fd the file descriptor of the UDP socket to send job messages to the server.
     * @param server_addr the address of the server to send job messages to.
     * @return 0 on success, non-zero on failure.
     */

    double x, y;
    int sleep_error, send_error;
    uint32_t floor_x, floor_y, pid = (uint32_t) getpid();
    struct timespec sleep_time = {0, 0};
    
    for (int i = 0; i < num_jobs; i++) {
        // sample two exponential random variables
        x = randexp(lambda);
        y = randexp(mu);

        // convert x and y from milliseconds to nanoseconds and take the floor of the values.
        floor_x = (uint32_t) floor(x * 1e6);
        floor_y = (uint32_t) floor(y * 1e6);
        // Set the sleep time to x and sleep.
        sleep_time.tv_sec = floor_x / GIGA;
        sleep_time.tv_nsec = (long) (floor_x % GIGA);
        sleep_error = nanosleep(&sleep_time, NULL);
        if (sleep_error != 0) {
            perror("nanosleep");
            return 1;
        }
        // Send a job message to the server. Return if there is an error sending the message.
        if ((send_error = send_job_message(socket_fd, server_addr, pid, (uint16_t) i, floor_y))) {
            return send_error;
        }
        printf("%08x:%04x\t%d:%d\t%d\t%d\n", ntohl((server_addr->sin_addr).s_addr), ntohs(server_addr->sin_port), pid, (uint16_t) i, floor_x, floor_y);
    }
    return 0;
}



int main(int argc, char *argv[]) {
    int ip_error, port, num_jobs, seed, socket_fd, execute_status;
    struct sockaddr_in server_addr;
    double lambda, mu;
    struct in_addr ip;

    // Check for the correct number of command line arguments
    if (argc != 7) {
        fprintf(stderr,"Usage: client <ip> <port> <num_jobs> <seed> <lambda> <mu>\n");
        return 1;
    }

    // Validate and convert the IP address
    ip_error = inet_pton(AF_INET, argv[1], &ip);
    if (ip_error != 1) {
        if (ip_error == -1) {
            perror("inet_pton");
        } else {
            fprintf(stderr, "Invalid IP address format: %s\n", argv[1]);
        }
        return 1;
    }
    // Convert port, num_jobs, and seed from command line arguments to int:
    // TODO: Should we check for conversion errors here? Instructions say we can assume the arguments are valid.
    port = strtol(argv[2], NULL, 10);
    num_jobs = strtol(argv[3], NULL, 10);
    seed = strtol(argv[4], NULL, 10);
    // Convert lambda and mu from command line arguments to double:
    lambda = strtod(argv[5], NULL);
    mu = strtod(argv[6], NULL);
    // Set the random seed.
    srand(seed);
    // Create a UDP socket.
    socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd == -1) {
        perror("socket creation");
        return 1;
    }
    // Set up the server address structure.
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr = ip;

    // Generate and execute num_jobs jobs.
    execute_status = execute_all_jobs(num_jobs, lambda, mu, socket_fd, &server_addr);
    close(socket_fd);
    return execute_status;
}
