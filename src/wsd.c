#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <syslog.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <pwd.h>
#include "config.h"
#include "wstypes.h"
#include "wschild.h"

static const char *ident = "wsd";
static int drop_priv(uid_t new_uid);
static int open_socket(int p);

int
main(int argc, char **argv)
{
     int opt;
     int port_arg = 6084; /* default */
     int no_fork_arg = 0;
     int verbose_arg = 0;
     const char *host_arg = NULL;
     const char *user_arg = NULL;

     while ((opt = getopt(argc, argv, "h:p:u:dv")) != -1) {
          switch (opt) {
          case 'h':
               host_arg = optarg;
               break;
          case 'u':
               user_arg = optarg;
               break;
          case 'p':
               port_arg = atoi(optarg);
               break;
          case 'd':
               no_fork_arg = 1;
               break;
          case 'v':
               verbose_arg++;
               break;
          default:
               fprintf(stderr, "wsd: %s: unknown option\n", argv[0]);
               exit(1);
          }
     }

     if (NULL == host_arg)
          host_arg = "127.0.0.1";

     if (NULL == user_arg)
          user_arg = "wsd";

     struct passwd *pwent;
     if (NULL == (pwent = getpwnam(user_arg))) {
          fprintf(stderr, "wsd: unknown user: %s\n", user_arg);
          exit(1);
     }

     if (0 == pwent->pw_uid) {
          fprintf(stderr, "wsd: daemon user can't be root\n");
          exit(1);
     }

     wsd_config_t cfg;
     memset(&cfg, 0x0, sizeof(wsd_config_t));
     cfg.uid = pwent->pw_uid;
     cfg.username = malloc(strlen(pwent->pw_name)+1);
     A(cfg.username);
     strcpy(cfg.username, pwent->pw_name);
     cfg.port = port_arg;
     cfg.verbose = verbose_arg;
     cfg.no_fork = no_fork_arg;

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

          if (!cfg.no_fork) {
               close(STDIN_FILENO);
               close(STDOUT_FILENO);
               close(STDERR_FILENO);
          }

          ERREXIT(0 > (cfg.lfd = open_socket(cfg.port)), "open_socket");

          if (0 == getuid()) {
               if (0 > drop_priv(cfg.uid)) {
                    AZ(close(cfg.lfd));
                    exit(1);
               }
          }

          int rv = wschild_main(&cfg);

          syslog(LOG_INFO, "stopped");
          closelog();

          exit(rv);
     }

     _exit(0);

     /* not reached */
     return 0;
}

static int
drop_priv(uid_t new_uid)
{
     ERRET(0 > setuid(new_uid), "setuid");

     return 0;
}

static int
open_socket(int p)
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
     addr.sin_port = htons(p);
     AZ(bind(s, (struct sockaddr *)&addr, sizeof(struct sockaddr_in)));

     return s;
}
