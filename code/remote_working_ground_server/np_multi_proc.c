#include "np_multi_proc.h"

int main(int argc, char const *argv[]) {
    int msockfd, port;
    struct sockaddr_in client_addr;
    pid_t pid;
    if (!argv[1]) {
        fprintf(stderr, "Error: didn't get the port number\n");
        exit(0);
    }
    else {
        port = atoi(argv[1]);
    }
    msockfd = connect_sock(port);
    create_shared_mem();
    signal(SIGCHLD, child_handler);
    signal(SIGUSR1, msg_handler);
    signal(SIGUSR2, user_pipe_handler);
    signal(SIGINT, ctrl_c_handler);
    while (1) {
        int client_len = sizeof(client_addr);
        sockfd = accept(msockfd, (struct sockaddr*)&client_addr, &client_len);
        if (sockfd < 0) {
            fprintf(stderr, "Error: accept error\n");
        }
        else {
            while((pid = fork()) < 0) {
                waitpid(-1, NULL, 0);
            }
            if (!pid) {
                //client login
                sem_wait(&connect_state->lock2);
                id = connect_state->min_id;
                connect_state->client_num++;
                update_client_infor(id, client_addr);
                for (int i = 1; i <= max_client_num; i++) {
                    if (!client_list[i].state) {
                        connect_state->min_id = i;
                        break;
                    }
                }
                sem_post(&connect_state->lock);
                sem_post(&connect_state->lock2);
                close(msockfd);
                print_welcome_msg();
                print_login_msg();
                run_shell();
                //client logout
                stopped = 1;
                char name[max_name_len] = "";
                sem_wait(&connect_state->lock2);
                close(sockfd);
                delete_client_infor(name);
                connect_state->client_num--;
                if (id < connect_state->min_id) {
                    connect_state->min_id = id;
                }
                print_logout_msg(name);
                sem_post(&connect_state->lock2);
                exit(0);
            }
            else {
                close(sockfd);
                sem_wait(&connect_state->lock);
                while(connect_state->client_num >= max_client_num) {}
            }
        }
    }
    close(msockfd);
    kill(client_list[id].pid, SIGINT);
    return 0;
}

int connect_sock(int port) {
    struct sockaddr_in server_addr;
    int msockfd = socket(AF_INET, SOCK_STREAM, 0), reuse_enable = 1;
    if (msockfd < 0) {
        fprintf(stderr, "Error: can't open stream socket\n");
        exit(0);
    }
    //fill the server information
    bzero((char*)&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    //set SO_REUSEADDR
    if (setsockopt(msockfd, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse_enable, sizeof(int)) < 0) {
        fprintf(stderr, "Error: can't set SO_REUSEADDR\n");
        exit(0);
    }
    //bind and listen
    if (bind(msockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        fprintf(stderr, "Error: can't bind local address\n");
        exit(0);
    }
    listen(msockfd, 50);
    return msockfd;
}

void create_shared_mem() {
    cli_infor_shmid = shmget(IPC_PRIVATE, sizeof(client_infor)*(max_client_num+1), IPC_CREAT | 0666);
    con_state_shmid = shmget(IPC_PRIVATE, sizeof(connection_state), IPC_CREAT | 0666);
    fifo_infor_shmid = shmget(IPC_PRIVATE, sizeof(fifo_information), IPC_CREAT | 0666);
    if (cli_infor_shmid == -1 || con_state_shmid == -1 || fifo_infor_shmid == -1) {
        fprintf(stderr, "Error: shmget failed\n");
        exit(0);
    }
    client_list = shmat(cli_infor_shmid, NULL, 0);
    connect_state = shmat(con_state_shmid, NULL, 0);
    fifo_infor = shmat(fifo_infor_shmid, NULL, 0);
    if (client_list == (client_infor*)-1 || connect_state == (connection_state*)-1 || fifo_infor == (fifo_information*)-1) {
        fprintf(stderr, "Error: shmat failed\n");
        exit(0);
    }

    client_list[0].pid = getpid();
    client_list[0].state = 1;
    for (int i = 1; i <= max_client_num; i++) {
        client_list[i].state = 0;
        sem_init(&client_list[i].lock, 1, 1);
        for (int j = 0; j <= max_client_num; j++) {
            client_list[i].user_pipe[j] = -1;
        }
    }
    connect_state->client_num = 0;
    connect_state->min_id = 1;
    sem_init(&connect_state->lock, 1, 0);
    sem_init(&connect_state->lock2, 1, 1);
    fifo_infor->count = 0;
    sem_init(&fifo_infor->lock, 1, 1);
}

void ctrl_c_handler(int signo) {
    if (getpid() == client_list[0].pid) {
        //delete fifo
        DIR *d;
        struct dirent *dir;
        d = opendir(fifo_dir);
        if (d) {
            while((dir = readdir(d)) != NULL) {
                if (strcmp(dir->d_name, ".") && strcmp(dir->d_name, "..")) {
                    char file[20] = "";
                    strcat(file, fifo_dir);
                    strcat(file, dir->d_name);
                    remove(file);
                }
            }
            closedir(d);
        }
    }
    //delete shared memory
    shmdt(client_list);
    shmdt(connect_state);
    shmdt(fifo_infor);
    shmctl(cli_infor_shmid, IPC_RMID, 0);
    shmctl(con_state_shmid, IPC_RMID, 0);
    shmctl(fifo_infor_shmid, IPC_RMID, 0);
    exit(0);
}

void child_handler(int signo) {
    while (waitpid(-1, NULL, WNOHANG) > 0){}
}

void update_client_infor(int id, struct sockaddr_in client_addr) {
    client_list[id].pid = getpid();
    client_list[id].state = 1;
    strcpy(client_list[id].name, "(no name)");
    client_list[id].connection_infor = client_addr;
}

void print_welcome_msg() {
    char welcome_msg[130] = "";
    strcat(welcome_msg, "****************************************\n");
    strcat(welcome_msg, "** Welcome to the information server. **\n");
    strcat(welcome_msg, "****************************************\n");
    write(sockfd, welcome_msg, strlen(welcome_msg));
}

void print_login_msg() {
    char msg[100] = "";
    strcat(msg, "*** User '");
    strcat(msg, client_list[id].name);
    strcat(msg, "' entered from ");
    strcat(msg, inet_ntoa(client_list[id].connection_infor.sin_addr));
    strcat(msg, ":");
    cat_num_to_msg(msg, ntohs(client_list[id].connection_infor.sin_port));
    strcat(msg, ". ***\n");
    broadcast(msg);
}

void delete_client_infor(char *name) {
    client_list[id].state = 0;
    strcpy(name, client_list[id].name);
    for (int i = 0; i <= max_client_num; i++) {
        client_list[id].user_pipe[i] = -1;
    }
    for (int i = 1; i <= max_client_num; i++) {
        if (client_list[i].state) {
            client_list[i].user_pipe[id] = -1;
        }
    }
}

void print_logout_msg(char *name) {
    char msg[50] = "";
    strcat(msg, "*** User '");
    strcat(msg, name);
    strcat(msg, "' left. ***\n");
    broadcast(msg);
}

void run_shell() {
    char cmd[max_input_len], start_symbol[3] = "% ";
    setenv("PATH", "bin:.", 1);
    cross_pipe infor = {-1, 0, 0, NULL};
    initial_fd_infor_list(&infor);
    while(1) {
        write(sockfd, start_symbol, strlen(start_symbol));
        memset(cmd, '\0', sizeof(cmd));
        while (read(sockfd, cmd, max_input_len) < 0) {}
        if ((int)cmd[0] == 4) {//end of file
            if (infor.data_num) {
                close_cross_pipe_fd(&infor, -1);
                free(infor.close_fd_record);
            }
            return ;
        }
        if (strcmp(cmd, "\r\n") && strcmp(cmd, "\n")) {
            count_down_and_check(&infor);
            strtok(cmd, "\r\n");
            if (!strcmp(cmd, "exit")) {
                if (infor.data_num) {
                    close_cross_pipe_fd(&infor, -1);
                    free(infor.close_fd_record);
                }
                return ;
            }
            else {
                if (!check_cmd(cmd)) {
                    string_split(cmd, &infor);
                }
            }
        }
    }
}

void initial_fd_infor_list(cross_pipe *infor) {
    for (int i = 0; i < fd_infor_list_size; i++) {
        infor->list[i].count = -1; //count equal to -1 means no data in this data_block
        infor->list[i].write_fd = -1;
        infor->list[i].read_fd = -1;
    }
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

int check_cmd(char *cmd) {
    if (!strstr(cmd, "tell") && !strstr(cmd, "yell")) {
        return 0;
    }
    else {
        char copy_str[max_input_len], *buf;
        strcpy(copy_str, cmd);
        buf = strtok(copy_str, " ");
        if (!strcmp(buf, "tell")) {
            cmd_tell(cmd);
        }
        else if (!strcmp(buf, "yell")) {
            cmd_yell(cmd);
        }
        else {
            return 0;
        }
    }
    return 1;
}

void string_split(char *all_cmd, cross_pipe *infor) {
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
        if (!strcmp(cmd_collection[0][0], "setenv") || !strcmp(cmd_collection[0][0], "printenv")) {
            env_setting(cmd_collection[0]);
        }
        else if (!strcmp(cmd_collection[0][0], "who") || !strcmp(cmd_collection[0][0], "name")) {
            if (!strcmp(cmd_collection[0][0], "who")) {
                cmd_who();
            }
            else {
                cmd_name(cmd_collection[0][1]);
            }
        }
        else {
            single_cmd(cmd_collection, cmd_reserve, cross_num, infor, redir, user_in_id, user_out_id);
        }
    }
    else {
        if (!redir) {
            ordinary_pipe(cmd_collection, cmd_reserve, cmd_num+1, cross_num, infor, redir, user_in_id, user_out_id);
        }
        else {
            ordinary_pipe(cmd_collection, cmd_reserve, cmd_num, cross_num, infor, redir, user_in_id, user_out_id);
        }
    }
    for (int i = 0; i <= cmd_num; i++) {
        free(cmd_collection[i]);
    }
    if (infor->data_num) {
        free(infor->close_fd_record);
    }
}

void env_setting(char **cmd) {
    if (!strcmp(cmd[0], "setenv")) {
        setenv(cmd[1], cmd[2], 1);
    }
    else { //print env
        char *output = getenv(cmd[1]);
        if (output != NULL) {
            write(sockfd, output, strlen(output));
            write(sockfd, "\n", strlen("\n"));
        }
    }
}

void single_cmd(char ***cmd, char *cmd_reserve, int cross_num, cross_pipe *infor, int redir, int user_in_id, int user_out_id) {
    int fd[2], pos, upipe_write_fd, upipe_read_fd = client_list[id].user_pipe[user_in_id], upipe_write_exist = 0, user_out_state = client_list[user_out_id].state;
    pid_t pid;
    if (user_in_id || user_out_id) {
        print_user_pipe_msg_handler(cmd_reserve, user_in_id, user_out_id);
    }
    if (cross_num) {
        if ((pos = find_same_cross_pipe(infor, fd, cross_num)) < 0) {
            while (pipe(fd) < 0) {}
        }
    }
    else if(user_out_id) {
        if (user_out_id <= max_client_num && client_list[user_out_id].state && client_list[user_out_id].user_pipe[id] < 0) {
            if (user_out_state) { //Protection for client suddenly login
                upipe_write_fd = create_user_pipe(user_out_id);
            }
            else { //If client was offline before, direct output to dev/null
                upipe_write_exist = 1;
            }
        }
        else if (client_list[user_out_id].user_pipe[id] >= 0) {
            upipe_write_exist = 1;
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
            if (user_in_id > max_client_num || !client_list[user_in_id].state || upipe_read_fd < 0) {
                int dev_null = open("/dev/null", O_RDWR);
                dup2(dev_null, STDIN_FILENO);
                close(dev_null);
            }
            else {
                dup2(upipe_read_fd, STDIN_FILENO);
                close(upipe_read_fd);
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
            if (user_out_id > max_client_num || !client_list[user_out_id].state || upipe_write_exist) {
                int dev_null = open("/dev/null", O_RDWR);
                dup2(dev_null, STDOUT_FILENO);
                close(dev_null);
            }
            else {
                if (!user_out_state) { //If client was offline before, direct output to dev/null
                    upipe_write_fd = open("/dev/null", O_RDWR);
                }
                dup2(upipe_write_fd, STDOUT_FILENO);
                close(upipe_write_fd);
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
            if (user_in_id <= max_client_num && client_list[user_in_id].state && upipe_read_fd >= 0) {
                close(upipe_read_fd);
                client_list[id].user_pipe[user_in_id] = -1;
            }
        }
        if (cross_num) {
            if (pos < 0) {
                update_cross_pipe_fd(infor, fd, cross_num);
            }
        }
        else if (user_out_id) {
            if (user_out_id <= max_client_num && client_list[user_out_id].state && !upipe_write_exist) {
                if (user_out_state) {
                    close(upipe_write_fd);
                }
            }
        }
        if (!cross_num && !redir && !user_out_id) {
            waitpid(pid, NULL, 0);
        }
        else if (user_out_id) {
            if (user_out_id > max_client_num || !client_list[user_out_id].state || upipe_write_exist) {
                waitpid(pid, NULL, 0);
            }
            else if (client_list[user_out_id].state && !user_out_state) {
                waitpid(pid, NULL, 0);
            }
        }

    }
}

void ordinary_pipe(char ***cmd, char *cmd_reserve, int cmd_num, int cross_num, cross_pipe *infor, int redir, int user_in_id, int user_out_id) {
    int fd[2], ex_fd0, pos = -1, fd_redir, upipe_write_fd, upipe_read_fd = client_list[id].user_pipe[user_in_id], upipe_write_exist = 0, user_out_state = client_list[user_out_id].state;
    pid_t pid, pid_table[cmd_num];
    if (redir) {//redirection
        if ((fd_redir = open(cmd[cmd_num][0], O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR)) == -1) {
            write(sockfd, "Error: can't open file\n", strlen("Error: can't open file\n"));
            return ;
        }
    }
    if (user_in_id || user_out_id) {
        print_user_pipe_msg_handler(cmd_reserve, user_in_id, user_out_id);
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
            else if(user_out_id) {
                if (user_out_id <= max_client_num && client_list[user_out_id].state && client_list[user_out_id].user_pipe[id] < 0) {
                    if (user_out_state) { //Protection for client suddenly login
                        upipe_write_fd = create_user_pipe(user_out_id);
                    }
                    else { //If client was offline before, direct output to dev/null
                        upipe_write_exist = 1;
                    }
                }
                else if (client_list[user_out_id].user_pipe[id] >= 0) {
                    upipe_write_exist = 1;
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
                    if (user_in_id > max_client_num || !client_list[user_in_id].state || upipe_read_fd < 0) {
                        int dev_null = open("/dev/null", O_RDWR);
                        dup2(dev_null, STDIN_FILENO);
                        close(dev_null);
                    }
                    else {
                        dup2(upipe_read_fd, STDIN_FILENO);
                        close(upipe_read_fd);
                    }
                }
                else {
                    dup2(sockfd, STDIN_FILENO);
                }
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
                    if (user_out_id > max_client_num || !client_list[user_out_id].state || upipe_write_exist) {
                        int dev_null = open("/dev/null", O_RDWR);
                        dup2(dev_null, STDOUT_FILENO);
                        close(dev_null);
                    }
                    else {
                        if (!user_out_state) { //If client was offline before, direct output to dev/null
                            upipe_write_fd = open("/dev/null", O_RDWR);
                        }
                        dup2(upipe_write_fd, STDOUT_FILENO);
                        close(upipe_write_fd);
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
                    if (user_in_id <= max_client_num && client_list[user_in_id].state && upipe_read_fd >= 0) {
                        close(upipe_read_fd);
                        client_list[id].user_pipe[user_in_id] = -1;
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
                    if (user_out_id <= max_client_num && client_list[user_out_id].state && !upipe_write_exist) {
                        if (user_out_state) {
                            close(upipe_write_fd);
                        }
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
    if (!cross_num && !redir && !user_out_id) {//do not wait for child, if cross pipe happen
        for (int i = 0; i < cmd_num; i++) {
            waitpid(pid_table[i], NULL, 0);
        }
    }
    else if (user_out_id) {
        if (user_out_id > max_client_num || !client_list[user_out_id].state || upipe_write_exist) {
            for (int i = 0; i < cmd_num; i++) {
                waitpid(pid_table[i], NULL, 0);
            }
        }
        else if (client_list[user_out_id].state && !user_out_state) {
            for (int i = 0; i < cmd_num; i++) {
                waitpid(pid_table[i], NULL, 0);
            }
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
    for (int i = 0; i < infor->data_num; i++) {
        if (pos != infor->close_fd_record[i]) {
            close(infor->list[infor->close_fd_record[i]].write_fd);
            close(infor->list[infor->close_fd_record[i]].read_fd);
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
    for (int i = 0; i < infor->data_num; i++) {
        if (cross_num == infor->list[infor->close_fd_record[i]].count) {
            fd[0] = infor->list[infor->close_fd_record[i]].read_fd;
            fd[1] = infor->list[infor->close_fd_record[i]].write_fd;
            return infor->close_fd_record[i];
        }
    } //if it did not find the data_block with same count down, it return position value -1
    return -1;
}

void print_user_pipe_msg_handler(char *cmd, int user_in_id, int user_out_id) {
    if (user_in_id) {
        if (user_in_id > max_client_num || !client_list[user_in_id].state || client_list[id].user_pipe[user_in_id] < 0) {
            if (user_in_id > max_client_num || !client_list[user_in_id].state) {
                print_user_pipe_msg(NULL, user_in_id, -1 , 0);
            }
            else {
                print_user_pipe_msg(NULL, user_in_id, 0, 0);
            }
        }
        else {
            print_user_pipe_msg(cmd, user_in_id, 1, 0);
        }
    }
    if (user_out_id) {
        if (user_out_id > max_client_num || !client_list[user_out_id].state || client_list[user_out_id].user_pipe[id] >= 0) {
            if (user_out_id > max_client_num || !client_list[user_out_id].state) {
                print_user_pipe_msg(NULL, user_out_id, -1, 1);
            }
            else {
                print_user_pipe_msg(NULL, user_out_id, 0, 1);
            }
        }
        else {
            print_user_pipe_msg(cmd, user_out_id, 1, 1);
        }
    }
}

void print_user_pipe_msg(char *cmd, int target_id, int mode, int in_out) {
    char msg[max_input_len+100] = "";
    strcat(msg, "*** ");
    if (mode > 0) {
        strcat(msg, client_list[id].name);
        strcat(msg, " (#");
        cat_num_to_msg(msg, id);
        if (!in_out) {//in
            strcat(msg, ") just received from ");
        }
        else { //out
            strcat(msg, ") just piped '");
            strcat(msg, cmd);
            strcat(msg, "' to ");
        }
        strcat(msg, client_list[target_id].name);
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
        broadcast(msg);
        return ;
    }
    else if (!mode) {
        strcat(msg, "Error: the pipe #");
        if (!in_out) {
            cat_num_to_msg(msg, target_id);
            strcat(msg, "->#");
            cat_num_to_msg(msg, id);
            strcat(msg, " does not exist yet. ***\n");
        }
        else {
            cat_num_to_msg(msg, id);
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
    write(sockfd, msg, strlen(msg));
}

int create_user_pipe(int user_out_id) {
    sem_wait(&fifo_infor->lock);
    fifo_infor->count++;
    int file_name = fifo_infor->count;
    sem_post(&fifo_infor->lock);
    char file_path[30] = "";
    strcat(file_path, fifo_dir);
    cat_num_to_msg(file_path, file_name);
    mkfifo(file_path, 0666);
    sem_wait(&client_list[user_out_id].lock);
    strcpy(client_list[user_out_id].mailbox, file_path);
    client_list[user_out_id].from_id = id;
    if (client_list[user_out_id].state) { //Protection for client suddenly leave
        kill(client_list[user_out_id].pid, SIGUSR2);
        return open(file_path, O_WRONLY);
    }
    else {
        sem_post(&client_list[user_out_id].lock);
        return -1;
    }
}

void user_pipe_handler(int signo) {
    if (stopped) {
        int garbage_fd = open(client_list[id].mailbox, O_RDONLY);
        sem_post(&client_list[id].lock);
        return ;
    }
    int from_id = client_list[id].from_id;
    client_list[id].user_pipe[from_id] = open(client_list[id].mailbox, O_RDONLY);
    sem_post(&client_list[id].lock);
}

void print_unknown_cmd(char *cmd) {
    char err_msg[max_cmd_len] = "";
    strcat(err_msg, "Unknown command: [");
    strcat(err_msg, cmd);
    strcat(err_msg, "].\n");
    write(STDERR_FILENO, err_msg, strlen(err_msg));
}

void cmd_who() {
    char msg[3000] = "", head[] = "<ID>    <nickname>    <IP:port>    <indicate me>\n", tab[] = "    ";
    strcat(msg, head);
    for (int i = 1; i <= max_client_num; i++) {
        if (client_list[i].state) {
            cat_num_to_msg(msg, i);
            strcat(msg, tab);
            strcat(msg, client_list[i].name);
            strcat(msg, tab);
            strcat(msg, inet_ntoa(client_list[i].connection_infor.sin_addr));
            strcat(msg, ":");
            cat_num_to_msg(msg, ntohs(client_list[i].connection_infor.sin_port));
            if (id == i) {
                strcat(msg, tab);
                strcat(msg, "<-me");
            }
            strcat(msg, "\n");
        }
    }
    write(sockfd, msg, strlen(msg));
}

void cmd_tell(char *cmd) {
    int tell_id;
    char *buf;
    strtok(cmd, " ");
    tell_id = atoi(strtok(NULL, " "));
    if (tell_id > max_client_num || !client_list[tell_id].state) {
        char err_msg[50] = "";
        strcat(err_msg, "*** Error: user #");
        cat_num_to_msg(err_msg, tell_id);
        strcat(err_msg, " does not exist yet. ***\n");
        write(sockfd, err_msg, strlen(err_msg));
    }
    else {
        char msg[max_msg_len*2] = "";
        buf = strtok(NULL, " ");
        *(buf+strlen(buf)) = ' ';
        buf = strtok(buf, "\r\n");
        strcat(msg, "*** ");
        strcat(msg, client_list[id].name);
        strcat(msg, " told you ***: ");
        strcat(msg, buf);
        strcat(msg, "\n");
        sem_wait(&client_list[tell_id].lock);
        strcpy(client_list[tell_id].mailbox, msg);
        if (client_list[tell_id].state) { //Protection for client suddenly leave
            kill(client_list[tell_id].pid, SIGUSR1);
        }
        else {
            sem_post(&client_list[tell_id].lock);
        }
    }
}

void cmd_yell(char *cmd) {
    char *buf, msg[max_msg_len*2] = "";
    strtok(cmd, " ");
    buf = strtok(NULL, " ");
    *(buf+strlen(buf)) = ' ';
    buf = strtok(buf, "\r\n");
    strcat(msg, "*** ");
    strcat(msg, client_list[id].name);
    strcat(msg, " yelled ***: ");
    strcat(msg, buf);
    strcat(msg, "\n");
    broadcast(msg);
}

void cmd_name(char *name) {
    for (int i = 1; i <= max_client_num; i++) {
        if (i != id) {
            if (client_list[i].state) {
                if (!strcmp(name, client_list[i].name)) {
                    char err_msg[60] = "";
                    strcat(err_msg, "*** User '");
                    strcat(err_msg, name);
                    strcat(err_msg, "' already exists. ***\n");
                    write(sockfd, err_msg, strlen(err_msg));
                    return ;
                }
            }
        }
    }
    strcpy(client_list[id].name, name);
    char msg[100] = "";
    strcat(msg, "*** User from ");
    strcat(msg, inet_ntoa(client_list[id].connection_infor.sin_addr));
    strcat(msg, ":");
    cat_num_to_msg(msg, ntohs(client_list[id].connection_infor.sin_port));
    strcat(msg, " is named '");
    strcat(msg, name);
    strcat(msg, "'. ***\n");
    broadcast(msg);
}

void broadcast(char *msg) {
    for (int i = 1; i <= max_client_num; i++) {
        if (client_list[i].state) {
            if (i == id) {
                write(sockfd, msg, strlen(msg));
            }
            else {
                sem_wait(&client_list[i].lock);
                strcpy(client_list[i].mailbox, msg);
                if (client_list[i].state) { //Protection for client suddenly leave
                    kill(client_list[i].pid, SIGUSR1);
                }
                else {
                    sem_post(&client_list[i].lock);
                }
            }
        }
    }
}

void msg_handler(int signo) {
    if (stopped) {
        sem_post(&client_list[id].lock);
        return ;
    }
    write(sockfd, client_list[id].mailbox, strlen(client_list[id].mailbox));
    sem_post(&client_list[id].lock);
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
