#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <semaphore.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <dirent.h>

#define fd_infor_list_size 511 //equal to (1024-3)/2
#define max_input_len 15000
#define max_cmd_len 256
#define max_client_num 30
#define max_name_len 20
#define max_msg_len 1024
#define fifo_dir "user_pipe/"

typedef struct {
    int output_point;
    int min_input_point;
    int data_num;
    int *close_fd_record;
    struct fd_infor{
        int count;
        int write_fd;
        int read_fd;
    } list[fd_infor_list_size];
} cross_pipe;

typedef struct {
    pid_t pid;
    int state;
    char name[max_name_len];
    char mailbox[max_msg_len];
    int from_id;
    int user_pipe[max_client_num+1];
    struct sockaddr_in connection_infor;
    sem_t lock;
} client_infor;

typedef struct {
    int client_num;
    int min_id;
    sem_t lock;
    sem_t lock2;
} connection_state;

typedef struct {
    int count;
    sem_t lock;
} fifo_information;

int id = 0;
int sockfd;
int cli_infor_shmid;
int con_state_shmid;
int fifo_infor_shmid;
int stopped = 0;
client_infor *client_list;
connection_state *connect_state;
fifo_information *fifo_infor;

int connect_sock(int);
void create_shared_mem();
void ctrl_c_handler(int);
void child_handler(int);
void update_client_infor(int, struct sockaddr_in);
void print_welcome_msg();
void print_login_msg();
void delete_client_infor(char*);
void print_logout_msg(char*);
void run_shell();
void initial_fd_infor_list(cross_pipe*);
void count_down_and_check(cross_pipe*);
int check_cmd(char*);
void string_split(char*, cross_pipe*);
void env_setting(char **);
void single_cmd(char***, char*, int, cross_pipe*, int, int, int);
void ordinary_pipe(char***, char*, int, int, cross_pipe*, int, int, int);
void close_and_connect_fd(int, int, int, int[], int, pid_t);
void connect_pipe_to_first_process(cross_pipe*, pid_t);
void close_cross_pipe_fd(cross_pipe*, int);
void update_cross_pipe_fd(cross_pipe*, int[], int);
int find_same_cross_pipe(cross_pipe*, int*, int);
void print_user_pipe_msg_handler(char*, int, int);
void print_user_pipe_msg(char*, int, int, int);
int create_user_pipe(int);
void user_pipe_handler(int);
void print_unknown_cmd(char *);
void cmd_who();
void cmd_tell(char*);
void cmd_yell(char*);
void cmd_name(char*);
void broadcast(char*);
void msg_handler(int);
void cat_num_to_msg(char*, unsigned int);
char int_to_char(unsigned int);
