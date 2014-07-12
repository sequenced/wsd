#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <syslog.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <pwd.h>
#include "wstypes.h"
#include "wschild.h"
#include "list.h"
#include "chat1.h" /* TODO make configurable */

static const char *ident="wsd";
static int drop_priv(uid_t new_uid);
static void sighup(int);
static int open_socket(int p);
static int fill_in_config_from_file(wsd_config_t *cfg);

int
main(int argc, char **argv)
{
  int opt;
  int port_arg=3000; /* default */
  int no_fork_arg=0;
  int verbose_arg=0;
  const char *host_arg=NULL;
  const char *user_arg=NULL;

  while ((opt=getopt(argc, argv, "h:p:u:fv"))!=-1)
    {
      switch (opt)
        {
        case 'h':
          host_arg=optarg;
          break;
        case 'u':
          user_arg=optarg;
          break;
        case 'p':
          port_arg=atoi(optarg);
          break;
        case 'f':
          no_fork_arg=1;
          break;
        case 'v':
          verbose_arg++;
          break;
        default:
          /* TODO */
          fprintf(stderr, "%s: unknown option\n", argv[0]);
          exit(1);
        }
    }

  if (NULL==host_arg)
    host_arg="127.0.0.1";

  if (NULL==user_arg)
    user_arg="wsd";

  struct passwd *pwent;
  if (NULL==(pwent=getpwnam(user_arg)))
    {
      fprintf(stderr, "unknown user: %s\n", user_arg);
      exit(1);
    }

  if (0==pwent->pw_uid)
    {
      fprintf(stderr, "daemon user can't be root\n");
      exit(1);
    }

  wsd_config_t cfg;
  memset(&cfg, 0x0, sizeof(wsd_config_t));
  cfg.uid=pwent->pw_uid;
  cfg.username=malloc(strlen(pwent->pw_name)+1);
  strcpy(cfg.username, pwent->pw_name);
  cfg.port=port_arg;
  cfg.verbose=verbose_arg;
  cfg.no_fork=no_fork_arg;

  if (0>fill_in_config_from_file(&cfg))
    {
      /* TODO */
      fprintf(stderr, "config file\n");
      exit(1);
    }

  openlog(ident, LOG_PID, LOG_USER);
  syslog(LOG_INFO, "starting");

  pid_t pid=0;
  if (!cfg.no_fork)
    {
      if (0>(pid=fork()))
        {
          perror("fork");
          exit(1);
        }
    }

  if (0==pid
      || cfg.no_fork)
    {
      /* child */
      if (0>(cfg.sock=open_socket(cfg.port)))
        {
          perror("open_socket");
          exit(1);
        }

      if (0==getuid())
        if (0>drop_priv(cfg.uid))
          {
            close(cfg.sock);
            exit(1);
          }

      int rv;
      rv=wschild_main(&cfg);

      if (cfg.no_fork)
        syslog(LOG_INFO, "stopping");

      exit(rv);
    }
  else
    {
      /* parent */
      struct sigaction act;
      memset(&act, 0x0, sizeof(struct sigaction));
      act.sa_handler=sighup;
      if (0>sigaction(SIGHUP, &act, NULL))
        {
          perror("sigaction");
          exit(1);
        }

      int status;
      if (0>waitpid(-1, &status, 0))
        {
          perror("wait");
          exit(1);
        }
    }

  syslog(LOG_INFO, "stopping");

  return 0;
}

static int
drop_priv(uid_t new_uid)
{
  if (0>setuid(new_uid))
    {
      perror("setuid");
      return -1;
    }

  return 0;
}

static void
sighup(int sig)
{
  /* signal children if any */
  kill(0, sig);
}

static int
open_socket(int p)
{
  int s;
  if (0>(s=socket(AF_INET, SOCK_STREAM|SOCK_NONBLOCK, 0)))
    return -1;

  struct sockaddr_in addr;
  memset(&addr, 0x0, sizeof(addr));
  addr.sin_family=AF_INET;
  addr.sin_addr.s_addr=INADDR_ANY;
  addr.sin_port=htons(p);
  if (0>bind(s, (struct sockaddr *)&addr, sizeof(struct sockaddr_in)))
    return -1;

  return s;
}

static int
fill_in_config_from_file(wsd_config_t *cfg)
{
  /* TODO actually read file */
  init_list_head(&cfg->location_list);

  location_config_t *loc;
  if (!(loc=malloc(sizeof(location_config_t))))
    return -1;

  loc->url="/chatterbox";
  loc->protocol="chat1";
  loc->on_data_frame=chat1_on_frame;
  loc->on_open=chat1_on_open;
  loc->on_close=chat1_on_close;

  list_add_tail(&loc->list_head, &cfg->location_list);

  return 0;
}
