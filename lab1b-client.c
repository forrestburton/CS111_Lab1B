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
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <zlib.h>

struct termios normal_mode;
int compress_option = 0;
int sock_fd;
int stdin_error = 0;
void reset_terminal(void);

void setup_terminal_mode(void) {
    //save current mode so we can restore it at the end 
    int error_check = tcgetattr(0, &normal_mode);  
    if (error_check < 0) {
        fprintf(stderr, "Error saving up terminal mode: %s\n", strerror(errno));
        exit(1);
    }
    struct termios new_mode = normal_mode;
    atexit(reset_terminal);
    //setting terminal to non-cononical, non-echo mode(letters aren't echoed)
    new_mode.c_iflag = ISTRIP;
    new_mode.c_oflag = 0; 
    new_mode.c_lflag = 0;
    error_check = tcsetattr(0, TCSANOW, &new_mode);
    if (error_check < 0) {
        fprintf(stderr, "Error setting up new terminal mode: %s\n", strerror(errno));
        exit(1);
    }
}

void reset_terminal(void) {  //reset to original mode
    close(sock_fd);
    int error_check = tcsetattr(0, TCSANOW, &normal_mode);
    if (error_check < 0) {
        fprintf(stderr, "Error restoring terminal mode: %s\n", strerror(errno));
        exit(1);
    } 
    exit(0);
}

int read_from_socket(void) {
    int write_check;
    char buf[256];
    ssize_t ret2 = read(sock_fd, buf, sizeof(char)*256);
    if (ret2 == -1) {  
        fprintf(stderr, "Error reading from socket: %s\n", strerror(errno));
        exit(1);
    }
    else if (ret2 == 0) { //0 bytes were read in from server. This mean child process should exit
        return -1;
    }

    for (int i = 0; i < ret2; i++) {
        if (buf[i] == 0x4) {  //If a ^D is entered on the terminal, simply pass it through to the server like any other character
            write_check = write(1, "^D", 2*sizeof(char));
            if (write_check == -1) {  
                fprintf(stderr, "Error writing to standard output: %s\n", strerror(errno));
                exit(1);
            }     
        }
        else if (buf[i] == '\r' || buf[i] == '\n') {
            write_check = write(1, "\r\n", 2*sizeof(char));
            if (write_check == -1) {  
                fprintf(stderr, "Error writing to standard output: %s\n", strerror(errno));
                exit(1);
            }
        }
        else if (buf[i] == 0x3) { //if a ^C is entered on the terminal, simply pass it through to the server like any other character.
            write_check = write(1, "^C", 2*sizeof(char));
            if (write_check == -1) {  
                fprintf(stderr, "Error writing to standard output: %s\n", strerror(errno));
                exit(1);
            }
        }
        else {
            write_check = write(1, &buf[i], sizeof(char));
            if (write_check == -1) {  
                fprintf(stderr, "Error writing to standard output: %s\n", strerror(errno));
                exit(1);
            }
        }
    }
    return 0;
}

int establish_connection(char* host, unsigned int port_num) {
    struct sockaddr_in server_address; //for specifying port and address of server for socket
    struct hostent* server;
    //creating socket
    sock_fd = socket(AF_INET, SOCK_STREAM, 0); //(socket_family, type, protocol). AF_INET for IPv4. SOCK_STREAM for TCP. 0 for default settings
    if (sock_fd == -1) {
        fprintf(stderr, "Error with creating socket: %s\n", strerror(errno));
        exit(1);
    }

    //enter socket address info and port for server
    server_address.sin_family = AF_INET; //address family 
    server_address.sin_port = htons(port_num); //taking port number and converting from host byte order to newtwork byte order. We need to convert this since we are sending over network because they use different endians 
    server = gethostbyname(host); //get IP Address from host name
    if(server == NULL) {
        fprintf(stderr, "Error getting IP Address from host name: %s\n", strerror(errno));
        exit(1);
    }
    memcpy(&server_address.sin_addr.s_addr, server->h_addr, server->h_length); //memcpy IP address to server_address
    memset(server_address.sin_zero, '\0', sizeof(server_address.sin_zero));//pad with 0's

    //make proper connections of socket and addresses
    int error_check;
    error_check = connect(sock_fd, (struct sockaddr*)&server_address, sizeof(server_address));
    if(error_check) {
        fprintf(stderr, "Error connecting to server: %s\n", strerror(errno));
        exit(1);
    }
    return sock_fd;
}

int main(int argc, char *argv[]) {
    int c;
    int port_number = -1;
    char* file_name;
    while(1) {
        int option_index = 0;
        static struct option long_options[] = {
            {"port", required_argument, 0, 'p' },
            {"log", required_argument, 0, 'l' },
            {"compress", no_argument, 0, 'c' },
            {0,     0,             0, 0 }};
        c = getopt_long(argc, argv, "p:l:c", long_options, &option_index);
        if (c == -1) break;
        switch (c) {
            case 'p':
                port_number = atoi(optarg);
                break;
            case 'l':
                file_name = optarg;
                break;
            case 'c':
               compress_option = 1;
                break;
            default:
                printf("Incorrect usage: accepted options are: [--log=filename --port=portnum --compress]\n");
                exit(1);
        }
    }

    setup_terminal_mode();  //setup terminal to non-canonical, no echo mode

    if (port_number == -1) {
        fprintf(stderr, "Client error, specify port number\n");
        exit(1);
    }

    sock_fd = establish_connection("localhost", port_number); //don't need to error check here, I already do it in my function

    //Poll blocks and returns when: one set of file descriptors is ready for I/O or a specified time has passed 
    struct pollfd poll_event[2];
    poll_event[0].fd = 0;  //read from stdin
    poll_event[0].events = POLLIN + POLLHUP + POLLERR;

    poll_event[1].fd = sock_fd; //read in from socket
    poll_event[1].events = POLLIN + POLLHUP + POLLERR;

    int poll_val;
    int write_check;

    while(1) {
        poll_val = poll(poll_event, 2, -1);
        if (poll_val < 0) {
            fprintf(stderr, "Error polling: %s\n", strerror(errno));
            exit(1);
        }

        //Read input from stdin
        if (poll_event[0].revents & POLLIN) { 
            char buffer[256];
            ssize_t ret1 = read(0, buffer, sizeof(char)*256);  //parent and child each have copies of file descripters. In parent 0 still maps to stdin
            if (ret1 == -1) {  
                fprintf(stderr, "Error reading from standard input: %s\n", strerror(errno));
                exit(1);
            }
            
            for (int i = 0; i < ret1; i++) { //for each char we read in the buffer
                if (buffer[i] == 0x4) {  //If a ^D is entered on the terminal, simply pass it through to the server like any other character
                    write_check = write(1, "^D", 2*sizeof(char));
                    if (write_check == -1) {  
                        fprintf(stderr, "Error writing to standard output: %s\n", strerror(errno));
                        exit(1);
                    }     
                    write_check = write(sock_fd, "^D", 2*sizeof(char));
                    if (write_check == -1) {  
                        fprintf(stderr, "Error writing to standard output: %s\n", strerror(errno));
                        exit(1);
                    }    
                }
                else if (buffer[i] == '\r' || buffer[i] == '\n') {
                    write_check = write(1, "\r\n", 2*sizeof(char));
                    if (write_check == -1) {  
                        fprintf(stderr, "Error writing to standard output: %s\n", strerror(errno));
                        exit(1);
                    }
                    write_check = write(sock_fd, "\n", sizeof(char));  //write to socket
                    if (write_check == -1) {  
                        fprintf(stderr, "Error writing to socket: %s\n", strerror(errno));
                        exit(1);
                    }
                }
                else if (buffer[i] == 0x3) { //if a ^C is entered on the terminal, simply pass it through to the server like any other character.
                    write_check = write(1, "^C", 2*sizeof(char));
                    if (write_check == -1) {  
                        fprintf(stderr, "Error writing to standard output: %s\n", strerror(errno));
                        exit(1);
                    }
                    write_check = write(sock_fd, "^C", 2*sizeof(char));
                    if (write_check == -1) {  
                        fprintf(stderr, "Error writing to standard output: %s\n", strerror(errno));
                        exit(1);
                    }
                }
                else {
                    write_check = write(1, &buffer[i], sizeof(char));
                    if (write_check == -1) {  
                        fprintf(stderr, "Error writing to standard output: %s\n", strerror(errno));
                        exit(1);
                    }
                    write_check = write(sock_fd, &buffer[i], sizeof(char)); //write to socket
                    if (write_check == -1) {  
                        fprintf(stderr, "Error writing to socket: %s\n", strerror(errno));
                        exit(1);
                    }
                }
            }
        }

        //Read input from socket
        if (poll_event[1].revents & POLLIN) {
            if (read_from_socket() == -1) {
                break;
            }
        }

        if (poll_event[0].revents & (POLLHUP | POLLERR)) {
            if (read_from_socket() == -1) {
                stdin_error = 1;
                break;
            }
        }
        if (poll_event[1].revents & (POLLHUP | POLLERR)) {  //read every last byte from socket_fd, write to stdout, restore terminal and exit
            read_from_socket();
            fprintf(stderr, "socket input error: %s\n", strerror(errno));
            exit(1);
        }
    }
    
    if (stdin_error) {
        fprintf(stderr, "stdin error: %s\n", strerror(errno));
        exit(1);
    }
    exit(0);   
}