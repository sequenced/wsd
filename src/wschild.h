#ifndef __WSCHILD_H__
#define __WSCHILD_H__

int wschild_main(const wsd_config_t *cfg);
int wschild_register_user_fd(int fd,
                             int (*on_read)(struct wsconn *conn),
                             int (*on_write)(struct wsconn *conn),
                             short events);
struct wsconn* wschild_lookup_kernel_fd(int fd);

#endif /* #ifndef __WSCHILD_H__ */
