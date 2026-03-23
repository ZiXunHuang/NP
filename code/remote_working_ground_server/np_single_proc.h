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

#define fd_infor_list_size 511 //equal to (1024-3)/2
#define max_input_len 15000
#define max_cmd_len 256
#define max_client_num 30
#define max_name_len 20
#define max_msg_len 1024

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
    int id;
    struct client_infor{
        int sockfd;
        int state;
        int *user_pipe;
        char name[max_name_len];
        struct sockaddr_in connection_infor;
        cross_pipe *cp_infor;
    } list[max_client_num+1];
} client_structure;

typedef struct env_variable {
    int user_num;
    char *env_name;
    char *env_value[max_client_num+1];
    struct env_variable *next;
} env_var;

void child_handler(int);
int connect_sock(int);
env_var *initial_env_var();
void set_env(env_var*, char*, char*, int, int);
env_var *create_env_var_node(int, char*, char*, int);
void renew_env_var(env_var*, int);
void delete_env_var(env_var*, int);
void print_welcome_msg(int);
void print_login_msg(client_structure*);
void print_logout_msg(client_structure*, char*);
void initial_client_infor(client_structure*, int*, int);
void update_client_infor(client_structure*, int*, int, struct sockaddr_in, int*);
void delete_client_infor(client_structure*, char*);
void initial_cross_pipe_infor(cross_pipe*);
int run_shell(client_structure*, env_var*);
void count_down_and_check(cross_pipe*);
int check_cmd(char*, client_structure*);
void string_split(char*, client_structure*, env_var*);
void env_setting(char **, int, int, env_var*);
void single_cmd(char***, char*, int, int, int, int, client_structure*);
void ordinary_pipe(char***, char*, int, int, int, int, int , client_structure*);
void close_and_connect_fd(int, int, int, int[], int, pid_t);
void connect_pipe_to_first_process(cross_pipe*, pid_t);
void close_cross_pipe_fd(cross_pipe*, int);
void update_cross_pipe_fd(cross_pipe*, int[], int);
int find_same_cross_pipe(cross_pipe*, int*, int);
void print_user_pipe_msg_handler(char*, int, int, client_structure*);
void print_user_pipe_msg(char*, int, int, int, client_structure*);
void print_unknown_cmd(char*);
void cmd_who(client_structure*);
void cmd_tell(char*, client_structure*);
void cmd_yell(char*, client_structure*);
void cmd_name(char*, client_structure*);
void broadcast(char*, client_structure*);
void cat_num_to_msg(char*, unsigned int);
char int_to_char(unsigned int);
