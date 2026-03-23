#include "np_single_proc.h"

int main(int argc, char const *argv[]) {
    int msockfd, port, total_fd_num = getdtablesize(), fd_id_list[total_fd_num], client_num = 0, min_id = 1, env_id = 0;
    struct sockaddr_in client_addr;
    fd_set read_fd_set;
    fd_set active_fd_set;
    client_structure client = {0};
    env_var *env_head = initial_env_var();
    if (!argv[1]) {
        fprintf(stderr, "Error: didn't get the port number\n");
        exit(0);
    }
    else {
        port = atoi(argv[1]);
    }
    msockfd = connect_sock(port);
    FD_ZERO(&active_fd_set);
    FD_SET(msockfd, &active_fd_set);
    signal(SIGCHLD, child_handler);
    initial_client_infor(&client, fd_id_list, total_fd_num);

    while (1) {
        memcpy(&read_fd_set, &active_fd_set, sizeof(read_fd_set));
        while (select(total_fd_num, &read_fd_set, NULL, NULL, NULL) < 0) {}
        if (FD_ISSET(msockfd, &read_fd_set) && client_num < max_client_num) {
            int client_len = sizeof(client_addr), ssockfd;
            ssockfd = accept(msockfd, (struct sockaddr*)&client_addr, &client_len);
            if (ssockfd < 0) {
                fprintf(stderr, "Error: accept error\n");
            }
            else {
                FD_SET(ssockfd, &active_fd_set);
                print_welcome_msg(ssockfd);
                set_env(env_head, "PATH", "bin:.", min_id, 0);
                update_client_infor(&client, fd_id_list, ssockfd, client_addr, &min_id);
                client_num++;
                print_login_msg(&client);
                write(ssockfd, "% ", strlen("% "));
            }
        }
        for (int fd = 0; fd < total_fd_num; fd++) {
            if (fd != msockfd && FD_ISSET(fd, &read_fd_set)) {
                client.id = fd_id_list[fd];
                if (client.id != env_id) {
                    env_id = client.id;
                    renew_env_var(env_head, env_id);
                }
                if (run_shell(&client, env_head) < 0) {
                    char name[20];
                    close(fd);
                    FD_CLR(fd, &active_fd_set);
                    delete_client_infor(&client, name);;
                    print_logout_msg(&client, name);
                    delete_env_var(env_head, env_id);
                    if (fd_id_list[fd] < min_id) {
                        min_id = fd_id_list[fd];
                    }
                    fd_id_list[fd] = -1;
                    client_num--;
                }
                else {
                    write(fd, "% ", strlen("% "));
                }
            }
        }
    }
    return 0;
}

void child_handler(int signo) {
    while (waitpid(-1, NULL, WNOHANG) > 0) {}
}

int connect_sock(int port) {
    struct sockaddr_in server_addr;
    int sockfd = socket(AF_INET, SOCK_STREAM, 0), reuse_enable = 1;
    if (sockfd < 0) {
        fprintf(stderr, "Error: can't open stream socket\n");
        exit(0);
    }
    //fill the server information
    bzero((char*)&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse_enable, sizeof(int)) < 0) {
        fprintf(stderr, "Error: can't set SO_REUSEADDR\n");
        exit(0);
    }
    //bind and listen
    if (bind(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        fprintf(stderr, "Error: can't bind local address\n");
        exit(0);
    }
    listen(sockfd, 50);
    return sockfd;
}

env_var *initial_env_var() {
    env_var *tmp = create_env_var_node(-1, NULL, NULL, -1);
    tmp->next = create_env_var_node(-1, "PATH", NULL, -1);
}

void set_env(env_var *head, char *name, char *value, int id, int overwrite) {
    env_var *pos = head->next, *pre = head;
    setenv(name, value, overwrite);
    while (pos) {
        if (!strcmp(name, pos->env_name)) {
            if (!pos->env_value[id]) {
                pos->env_value[id] = malloc(sizeof(char)*(strlen(value)+1));
                strcpy(pos->env_value[id], value);
                if (pos->user_num >= 0)
                    pos->user_num++;
            }
            else if (strcmp(pos->env_value[id], value)) {
                if (strlen(value) > strlen(pos->env_value[id])) {
                    free(pos->env_value[id]);
                    pos->env_value[id] = malloc(sizeof(char)*(strlen(value)+1));
                }
                strcpy(pos->env_value[id], value);
            }
            return ;
        }
        pos = pos->next;
        pre = pre->next;
    }
    pre->next = create_env_var_node(1, name, value, id);
}

env_var *create_env_var_node(int user_num, char *name, char *value, int id) {
    env_var *tmp;
    tmp = malloc(sizeof(env_var));
    tmp->user_num = user_num;
    if (name) {
        tmp->env_name = malloc(sizeof(char)*(strlen(name)+1));
        strcpy(tmp->env_name, name);
    }
    else
        tmp->env_name = NULL;
    for (int i = 0; i <= max_client_num; i++) {
        if (i == id) {
            tmp->env_value[i] = malloc(sizeof(char)*(strlen(value)+1));
            strcpy(tmp->env_value[i], value);
        }
        else {
            tmp->env_value[i] = NULL;
        }
    }
    tmp->next = NULL;
}

void renew_env_var(env_var *head, int id) {
    env_var *pos = head->next;
    while (pos) {
        if (!pos->env_value[id]) {
            setenv(pos->env_name, "", 1);
        }
        else {
            setenv(pos->env_name, pos->env_value[id], 1);
        }
        pos = pos->next;
    }
}

void delete_env_var(env_var *head, int id) {
    env_var *pos = head->next, *pre = head;
    while (pos) {
        if (pos->env_value[id]) {
            free(pos->env_value[id]);
            pos->env_value[id] = NULL;
            if (pos->user_num >= 0) {
                pos->user_num--;
            }
            if (!pos->user_num) {
                free(pos->env_name);
                pre->next = pos->next;
                free(pos);
            }
        }
        pos = pos->next;
        pre = pre->next;
    }
}

void print_welcome_msg(int sockfd) {
    char welcome_msg[130] = "";
    strcat(welcome_msg, "****************************************\n");
    strcat(welcome_msg, "** Welcome to the information server. **\n");
    strcat(welcome_msg, "****************************************\n");
    write(sockfd, welcome_msg, strlen(welcome_msg));
}

void print_login_msg(client_structure *client) {
    char msg[100] = "";
    strcat(msg, "*** User '");
    strcat(msg, client->list[client->id].name);
    strcat(msg, "' entered from ");
    strcat(msg, inet_ntoa(client->list[client->id].connection_infor.sin_addr));
    strcat(msg, ":");
    cat_num_to_msg(msg, ntohs(client->list[client->id].connection_infor.sin_port));
    strcat(msg, ". ***\n");
    broadcast(msg, client);
}

void print_logout_msg(client_structure *client, char *name) {
    char msg[50] = "";
    strcat(msg, "*** User '");
    strcat(msg, name);
    strcat(msg, "' left. ***\n");
    broadcast(msg, client);
}

void initial_client_infor(client_structure *client, int *fd_id_list, int total_fd_num) {
    for (int i = 0; i <= max_client_num; i++) {
        client->list[i].sockfd = -1;
        client->list[i].state = 0;
        client->list[i].cp_infor = NULL;
        client->list[i].user_pipe = NULL;
        strcpy(client->list[i].name, "");
    }
    for (int i = 0; i < total_fd_num; i++) {
        fd_id_list[i] = -1;
    }
}

void update_client_infor(client_structure *client, int *fd_id_list, int sockfd, struct sockaddr_in client_addr, int *min_id) {
    client->id = (*min_id);
    client->list[*min_id].sockfd = sockfd;
    client->list[*min_id].state = 1;
    strcpy(client->list[*min_id].name, "(no name)");
    client->list[*min_id].connection_infor = client_addr;
    client->list[*min_id].cp_infor = malloc(sizeof(cross_pipe));
    initial_cross_pipe_infor(client->list[*min_id].cp_infor);
    client->list[*min_id].user_pipe = malloc(sizeof(int)*(max_client_num+1));
    for (int i = 0; i <= max_client_num; i++) {
        client->list[*min_id].user_pipe[i] = -1;
    }
    fd_id_list[sockfd] = (*min_id);
    for (int i = 1; i <= max_client_num; i++) {
        if (!client->list[i].state) {
            (*min_id) = i;
            return ;
        }
    }
}

void delete_client_infor(client_structure *client, char *name) {
    client->list[client->id].sockfd = -1;
    client->list[client->id].state = 0;
    strcpy(name, client->list[client->id].name);
    strcpy(client->list[client->id].name, "");
    if (client->list[client->id].cp_infor->data_num) {
        close_cross_pipe_fd(client->list[client->id].cp_infor, -1);
        free(client->list[client->id].cp_infor->close_fd_record);
    }
    free(client->list[client->id].cp_infor);
    client->list[client->id].cp_infor = NULL;
    for (int i = 1; i <= max_client_num; i++) {
        if (client->list[client->id].user_pipe[i] >= 0) {
            close(client->list[client->id].user_pipe[i]);
        }
        if (client->list[i].user_pipe) {
            if (client->list[i].user_pipe[client->id] >= 0) {
                close(client->list[i].user_pipe[client->id]);
                client->list[i].user_pipe[client->id] = -1;
            }
        }
    }
    free(client->list[client->id].user_pipe);
    client->list[client->id].user_pipe = NULL;
}

void initial_cross_pipe_infor(cross_pipe *infor) {
    infor->output_point = -1;
    infor->min_input_point = 0;
    infor->data_num = 0;
    infor->close_fd_record = NULL;
    for (int i = 0; i < fd_infor_list_size; i++) {
        infor->list[i].count = -1; //count equal to -1 means no data in this data_block
        infor->list[i].write_fd = -1;
        infor->list[i].read_fd = -1;
    }
}

int run_shell(client_structure *client, env_var *env_head) {
    char cmd[max_input_len];
    memset(cmd, '\0', sizeof(cmd));
    while (read(client->list[client->id].sockfd, cmd, max_input_len) < 0) {}
    if ((int)cmd[0] == 4) //end of file
        return -1;
    if (strcmp(cmd, "\r\n") && strcmp(cmd, "\n")) {
        count_down_and_check(client->list[client->id].cp_infor);
        strtok(cmd, "\r\n");
        if (!strcmp(cmd, "exit")) {
            return -1;
        }
        else {
            if (check_cmd(cmd, client)) {
                return 0;
            }
            string_split(cmd, client, env_head);
        }
    }
    return 0;
}

void count_down_and_check(cross_pipe *infor) {
    int tmp[fd_infor_list_size];
    infor->data_num = 0;
    infor->output_point = -1;
    infor->close_fd_record = NULL;
    for (int i = 0; i < fd_infor_list_size; i++) {
        if (infor->list[i].count > 0) {
            infor->list[i].count--;
            if (!infor->list[i].count) { //get the data_block that should output data from pipe now
                infor->output_point = i;
                infor->list[i].count = -1;
            }
            else { //record the position of data_block that are still waiting to output in tmp memory
                tmp[infor->data_num] = i;
                infor->data_num += 1;
            }
        }
    }
    if (infor->data_num) { //copy the data from tmp memory
        infor->close_fd_record = malloc(sizeof(int)*infor->data_num);
        for (int i = 0; i < infor->data_num; i++) {
            infor->close_fd_record[i] = tmp[i];
        }
    }//renew minmum input point if output point is smaller than it
    if (infor->output_point >= 0 && infor->output_point < infor->min_input_point) {
        infor->min_input_point = infor->output_point;
    }
}

int check_cmd(char *cmd, client_structure *client) {
    if (!strstr(cmd, "tell") && !strstr(cmd, "yell")) {
        return 0;
    }
    else {
        char copy_str[max_input_len], *buf;
        strcpy(copy_str, cmd);
        buf = strtok(copy_str, " ");
        if (!strcmp(buf, "tell")) {
            cmd_tell(cmd, client);
        }
        else if (!strcmp(buf, "yell")) {
            cmd_yell(cmd, client);
        }
        else {
            return 0;
        }
    }
    return 1;
}

void string_split(char *all_cmd, client_structure *client, env_var *env_head) {
    int cmd_num = 0, count = 0, cross_num = 0, redir = 0, user_in_id = 0, user_out_id = 0, mode = 0;
    char **cmd_collection[max_input_len/4], *buf, cmd_reserve[max_input_len];
    cmd_collection[cmd_num] = malloc(sizeof(char*)*(max_cmd_len/2));
    strcpy(cmd_reserve, all_cmd);
    //command analysis
    buf = strtok(all_cmd, " ");
    while (buf != NULL) {
        if (!strcmp(buf, "|") || !strcmp(buf, ">")) {
            cmd_collection[cmd_num][count] = NULL;
            cmd_num++;
            cmd_collection[cmd_num] = malloc(sizeof(char*)*(max_cmd_len/2));
            count = 0;
            if (!strcmp(buf, "|")) {
                if (!mode) {
                    mode = 1;
                }
            }
            else {
                redir = 1;
            }
        }
        else {
            if (buf[0] == '|' || buf[0] == '!' || buf[0] == '<' || buf[0] == '>') {
                char num[strlen(buf)];
                for (int i = 1; i < strlen(buf); i++) {
                    num[i-1] = buf[i];
                }
                num[strlen(buf)-1] = '\0';
                if (buf[0] == '|' || buf[0] == '!') {
                    cross_num = atoi(num);
                    if (buf[0] == '!') {
                        cross_num *= -1; //error pipe
                    }
                }
                else if (buf[0] == '<') {
                    user_in_id = atoi(num);
                }
                else {
                    user_out_id = atoi(num);
                }
            }
            else {
                cmd_collection[cmd_num][count] = buf;
                count++;
            }
        }
        buf = strtok(NULL, " ");
    }
    cmd_collection[cmd_num][count] = NULL;
    //mission handler->mode 0:single command, mode 1:pipe
    if (!mode) {
        if (!strcmp(cmd_collection[0][0], "who") || !strcmp(cmd_collection[0][0], "name")) {
            if (!strcmp(cmd_collection[0][0], "who")) {
                cmd_who(client);
            }
            else {
                cmd_name(cmd_collection[0][1], client);
            }
        }
        else if (!strcmp(cmd_collection[0][0], "setenv") || !strcmp(cmd_collection[0][0], "printenv")) {
            env_setting(cmd_collection[0], client->list[client->id].sockfd, client->id, env_head);
        }
        else {
            single_cmd(cmd_collection, cmd_reserve, cross_num, redir, user_in_id, user_out_id, client);
        }
    }
    else {
        if (!redir) {
            ordinary_pipe(cmd_collection, cmd_reserve, cmd_num+1, cross_num, redir, user_in_id, user_out_id, client);
        }
        else {
            ordinary_pipe(cmd_collection, cmd_reserve, cmd_num, cross_num, redir, user_in_id, user_out_id, client);
        }
    }
    for (int i = 0; i <= cmd_num; i++) {
        free(cmd_collection[i]);
    }
    if (client->list[client->id].cp_infor->data_num) {
        free(client->list[client->id].cp_infor->close_fd_record);
    }
}

void env_setting(char **cmd, int sockfd, int id, env_var *head) {
    if (!strcmp(cmd[0], "setenv")) {
        set_env(head, cmd[1], cmd[2], id, 1);
    }
    else { //print env
        char *output = getenv(cmd[1]);
        if (output != NULL) {
            if (strcmp(output, "")) {
                write(sockfd, output, strlen(output));
                write(sockfd, "\n", strlen("\n"));
            }
        }
    }
}

void single_cmd(char ***cmd, char *cmd_reserve, int cross_num, int redir, int user_in_id, int user_out_id, client_structure *client) {
    int fd[2], pos, sockfd = client->list[client->id].sockfd;
    cross_pipe *infor = client->list[client->id].cp_infor;
    pid_t pid;
    if (user_in_id || user_out_id) {
        print_user_pipe_msg_handler(cmd_reserve, user_in_id, user_out_id, client);
    }
    if (cross_num) {
        if ((pos = find_same_cross_pipe(infor, fd, cross_num)) < 0) {
            while (pipe(fd) < 0) {}
        }
    }
    else if(user_out_id) {
        if (user_out_id <= max_client_num && client->list[user_out_id].state && client->list[user_out_id].user_pipe[client->id] < 0) {
            while (pipe(fd) < 0) {}
        }
    }
    while ((pid = fork()) < 0) {
        waitpid(-1, NULL, 0);
    }
    if (!pid) { //child
        if (infor->data_num) {
            close_cross_pipe_fd(infor, pos);
        }
        if (infor->output_point >= 0) {
            connect_pipe_to_first_process(infor, pid);
        }
        else if (user_in_id) {
            if (user_in_id > max_client_num || !client->list[user_in_id].state || client->list[client->id].user_pipe[user_in_id] < 0) {
                int dev_null = open("/dev/null", O_RDWR);
                if (dev_null == -1) {
                    write(sockfd, "Error: can't open file\n", strlen("Error: can't open file\n"));
                    exit(0);
                }
                dup2(dev_null, STDIN_FILENO);
                close(dev_null);
            }
            else {
                dup2(client->list[client->id].user_pipe[user_in_id], STDIN_FILENO);
                close(client->list[client->id].user_pipe[user_in_id]);
            }
        }
        else {
            dup2(sockfd, STDIN_FILENO);
        }
        if (cross_num) {
            if (cross_num < 0) {
                dup2(fd[1], STDERR_FILENO);
            }
            close_and_connect_fd(0, 0, 1, fd, -1, pid); //let current == first, current != last
        }
        else if (redir) {
            int fd_redir = open(cmd[1][0], O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
            if (fd_redir == -1) {
                write(sockfd, "Error: can't open file\n", strlen("Error: can't open file\n"));
                exit(0);
            }
            dup2(fd_redir, STDOUT_FILENO);
            close(fd_redir);
        }
        else if (user_out_id) {
            if (user_out_id > max_client_num || !client->list[user_out_id].state || client->list[user_out_id].user_pipe[client->id] >= 0) {
                int dev_null = open("/dev/null", O_RDWR);
                if (dev_null == -1) {
                    write(sockfd, "Error: can't open file\n", strlen("Error: can't open file\n"));
                    exit(0);
                }
                dup2(dev_null, STDOUT_FILENO);
                close(dev_null);
            }
            else {
                close_and_connect_fd(0, 0, 1, fd, -1, pid); //let current == first, current != last
            }
        }
        else {
            dup2(sockfd, STDOUT_FILENO);
        }
        if (cross_num >= 0) {
            dup2(sockfd, STDERR_FILENO);
        }
        if (execvp(cmd[0][0], cmd[0]) < 0) {
            print_unknown_cmd(cmd[0][0]);
            exit(0);
        }
    }
    else { //parent
        if (infor->output_point >= 0) {
            connect_pipe_to_first_process(infor, pid);
        }
        else if (user_in_id) {
            if (user_in_id <= max_client_num && client->list[user_in_id].state && client->list[client->id].user_pipe[user_in_id] >= 0) {
                close(client->list[client->id].user_pipe[user_in_id]);
                client->list[client->id].user_pipe[user_in_id] = -1;
            }
        }
        if (cross_num) {
            if (pos < 0) {
                update_cross_pipe_fd(infor, fd, cross_num);
            }
        }
        else if (user_out_id) {
            if (user_out_id <= max_client_num && client->list[user_out_id].state && client->list[user_out_id].user_pipe[client->id] < 0) {
                close(fd[1]);
                client->list[user_out_id].user_pipe[client->id] = fd[0];
            }
        }
        if (!cross_num && !redir && !user_out_id) {
                waitpid(pid, NULL, 0);
        }
    }
}

void ordinary_pipe(char ***cmd, char *cmd_reserve, int cmd_num, int cross_num, int redir, int user_in_id, int user_out_id, client_structure *client) {
    int fd[2], ex_fd0, pos = -1, fd_redir, sockfd = client->list[client->id].sockfd;
    cross_pipe *infor = client->list[client->id].cp_infor;
    pid_t pid, pid_table[cmd_num];
    if (redir) {//redirection
        if ((fd_redir = open(cmd[cmd_num][0], O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR)) == -1) {
            write(sockfd, "Error: can't open file\n", strlen("Error: can't open file\n"));
            return ;
        }
    }
    if (user_in_id || user_out_id) {
        print_user_pipe_msg_handler(cmd_reserve, user_in_id, user_out_id, client);
    }
    //run command
    for (int i = 0; i < cmd_num; i++) {
        if (i != cmd_num-1) {
            while(pipe(fd) < 0) {}
        }
        else {
            if (cross_num) {
                if ((pos = find_same_cross_pipe(infor, fd, cross_num)) < 0) {
                    while(pipe(fd) < 0) {}
                }
            }
            else if (user_out_id) {
                if (user_out_id <= max_client_num && client->list[user_out_id].state && client->list[user_out_id].user_pipe[client->id] < 0) {
                    while (pipe(fd) < 0) {}
                }
            }
        }
        while((pid = fork()) < 0) {
            waitpid(-1, NULL, 0);
        }
        if (!pid) { //child
            if (infor->data_num) {
                close_cross_pipe_fd(infor, pos);
            }
            dup2(sockfd, STDERR_FILENO);
            if (!i) {//first
                if (infor->output_point >= 0) {
                    connect_pipe_to_first_process(infor, pid);
                }
                else if (user_in_id) {
                    if (user_in_id > max_client_num || !client->list[user_in_id].state || client->list[client->id].user_pipe[user_in_id] < 0) {
                        int dev_null = open("/dev/null", O_RDWR);
                        if (dev_null == -1) {
                            write(sockfd, "Error: can't open file\n", strlen("Error: can't open file\n"));
                            exit(0);
                        }
                        dup2(dev_null, STDIN_FILENO);
                        close(dev_null);
                    }
                    else {
                        dup2(client->list[client->id].user_pipe[user_in_id], STDIN_FILENO);
                        close(client->list[client->id].user_pipe[user_in_id]);
                    }
                }
                else
                    dup2(sockfd, STDIN_FILENO);
            }
            else if (i == cmd_num-1) { //last
                if (cross_num) {
                    if (cross_num < 0) {
                        dup2(fd[1], STDERR_FILENO);
                    }
                    close_and_connect_fd(0, 0, 1, fd, -1, pid);   //let current == first, current != last
                }
                else if (redir) {
                    dup2(fd_redir, STDOUT_FILENO);
                }
                else if (user_out_id) {
                    if (user_out_id > max_client_num || !client->list[user_out_id].state || client->list[user_out_id].user_pipe[client->id] >= 0) {
                        int dev_null = open("/dev/null", O_RDWR);
                        if (dev_null == -1) {
                            write(sockfd, "Error: can't open file\n", strlen("Error: can't open file\n"));
                            exit(0);
                        }
                        dup2(dev_null, STDOUT_FILENO);
                        close(dev_null);
                    }
                    else {
                        close_and_connect_fd(0, 0, 1, fd, -1, pid); //let current == first, current != last
                    }
                }
                else {
                    dup2(sockfd, STDOUT_FILENO);
                }
            }
            if (redir) {
                close(fd_redir);
            }
            close_and_connect_fd(i, 0, cmd_num-1, fd, ex_fd0, pid);
            if (execvp(cmd[i][0], cmd[i]) < 0) {
                print_unknown_cmd(cmd[i][0]);
                exit(0);
            }
        }
        else { // parent
            if (!i) {
                if (infor->output_point >= 0) {
                    connect_pipe_to_first_process(infor, pid);
                }
                else if (user_in_id) {
                    if (user_in_id <= max_client_num && client->list[user_in_id].state && client->list[client->id].user_pipe[user_in_id] >= 0) {
                        close(client->list[client->id].user_pipe[user_in_id]);
                        client->list[client->id].user_pipe[user_in_id] = -1;
                    }
                }
            }
            else if (i == cmd_num-1) {
                if (cross_num) {
                    if (pos < 0) {
                        update_cross_pipe_fd(infor, fd, cross_num);
                    }
                }
                else if (redir) {
                    close(fd_redir);
                }
                else if (user_out_id) {
                    if (user_out_id <= max_client_num && client->list[user_out_id].state && client->list[user_out_id].user_pipe[client->id] < 0) {
                        close(fd[1]);
                        client->list[user_out_id].user_pipe[client->id] = fd[0];
                    }
                }
            }
            close_and_connect_fd(i, 0, cmd_num-1, fd, ex_fd0, pid);
            if (i != cmd_num-1) {
                ex_fd0 = fd[0];
            }
            if (!cross_num) {
                pid_table[i] = pid;
            }
        }
    }
    //wait for child
    if (!cross_num && !redir && !user_out_id) { //do not wait for child, if cross pipe happen
            for (int i = 0; i < cmd_num; i++) {
                waitpid(pid_table[i], NULL, 0);
            }
    }
}

void close_and_connect_fd(int current, int first, int last, int fd[2], int ex_fd0, pid_t pid) {
    if (current != last) {
        if (!pid) { //child process
            close(fd[0]);
            dup2(fd[1], STDOUT_FILENO);
        }
        close(fd[1]);
    }
    if (current != first) {
        if (!pid) {//child process
            dup2(ex_fd0, STDIN_FILENO);
        }
        close(ex_fd0);
    }
}

void connect_pipe_to_first_process(cross_pipe *infor, pid_t pid) {
    close(infor->list[infor->output_point].write_fd);
    if (!pid) {//child process need to hook the read end of pipe, and parent process just close fd
        dup2(infor->list[infor->output_point].read_fd, STDIN_FILENO);
    }
    close(infor->list[infor->output_point].read_fd);
}

void close_cross_pipe_fd(cross_pipe *infor, int pos) {
    //every child fork from parent need to close fd of cross command pipe
    int fd_pos;
    for (int i = 0; i < infor->data_num; i++) {
        fd_pos = infor->close_fd_record[i];
        if (pos != fd_pos) {
            close(infor->list[fd_pos].write_fd);
            close(infor->list[fd_pos].read_fd);
        }
    }
}

void update_cross_pipe_fd(cross_pipe *infor, int fd[2], int cross_num) {
    if (cross_num < 0) {
        cross_num *= -1;
    }
    infor->list[infor->min_input_point].count = cross_num;
    infor->list[infor->min_input_point].write_fd = fd[1];
    infor->list[infor->min_input_point].read_fd = fd[0];
    for (int i = 0; i < fd_infor_list_size; i++) {
        if (infor->list[i].count < 0) {
            infor->min_input_point = i;
            return ;
        }
    }
}

int find_same_cross_pipe(cross_pipe *infor, int *fd, int cross_num) {
    if (cross_num < 0) {
        cross_num *= -1;
    }
    int fd_pos;
    for (int i = 0; i < infor->data_num; i++) {
        fd_pos = infor->close_fd_record[i];
        if (cross_num == infor->list[fd_pos].count) {
            fd[0] = infor->list[fd_pos].read_fd;
            fd[1] = infor->list[fd_pos].write_fd;
            return fd_pos;
        } //if it did not find the data_block with same count down, it return position value -1
    }
    return -1;
}

void print_user_pipe_msg_handler(char *cmd, int user_in_id, int user_out_id, client_structure *client) {
    if (user_in_id) {
        if (user_in_id > max_client_num || !client->list[user_in_id].state || client->list[client->id].user_pipe[user_in_id] < 0) {
            if (user_in_id > max_client_num || !client->list[user_in_id].state) {
                print_user_pipe_msg(NULL, user_in_id, -1 , 0, client);
            }
            else {
                print_user_pipe_msg(NULL, user_in_id, 0, 0, client);
            }
        }
        else {
            print_user_pipe_msg(cmd, user_in_id, 1, 0, client);
        }
    }
    if (user_out_id) {
        if (user_out_id > max_client_num || !client->list[user_out_id].state || client->list[user_out_id].user_pipe[client->id] >= 0) {
            if (user_out_id > max_client_num || !client->list[user_out_id].state) {
                print_user_pipe_msg(NULL, user_out_id, -1, 1, client);
            }
            else {
                print_user_pipe_msg(NULL, user_out_id, 0, 1, client);
            }
        }
        else {
            print_user_pipe_msg(cmd, user_out_id, 1, 1, client);
        }
    }
}

void print_user_pipe_msg(char *cmd, int target_id, int mode, int in_out, client_structure *client) {
    char msg[max_input_len+100] = "";
    strcat(msg, "*** ");
    if (mode > 0) {
        strcat(msg, client->list[client->id].name);
        strcat(msg, " (#");
        cat_num_to_msg(msg, client->id);
        if (!in_out) {//in
            strcat(msg, ") just received from ");
        }
        else { //out
            strcat(msg, ") just piped '");
            strcat(msg, cmd);
            strcat(msg, "' to ");
        }
        strcat(msg, client->list[target_id].name);
        strcat(msg, " (#");
        cat_num_to_msg(msg, target_id);
        if (!in_out) {
            strcat(msg, ") by '");
            strcat(msg, cmd);
            strcat(msg, "' ***\n");
        }
        else {
          strcat(msg, ") ***\n");
        }
        broadcast(msg, client);
        return ;
    }
    else if (!mode) {
        strcat(msg, "Error: the pipe #");
        if (!in_out) {
            cat_num_to_msg(msg, target_id);
            strcat(msg, "->#");
            cat_num_to_msg(msg, client->id);
            strcat(msg, " does not exist yet. ***\n");
        }
        else {
            cat_num_to_msg(msg, client->id);
            strcat(msg, "->#");
            cat_num_to_msg(msg, target_id);
            strcat(msg, " already exists. ***\n");
        }
    }
    else {
        strcat(msg, "Error: user #");
        cat_num_to_msg(msg, target_id);
        strcat(msg, " does not exist yet. ***\n");
    }
    write(client->list[client->id].sockfd, msg, strlen(msg));
}

void print_unknown_cmd(char *cmd) {
    char err_msg[max_cmd_len] = "";
    strcat(err_msg, "Unknown command: [");
    strcat(err_msg, cmd);
    strcat(err_msg, "].\n");
    write(STDERR_FILENO, err_msg, strlen(err_msg));
}

void cmd_who(client_structure *client) {
    char msg[3000] = "", head[] = "<ID>    <nickname>    <IP:port>    <indicate me>\n", tab[] = "    ";
    strcat(msg, head);
    for (int i = 1; i <= max_client_num; i++) {
        if (client->list[i].state) {
            cat_num_to_msg(msg, i);
            strcat(msg, tab);
            strcat(msg, client->list[i].name);
            strcat(msg, tab);
            strcat(msg, inet_ntoa(client->list[i].connection_infor.sin_addr));
            strcat(msg, ":");
            cat_num_to_msg(msg, ntohs(client->list[i].connection_infor.sin_port));
            if (client->id == i) {
                strcat(msg, tab);
                strcat(msg, "<-me");
            }
            strcat(msg, "\n");
        }
    }
    write(client->list[client->id].sockfd, msg, strlen(msg));
}

void cmd_tell(char *cmd, client_structure *client) {
    int tell_id;
    char *buf;
    strtok(cmd, " ");
    tell_id = atoi(strtok(NULL, " "));
    if (!client->list[tell_id].state) {
        char err_msg[50] = "";
        strcat(err_msg, "*** Error: user #");
        cat_num_to_msg(err_msg, tell_id);
        strcat(err_msg, " does not exist yet. ***\n");
        write(client->list[client->id].sockfd, err_msg, strlen(err_msg));
    }
    else {
        char msg[max_msg_len*2] = "";
        buf = strtok(NULL, " ");
        *(buf+strlen(buf)) = ' ';
        buf = strtok(buf, "\r\n");
        strcat(msg, "*** ");
        strcat(msg, client->list[client->id].name);
        strcat(msg, " told you ***: ");
        strcat(msg, buf);
        strcat(msg, "\n");
        write(client->list[tell_id].sockfd, msg, strlen(msg));
    }
}

void cmd_yell(char *cmd, client_structure *client) {
    char *buf, msg[max_msg_len*2] = "";
    strtok(cmd, " ");
    buf = strtok(NULL, " ");
    *(buf+strlen(buf)) = ' ';
    buf = strtok(buf, "\r\n");
    strcat(msg, "*** ");
    strcat(msg, client->list[client->id].name);
    strcat(msg, " yelled ***: ");
    strcat(msg, buf);
    strcat(msg, "\n");
    broadcast(msg, client);
}

void cmd_name(char *name, client_structure *client) {
    for (int i = 1; i <= max_client_num; i++) {
        if (i != client->id) {
            if (client->list[i].state) {
                if (!strcmp(name, client->list[i].name)) {
                    char err_msg[60] = "";
                    strcat(err_msg, "*** User '");
                    strcat(err_msg, name);
                    strcat(err_msg, "' already exists. ***\n");
                    write(client->list[client->id].sockfd, err_msg, strlen(err_msg));
                    return ;
                }
            }
        }
    }
    strcpy(client->list[client->id].name, name);
    char msg[100] = "";
    strcat(msg, "*** User from ");
    strcat(msg, inet_ntoa(client->list[client->id].connection_infor.sin_addr));
    strcat(msg, ":");
    cat_num_to_msg(msg, ntohs(client->list[client->id].connection_infor.sin_port));
    strcat(msg, " is named '");
    strcat(msg, name);
    strcat(msg, "'. ***\n");
    broadcast(msg, client);
}

void broadcast(char *msg, client_structure *client) {
    for (int i = 1; i <= max_client_num; i++) {
        if (client->list[i].state) {
            write(client->list[i].sockfd, msg, strlen(msg));
        }
    }
}

void cat_num_to_msg(char *msg, unsigned int num) {
    unsigned int tmp = num;
    int len = 0;
    char *num_str;
    if (!tmp) {
        len = 1;
    }
    else {
        while (tmp) {
          len++;
          tmp /= 10;
        }
    }
    num_str = malloc(sizeof(char)*(len+1));
    num_str[len] = '\0';
    for (int i = len-1; i >= 0; i--) {
        num_str[i] = int_to_char(num%10);
        num /= 10;
    }
    strcat(msg, num_str);
    free(num_str);
}

char int_to_char(unsigned int num) {
    switch (num) {
      case 0:
          return '0';
      case 1:
          return '1';
      case 2:
          return '2';
      case 3:
          return '3';
      case 4:
          return '4';
      case 5:
          return '5';
      case 6:
          return '6';
      case 7:
          return '7';
      case 8:
          return '8';
      case 9:
          return '9';
    }
}
