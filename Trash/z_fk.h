#include <sys/types.h>
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/user.h>
#include <sys/file.h>
#include <sys/sysctl.h>
#include <kvm.h> /* -lkvm */

#include <net/route.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <netinet/in.h>
#include <netinet/in_pcb.h>
#include <netinet/tcp.h>
#include <netinet/tcp_fsm.h>
#include <netinet/tcp_seq.h>
#include <netinet/tcp_var.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h> /* -lm */

#include <err.h>
#include <errno.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <dirent.h>
#include <ftw.h>
#include <pwd.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <signal.h>
#include <syslog.h>
#include <sys/event.h>
#include <setjmp.h>
#include <malloc_np.h>
#include <sys/mman.h>
#include <pthread.h> /* -lphread */

#define _uc		unsigned char
#define _c		char
#define _ui		unsigned int
#define _i		int
#define _ul		unsigned long int
#define _l		long int
#define _ull	unsigned long long int
#define _ll		long long int

#define KALV     -9
#define NEW      -8
#define AUTH     -7
#define ASK      -6

/** Common**/
void *z_free_source(void *arg);
void *z_check_self(void *arg);

void z_syslog(void);
void z_set_events(struct kevent *events, uintptr_t *fdlist, _i num, short filter, u_short flags, _ui fflags);
void z_exec_fork(const _c *cmd, _c **argv);
_c * z_str_to_base64(const _c *orig);
_c * z_int_to_base64(_i *orig, _i num);
_i z_port_alloc(_i port);
_i z_uid_alloc(void);
_i z_uid_check(_i uid);
_i z_username_to_uid(const _c *username);

_i z_creat_account(void);

void z_set_fw_in(_c *ruleNo, _c *setNo, _c *foreignaddr, _c *localaddr, _c *localport);
void z_set_fw_out(_c *ruleNo, _c *setNo, _c *localaddr, _c *foreignaddr, _c *foreignport);
void z_set_net_bandwidth(_c *ruleNo, _c *setNo, _c *pipeNo, _c *localaddr, _c *localport, _c *foreignaddr, _c *foreignport);
void z_unset_fw_policy(_c *ruleNo, _c *setNo);
void z_set_net_bandwidth_bypid(pid_t pid);
void z_unset_fw_policy_bypid(pid_t pid);
void z_set_net_bandwidth_bysd(_i sd, _c *laddr, _c *lport);
void z_set_fw_in_bysd(_i sd, _c *faddr);
void z_unset_fw_policy_bysd(_i sd);

void z_init_hints(struct addrinfo *hints, _i hflags);
_i z_try_connect(_i domain, _i type, _i protocol, struct sockaddr *addr, socklen_t len);
_i z_establish_connect(_c *host, _c *port, _i hflags);
void z_get_result(_c *request, _c *host, _c *port, _i hflags);

_i z_pid_to_euid(_i pid);

struct z_cmd_info ** z_getcmdinfo(_c *cmd);

void z_free_source_0(_c *cmd);
_i z_free_source_bysd(_i sd);
void z_free_source_1(void);
_i z_update_sdlist(_i sd);

_i z_checksshdstatus(void);

void z_daemonize(const _c *workdir);
struct xfile * z_get_fdlist(_i *num_of_fd);
void z_close_fds(void);

void z_set_sysctl_param(void);
_i z_nftw_subfunc(const _c *name, const struct stat *statbuf, _i typeflags, struct FTW *ftw);
_i z_set_sys_permissions(void);
void z_init_fw(void);
void z_link_domain (_i hflags);
void z_mmap_fk(void);

_i z_major_KQueue(uintptr_t ppid);
_i z_server_action(_i sd);
_i z_recv_authfile(_i sd, _i uid);
_i z_send_authfile(_i sd, _i uid);

void *z_homedir_KQueue(void *homepath);
uintptr_t * z_get_authfilelist (_i *num);
_i z_check_size(_i fd);

struct z_sockd_info * z_getsdinfo(pid_t pid);
