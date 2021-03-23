#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>

#define FIFO_NAME "graph.fifo"
#define FIFO_BASE_NAME "v"
#define MAX_NAME 20
#define MAX_COMMAND 10
#define PRINT "print"
#define PRINT_LENGTH 5
#define ADD "add"
#define ADD_FULL_LENGTH 7
#define CONN "conn"
#define CONN_FULL_LENGTH 8
#define MSG_SIZE 3
#define MAX_MESSAGE 200

#define ERR(source) (perror(source),\
		     fprintf(stderr,"%s:%d %s\n",__FILE__,__LINE__,source),\
		     exit(EXIT_FAILURE))

volatile sig_atomic_t last_int_signal = 0;
volatile sig_atomic_t last_pipe_signal = 0; 

int sethandler( void (*f)(int), int sigNo) 
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_handler = f;
    if (-1==sigaction(sigNo, &act, NULL))
        return -1;
    return 0;
}

void sigchld_handler(int sig)
{
    pid_t pid;
    for(;;)
    {
        pid = waitpid(0, NULL, WNOHANG);
        if(pid == 0)
            return;
        if(pid < 0)
        {
            if(errno == ECHILD)
                return;
            ERR("waitpid");
        }
    }
}

void sig_int_handler()
{
    last_int_signal = 1;
}

void sig_pipe_handler()
{
    last_pipe_signal = 1;
}

void usage(char *name)
{
    fprintf(stderr, "USAGE: %s 1 <= n <= 10 - number of vertices\n", name);
    exit(EXIT_FAILURE);
}

void usage_menu(int n)
{
    printf("USAGE: print\n");
    printf("USAGE: add x y\n");
    printf("USAGE: conn x y\n");
    printf("USAGE: 0<=x<%d 0<=y<%d\n",n,n);
}

void kill_children_and_me(int n, int *fd_w)
{
    if(kill(0, SIGINT) < 0)
        ERR("kill");
    for(int i=0;i<n;i++)
        if(TEMP_FAILURE_RETRY(close(fd_w[i])))
            ERR("close");
    free(fd_w);
    while(wait(NULL) > 0);
    exit(EXIT_SUCCESS);
}

void read_from_fifo(char* fifo_name, int n, int *fd_w)
{
    int fifo;
    if(last_int_signal || last_pipe_signal)  
        kill_children_and_me(n, fd_w);  
    if((fifo = open(fifo_name, O_RDONLY)) < 0)
        if(errno != EINTR && errno != EPIPE)
            ERR("open");
    if(last_int_signal || last_pipe_signal)  
        kill_children_and_me(n, fd_w);

    char buffer[MAX_COMMAND];
    char graph_buffer[MSG_SIZE];
    ssize_t count;
    char x,y;
    do
    {
        if(last_int_signal || last_pipe_signal)         
            break;
        if((count = read(fifo, buffer, MAX_COMMAND)) < 0)
            if(errno != EINTR && errno != EPIPE)
                ERR("read");
        if(last_int_signal || last_pipe_signal)         
            break;
        if(count >= strlen(PRINT) && strncmp(buffer, PRINT, strlen(PRINT))==0)
        {
            printf("command: print\n");
            struct timespec t = {0, 50000000};
            char c = 'p';
            for(int i=0;i<n;i++)
            {
                if(TEMP_FAILURE_RETRY(write(fd_w[i], &c, 1)) < 0)
                    if(errno != EINTR && errno != EPIPE)
                        ERR("write");       
                nanosleep(&t, NULL);
            }
        }
        else if(count >= ADD_FULL_LENGTH && strncmp(buffer, ADD, strlen(ADD))==0 && buffer[strlen(ADD)] == ' ' && buffer[strlen(ADD)+2] == ' ')
        {
            x = buffer[strlen(ADD)+1];
            y = buffer[strlen(ADD)+3];
            if(x-'0' >= n || x-'0' < 0 || y-'0' >= n || y-'0' < 0)
            {
                usage_menu(n);
                continue;
            }
            printf("command: add %c %c\n", x, y);
            graph_buffer[0] = 'a';
            graph_buffer[1] = x;
            graph_buffer[2] = y;
            if(TEMP_FAILURE_RETRY(write(fd_w[x-'0'], graph_buffer, MSG_SIZE)) < 0)
                if(errno != EINTR && errno != EPIPE)
                    ERR("write");
        }
        else if(count >= CONN_FULL_LENGTH && strncmp(buffer, CONN, strlen(CONN))==0 && buffer[strlen(CONN)] == ' ' && buffer[strlen(CONN)+2] == ' ')
        {
            x = buffer[strlen(CONN)+1];
            y = buffer[strlen(CONN)+3];
            if(x-'0' >= n || x-'0' < 0 || y-'0' >= n || y-'0' < 0)
            {
                usage_menu(n);
                continue;
            }
            printf("command: conn %c %c\n", x, y);
            graph_buffer[0] = 'c';
            graph_buffer[1] = x;
            graph_buffer[2] = y;
            if(TEMP_FAILURE_RETRY(write(fd_w[x-'0'], graph_buffer, MSG_SIZE)) < 0)
                if(errno != EINTR && errno != EPIPE)
                    ERR("write");
        }
        else if(count > 0)
        {  
            usage_menu(n);            
        }

    }while(count > 0);
    if(TEMP_FAILURE_RETRY(close(fifo)))
        ERR("close");
    kill_children_and_me(n, fd_w);
}

void child_work(int n, int id, int fd_r, int *fd_w)
{
    static char from_prev = 'n';
    char buffer[MAX_MESSAGE];
    int count;
    char *is_visited = (char*)malloc(sizeof(char)*n);
    if(is_visited == NULL)
        ERR("malloc");
    int *connected_ids = (int*)malloc(sizeof(int)*n);
    if(connected_ids == NULL)
        ERR("malloc");
    for(int i=0;i<n;i++)
        connected_ids[i] = 0;
    for(;;)
    {
        char c = 0;
        if(last_int_signal == 1 || last_pipe_signal == 1)
            break;
        if(read(fd_r, &c, 1) < 0)
            if(errno != EINTR && errno != EPIPE)
                ERR("read");
        if(c == 'p')
        {
            char to_print[MAX_MESSAGE];
            sprintf(to_print, "Vertex %d is connected with vertices: \n", id);
            for(int i=0;i<n;i++)
                if(connected_ids[i])
                    sprintf(to_print+strlen(to_print), "%d\n", i);
            printf("%s",to_print);
        }
        else if(c == 'a')
        {
            if(TEMP_FAILURE_RETRY(read(fd_r, buffer, 2)) < 0)
                if(errno != EINTR && errno != EPIPE)
                    ERR("read");
            if(buffer[0] - '0' == id)
                connected_ids[buffer[1] - '0'] = 1;            
        }
        else if(c == 'c')
        {
            if(TEMP_FAILURE_RETRY(read(fd_r, buffer, 2)) < 0)
                if(errno != EINTR && errno != EPIPE)
                    ERR("read");
            if(buffer[0] - '0' == id && connected_ids[buffer[1] - '0'])
            {
                printf("There is connection between vertices %c - %c\n",buffer[0], buffer[1]);     
                continue;               
            }
            
            for(int i=0;i<n;i++)
                is_visited[i] = '0';
            is_visited[id] = '1';
            char from = buffer[0];
            char x = buffer[0];
            char y = buffer[1];
            char is_back = '0';
            buffer[0] = 's';
            buffer[1] = from;
            buffer[2] = x;
            buffer[3] = y;
            buffer[4] = is_back;
            from_prev = 'n';
            for(int i=0;i<n;i++)
                buffer[i+5] = is_visited[i];
            int next = -1;
            for(int i=0;i<n;i++)
                if(connected_ids[i] && is_visited[i] == '0')
                    next = i;
            if(next == -1)
                printf("There is no connection between vertices %c - %c\n",x, y);
            if(fd_w[next])
            {
                count = TEMP_FAILURE_RETRY(write(fd_w[next], buffer, n+5));
                if(count != n+5)
                {
                    if(TEMP_FAILURE_RETRY(close(fd_w[next])))
                        ERR("close");
                    fd_w[next] = 0;                   
                }                
            }
        }
        else if(c == 's') //searching for vertex
        {
            if(TEMP_FAILURE_RETRY(read(fd_r, buffer, n+4)) < 0)
                if(errno != EINTR && errno != EPIPE)
                    ERR("read");
            char from = buffer[0];
            char x = buffer[1];
            char y = buffer[2];
            char is_back = buffer[3];
            for(int i=0;i<n;i++)
               is_visited[i] = buffer[i+4];
            if(connected_ids[y - '0'])
            {
                printf("There is connection between vertices %c - %c\n",x, y);     
                continue;               
            }
            is_visited[id] = '1';
            if(is_back == '1')
            {
                if(from_prev == 'n')
                {
                    printf("There is no connection between vertices %c - %c\n",x, y);     
                    continue;
                }
                from = from_prev;
            }
            else
            {
                from_prev = from;
            }
            is_back = '0';
            int next = -1;
            for(int i=0;i<n;i++)
                if(connected_ids[i] && is_visited[i] == '0')
                    next = i;
            if(next == -1)
            {
                next = from - '0';
                is_back = '1';                
            }

            if(fd_w[next])
            {
                buffer[0] = 's';
                buffer[1] = id+'0';
                buffer[2] = x;
                buffer[3] = y;
                buffer[4] = is_back;
                for(int i=0;i<n;i++)
                    buffer[i+5] = is_visited[i];
                count = TEMP_FAILURE_RETRY(write(fd_w[next], buffer, n+5));
                if(count != n+5)
                {
                    if(TEMP_FAILURE_RETRY(close(fd_w[next])))
                        ERR("close");
                    fd_w[next] = 0;                   
                }                
            }
        }
    }
    free(connected_ids);
    free(is_visited);

    if(TEMP_FAILURE_RETRY(close(fd_r)))
        ERR("close");
    for(int i=0;i<n;i++)
        if(fd_w[i])
            if(TEMP_FAILURE_RETRY(close(fd_w[i])))
                ERR("close");    
    free(fd_w);
    exit(EXIT_SUCCESS);
}

int main(int argc, char** argv)
{
    if(argc != 2)
        usage(argv[0]);
    int n = atoi(argv[1]);
    if(n < 1 || n > 10)
        usage(argv[0]);

    if(sethandler(sigchld_handler, SIGCHLD))
        ERR("sethandler");
    if(sethandler(sig_int_handler, SIGINT))
        ERR("sethandler");
    if(sethandler(sig_pipe_handler, SIGPIPE))
        ERR("sethandler");

    pid_t pid;
    int pipe_fd[2];
    int *fd_w = (int*)malloc(n*sizeof(int));
    int *fd_r = (int*)malloc(n*sizeof(int));
    for(int i=0;i<n;i++)
    {
        if(pipe(pipe_fd) < 0)
            ERR("pipe");
        fd_r[i] = pipe_fd[0];
        fd_w[i] = pipe_fd[1];
    }

    for(int i=0;i<n;i++)
    {
        if((pid = fork()) < 0)
            ERR("fork");
        if(pid == 0)
        {
            for(int j=0;j<n;j++)
            {
                if(j == i)
                    continue;
                if(TEMP_FAILURE_RETRY(close(fd_r[j])))
                        ERR("close");
            }
            child_work(n, i, fd_r[i], fd_w);
            exit(EXIT_SUCCESS);
        }
    }
    //parent
    for(int i=0;i<n;i++)
    {
        if(TEMP_FAILURE_RETRY(close(fd_r[i])))
            ERR("close");
    }
    free(fd_r);

    if(mkfifo(FIFO_NAME, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP) < 0)
    {
        if(errno != EEXIST)
            ERR("mkfifo");
        if(unlink(FIFO_NAME) < 0)
            ERR("unlink");
        if(mkfifo(FIFO_NAME, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP) < 0)
            ERR("mkfifo");
    }
       
    read_from_fifo(FIFO_NAME, n, fd_w);

    return EXIT_SUCCESS;
}