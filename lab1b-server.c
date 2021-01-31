//NAME: Forrest Burton
//EMAIL: burton.forrest10@gmail.com
//ID: 005324612

#include <termios.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <getopt.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/wait.h>
#include <poll.h>
#include <zlib.h>

#define BUF_SIZE 256

struct termios normal_mode;
int pipe_to_shell[2];  //to shell. pipe[0] is read end of pipe. pipe[1] is write end of pipe
int pipe_to_server[2];  //from shell
pid_t pid;
int sock_fd;
int compress_option;
int write_to_shell_closed = 0;

void reset(void);

void handle_sigpipe() {
    //Harvest the shell's completion status 
    reset();
    exit(0);
}

void reset(void) {  //reset to original mode
    int shell_status;
    //wait for shell to exit
    waitpid(pid, &shell_status, 0);
    if (shell_status == -1) {
        fprintf(stderr, "Error with child process terminating: %s\n", strerror(errno));
        exit(1);
    }

    //print exit message
    int signal = WTERMSIG(shell_status);
    int status = WEXITSTATUS(shell_status);
    fprintf(stderr, "SHELL EXIT SIGNAL=%d STATUS=%d \n", signal, status);
    close(sock_fd);
    exit(0);
}

int establish_connection(unsigned int port_num) {
    unsigned int sin_size;
    int fd;
    struct sockaddr_in current_address;
    struct sockaddr_in client_address;

    sock_fd = socket(AF_INET, SOCK_STREAM, 0); //IPv4, TCP, default
    if (sock_fd == -1) {
        fprintf(stderr, "Error with creating socket: %s\n", strerror(errno));
        exit(1);
    }

    //enter socket address info and port 
    current_address.sin_family = AF_INET; //address family (IPv4)
    current_address.sin_port = htons(port_num);  //portnumber I specify in command line
    current_address.sin_addr.s_addr = INADDR_ANY; //which address I'm execting to receive a connection from. INADDR_ANY means I can connect to any of the client's IP Addresses
    
    //padding
    memset(current_address.sin_zero, '\0', sizeof(current_address.sin_zero));
    
    //binding the socket to the server's IP Address and port number
    if(bind(sock_fd, (struct sockaddr *) &current_address, sizeof(struct sockaddr)) == -1) { //(socket, server IP address, size struct)
        fprintf(stderr, "Error binding socket to server: %s\n", strerror(errno));
        exit(1);
    }
    
    //puts socket into a passive state until connection is establihed
    if (listen(sock_fd, 5) == -1) { //(socket, max number of connections)
        fprintf(stderr, "Error listening for client connection: %s\n", strerror(errno));
        exit(1);
    } 
    
    sin_size = sizeof (struct sockaddr_in);
    //accept client's connection and store the IP address. accept will be called as many clients as there are trying to connect, in this case just 1
    fd = accept(sock_fd, (struct sockaddr*)&client_address, &sin_size);
    if (fd == -1) {
        fprintf(stderr, "Error accepting client connection: %s\n", strerror(errno));
        exit(1);
    }
    return fd;
}

int main(int argc, char *argv[]) {
    int c;
    int port_number = -1;
    while(1) {
        int option_index = 0;
        static struct option long_options[] = {
            {"port", required_argument, 0, 'p' },
            {"compress", no_argument, 0, 'c' },
            {0,     0,             0, 0 }};
        c = getopt_long(argc, argv, "p:c", long_options, &option_index);
        if (c == -1) break;
        switch (c) {
            case 'p':
                port_number = atoi(optarg);
                break;
            case 'c':
                compress_option = 1;
                break;
            default:
                printf("Incorrect usage: accepted options are: [--port=portnum --compress]\n");
                exit(1);
        }
    }

    if (port_number == -1) {
        fprintf(stderr, "Server error, specify port number\n");
        exit(1);
    }

    atexit(reset);
    
    sock_fd  = establish_connection(port_number);

    int ret1 = pipe(pipe_to_shell);  //parent->child (server->shell)
    int ret2 = pipe(pipe_to_server);  //child->parent (shell->server)
    if (ret1 == -1) {
        fprintf(stderr, "Error when piping parent->child: %s\n", strerror(errno));
        exit(1);
    }
    if (ret2 == -1) {
        fprintf(stderr, "Error when piping child->parent: %s\n", strerror(errno));
        exit(1);
    }
    
    signal(SIGPIPE, handle_sigpipe);

    //stdin is file descriptor 0, stdout is file descripter 1
    //Both parent and child have access to both ends of the pipe. This is how they communicate 
    pid = fork(); //create a child process from main
    if (pid == -1) {
        fprintf(stderr, "Error when forking main: %s\n", strerror(errno));
        exit(1);
    }
    else if (pid == 0) {  //child process will have return value of 0. Output is by default nondeterministic. We don't know order of execution so we need poll

        //close ends of pipe we aren't using
        if (close(pipe_to_shell[1]) == -1) { //close writing in pipe to shell 
            fprintf(stderr, "Error when closing file descriptor: %s\n", strerror(errno));
            exit(1);
        }  
        if (close(pipe_to_server[0]) == -1) { //close reading in pipe from shell
            fprintf(stderr, "Error when closing file descriptor: %s\n", strerror(errno));
            exit(1);
        }
        
        //dup creates a copy of the file descriptor oldfd, dup2 allows us to specify file descriptor to be used 
        if (dup2(pipe_to_shell[0], 0) == -1) { //read stdin to shell(child). so now fd 0 points to the read of the shell
            fprintf(stderr, "Error when copying file descriptor: %s\n", strerror(errno));
            exit(1);
        } 
        if (close(pipe_to_shell[0]) == -1) {  
            fprintf(stderr, "Error when closing file descriptor: %s\n", strerror(errno));
            exit(1);
        }  

        if (dup2(pipe_to_server[1], 1) == -1) { //write shell to stdout. fd 1 
            fprintf(stderr, "Error when copying file descriptor: %s\n", strerror(errno));
            exit(1);
        }
        if (dup2(pipe_to_server[1], 2) == -1) { //write shell to stderr. fd 2
            fprintf(stderr, "Error when copying file descriptor: %s\n", strerror(errno));
            exit(1);    
        }
        
        if (close(pipe_to_server[1]) == -1) { 
            fprintf(stderr, "Error when closing file descriptor: %s\n", strerror(errno));
            exit(1);
        }   

        int ret;
        char* args_exec[2];
        char shell_path[] = "/bin/bash";
        args_exec[0] = shell_path;
        args_exec[1] = NULL;
        ret = execvp("/bin/bash", args_exec);  //executing a new (shell) program: /bin/bash. exec(3) replaces current image process with new one
        if (ret == -1) {
            fprintf(stderr, "Error when executing execvp in child process: %s\n", strerror(errno));
            exit(1);
        }
    }
    else if (pid > 0) {  //parent process will have return value of > 0
        //close ends of pipe we aren't using
        if (close(pipe_to_shell[0]) == -1) { //closing read for to shell
            fprintf(stderr, "Error when closing file descriptor: %s\n", strerror(errno));
            exit(1);
        }  
        if (close(pipe_to_server[1]) == -1) { //closing write for from shell 
            fprintf(stderr, "Error when closing file descriptor: %s\n", strerror(errno));
            exit(1);
        }

        //Poll blocks and returns when: one set of file descriptors is ready for I/O or a specified time has passed 
        struct pollfd poll_event[2];
        poll_event[0].fd = sock_fd;  //socket
        poll_event[0].events = POLLIN + POLLHUP + POLLERR;

        poll_event[1].fd = pipe_to_server[0]; //read in from shell
        poll_event[1].events = POLLIN + POLLHUP + POLLERR;

        int poll_val;
        int write_check;

        while(1) {
            poll_val = poll(poll_event, 2, -1);
            if (poll_val < 0) {
                fprintf(stderr, "Error polling: %s\n", strerror(errno));
                exit(1);
            }
            //Read input from socket
            if (poll_event[0].revents & POLLIN) { 
                char buffer[256];
                ssize_t ret1 = read(sock_fd, buffer, sizeof(char)*256);  //parent and child each have copies of file descripters. In parent 0 still maps to stdin
                if (ret1 == -1) {  
                    fprintf(stderr, "Error reading from standard input: %s\n", strerror(errno));
                    exit(1);
                }

                if (compress_option) {  //decompression
                    char decompress_output[BUF_SIZE];

                    //1) initialize a compression stream
                    z_stream stream;
                    stream.zalloc = Z_NULL;  //set to Z_NULL for default routines
                    stream.zfree = Z_NULL;
                    stream.opaque = Z_NULL;
                    int z_result = inflateInit(&stream);  //(ptr to struct, default compression level)
                    if (z_result != Z_OK) {
                        fprintf(stderr, "Client error, failed to inflate stream for compression: %s\n", strerror(errno));
                        exit(1);
                    }

                    //2) use zlib to decompress data from original buf and put in output buf 
                    stream.avail_in = (uInt) ret1; //number of bytes read in
                    stream.next_in = (Bytef *) buffer;  //next input byte
                    stream.avail_out = BUF_SIZE; //remaining free space at next_out
                    stream.next_out = (Bytef *) decompress_output; //next output byte
                    
                    int inflate_ret;
                    while (stream.avail_in > 0) {
                        inflate_ret = inflate(&stream, Z_SYNC_FLUSH); //Z_SYNC_FLUSH for independent messages
                        if (inflate_ret == -1 ) {
                            fprintf(stderr, "Error inflating: %s\n", strerror(errno));
                            exit(1);
                        }
                    }

                    int bytes_decompressed = BUF_SIZE - stream.avail_out;

                    //3) write decrompressed output to stdout 
                    for (int i = 0; i < bytes_decompressed; i++) { //for each char we read in the buffer
                        if (decompress_output[i] == 0x4) {   
                            if (!write_to_shell_closed) {
                                if (close(pipe_to_shell[1]) == -1) { //close writing in pipe to shell 
                                    fprintf(stderr, "Error when closing file descriptor: %s\n", strerror(errno));
                                    exit(1);
                                }
                                write_to_shell_closed = 1;
                            }
                        }
                        else if (decompress_output[i] == '\r' || decompress_output[i] == '\n') {
                            write_check = write(pipe_to_shell[1], "\n", sizeof(char));
                            if (write_check == -1) {  
                                fprintf(stderr, "Error writing to standard output: %s\n", strerror(errno));
                                exit(1);
                            }
                        }
                        else if (decompress_output[i] == 0x3) {
                            kill(pid, SIGINT); //kill child process
                        }
                        else {
                            write_check = write(pipe_to_shell[1], &decompress_output[i], sizeof(char)); //write to shell 
                            if (write_check == -1) {  
                                fprintf(stderr, "Error writing to shell Line 295: %s\n", strerror(errno));
                                exit(1);
                            }
                        }
                    }
                    //4) close compression stream
                    inflateEnd(&stream);
                }
                else {
                    for (int i = 0; i < ret1; i++) { //for each char we read in the buffer
                        if (buffer[i] == 0x4) {   
                            if (!write_to_shell_closed) {
                                if (close(pipe_to_shell[1]) == -1) { //close writing in pipe to shell 
                                    fprintf(stderr, "Error when closing file descriptor: %s\n", strerror(errno));
                                    exit(1);
                                }
                                write_to_shell_closed = 1;
                            }
                        }
                        else if (buffer[i] == '\r' || buffer[i] == '\n') {
                            write_check = write(pipe_to_shell[1], "\n", sizeof(char));
                            if (write_check == -1) {  
                                fprintf(stderr, "Error writing to standard output: %s\n", strerror(errno));
                                exit(1);
                            }
                        }
                        else if (buffer[i] == 0x3) {
                            kill(pid, SIGINT); //kill child process
                        }
                        else {
                            write_check = write(pipe_to_shell[1], &buffer[i], sizeof(char)); //write to shell 
                            if (write_check == -1) {  
                                fprintf(stderr, "Error writing to shell: %s\n", strerror(errno));
                                exit(1);
                            }
                        }
                    }
                }
            }

            //Read input from shell
            if (poll_event[1].revents & POLLIN) {
                char buf[256];
                ssize_t ret2 = read(pipe_to_server[0], buf, sizeof(char)*256);
                if (ret2 == -1) {  
                    fprintf(stderr, "Error reading from shell: %s\n", strerror(errno));
                    exit(1);
                }
                if (compress_option) { //compression
                    char compress_output[BUF_SIZE];

                    //1) initialize a compression stream
                    z_stream stream;
                    stream.zalloc = Z_NULL;  //set to Z_NULL for default routines
                    stream.zfree = Z_NULL;
                    stream.opaque = Z_NULL;
                    int z_result = deflateInit(&stream, Z_DEFAULT_COMPRESSION);  //(ptr to struct, default compression level)
                    if (z_result != Z_OK) {
                        fprintf(stderr, "Client error, failed to deflate stream for compression: %s\n", strerror(errno));
                        exit(1);
                    }

                    //2) use zlib to compress data from original buf and put in output buf 
                    stream.avail_in = ret2; //number of bytes read in
                    stream.next_in = (Bytef *) buf;  //next input byte
                    stream.avail_out = BUF_SIZE; //remaining free space at next_out
                    stream.next_out = (Bytef *) compress_output; //next output byte
                    
                    int deflate_ret;
                    while (stream.avail_in > 0) {
                        deflate_ret = deflate(&stream, Z_SYNC_FLUSH); //Z_SYNC_FLUSH for independent messages
                        if (deflate_ret == -1 ) {
                            fprintf(stderr, "Error deflating: %s\n", strerror(errno));
                            exit(1);
                        }
                    }

                    int bytes_compressed = BUF_SIZE - stream.avail_out;

                    //3) send output buf to client
                    if (write(sock_fd, compress_output, bytes_compressed) == -1) {
                        fprintf(stderr, "Client error writing compressed output: %s\n", strerror(errno));
                        exit(1);
                    }

                    //4) close compression stream
                    deflateEnd(&stream);
                }
                else {
                    for (int i = 0; i < ret2; i++) {
                        write_check = write(sock_fd, &buf[i], sizeof(char));
                        if (write_check == -1) {  
                            fprintf(stderr, "Error writing to standard output 6: %s\n", strerror(errno));
                            exit(1);
                        }    
                    }
                }
            }

            if (poll_event[0].revents & (POLLHUP | POLLERR)) {
                if (!write_to_shell_closed) {
                    if (close(pipe_to_shell[1]) == -1) { //close writing in pipe to shell 
                        fprintf(stderr, "Error when closing file descriptor: %s\n", strerror(errno));
                        exit(1);
                    }
                    write_to_shell_closed = 1;
                }
                fprintf(stderr, "stdin error: %s\n", strerror(errno));
                exit(1);
            }
            if (poll_event[1].revents & (POLLHUP | POLLERR)) {  //read every last byte from from_shell[0], write to socket_fd, get the exit status of the process and report to stderr.
                char buf[256];
                ssize_t ret2 = read(pipe_to_server[0], buf, sizeof(char)*256);
                if (ret2 == -1) {  
                    fprintf(stderr, "Error reading from shell: %s\n", strerror(errno));
                    exit(1);
                }
                for (int i = 0; i < ret2; i++) {
                    write_check = write(sock_fd, &buf[i], sizeof(char));
                    if (write_check == -1) {  
                        fprintf(stderr, "Error writing to standard output 6: %s\n", strerror(errno));
                        exit(1);
                    }    
                }
                exit(1);
            }
        } 
    }
    exit(0);  
}
