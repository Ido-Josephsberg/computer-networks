#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <time.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <threads.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <netinet/in.h>
#include <sys/socket.h>

#define GIGA 1000000000L

// Define the job queue and its head
TAILQ_HEAD(job_queue, job_node);

// Structure to represent a job message
typedef struct __attribute__((__packed__)) {
    uint32_t client_id;                 // ID of the client sending the job (process ID)
    uint16_t job_id;                    // Number of the job in that client (starts at 0)
    uint32_t execution_time;            // Execution time (in nanoseconds) of the job
} job_message;

// Structure to represent a job in the server's job queue
typedef struct job_node {
    uint32_t client_addr;               // IP address of the client sending the job
    uint16_t client_port;               // Port number of the client sending the job
    uint32_t client_id;                 // ID of the client
    uint16_t job_id;                    // Number of the job in that client
    uint32_t execution_time;            // Execution time (in nanoseconds) of the job
    long arrival_time;                  // Time when the job arrived at the server in nanoseconds since the server started
    TAILQ_ENTRY(job_node) entries;      // Pointers for the job queue
} job_node;

// Structure to represent a job queue and its associated mutex and condition variable
typedef struct {
    struct job_queue *job_queue_head;   // Head of the job queue
    int qsize;                          // Maximum size of the job queue
    int q_current_size;                 // Current size of the job queue
    long total_demand;                  // Total demand of all jobs that have arrived at the server
    struct timespec server_start_time;  // Time when the server started
    mtx_t job_queue_mutex;              // Mutex for the job queue
    cnd_t job_queue_cond;               // Condition variable for the job queue
    int done_accepting_jobs;            // Flag to indicate when the server is done accepting jobs
} job_queue_struct;

long timespec_diff(struct timespec *start, struct timespec *end) {
    /**
     * @brief Calculate the difference between two timespecs in nanoseconds.
     * @param start the starting time.
     * @param end the ending time.
     * @return the difference between the two timespecs in nanoseconds.
     */
    return (end->tv_sec - start->tv_sec) * GIGA + (end->tv_nsec - start->tv_nsec);
}

int init_job_queue(job_queue_struct *job_queue, struct job_queue *job_queue_head, struct timespec *server_start_time) {
    /**
     * @brief Initialize the job queue and its associated mutex and condition variable.
     * @param job_queue the job queue to initialize.
     * @param job_queue_head the head of the job queue.
     * @param server_start_time pointer to the time when the server started.
     * @return 0 on success, non-zero on failure.
     */
    // Initialize the job queue
    job_queue->job_queue_head = job_queue_head;
    TAILQ_INIT(job_queue->job_queue_head);
    job_queue->q_current_size = 0;
    job_queue->total_demand = 0;
    job_queue->server_start_time = *server_start_time;
    job_queue->done_accepting_jobs = 0;

    // Initialize the mutex and condition variable for the job queue
    if (mtx_init(&(job_queue->job_queue_mutex), mtx_plain) != thrd_success) {
        fprintf(stderr, "Failed to initialize mutex for job queue\n");
        return 1;
    }
    if (cnd_init(&(job_queue->job_queue_cond)) != thrd_success) {
        fprintf(stderr, "Failed to initialize condition variable for job queue\n");
        return 1;
    }
    return 0;
}

int worker_thread_function(void *job_queue) {
    /**
     * @brief Function to be run by the worker thread to process jobs from the job queue.
     * @param job_queue pointer to the job queue struct containing the job queue and its associated mutex and condition variable.
     * @return 0 on success, non-zero on failure.
     */
    int sleep_error, current_queue_size;
    long departure_time, total_demand;
    job_node *job_to_process;
    struct timespec *server_start_time, sleep_time, finish_job_time;
    // Cast job_queue to job_queue_struct pointer
    job_queue_struct *queue = (job_queue_struct *) job_queue;
    server_start_time = &(queue->server_start_time);
    // Loop until the server is done accepting jobs and the job queue is empty
    while (1) {
        // Wait for queue to be non-empty.
        mtx_lock(&(queue->job_queue_mutex));
        while (queue->q_current_size == 0 && !queue->done_accepting_jobs) {
            cnd_wait(&(queue->job_queue_cond), &(queue->job_queue_mutex));
        }
        // Exit if the server is done accepting jobs and the job queue is empty
        if (queue->done_accepting_jobs && queue->q_current_size == 0) {
                mtx_unlock(&(queue->job_queue_mutex));
                return 0;
        }

        // Pop the first job from the queue
        job_to_process = TAILQ_FIRST(queue->job_queue_head);
        TAILQ_REMOVE(queue->job_queue_head, job_to_process, entries);
        total_demand = queue->total_demand;
        queue->total_demand -= job_to_process->execution_time;
        (queue->q_current_size)--;
        current_queue_size = queue->q_current_size;
        mtx_unlock(&(queue->job_queue_mutex));
        // Simulate execution by sleeping
        sleep_time.tv_sec = (job_to_process->execution_time) / GIGA;
        sleep_time.tv_nsec = (job_to_process->execution_time) % GIGA;
        sleep_error = nanosleep(&sleep_time, NULL);
        if (sleep_error == -1) {
            perror("nanosleep");
            free(job_to_process);
            return 1;
        }
        if (clock_gettime(CLOCK_MONOTONIC, &finish_job_time) == -1) {
            perror("clock_gettime");
            free(job_to_process);
            return 1;
        }
        departure_time = timespec_diff(server_start_time, &finish_job_time);
        printf("%08x:%04x\t%d:%d\t%ld\t%ld\t%d\t%ld\n",job_to_process->client_addr, job_to_process->client_port, job_to_process->client_id, job_to_process->job_id, job_to_process->arrival_time, departure_time, current_queue_size, total_demand);
        free(job_to_process);
    }
    return 0;
}

int job_acceptor(int socket_fd, job_queue_struct *job_queue, int job_number) {
    /**
     * @brief Function to be run by the acceptor to receive job messages from clients and add them to the job queue.
     * @param socket_fd the file descriptor of the UDP socket to receive job messages from clients.
     * @param job_queue the job queue to add received jobs to.
     * @param job_number the total number of jobs to receive before stopping the acceptor.
     * @return 0 on success, non-zero on failure.
     */
    job_message msg;
    ssize_t bytes_received;
    job_node *new_job_node;
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    struct timespec *server_start_time, job_arrival_time;
    server_start_time = &(job_queue->server_start_time);
    for (int i = 0; i < job_number; i++) {
        // Receive a job message from a client
        bytes_received = recvfrom(socket_fd, &msg, sizeof(msg), 0, (struct sockaddr *)&client_addr, &client_addr_len);
        if (bytes_received == -1) {
            perror("recvfrom");
            return 1;
        }
        // Get job arrival time
        if (clock_gettime(CLOCK_MONOTONIC, &job_arrival_time) == -1) {
            perror("clock_gettime");
            return 1;
        }
        // Convert the fields of the received job message from network byte order to host byte order
        msg.client_id = ntohl(msg.client_id);
        msg.job_id = ntohs(msg.job_id);
        msg.execution_time = ntohl(msg.execution_time);

        // Create a new job node
        new_job_node = malloc(sizeof(job_node));
        if (new_job_node == NULL) {
            perror("malloc");
            return 1;
        }
        new_job_node->client_addr = ntohl(client_addr.sin_addr.s_addr);
        new_job_node->client_port = ntohs(client_addr.sin_port);
        new_job_node->client_id = msg.client_id;
        new_job_node->job_id = msg.job_id;
        new_job_node->execution_time = msg.execution_time;
        new_job_node->arrival_time = timespec_diff(server_start_time, &job_arrival_time);

        // Add the new job node to the job queue
        mtx_lock(&(job_queue->job_queue_mutex));
        if (job_queue->q_current_size < job_queue->qsize) {
            TAILQ_INSERT_TAIL(job_queue->job_queue_head, new_job_node, entries);
            (job_queue->q_current_size)++;
            job_queue->total_demand += new_job_node->execution_time;
            cnd_signal(&(job_queue->job_queue_cond));
        } else {
            // If the job queue is full, do not add the new job node but count it as accepted.
            free(new_job_node);
        }
        mtx_unlock(&(job_queue->job_queue_mutex));

    }
    mtx_lock(&(job_queue->job_queue_mutex));
    job_queue->done_accepting_jobs = 1;
    cnd_signal(&(job_queue->job_queue_cond));
    mtx_unlock(&(job_queue->job_queue_mutex));
    return 0;
}

void free_job_queue(job_queue_struct *job_queue) {
    /**
     * @brief Free all memory associated with the job queue.
     * @param job_queue the job queue to free.
     */
    job_node *next_job_node, *current_job_node = TAILQ_FIRST(job_queue->job_queue_head);

    while (current_job_node != NULL) {
        next_job_node = TAILQ_NEXT(current_job_node, entries);
        TAILQ_REMOVE(job_queue->job_queue_head, current_job_node, entries);
        free(current_job_node);
        current_job_node = next_job_node;
    }

    mtx_destroy(&(job_queue->job_queue_mutex));
    cnd_destroy(&(job_queue->job_queue_cond));
}


int main (int argc, char *argv[]) {
    int port, num_jobs, qsize, socket_fd, thread_create_status, job_acceptor_status, worker_return_status;
    struct timespec server_start_time;
    struct sockaddr_in server_addr;
    struct job_queue job_queue_head;
    job_queue_struct job_queue;
    thrd_t worker_thread;
    if (argc != 4) {
        fprintf(stderr, "Usage: server <port> <num_jobs> <qsize>\n");
        return 1;
    }
    // set the server start time to the current time
    if (clock_gettime(CLOCK_MONOTONIC, &server_start_time) == -1) {
        perror("clock_gettime");
        return 1;
    }
    // Convert port, num_jobs, and qsize from command line arguments to int:
    // TODO: Should we check for conversion errors here? Instructions say we can assume the arguments are valid.
    port = strtol(argv[1], NULL, 10);
    num_jobs = strtol(argv[2], NULL, 10);
    qsize = strtol(argv[3], NULL, 10);


    // Create a UDP socket.
    socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd == -1) {
        perror("socket creation");
        return 1;
    }
    // Set up the server address structure.
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((uint16_t) port);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind");
        close(socket_fd);
        return 1;
    }

    // Initialize the job queue and its associated mutex and condition variable
    job_queue.qsize = qsize;
    if (init_job_queue(&job_queue, &job_queue_head, &server_start_time) != 0) {
        close(socket_fd);
        return 1;
    }

    // Start a worker thread to process jobs from the job queue.
    if ((thread_create_status = thrd_create(&worker_thread, worker_thread_function, &job_queue)) != thrd_success) {
        if (thread_create_status == thrd_nomem) {
            fprintf(stderr, "Failed to create worker thread: not enough memory\n");
        } else {
            fprintf(stderr, "Failed to create worker thread\n");
        }
        close(socket_fd);
        free_job_queue(&job_queue);
        return 1;
    }

    // Start acceptor to receive jobs
    job_acceptor_status = job_acceptor(socket_fd, &job_queue, num_jobs);
    if (job_acceptor_status != 0) {
        free_job_queue(&job_queue);
        close(socket_fd);
        return job_acceptor_status;
    }
    // Wait for the worker thread to finish processing jobs
    if (thrd_join(worker_thread, &worker_return_status) != thrd_success) {
        fprintf(stderr, "Failed to join worker thread\n");
        free_job_queue(&job_queue);
        close(socket_fd);
        return 1;
    }
    if (worker_return_status != 0) {
        fprintf(stderr, "Worker thread returned with an error\n");
        free_job_queue(&job_queue);
        close(socket_fd);
        return worker_return_status;
    }
    free_job_queue(&job_queue);
    close(socket_fd);
    return 0;
}
