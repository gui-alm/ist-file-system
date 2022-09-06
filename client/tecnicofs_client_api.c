#include "tecnicofs_client_api.h"

#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>

static int session_id;
static int server_pipe;
static int client_pipe;
char const *client_pipe_name;


int send_pipename(char const *pipename){

    char buf[1+40];
    char opcode = '1';
    memcpy(buf, &opcode, 1);
    memcpy(buf+1, pipename, 40);
    ssize_t ret = write(server_pipe, buf, 40+1);
    if (ret == -1) {
        return -1;
    }
    return 0;
}

int tfs_mount(char const *client_pipe_path, char const *server_pipe_path) {
    char buffer[sizeof(int)];
    if (unlink(client_pipe_path) != 0 && errno != ENOENT) {
        return -1;
    }

    if (mkfifo(client_pipe_path, 0640) != 0) {
        return -1;
    } 

    server_pipe = open(server_pipe_path, O_WRONLY);
    if (server_pipe == -1) {
        return -1;
    }

    send_pipename(client_pipe_path);
    client_pipe_name = client_pipe_path;
    client_pipe = open(client_pipe_path, O_RDONLY);
    if(client_pipe == -1) {
        return -1;
    }
    ssize_t rread = read(client_pipe, buffer, sizeof(int));
    if (rread == -1) {
        return -1;
    }
    memcpy(&session_id, buffer, sizeof(int));
    if (session_id == -1) {
        return -1;
    }
    return 0;
}


int tfs_unmount() {
    char buf[1+sizeof(int)];
    char rbuf[sizeof(int)];
    char opcode = '2';
    memcpy(buf, &opcode, 1);
    memcpy(buf+1, &session_id, sizeof(int));
    ssize_t wr = write(server_pipe, buf, 1+sizeof(int));
    if (wr == -1) {
        return -1;
    }
    ssize_t rr = read(client_pipe, rbuf, sizeof(int));
    if (rr == -1) {
        return -1;
    }
    int result;
    memcpy(&result, rbuf, sizeof(int));
    if (result == 0) {
        close(server_pipe);
        close(client_pipe);
        if (unlink(client_pipe_name) != 0) {
            return -1;
        }
    }
    else if (result == -1)
        return -1;
    return 0;
}


int tfs_open(char const *name, int flags) {
    char buf[1+(2*sizeof(int))+40];
    char rbuf[sizeof(int)];
    char opcode = '3';

    memcpy(buf, &opcode, 1);
    memcpy(buf+1, &session_id, sizeof(int));
    memcpy(buf+1+sizeof(int), name, 40);
    memcpy(buf+1+sizeof(int)+40, &flags, sizeof(int));

    ssize_t wr = write(server_pipe, buf, 1+sizeof(int)+40+sizeof(int));
    if (wr == -1) {
        return -1;
    }
    ssize_t rr = read(client_pipe, rbuf, sizeof(int));
    if (rr == -1) {
        return -1;
    }
    int result;
    memcpy(&result, rbuf, sizeof(int));
    return result;
}

int tfs_close(int fhandle) {
    char buf[1+(2*sizeof(int))];
    char rbuf[sizeof(int)];
    char opcode = '4';

    memcpy(buf, &opcode, 1);
    memcpy(buf+1, &session_id, sizeof(int));
    memcpy(buf+1+sizeof(int), &fhandle, sizeof(int));

    ssize_t wr = write(server_pipe, buf, 1+(2*sizeof(int)));
    if (wr == -1) {
        return -1;
    }
    ssize_t rr = read(client_pipe, rbuf, sizeof(int));
    if (rr == -1) {
        return -1;
    }
    int result;
    memcpy(&result, rbuf, sizeof(int));
    return result;
}

ssize_t tfs_write(int fhandle, void const *buffer, size_t len) {
    char buf[1+(2*sizeof(int))+sizeof(size_t)+len];
    char rbuf[sizeof(ssize_t)];
    char opcode = '5';
    memcpy(buf, &opcode, 1);
    memcpy(buf+1, &session_id, sizeof(int));
    memcpy(buf+1+sizeof(int), &fhandle, sizeof(int));
    memcpy(buf+1+(2*sizeof(int)), &len, sizeof(size_t));
    memcpy(buf+1+(2*sizeof(int))+sizeof(size_t), buffer, len);

    ssize_t wr = write(server_pipe, buf, 1+(2*sizeof(int))+sizeof(size_t)+len);
    if (wr == -1) {
        return -1;
    }
    ssize_t rr = read(client_pipe, rbuf, sizeof(int));
    if (rr == -1) {
        return -1;
    }
    int result;
    memcpy(&result, rbuf, sizeof(int));
    return (ssize_t) result;
}

ssize_t tfs_read(int fhandle, void *buffer, size_t len) {
    char buf[1+(2*sizeof(int))+sizeof(size_t)];
    char rbuf[sizeof(ssize_t)+len];
    char opcode = '6';
    memcpy(buf, &opcode, 1);
    memcpy(buf+1, &session_id, sizeof(int));
    memcpy(buf+1+sizeof(int), &fhandle, sizeof(int));
    memcpy(buf+1+(2*sizeof(int)), &len, sizeof(size_t));

    ssize_t wr = write(server_pipe, buf, 1+(2*sizeof(int))+sizeof(size_t));
    if (wr == -1) {
        return -1;
    }
    ssize_t rr = read(client_pipe, rbuf, sizeof(int)+len);
    if (rr == -1) {
        return -1;
    }
    int result;
    memcpy(&result, rbuf, sizeof(int));
    if (result != -1) {
        memcpy(buffer, rbuf+sizeof(int), (size_t) result);
    }
    return (ssize_t) result;
}

int tfs_shutdown_after_all_closed() {
    char buf[1+sizeof(int)];
    char rbuf[sizeof(int)];
    char opcode = '7';
    int result;
    memcpy(buf, &opcode, 1);
    memcpy(buf+1, &session_id, sizeof(int));

    ssize_t wr = write(server_pipe, buf, 1+sizeof(int));
    if (wr == -1) {
        return -1;
    }
    ssize_t rr = read(client_pipe, rbuf, sizeof(int));
    if (rr == -1) {
        return -1;
    }

    memcpy(&result, rbuf, sizeof(int));

    return result;
}