/*
 *  Copyright (C) 2014-2018 Michael Goldschmidt
 *
 *  This file is part of wsd/wscat.
 *
 *  wsd/wscat is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  wsd/wscat is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with wsd/wscat.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"

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

#include "wschild.h"
#include "common.h"

#define DEFAULT_CLOSING_HANDSHAKE_TIMEOUT 8000   /* 8 seconds  */
#define DEFAULT_IDLE_TIMEOUT              -1     /* disabled   */
#define DEFAULT_FORWARD_PORT              "6085"
#define DEFAULT_FORWARD_HOST              "127.0.0.1"
#define DEFAULT_LISTENING_PORT            6084
#define DEFAULT_MAX_HOSTNAMES             16

static const char *ident = "wsd";
static int drop_priv(uid_t new_uid);
static int listen_sk_bind(const int port);
static void print_help();

int
main(int argc, char **argv)
{
     int opt;
     int pidfd;
     int o_arg = DEFAULT_LISTENING_PORT;
     bool d_arg = false;
     int i_arg = DEFAULT_IDLE_TIMEOUT;
     int v_arg = 0;
     int n_arg = -1;
     const char *f_arg = DEFAULT_FORWARD_PORT;
     const char *u_arg = NULL;
     const char *p_arg = NULL;
     const char *P_arg = NULL;
     unsigned int h_arg_num = 0;
     char **h_arg = calloc(sizeof *h_arg, DEFAULT_MAX_HOSTNAMES);
     A(h_arg);

     while ((opt = getopt(argc, argv, "h:p:P:o:f:u:i:n:dv?")) != -1) {
          switch (opt) {
          case 'h':
               if (h_arg_num >= DEFAULT_MAX_HOSTNAMES) {
                    fprintf(stderr,
                            "%s: too many hostnames (maximum is %u)\n",
                            argv[0],
                            DEFAULT_MAX_HOSTNAMES);
                    exit(EXIT_FAILURE);
               }
               h_arg[h_arg_num++] = optarg;
               break;
          case 'u':
               u_arg = optarg;
               break;
          case 'p':
               p_arg = optarg;
               break;
          case 'P':
               P_arg = optarg;
               break;
          case 'o':
               o_arg = atoi(optarg);
               break;
          case 'n':
               n_arg = atoi(optarg);
               break;
          case 'f':
               f_arg = optarg;
               break;
          case 'd':
               d_arg = true;
               break;
          case 'v':
               v_arg++;
               break;
          case 'i':
               i_arg = atoi(optarg);
               break;
          case '?':
               print_help(argv[0]);
               exit(EXIT_SUCCESS);
               break;
          default:
               fprintf(stderr,
                       "%s: unknown option\nTry '%s -?' for help.\n",
                       optarg,
                       argv[0]);
               exit(EXIT_FAILURE);
          }
     }

     if (0 == h_arg_num)
          h_arg[h_arg_num++] = DEFAULT_FORWARD_HOST;

     if (NULL == u_arg)
          u_arg = "wsd";

     if (0 > i_arg)
          i_arg = DEFAULT_IDLE_TIMEOUT;

     struct passwd *pwent;
     if (NULL == (pwent = getpwnam(u_arg))) {
          fprintf(stderr, "%s: unknown user: %s\n", argv[0], u_arg);
          exit(EXIT_FAILURE);
     }

     if (0 == pwent->pw_uid) {
          fprintf(stderr, "%s: daemon user can't be root\n", argv[0]);
          exit(EXIT_FAILURE);
     }

     if (p_arg) {
          pidfd = open(p_arg,
                       O_CREAT|O_EXCL|O_WRONLY,
                       S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
          if (0 > pidfd) {
               fprintf(stderr, "%s: cannot open: %s\n", argv[0], p_arg);
               exit(EXIT_FAILURE);
          }

          if (0 > fchown(pidfd, pwent->pw_uid, pwent->pw_gid)) {
               perror(argv[0]);
               exit(EXIT_FAILURE);
          }
     }

     wsd_config_t cfg;
     memset(&cfg, 0x0, sizeof(wsd_config_t));
     cfg.uid = pwent->pw_uid;
     cfg.lport = o_arg;
     cfg.fport = strdup(f_arg);
     cfg.fhostname = h_arg;
     cfg.fhostname_num = h_arg_num;
     cfg.verbose = v_arg;
     cfg.no_fork = d_arg;
     cfg.pidfilename = p_arg;
     cfg.sec_ws_proto = P_arg;
     cfg.idle_timeout = i_arg;
     cfg.ping_interval = n_arg * 1000; /* convert sec to ms */
     cfg.closing_handshake_timeout = DEFAULT_CLOSING_HANDSHAKE_TIMEOUT;

     pid_t pid = 0;
     if (!cfg.no_fork) {
          ERREXIT(0 > (pid = fork()), "fork");
     }

     if (0 == pid || cfg.no_fork) {
          umask(0);

          openlog(ident, LOG_PID, LOG_USER);
          syslog(LOG_INFO, "Starting");

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

          cfg.lfd = listen_sk_bind(cfg.lport);
          if (0 > cfg.lfd) {
               perror("listen_sk_bind");
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
          if (cfg.pidfilename)
               AZ(unlink(cfg.pidfilename));
          free(cfg.fport);
          free(cfg.fhostname[0]);
          free(cfg.fhostname);
          syslog(LOG_INFO, "Stopped");
          closelog();
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
listen_sk_bind(const int port)
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

void
print_help(const char *bin)
{
     printf("Usage: %s [OPTIONS]\n", bin);
     fputs("\
Terminate websockets and multiplex their frames to some backend.\n\n\
  -h  multiplex to host, defaults to 127.0.0.1\n\
  -f  multiplex to port, defaults to 6085\n\
  -o  listen on port for incoming websocket connections, defaults to 6084\n\
  -p  store process id in file\n\
  -P  sets expected Sec-WebSocket-Protocol HTTP request string, disabled by default\n\
  -u  run daemon as user, defaults to wsd\n\
  -d  do not fork and stay attached to terminal\n\
  -i  idle read/write timeout in milliseconds, disabled by default\n\
  -n  ping interval in seconds, defaults to none\n\
  -v  be verbose (use multiple times for maximum effect)\n\
  -?  display this help and exit\n\n\
", stdout);
     printf("Version %s - Please send bug reports to: %s\n",
            PACKAGE_VERSION,
            PACKAGE_BUGREPORT);
}
