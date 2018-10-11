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

static void die(const char *message) __attribute__((noreturn));
static void die(const char *message) {
    fprintf(stderr, "%s\n", message);
    exit(1);
}

static void die_with_error(const char *message) __attribute__((noreturn));
static void die_with_error(const char *message) {
    perror(message);
    exit(1);
}

static void dup2_or_die(int oldfd, int newfd) {
    if (dup2(oldfd, newfd) == -1)
        die_with_error("dup2");
}

static void set_io_fds(int oldfd, const int newfds[], size_t num_newfds) {
    for (size_t i = 0; i < num_newfds; ++i)
        dup2_or_die(oldfd, newfds[i]);
}

static const int IO_FDS[] = {
    STDIN_FILENO,
    STDOUT_FILENO,
    STDERR_FILENO,
};

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static char *const *shell_argv(void) {
    static char *const argv[] = {
        "/bin/sh",
        "-i",
        "-l",
        NULL,
    };

    return argv;
}

static void exec_shell(void) __attribute__((noreturn));
static void exec_shell(void) {
    execv("/bin/sh", shell_argv());
    die_with_error("execv");
}

static void new_session() {
    if (setsid() == -1)
        die_with_error("setsid");
}

static void child(int server, int client) __attribute__((noreturn));
static void child(int server, int client) {
    close(server);
    set_io_fds(client, IO_FDS, ARRAY_SIZE(IO_FDS));
    new_session();
    exec_shell();
}

static const struct addrinfo *get_server_address_hints() {
    static const struct addrinfo hints = {
        .ai_family = AF_INET6,
        .ai_socktype = SOCK_STREAM,
        .ai_protocol = 0,
        .ai_flags = AI_V4MAPPED | AI_PASSIVE,
    };

    return &hints;
}

static int get_server_tcp_socket(void) {
    int s = socket(AF_INET6, SOCK_STREAM | SOCK_CLOEXEC, 0);

    if (s == -1)
        die_with_error("socket");

    return s;
}

static void validate_getaddrinfo_or_die(int gaierr, struct addrinfo *addrs) {
    if (gaierr)
        die(gai_strerror(gaierr));

    if (addrs == NULL)
        die("No addresses");
}

static struct addrinfo *get_bind_addresses_and_check_gai_error(void) {
    struct addrinfo *addrs = NULL;

    int gaierr = getaddrinfo(NULL, PORT, get_server_address_hints(), &addrs);

    validate_getaddrinfo_or_die(gaierr, addrs);

    return addrs;
}

// Must call freeaddrinfo() on returned pointer
static struct addrinfo *get_bind_addresses(void) {
    struct addrinfo *addrs = get_bind_addresses_and_check_gai_error();
    if (addrs == NULL)
        die("No addresses");

    return addrs;
}

static void bind_server_address(int sock) {
    struct addrinfo *addrs = get_bind_addresses();
    if (bind(sock, addrs->ai_addr, addrs->ai_addrlen) == -1)
        die_with_error("bind");
    freeaddrinfo(addrs);
}

static void start_listening(int sock) {
    if (listen(sock, BACKLOG) == -1)
        die_with_error("listen");
}

static void prepare_server_socket(int sock) {
    bind_server_address(sock);
    start_listening(sock);
}

static int get_server_socket(void) {
    int sock = get_server_tcp_socket();
    prepare_server_socket(sock);
    return sock;
}

static void enable_child_process_auto_reap(void) {
    if (signal(SIGCHLD, SIG_IGN) == SIG_ERR)
        die_with_error("signal");
}

static pid_t fork_or_die(void) {
    pid_t pid = fork();

    if (pid == -1)
        die_with_error("fork");

    return pid;
}

static void handle_client(int server, int client) {
    pid_t pid = fork_or_die();

    if (pid == 0)
        child(server, client);
}

static void handle_clients(int sock) {
    int client = -1;

    while ((client = accept4(sock, NULL, NULL, SOCK_CLOEXEC)) != -1)
        handle_client(sock, client);
}

static void cleanup_server(int server) {
    if (close(server) == -1)
        die_with_error("server close");
}

static void run_until_death(int server) {
    enable_child_process_auto_reap();
    handle_clients(server);
    cleanup_server(server);
}

int main(void) {
    run_until_death(get_server_socket());
    return 1;
}
