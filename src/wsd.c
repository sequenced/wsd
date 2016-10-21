#include "config.h"
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <syslog.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <pwd.h>
#include "wstypes.h"
#include "wschild.h"
#include "list.h"
#include "config_parser.h"

static const char *ident = "wsd";
static int drop_priv(uid_t new_uid);
static int open_socket(int p);
static int fill_in_config_from_file(wsd_config_t *cfg, const char* filename);
static int resolve_dl_dependencies(struct list_head *parent);

int
main(int argc, char **argv)
{
     int opt;
     int port_arg = 6084; /* default */
     int no_fork_arg = 0;
     int verbose_arg = 0;
     const char *filename_arg = "/etc/wsd.conf";   /* default */
     const char *host_arg = NULL;
     const char *user_arg = NULL;

     while ((opt = getopt(argc, argv, "h:p:u:f:dv")) != -1)
     {
          switch (opt)
          {
          case 'f':
               filename_arg = optarg;
               break;
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
               /* TODO */
               fprintf(stderr, "wsd: %s: unknown option\n", argv[0]);
               exit(1);
          }
     }

     if (NULL == host_arg)
          host_arg = "127.0.0.1";

     if (NULL == user_arg)
          user_arg = "wsd";

     struct passwd *pwent;
     if (NULL == (pwent = getpwnam(user_arg)))
     {
          fprintf(stderr, "wsd: unknown user: %s\n", user_arg);
          exit(1);
     }

     if (0 == pwent->pw_uid)
     {
          fprintf(stderr, "wsd: daemon user can't be root\n");
          exit(1);
     }

     wsd_config_t cfg;
     memset(&cfg, 0x0, sizeof(wsd_config_t));
     cfg.uid = pwent->pw_uid;
     cfg.username = malloc(strlen(pwent->pw_name)+1);
     strcpy(cfg.username, pwent->pw_name);
     cfg.port = port_arg;
     cfg.verbose = verbose_arg;
     cfg.no_fork = no_fork_arg;
     cfg.register_user_fd = wschild_register_user_fd;
     cfg.lookup_kernel_fd = wschild_lookup_kernel_fd;

     if (0 > fill_in_config_from_file(&cfg, filename_arg))
          exit(1);

     pid_t pid = 0;
     if (!cfg.no_fork)
     {
          if (0 > (pid = fork()))
          {
               perror("wsd: fork");
               exit(1);
          }
     }

     if (0 == pid || cfg.no_fork)
     {
          umask(0);

          openlog(ident, LOG_PID, LOG_USER);
          syslog(LOG_INFO, "starting");

          if (!cfg.no_fork && 0 > setsid())
          {
               perror("wsd: setsid");
               exit(1);
          }

          if (0 > chdir("/"))
          {
               perror("wsd: chdir");
               exit(1);
          }

          if (!cfg.no_fork)
          {
               close(STDIN_FILENO);
               close(STDOUT_FILENO);
               close(STDERR_FILENO);
          }

          /* child */
          if (0 > (cfg.lfd = open_socket(cfg.port)))
          {
               perror("wsd: open_socket");
               exit(1);
          }

          if (0 == getuid())
               if (0 > drop_priv(cfg.uid))
               {
                    close(cfg.lfd);
                    exit(1);
               }

          int rv;
          rv = wschild_main(&cfg);

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
     if (0 > setuid(new_uid))
     {
          perror("wsd: setuid");
          return -1;
     }

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

     if (s < 0)
          return (-1);

#ifndef SYS_LINUX
     /* TODO set non-blocking */
#endif  

     int opt = 1;
     if (0 > setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(int)))
          return (-1);
     
     struct sockaddr_in addr;
     memset(&addr, 0x0, sizeof(addr));
     addr.sin_family = AF_INET;
     addr.sin_addr.s_addr = INADDR_ANY;
     addr.sin_port = htons(p);
     if (0 > bind(s, (struct sockaddr *)&addr, sizeof(struct sockaddr_in)))
          return -1;

     return s;
}

static int
fill_in_config_from_file(wsd_config_t *cfg, const char* filename)
{
     int fd;
     if (0 > (fd = open(filename, O_RDONLY)))
     {
          perror("wsd: open");
          return -1;
     }

     struct stat sb;
     if (0 > fstat(fd, &sb))
     {
          perror("wsd: fstat");
          close(fd);
          return -1;
     }

     buf_t *b;
     if (NULL == (b = buf_alloc(sb.st_size)))
     {
          perror("wsd: buf_alloc");
          close(fd);
          return -1;
     }

     int len = read(fd, buf_ref(b), buf_len(b));
     close(fd);
     if (0 > len)
     {
          perror("wsd: read");
          buf_free(b);
          return -1;
     }
     buf_fwd(b, len);
     buf_flip(b);

     init_list_head(&cfg->location_list);

     if (0 > parse_config(&cfg->location_list, b))
          return -1;

     if (0 > resolve_dl_dependencies(&cfg->location_list))
          return -1;

     return 0;
}

static int
resolve_dl_dependencies(struct list_head *parent)
{
     const int buf_len = CONFIG_MAX_VALUE_LENGTH*2; 
     char buf[buf_len];

     location_config_t *cursor;
     list_for_each_entry(cursor, parent, list_head)
     {
          memset((void*)buf, 0x0, buf_len);
          sprintf(buf, "lib%s.so", cursor->protocol);

          void *handle = dlopen(buf, RTLD_NOW);
          if (NULL == handle)
          {
               fprintf(stderr, "wsd: %s\n", dlerror());
               return -1;
          }

          memset((void*)buf, 0x0, buf_len);
          sprintf(buf, "%s_on_frame", cursor->protocol);
          cursor->on_data_frame = dlsym(handle, buf);
          if (NULL == cursor->on_data_frame)
          {
               fprintf(stderr, "wsd: %s\n", dlerror());
               return -1;
          }

          memset((void*)buf, 0x0, buf_len);
          sprintf(buf, "%s_on_open", cursor->protocol);
          cursor->on_open = dlsym(handle, buf);
          if (NULL == cursor->on_open)
          {
               fprintf(stderr, "wsd: %s\n", dlerror());
               return -1;
          }

          memset((void*)buf, 0x0, buf_len);
          sprintf(buf, "%s_on_close", cursor->protocol);
          cursor->on_close = dlsym(handle, buf);
          if (NULL == cursor->on_close)
          {
               fprintf(stderr, "wsd: %s\n", dlerror());
               return -1;
          }

          /* TODO retain dl handle for unloading later */
     }

     return 0;
}
