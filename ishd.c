/*
 * This file is part of ish — the insecure shell.
 *
 * MIT License
 *
 * Copyright (c) 2018 Ted Percival
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#define _GNU_SOURCE 1

#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define BACKLOG 1
#define PORT "2323"

static void dup2_or_die(int oldfd, int newfd) {
    if (dup2(oldfd, newfd) == -1) {
        perror("dup2");
        exit(1);
    }
}

static void child(int s) __attribute__((noreturn));
static void child(int s) {
    dup2_or_die(s, STDIN_FILENO);
    dup2_or_die(s, STDOUT_FILENO);
    dup2_or_die(s, STDERR_FILENO);

    if (setsid() == -1) {
        perror("setsid");
        exit(1);
    }

    static char *const argv[] = {
        "/bin/sh",
        "-i",
        "-l",
        NULL,
    };

    execv("/bin/sh", argv);

    perror("execv");
    exit(1);
}

int main(void) {
    int s = socket(AF_INET6, SOCK_STREAM | SOCK_CLOEXEC, 0);

    if (s == -1) {
        perror("socket");
        return 1;
    }

    const struct addrinfo hints = {
        .ai_family = AF_INET6,
        .ai_socktype = SOCK_STREAM,
        .ai_protocol = 0,
        .ai_flags = AI_V4MAPPED | AI_PASSIVE,
    };

    struct addrinfo *addrs = NULL;

    int gaierr = getaddrinfo(NULL, PORT, &hints, &addrs);
    if (gaierr) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(gaierr));
        return 1;
    }

    if (addrs == NULL) {
        fprintf(stderr, "No addresses\n");
        return 1;
    }

    if (bind(s, addrs->ai_addr, addrs->ai_addrlen) == -1) {
        perror("bind");
        return 1;
    }

    if (listen(s, BACKLOG) == -1) {
        perror("listen");
        return 1;
    }

    // auto-reap children
    if (signal(SIGCHLD, SIG_IGN) == SIG_ERR) {
        perror("signal");
        return 1;
    }

    int client = -1;
    while ((client = accept4(s, NULL, NULL, SOCK_CLOEXEC)) != -1) {
        pid_t pid = fork();

        if (pid == -1) {
            perror("fork");
            break;
        }

        if (pid == 0) {
            close(s);
            child(client);
        }
    }

    if (close(s) == -1) {
        perror("close");
        return 1;
    }

    freeaddrinfo(addrs);

    return 1;
}
