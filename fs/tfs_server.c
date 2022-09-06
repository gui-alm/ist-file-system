#include "operations.h"
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>

#define BUFFER_SIZE (1024)
#define S (2) /*Max number of sessions possible, can be altered*/

typedef struct {
    int s_id;
    int s_working;
    char s_buffer[BUFFER_SIZE];
    pthread_mutex_t s_lock;
    pthread_cond_t s_cond;
} session;

static int Num_Sessions = 0;
static char free_sessions[S];
static int open_session[S];
static session session_table[S];
static pthread_t threads[S];

static pthread_mutex_t single_global_lock;

int session_alloc() {
    if (pthread_mutex_lock(&single_global_lock) != 0)
        return -1;
    for (int i = 0; i < S; i++) {
        if (free_sessions[i] == FREE) {
            free_sessions[i] = TAKEN;
            if (pthread_mutex_unlock(&single_global_lock) != 0)
                return -1;
            return i;
        }
    }
    if (pthread_mutex_unlock(&single_global_lock) != 0)
        return -1;
    return -1;
}

void open_session_alloc(int id, int tx) {
    open_session[id] = tx;
}

int session_create(int id) {
    session_table[id].s_id = id;
    session_table[id].s_working = 0;
    if (pthread_mutex_init(&session_table[id].s_lock, 0) != 0)
        return -1;
    if (pthread_cond_init(&session_table[id].s_cond, NULL) != 0)
        return -1;
    return 0;
}

session *session_get(int id) {
    return &session_table[id];
}

int op_code1(int tx) {
    int session_id;
    char buffer[sizeof(int)];
    if (Num_Sessions < S) {
        session_id = session_alloc();
        Num_Sessions++;
        open_session_alloc(session_id, tx);
    }
    else {
        session_id = -1;
    }
    memcpy(buffer, &session_id, sizeof(int));
    ssize_t ret = write(tx, buffer, sizeof(int));
    if(ret < 0 || session_id == -1) {
        close(tx);
    }
    return 0;
}

int op_code2(char *buf) {
    int id, res = 0;
    memcpy(&id, buf+1, sizeof(int));
    int tx = open_session[id];
    char buffer[sizeof(int)];
    if (pthread_mutex_lock(&single_global_lock) != 0)
        return -1;
    if (free_sessions[id] == TAKEN) {
        free_sessions[id] = FREE;
        open_session[id] = 0;
    }
    else {
        res = -1;
    }
    if (pthread_mutex_unlock(&single_global_lock) != 0)
        return -1;
    memcpy(buffer, &res, sizeof(int));
    ssize_t ret = write(tx, buffer, sizeof(int));
    if (ret == -1) {/*Even if there is an error, the pipe will be closed after*/}
    close(tx);
    return 0;
}

int op_code3(char *buffer) {
    int id;
    memcpy(&id, buffer+1, sizeof(int));
    char result_buffer[sizeof(int)];
    int flags;
    int fd;

    char *file_name = malloc(40);
    memcpy(file_name, buffer+1+sizeof(int), 40);
    memcpy(&flags, buffer+1+sizeof(int)+40, sizeof(int));
    fd = tfs_open(file_name, flags);
    free(file_name);
    memcpy(result_buffer, &fd, sizeof(int));
    ssize_t ret = write(open_session[id], result_buffer, sizeof(int));
    if(ret == -1){
        close(open_session[id]);
    }

    return 0;
}

int op_code4(char *buffer) {
    int id, fd, res;
    memcpy(&id, buffer+1, sizeof(int));
    char result_buffer[sizeof(int)];
    memcpy(&fd, buffer+1+sizeof(int), sizeof(int));
    res = tfs_close(fd);
    memcpy(result_buffer, &res, sizeof(int));
    ssize_t ret = write(open_session[id], result_buffer, sizeof(int));
    if(ret == -1){
        close(open_session[id]);
    }
    return 0;
}

int op_code5(char *buffer) {
    int id, fd;
    ssize_t res;
    size_t len;
    char result_buffer[sizeof(ssize_t)];
    memcpy(&id, buffer+1, sizeof(int));
    memcpy(&fd, buffer+1+sizeof(int), sizeof(int));
    memcpy(&len, buffer+1+(2*sizeof(int)), sizeof(size_t));
    void *buf = malloc(len);
    memcpy(buf, buffer+1+(2*sizeof(int))+sizeof(size_t), len);
    res = tfs_write(fd, buf, len);
    free(buf);
    memcpy(result_buffer, &res, sizeof(int));
    ssize_t ret = write(open_session[id], result_buffer, sizeof(int));
    if(ret == -1){
        close(open_session[id]);
    }
    return 0;
}

int op_code6(char *buffer) {
    int id, fd;
    ssize_t res;
    size_t len;
    memcpy(&id, buffer+1, sizeof(int));
    memcpy(&fd, buffer+1+sizeof(int), sizeof(int));
    memcpy(&len, buffer+1+(2*sizeof(int)), sizeof(size_t));
    void *buf = malloc(len);
    res = tfs_read(fd, buf, len);
    if (res != -1) {
        char result_buffer[sizeof(int) + (size_t) res];
        memcpy(result_buffer, &res, sizeof(int));
        memcpy(result_buffer+sizeof(int), buf, (size_t) res);
        ssize_t ret = write(open_session[id], result_buffer, sizeof(ssize_t) + (size_t) res);
        if(ret == -1) {
            close(open_session[id]);
        }
    } else {
        char result_buffer[sizeof(int)];
        memcpy(result_buffer, &res, sizeof(int));
        ssize_t ret = write(open_session[id], result_buffer, sizeof(int));
        if(ret == -1) {
            close(open_session[id]);
        }
    }
    free(buf);
    return 0;
}

int op_code7(char *buffer){
    char buf[sizeof(int)]; 
    int result, id;
    memcpy(&id, buffer+1, sizeof(int));
    int tx = open_session[id];

    result = tfs_destroy_after_all_closed();
    memcpy(buf, &result, sizeof(int));
    ssize_t ret = write(tx, buf, sizeof(int));
    if (ret == -1) {
        close(tx);
    }
    exit(0);
    return 0;
}

void choose_opcode(char opcode, char *buffer) {
    if (opcode == '2') {
        op_code2(buffer);
    }
    if (opcode == '3') {
        op_code3(buffer);
    }
    if (opcode == '4') {
        op_code4(buffer);
    }
    if (opcode == '5') {
        op_code5(buffer);
    }
    if (opcode == '6') {
        op_code6(buffer);
    }
    if (opcode == '7') {
        op_code7(buffer);
    }
}


void *working_threads(void *ses) {
    session *s = (session*) ses;
    while (true) {
        pthread_mutex_lock(&s->s_lock);
        
        while ((s->s_working) == 0) {
            pthread_cond_wait(&s->s_cond, &s->s_lock);
        }
        choose_opcode(s->s_buffer[0], s->s_buffer);
        s->s_working = 0;
        pthread_mutex_unlock(&s->s_lock);
    }
    return NULL;
}

void server_init() {
    
    for (int i = 0; i < S; i++) {
        session *s = session_get(i);
        pthread_create(&threads[i], NULL, working_threads, (void*)s);
    }
}

int main(int argc, char **argv) {
    char buffer[BUFFER_SIZE];
    if (tfs_init() != 0) {
        return -1;
    }
    if (argc < 2) {
        printf("Please specify the pathname of the server's pipe.\n");
        return 1;
    }
    char *pipename = argv[1];
    printf("Starting TecnicoFS server with pipe called %s\n", pipename);

    if (unlink(pipename) != 0 && errno != ENOENT) {
        return -1;
    }

    if (mkfifo(pipename, 0640) != 0) {
        return -1;
    }

    if (pthread_mutex_init(&single_global_lock, 0) != 0)
        return -1;

    for (int i = 0; i < S; i++) {
        free_sessions[i] = FREE;
        open_session[i] = 0;
        session_create(i);
    }

    server_init();
    
    int rx = open(pipename, O_RDONLY);
    if (rx == -1) {
        return -1;
    }

    while (true) {
        ssize_t ret = read(rx, buffer, BUFFER_SIZE - 1);
        if (ret == 0) {
            int temp = open(pipename, O_RDONLY);
            close(rx);
            rx = temp;
            continue; 
        }
        if (ret == -1) {
           break;
        }

        if (ret > 0) {
            if (buffer[0] == '1') {
                char *client_pipename = malloc(41-1);
                memcpy(client_pipename, buffer+1, 41-1);
                int tx = open(client_pipename, O_WRONLY);
                free(client_pipename);
                if (tx == -1) {
                    close(tx);
                }
                op_code1(tx);
            }
            else {
                int id;
                memcpy(&id, buffer+1, sizeof(int));
                if (free_sessions[id] == TAKEN) {
                    session *s = session_get(id);
                    pthread_mutex_lock(&s->s_lock);
                    s->s_working = 1;
                    memcpy(s->s_buffer, buffer, BUFFER_SIZE);
                    pthread_cond_signal(&s->s_cond);
                    pthread_mutex_unlock(&s->s_lock);
                }
            }
        }
    }
    
    return 0; 
}
