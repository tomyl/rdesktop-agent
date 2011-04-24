/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   Agent protocol

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#define _GNU_SOURCE

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <limits.h>
#include <unistd.h>
#include <sys/select.h>

#include "rdesktop.h"

extern char g_agent[PATH_MAX];

int running = 0;
int pipein[2]; /* input from agent */
int pipeout[2]; /* output to agent */
char inbuf[2048];
int offset = 0;

void
agent_init()
{
    if (!*g_agent)
        return;

    if (pipe2(pipein, O_NONBLOCK) == -1) {
        perror("pipe");
        exit(EXIT_FAILURE);
    }

    if (pipe2(pipeout, O_NONBLOCK) == -1) {
        perror("pipe");
        exit(EXIT_FAILURE);
    }

    switch (fork()) {
        case -1:
            perror("fork");
            exit(EXIT_FAILURE);

        case 0: /* child */
            dup2(pipein[1], STDOUT_FILENO);
            dup2(pipeout[0], STDIN_FILENO);
            close(pipein[0]); /* close read end */
            close(pipeout[1]); /* close write end */
            execlp(g_agent, g_agent, NULL);
            perror("exec");
            exit(EXIT_FAILURE);

        default: /* parent */
            close(pipein[1]); /* close write end */
            close(pipeout[0]); /* close read end */
            running = 1;
    }

    agent_send("init\n");
}

void
agent_close()
{
    if (pipein[0])
        close(pipein[0]);
    if (pipeout[1])
        close(pipeout[1]);
    running = 0;
}

void
agent_add_fds(int *n, fd_set * rfds, fd_set * wfds)
{
    if (!running)
        return;

    *n = MAX(*n, pipein[0]);
    FD_SET(pipein[0], rfds);
}

void
agent_send(const char *fmt, ...)
{
    va_list ap;
    char buf[1024];

    if (!running)
        return;

    va_start(ap, fmt);
    vsprintf(buf, fmt, ap);
    va_end(ap);

    fprintf(stderr, "Sending to agent: %s", buf);
    
    if (write(pipeout[1], buf, strlen(buf)) < 0)
        perror("write");
}

void
agent_check_fds(fd_set * rfds, fd_set * wfds)
{
    int n;
    char *start;
    char *end;
    char *cur;
    char *next;

    char cmd[1024];
    char *arg;
    char *pos;

    if (!running)
        return;

    if (!FD_ISSET(pipein[0], rfds))
        return;

    fprintf(stderr, "READING FROM AGENT\n");
    start = inbuf + offset;
    n = read(pipein[0], start, 1023);

    if (n == -1)
        perror("read");
    else if (n == 0)
        agent_close();
    else {
        end = start+n;
        *end = '\0';
        cur = start;

        while (cur < end) {
            next = strchr(cur+1, '\n');
            if (!next)
                break;

            strncpy(cmd, cur, next-cur); 
            cmd[next-cur] = '\0';
            cur = next+1;

            arg = (char *) NULL;
            pos = strchr(cmd, ' ');

            if (pos) {
                *pos = '\0';
                arg = pos+1;
            } else {
                pos = strchr(cmd, '\n');
                if (pos) {
                    *pos = '\0';
                }
            }
            if (arg) {
                pos = strchr(arg, '\n');
                if (pos) {
                    *pos = '\0';
                }
            }

            fprintf(stderr, "From agent: cmd='%s' arg='%s'\n", cmd, arg);

            if (!strcmp(cmd, "exit")) {
                fprintf(stderr, "Exiting...\n");
                exit(0);
            } else if (!strcmp(cmd, "send")) {
                if (arg)
                    xkeymap_send_string(arg);
            }
        }

        offset = end-cur;

        if (offset)
            memcpy(inbuf, cur, offset);
    }
}

