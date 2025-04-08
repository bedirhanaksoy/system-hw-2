#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <errno.h>

#define FIFO1 "/tmp/fifo1"
#define FIFO2 "/tmp/fifo2"
#define LOG_FILE "/tmp/syswh2_daemon_log.txt"
#define TIMEOUT 30  // seconds

int child_exit_count = 0;
pid_t child_pids[2];
int child_exit_status[2] = {-1, -1};

FILE *log_file;

// logging function to write messages to the log file
void log_message(const char *message) {
    time_t now = time(NULL);
    char *timestamp = ctime(&now);
    timestamp[strlen(timestamp)-1] = '\0'; // remove newline
    fprintf(log_file, "[%s] %s\n", timestamp, message);
    fflush(log_file);
}

// signal handler for daemon termination and reconfiguration
void daemon_signal_handler(int sig) {
    if (sig == SIGTERM) {
        log_message("Daemon received SIGTERM, exiting.");
        fclose(log_file);
        unlink(FIFO1);
        unlink(FIFO2);
        exit(0);
    } else if (sig == SIGHUP) {
        log_message("Daemon received SIGHUP.");
    }
}

void handle_sigchld(int sig) {
    int status;
    pid_t pid;
    // uses a while loop to handle all terminated children without blocking
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        char msg[128];
        int exit_status = -1; // temporary variable to store exit status or signal
        // checks if the child exited normally and get its exit status
        if (WIFEXITED(status)) {
            exit_status = WEXITSTATUS(status);
            snprintf(msg, sizeof(msg), "Child %d exited with status %d", pid, exit_status);
        } 
        // checks if the child was terminated by a signal and get the signal number
        else if (WIFSIGNALED(status)) {
            exit_status = WTERMSIG(status);
            snprintf(msg, sizeof(msg), "Child %d terminated by signal %d", pid, exit_status);
        }
        // updates the global child_exit_status array with the child's status
        for (int i = 0; i < 2; i++) {
            if (child_pids[i] == pid) {
                child_exit_status[i] = exit_status; // stores the processed exit status or signal
                break;
            }
        }
        // logs the child's termination status
        log_message(msg);
        // increments the count of exited children
        child_exit_count++;
    }
}

void become_daemon() {
    pid_t pid = fork();
    if (pid < 0) exit(EXIT_FAILURE); // Error in fork
    if (pid > 0) exit(EXIT_SUCCESS); // Parent exits

    // create a new session and become the session leader
    if (setsid() < 0) exit(EXIT_FAILURE);

    // set the signal handlers for the daemon
    signal(SIGTERM, daemon_signal_handler);
    signal(SIGHUP, daemon_signal_handler);

    // prevent acquiring a controlling terminal for the daemon process to avoid being killed by a terminal signal
    pid = fork();

    // error in fork
    if (pid < 0) exit(EXIT_FAILURE);

    // parent exits
    if (pid > 0) exit(EXIT_SUCCESS);

    // re-set SIGCHLD handler after the second fork
    signal(SIGCHLD, handle_sigchld);

    // change working directory to root
    chdir("/");

    // clear file mode creation mask 
    umask(0);

    // close inherited file descriptors
    int log_fd = fileno(log_file); // Get the file descriptor of log_file
    for (int i = 0; i < sysconf(_SC_OPEN_MAX); i++) {
        if (i != log_fd) close(i); // Skip closing the log file descriptor
    }

    // reopen stdin, stdout, stderr to /dev/null or log files
    int null_fd = open("/dev/null", O_RDWR);
    dup2(null_fd, STDIN_FILENO);
    dup2(log_fd, STDOUT_FILENO); // Redirect stdout to log_file
    dup2(log_fd, STDERR_FILENO); // Redirect stderr to log_file
    close(null_fd);              // Close the extra /dev/null descriptor

}

int main(int argc, char *argv[]) {
    // check the argument count
    if (argc != 3) {                                                                    
        fprintf(stderr, "Usage: %s <int1> <int2>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // read the integers from arguments
    int num1 = atoi(argv[1]);                                                            
    int num2 = atoi(argv[2]);                                                           

    // open the log file
    log_file = fopen(LOG_FILE, "a");                                                    
    if (!log_file) {
        perror("Failed to open log file");
        exit(EXIT_FAILURE);
    }

    // log the start of the program and the creation of fifos
    log_message("Program started."); 
    log_message("Creating FIFOs...");                                                   
    
    // create fifos
    mkfifo(FIFO1, 0666);                                                                
    mkfifo(FIFO2, 0666);                                                                

    // become a daemon program and log the start of the daemon
    become_daemon();
    log_message("Daemon started.");
    
    // set up signal handler for child termination
    signal(SIGCHLD, handle_sigchld);                                                    

    // child process 1
    // fork the first child process
    child_pids[0] = fork();
    // check if fork was successful
    if (child_pids[0] == 0) {
        // sleep for 5 seconds to simulate work and allow parent to write fifo first
        sleep(5);

        // open the first fifo for reading and read the two integers from the fifo with non-blocking flag
        int fd1 = open(FIFO1, O_RDONLY | O_NONBLOCK);
        
        // read the integers from the fifo with using usleep to avoid busy waiting (its kind of a busy wait but with a sleep)
        int a = 0, b = 0;
        int read_bytes = 0;
        while ((read_bytes = read(fd1, &a, sizeof(int))) <= 0) usleep(100);
        while ((read_bytes = read(fd1, &b, sizeof(int))) <= 0) usleep(100);

        // close the first fifo
        close(fd1);

        // determine the maximum of the two integers
        int max = (a > b) ? a : b;

        // write the maximum to the second fifo
        int fd2 = open(FIFO2, O_WRONLY);
        write(fd2, &max, sizeof(int));

        // close the second fifo
        close(fd2);

        // exit status
        exit(10); 
    }

    // child process 2
    // fork the second child process
    child_pids[1] = fork();
    // check if fork was successful
    if (child_pids[1] == 0) {
        // sleep for 10 seconds to simulate work and allow parent to write first fifo and allow first child to write second fifo
        sleep(10);

        // open the second fifo for reading and read the maximum integer from the fifo with non-blocking flag
        int fd2 = open(FIFO2, O_RDONLY | O_NONBLOCK);
        
        // read the maximum integer from the fifo with using usleep to avoid busy waiting (its kind of a busy wait but with a sleep)
        int max;
        int read_bytes = 0;
        while ((read_bytes = read(fd2, &max, sizeof(int))) <= 0) usleep(100);

        // close the second fifo
        close(fd2);
        
        // print and log the maximum integer
        char msg[128];
        snprintf(msg, sizeof(msg), "The larger number is: %d", max);
        log_message(msg);

        // exit status
        exit(20); 
    }

    // parent process
    // open the first fifo for writing and write the two integers to the fifo
    int fd1 = open(FIFO1, O_WRONLY);
    write(fd1, &num1, sizeof(int));
    write(fd1, &num2, sizeof(int));

    // close the first fifo
    close(fd1);
    
    // initialize the start time for timeout
    time_t start_time = time(NULL);

    // monitor loop to check for child process termination
    while (child_exit_count < 2) {
        // print and proceeding message for every two seconds
        log_message("Daemon proceeding...");
        sleep(2);

        // check for child process termination, if the time is out or not
        if (time(NULL) - start_time > TIMEOUT) {
            // if timeout reached, kill the remaining child processes
            log_message("Timeout reached. Killing remaining child processes...");
            for (int i = 0; i < 2; i++) {
                // check if the child process is still running
                if (child_exit_status[i] == -1) {
                    kill(child_pids[i], SIGKILL);

                    // log the killing of the child process
                    log_message("Killed child due to timeout.");
                }
            }
            
            // log the exit status of the child processes
            char msg[128];
            for (int i = 0; i < 2; i++) {
                snprintf(msg, sizeof(msg), "Child PID %d exit status: %d", child_pids[i], WEXITSTATUS(child_exit_status[i]));
                log_message(msg);
            }

            // exit the loop if the timeout termination happens
            break;
        }
    }

    // log the end of the daemon program
    log_message("Daemon shutting down.");

    // close the log file and unlink the fifos
    fclose(log_file);
    unlink(FIFO1);
    unlink(FIFO2);

    return 0;
}