/*
 *  Copyright (C) 2014-2017 Michael Goldschmidt
 *
 *  This file is part of wsd.
 *
 *  wsd is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  wsd is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with wsd.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <stdlib.h>
#include <syslog.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <pwd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <netinet/in.h>

#include "config.h"
#include "wschild.h"
#include "common.h"

static const char *ident = "wsd";
static int drop_priv(uid_t new_uid);
static int listen_sock_bind(const int port);

int
main(int argc, char **argv)
{
     int opt;
     int pidfd;
     int port_arg = 6084;                 /* default */
     int no_fork_arg = 0;
     int idle_timeout_arg = -1;
     int verbose_arg = 0;
     const char *fwd_port_arg = "6085";   /* default */
     const char *fwd_hostname_arg = NULL;
     const char *user_arg = NULL;
     const char *pidfile_arg = NULL;

     while ((opt = getopt(argc, argv, "h:p:o:f:u:i:dv")) != -1) {
          switch (opt) {
          case 'h':
               fwd_hostname_arg = optarg;
               break;
          case 'u':
               user_arg = optarg;
               break;
          case 'p':
               pidfile_arg = optarg;
               break;
          case 'o':
               port_arg = atoi(optarg);
               break;
          case 'f':
               fwd_port_arg = optarg;
               break;
          case 'd':
               no_fork_arg = 1;
               break;
          case 'v':
               verbose_arg++;
               break;
          case 'i':
               idle_timeout_arg = atoi(optarg);
               break;
          default:
               fprintf(stderr, "wsd: %s: unknown option\n", argv[0]);
               exit(EXIT_FAILURE);
          }
     }

     if (NULL == fwd_hostname_arg)
          fwd_hostname_arg = "127.0.0.1";

     if (NULL == user_arg)
          user_arg = "wsd";

     struct passwd *pwent;
     if (NULL == (pwent = getpwnam(user_arg))) {
          fprintf(stderr, "%s: unknown user: %s\n", argv[0], user_arg);
          exit(EXIT_FAILURE);
     }

     if (0 == pwent->pw_uid) {
          fprintf(stderr, "%s: daemon user can't be root\n", argv[0]);
          exit(EXIT_FAILURE);
     }

     if (pidfile_arg) {
          pidfd = open(pidfile_arg,
                       O_CREAT|O_EXCL|O_WRONLY,
                       S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
          if (0 > pidfd) {
               fprintf(stderr, "%s: cannot open: %s\n", argv[0], pidfile_arg);
               exit(EXIT_FAILURE);
          }

          AZ(fchown(pidfd, pwent->pw_uid, pwent->pw_gid));
     }

     wsd_config_t cfg;
     memset(&cfg, 0x0, sizeof(wsd_config_t));
     cfg.uid = pwent->pw_uid;
     cfg.port = port_arg;
     cfg.fwd_port = malloc(strlen(fwd_port_arg) + 1);
     A(cfg.fwd_port);
     strcpy(cfg.fwd_port, fwd_port_arg);
     cfg.fwd_hostname = malloc(strlen(fwd_hostname_arg) + 1);
     A(cfg.fwd_hostname);
     strcpy(cfg.fwd_hostname, fwd_hostname_arg);
     cfg.verbose = verbose_arg;
     cfg.no_fork = no_fork_arg;
     cfg.pidfilename = pidfile_arg;
     cfg.idle_timeout = idle_timeout_arg;

     pid_t pid = 0;
     if (!cfg.no_fork) {
          ERREXIT(0 > (pid = fork()), "fork");
     }

     if (0 == pid || cfg.no_fork) {
          umask(0);

          openlog(ident, LOG_PID, LOG_USER);
          syslog(LOG_INFO, "starting");

          if (!cfg.no_fork) {
               ERREXIT(0 > setsid(), "setsid");
          }

          ERREXIT(0 > chdir("/"), "chdir");

          if (cfg.pidfilename) {
               char pidstr[16];
               if (0 > snprintf(pidstr, sizeof(pidstr),
                                "%ju",
                                (uintmax_t)getpid())) {
                    unlink(cfg.pidfilename);
                    perror("snprintf");
                    exit(EXIT_FAILURE);
               }

               if (0 > write(pidfd, pidstr, strlen(pidstr))) {
                    unlink(cfg.pidfilename);
                    perror("write");
                    exit(EXIT_FAILURE);
               }

               AZ(close(pidfd));
          }
          
          if (!cfg.no_fork) {
               close(STDIN_FILENO);
               close(STDOUT_FILENO);
               close(STDERR_FILENO);
          }

          cfg.lfd = listen_sock_bind(cfg.port);
          if (0 > cfg.lfd) {
               perror("listen_sock_bind");
               exit(EXIT_FAILURE);
          }

          if (0 == getuid()) {
               if (0 > drop_priv(cfg.uid)) {
                    AZ(close(cfg.lfd));
                    exit(EXIT_FAILURE);
               }
          }

          int rv = wschild_main(&cfg);

          AZ(close(cfg.lfd));
          
          syslog(LOG_INFO, "stopped");
          closelog();

          if (cfg.pidfilename)
               AZ(unlink(cfg.pidfilename));

          exit(rv);
     }

     _exit(EXIT_SUCCESS);

     /* not reached */
     return EXIT_SUCCESS;
}

static int
drop_priv(uid_t new_uid)
{
     ERRET(0 > setuid(new_uid), "setuid");

     return 0;
}

int
listen_sock_bind(const int port)
{
     int s;
#ifdef SYS_LINUX
     s = socket(AF_INET, SOCK_STREAM|SOCK_NONBLOCK, 0);
#else
     s = socket(AF_INET, SOCK_STREAM, 0);
#endif

     A(s >= 0);

#ifndef SYS_LINUX
     /* TODO set non-blocking */
#endif

     int opt = 1;
     AZ(setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(int)));

     struct sockaddr_in addr;
     memset(&addr, 0x0, sizeof(addr));
     addr.sin_family = AF_INET;
     addr.sin_addr.s_addr = INADDR_ANY;
     addr.sin_port = htons(port);
     AZ(bind(s, (struct sockaddr *)&addr, sizeof(struct sockaddr_in)));

     return s;
}
