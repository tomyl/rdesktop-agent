/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   XXX

   Copyright (C) Matthew Chapman <matthewc.unsw.edu.au> 1999-2008
   Copyright (C) 2003-2008 Peter Astrand <astrand@cendio.se> for Cendio AB

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
FILE *fhin;
FILE *fhout;

void
agent_init()
{
    if (!*g_agent)
        return;

    if (pipe(pipein) == -1) {
        perror("pipe");
        exit(EXIT_FAILURE);
    }

    if (pipe(pipeout) == -1) {
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
            fhin = fdopen(pipein[0], "r");
            fhout = fdopen(pipeout[1], "w");
            running = 1;
    }

    //agent_send("init\n");
}

void
agent_close()
{
    if (pipein[0])
        close(pipein[0]);
    if (pipeout[1])
        close(pipeout[1]);
    if (fhin)
        fclose(fhin);
    if (fhout)
        fclose(fhout);
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

    if (!running)
        return;

    va_start(ap, fmt);
    vfprintf(fhout, fmt, ap);
    va_end(ap);

    fflush(fhout);
    //flush(pipeout[1]);
}

void
agent_check_fds(fd_set * rfds, fd_set * wfds)
{
    char cmd[1024];
    char *arg;
    char *pos;

    if (!running)
        return;

    if (!FD_ISSET(pipein[0], rfds))
        return;

    fprintf(stderr, "CHECK\n");

    while (fgets(cmd, 1024, fhin)) {
        fprintf(stderr, "== '%s'\n", cmd);
        arg = (char *) NULL;
        pos = strchr(cmd,  ' ');
        if (pos) {
            *pos = '\0';
            arg = pos+1;
        } else {
            pos = strchr(cmd,  '\n');
            if (pos) {
                *pos = '\0';
            }
        }
        if (arg) {
            pos = strchr(arg,  '\n');
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
}

