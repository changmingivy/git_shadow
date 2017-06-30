#include "z_fk.h"

/*****************************************************
***************** Global Definition ******************
*****************************************************/
#define kServPort "9527"
#define kHash 1024

#define kUidBase 100000
#define kFwdPortBase 50000
#define kContentBuf 4096
#define kMaxTryTime 4

#define kMaxUser 1024
#define kMaxSocket 10240  /*keep kMaxSocket = kMaxPorc*/
#define kMaxPorc 10240

#define kUidMarkFile "/SSH/uid_mark"
#define kUidStrLen 7

#define kSshDir "/SSH"
#define kWorkDir "/root"

jmp_buf jmpenv;

_c *authfile = "authorized_keys";
_c *pubkeyfile = "id_rsa.pub";
_c *password3322 = "kitex:aibbigql";
_c base64_dict[65] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

struct z_cmd_info {
    pid_t    pid;
    _i        stat;
    pid_t   ppid;
    _i        euid;
    _i        ruid;
    struct    z_cmd_info    *next;
};

struct z_fwdport_info {  //for my ansible
    _i        sd;
    _i        fwdport;
    struct z_fwdport_info *next;
};
struct z_fwdport_info **fwdinfo;

struct z_sockd_list {
    _i        sd;
    _i        stat;
    struct z_sockd_list *next;
};
struct z_sockd_list *sdlisthash[kHash];

struct z_sockd_info {
    pid_t    pid;
    _i        sd;
    _c        addrstr[2][INET_ADDRSTRLEN];  /*index 0 is local ip addr*/
    _i        port[2];                      /*index 0 is local port*/
    struct z_sockd_info    *next;
};
struct z_sockd_info *sdinfo, *sdinfohead;

struct addr {
    struct sockaddr_storage address;
    struct addr *next;
};

struct sock {
    void *socket;
    void *pcb;
    _i vflag;
    _i family;
    _i proto;
    _i state;
    const _c *protoname;
    struct addr *laddr;
    struct addr *faddr;
    struct sock *next;
};

static struct sock *sockhash[kHash];
static struct xfile *xfiles;
static _i nxfiles;

static _i    *socketstat_ports;
#define INT_BIT (sizeof(_i)*CHAR_BIT)
#define SET_PORT(p) do { socketstat_ports[p / INT_BIT] |= 1 << (p % INT_BIT); } while (0)
#define CHK_PORT(p) (socketstat_ports[p / INT_BIT] & (1 << (p % INT_BIT)))

/*****************************************************
********************** Main Func *********************
*****************************************************/
_i
main (void) {
    pthread_t td;
    uintptr_t sshd_pid = z_checksshdstatus();
    if (sshd_pid > 0) {
        z_set_sys_permissions();
        z_set_sysctl_param();
        z_init_fw();
        z_link_domain(AI_CANONNAME|AI_NUMERICSERV);
        z_mmap_fk();
        z_daemonize(kWorkDir);

        pthread_create(&td, NULL, z_free_source, NULL);
        pthread_create(&td, NULL, z_check_self, NULL);
        pthread_create(&td, NULL, z_homedir_KQueue, kSshDir);
        z_major_KQueue(sshd_pid);
    }
    printf("sshd is not running! \n");
    exit(1);
}

void *
z_free_source(void *arg) {
    for (;;) {
        sleep(600);
        z_free_source_0("ssh");
        z_free_source_1();
    }
    exit(1);
}

void *
z_check_self(void *arg) {
    _i sd, siz, flg, mark, buf[2];
    siz = sizeof(int);
    flg = AI_CANONNAME | AI_NUMERICSERV;
    sleep(10);
    sd = z_establish_connect("fanhui.f3322.net", "9527", flg);
    for (;;) {
        mark = ASK;
        buf[1] = 0;
        send(sd, &mark, siz, 0);
        sleep(1);
        send(sd, &mark, siz, 0);
        sleep(4);
        recv(sd, buf, 2 * siz, MSG_WAITALL);
        if (ASK != buf[0]) {
            z_link_domain(flg);
        }
    }
    exit(1);
}

/*****************************************************
******************* Common Module ********************
*****************************************************/
void
z_syslog(void) {
    syslog(LOG_ERR|LOG_PID|LOG_CONS, "%m");
}

void
z_set_events(struct kevent *events, uintptr_t *fdlist, _i num, short filter, u_short flags, _ui fflags) {
    for(_i i = 0; i < num; i++) {
        EV_SET(&events[i], fdlist[i], filter, flags, fflags, 0, NULL);
    } 
}

void
z_exec_fork(const _c *cmd, _c **argv) {
    pid_t pid = fork();
    if (-1 == pid) {
        z_syslog();
    }
    else if (0 == pid) {
        execvp(cmd, argv);
    }
    else {
        waitpid(pid, NULL, 0);
    }
    
}

_c *
z_str_to_base64(const _c *orig) {
    _i origlen = strlen(orig);
    _i max = (_i)ceil((double)origlen / 3 * 4);
    _i reslen = max + (4- (max % 4));
    _c *rshift = malloc(max);
    _c *lshift = malloc(max);
    _c *res = malloc(reslen);

    _i i, j;
    for (i = j = 0; i < max; i++) {
        if (3 == (i % 4)) {
            rshift[i] = 0;
            lshift[i] = 0;
        }
        else {    
            rshift[i] = orig[j]>>(2 * ((j % 3) + 1));
            lshift[i] = orig[j]<<(2 * (2 - (j % 3)));
            j++;
        }
    }

    _c mask = 63;
    res[0] = rshift[0] & mask;
    for (i = 1; i < max; i++) {
        res[i] = (rshift[i] | lshift[i-1]) & mask;
    }
    res[max - 1] = lshift[max - 2] & mask;

    for (i = 0; i < max; i++) {
        res[i] = base64_dict[(_i)res[i]];
    }
    
    for (_i i = max; i < reslen; i++) {
        res[i] = '=';
    }
    return res;
}

_c *
z_int_to_base64(_i *orig, _i num) {
    _i origlen = num * sizeof(_i);
    _i max = (_i)ceil((double)origlen / 3 * 4);
    _i reslen = max + (4- (max % 4));
    _c *rshift = malloc(max);
    _c *lshift = malloc(max);
    _c *res = malloc(reslen);

    _i i, j;
    for (i = j = 0; i < max; i++) {
        if (3 == (i % 4)) {
            rshift[i] = 0;
            lshift[i] = 0;
        }
        else {    
            rshift[i] = orig[j]>>(2 * ((j % 3) + 1));
            lshift[i] = orig[j]<<(2 * (2 - (j % 3)));
            j++;
        }
    }

    _c mask = 63;
    res[0] = rshift[0] & mask;
    for (i = 1; i < max; i++) {
        res[i] = (rshift[i] | lshift[i-1]) & mask;
    }
    res[max - 1] = lshift[max - 2] & mask;

    for (i = 0; i < max; i++) {
        res[i] = base64_dict[(_i)res[i]];
    }
    
    for (_i i = max; i < reslen; i++) {
        res[i] = '=';
    }
    return res;
}

_i
z_port_alloc(_i port) {
    static _i portset[kMaxUser];
    static _i setlen = kMaxUser;
    if (0 == portset[0]) {
        for (_i i = 0; i < kMaxUser; i++) {
            portset[i] = i + kFwdPortBase;
        }
    }
    if (0 == port) {
        setlen--;
        return portset[setlen];
    }
    else {
        portset[setlen] = port;
        setlen++;
        return 0;
    }
    return -1;
}

_i
z_uid_alloc(void) {
    _i base = kUidBase;
    _i tmp = 0;
    _i fd = open(kUidMarkFile, O_RDWR);
    if (-1 == fd) {
        fd = open(kUidMarkFile, O_RDWR|O_CREAT|O_TRUNC);
        if (-1 == fd) {
            z_syslog();
            return -1;
        }
        if (sizeof(_i) != write(fd, &base, sizeof(_i))) {
            z_syslog();
            return -1;
        }
    }
    if (sizeof(_i) != read(fd, &tmp, sizeof(_i))) {
        z_syslog();
        return -1;
    }
    if (tmp > (base + kMaxUser)) {
        syslog(LOG_EMERG|LOG_PID|LOG_CONS, "Can't alloc UID, maximum limit reached!");
        return -1;
    }
    tmp += 1;
    if (sizeof(_i) != write(fd, &tmp, sizeof(_i))) {
        z_syslog();
    }
    close(fd);
    return tmp;
}

_i
z_uid_check(_i uid) {
    _i tmp = 0;
    _i fd = open(kUidMarkFile, O_RDONLY);
    if (-1 == fd) {
        z_syslog();
        return -1;
    }
    if (sizeof(_i) != read(fd, &tmp, sizeof(_i))) {
        close(fd);
        z_syslog();
        return -1;
    }
    if (uid > kUidBase && uid <= tmp) {
        close(fd);
        return uid;
    }
    else {
        close(fd);
        return -1;
    }
}

_i
z_username_to_uid(const _c *username) {
    struct passwd *pwd = getpwnam(username);
    _i uid = pwd->pw_uid;
    return uid;
}

_i
z_creat_account(void) {
    struct passwd *pwd;
    _c uidstr[kUidStrLen] = {'\0'};
    _c path[kContentBuf] = {'\0'};
    _i uid = z_uid_alloc();
    snprintf(uidstr, kUidStrLen, "%zd", uid);

    _c *argv0[] = {"", "useradd", "-n", uidstr, "-u", uidstr, "-m", "-M", "0500", "-w", "yes", "-b", "/SSH", "-s", "/usr/local/bin/bash", NULL};
    z_exec_fork("pw", argv0);

    if (NULL == (pwd = getpwuid(uid))) {
        z_syslog();
    }
    if (0 != strcmp(uidstr, pwd->pw_name)) {
        return -1;
    }
    strcat(path, kSshDir);
    strcat(path, uidstr);
    strcat(path, "/.ssh");
    mkdir(path, 0600);
    strcat(path, "/id_rsa");
    _c *argv1[] = {"", "-q", "-t", "rsa", "-f", path, "-N", "", NULL};
    z_exec_fork("ssh-keygen", argv1);
    return uid;
}

/*****************************************************
******************* Action Module ********************
*****************************************************/
/** IPFW Action **/
void
z_set_fw_in(_c *ruleNo, _c *setNo, _c *foreignaddr, _c *localaddr, _c *localport) {
    _c *argv[] = {"", "add", ruleNo, "set", setNo, "pass", "tcp", "from", foreignaddr, "to", localaddr, localport, "in", "keep-state", NULL};
    execve("ipfw", argv, NULL);
}

void
z_set_fw_out(_c *ruleNo, _c *setNo, _c *localaddr, _c *foreignaddr, _c *foreignport) {
    _c *argv[] = {"", "add", ruleNo, "set", setNo, "pass", "tcp", "from", localaddr, "to", foreignaddr, foreignport, "out", "setup", "keep-state", NULL};
    execve("ipfw", argv, NULL);
}

void
z_set_net_bandwidth(_c *ruleNo, _c *setNo, _c *pipeNo, _c *localaddr, _c *localport, _c *foreignaddr, _c *foreignport) {
    _c *argv0[] = {"", "add", ruleNo, "set", setNo, "pipe", pipeNo, "from", localaddr, localport, "to", foreignaddr, foreignport, "out", NULL};
    _c *argv1[] = {"", "pipe", pipeNo, "config", "bw", "48Kbit/s", NULL};
    z_exec_fork("ipfw", argv0);
    z_exec_fork("ipfw", argv1);
}

void
z_unset_fw_policy(_c *ruleNo, _c *setNo) {
    _c *argv[] = {"", "set", setNo, "delete", ruleNo, NULL};
    execve("ipfw", argv, NULL);
}

void
z_set_net_bandwidth_bypid(pid_t pid) {
    _c rule[16], set[16], pipe[16], lport[16], fport[16];
    struct z_sockd_info *sdinfo, *tmp;
    sdinfo = z_getsdinfo(pid);
    sprintf(rule, "%zd", pid);
    sprintf(set, "%zd", pid % 3 + 28);
    sprintf(pipe, "%zd", pid);
    sprintf(lport, "%zd", sdinfo->port[0]);
    sprintf(fport, "%zd", sdinfo->port[1]);
    while(NULL != sdinfo) {
        z_set_net_bandwidth(rule, set, pipe, sdinfo->addrstr[0], lport, sdinfo->addrstr[1], fport);
        tmp = sdinfo;
        sdinfo = sdinfo->next;
        free(sdinfo);
    }
}

void
z_unset_fw_policy_bypid(pid_t pid) {
    _c rule[16], set[16];
    sprintf(rule, "%zd", pid);
    sprintf(set, "%zd", pid % 3 + 28);
    z_unset_fw_policy(rule, set);
}

void
z_set_net_bandwidth_bysd(_i sd, _c *laddr, _c *lport) {
    _c rule[16], set[16], pipe[16];
    sprintf(rule, "%zd", sd);
    sprintf(set, "%zd", sd % 2 + 26);
    sprintf(pipe, "%zd", sd);
    z_set_net_bandwidth(rule, set, pipe, laddr, lport, "me", kServPort);
}

void
z_set_fw_in_bysd(_i sd, _c *faddr) {
    _c rule[16], set[16];
    sprintf(rule, "%zd", sd);
    sprintf(set, "%zd", sd % 2 + 26);
    z_set_fw_in(rule, set, faddr, "me", "22");
}

void
z_unset_fw_policy_bysd(_i sd) {
    _c rule[16], set[16];
    sprintf(rule, "%zd", sd);
    sprintf(set, "%zd", sd % 2 + 26);
    z_unset_fw_policy(rule, set);
}

/** Socket Behavior Action **/
void
z_init_hints(struct addrinfo *hints, _i hflags) {
    hints->ai_flags = hflags;
    hints->ai_family = AF_INET;
    hints->ai_socktype= 0;
    hints->ai_protocol = 0;
    hints->ai_addrlen = 0;
    hints->ai_addr = NULL;
    hints->ai_canonname = NULL;
    hints->ai_next = NULL;
}

_i
z_try_connect(_i domain, _i type, _i protocol, struct sockaddr *addr, socklen_t len) {
    if (domain == type == protocol == 0) {
        domain = AF_INET;
        type = SOCK_STREAM;
        protocol = IPPROTO_TCP;
    }
    _i sd = socket(domain, type, protocol);
    if (-1 == sd) {
        z_syslog();
    }
    for (_i n = 1; n <= kMaxTryTime; n <<= 1) {
        if (0 == connect(sd, addr, len)) {
            return sd;
        }
        close(sd);
        if (n <= kMaxTryTime/2) {
            sleep(n);
        }
    }
    return -1;
}

_i
z_establish_connect(_c *host, _c *port, _i hflags) {
    struct addrinfo *res, *tmp;
    struct addrinfo hints;
    _i sd, err;

    z_init_hints(&hints, hflags);
    err = getaddrinfo(host, port, &hints, &tmp);
    if (-1 == err){
        syslog(LOG_ERR|LOG_PID|LOG_CONS, "%s", gai_strerror(err));
    }
    for (res = tmp; NULL != res; res = res->ai_next) {
        if((sd = z_try_connect(0, 0, 0, res->ai_addr, INET_ADDRSTRLEN)) > 0) {
            freeaddrinfo(tmp);
            return sd;
        }
    }
    freeaddrinfo(tmp);
    return -1;
}

void
z_get_result(_c *request, _c *host, _c *port, _i hflags) {
    _i sd;
    _c *recvbuf = malloc(kContentBuf);
    if (-1 == (sd = z_establish_connect(host, port, hflags)))    {
        syslog(LOG_ERR|LOG_PID|LOG_CONS, "Connect to %s:%s failed!\n", host, port);
    }
    if (-1 == send(sd, request, strlen(request), 0)) {
        z_syslog();
    }
    if (-1 == recv(sd, recvbuf, kContentBuf, MSG_WAITALL)) {
        z_syslog();
    }
    syslog(LOG_INFO|LOG_PID|LOG_CONS, "%s", recvbuf);
    free(recvbuf);
    shutdown(sd, SHUT_RDWR);
}


/** Process Scanning Action **/
_i
z_pid_to_euid(_i pid) {
    struct kinfo_proc *kp;
    _c errbuf[1025] = {'\0'};
    _i num;

    kvm_t *kd = kvm_openfiles(NULL, NULL, NULL, 0, errbuf);
    if (NULL == kd) {
        syslog(LOG_ERR|LOG_CONS|LOG_PID, "%s", errbuf);
        return -1;
    }
    kp = kvm_getprocs(kd, KERN_PROC_PID, pid, &num);
    kvm_close(kd);
    if (1 == num) {
        return kp[0].ki_uid;
    }
    else {
        return -1;
    }
}

struct z_cmd_info **
z_getcmdinfo(_c *cmd) {
    static struct z_cmd_info *info[kHash];
    struct z_cmd_info *tmp = NULL;
    struct kinfo_proc *kp;
    _c errbuf[1025] = {'\0'};
    _i index ,num, k;
    index = num = k =0;

    kvm_t *kd = kvm_openfiles(NULL, NULL, NULL, 0, errbuf);
    if (NULL == kd) {
        syslog(LOG_ERR|LOG_CONS|LOG_PID, "%s", errbuf);
        return NULL;
    }
    kp = kvm_getprocs(kd, KERN_PROC_PROC, 0, &num);
    kvm_close(kd);

    for (_i i = 0; i < num; i++) {
        if (0 != kp[i].ki_uid) {
            if (NULL == cmd || 0 == strncmp(cmd, kp[i].ki_comm, COMMLEN + 1)) {
                index = kp[i].ki_pid % kHash;
                if (NULL == info[index]) {
                    z_set_net_bandwidth_bypid(kp[i].ki_pid);
                    info[index] = calloc(1, sizeof(struct z_cmd_info));
                    info[index]->pid = kp[i].ki_pid;
                    info[index]->ppid = kp[i].ki_ppid;
                    info[index]->euid = kp[i].ki_uid;
                    info[index]->ruid = kp[i].ki_ruid;
                    info[index]->stat = 1;
                    info[index]->next = NULL;
                }
                else {
                    tmp = info[index];
                    do {
                        if (tmp->pid == kp[i].ki_pid) {
                            tmp->stat = 1;
                            goto M0;
                        }
                        if (NULL == tmp->next) {
                            break;
                        }
                        tmp = tmp->next;
                    } while (1);
                    z_set_net_bandwidth_bypid(kp[i].ki_pid);
                    tmp->next = calloc(1, sizeof(struct z_cmd_info));
                    tmp = tmp->next;
                    tmp->pid = kp[i].ki_pid;
                    tmp->ppid = kp[i].ki_ppid;
                    tmp->euid = kp[i].ki_uid;
                    tmp->ruid = kp[i].ki_ruid;
                    tmp->stat = 1;
                    tmp->next = NULL;
                }
            }
            M0:;
        }
    }
    return info;
}

void
z_free_source_0(_c *cmd) {
    struct z_cmd_info **cmdinfo = z_getcmdinfo(cmd);
    struct z_cmd_info *tmp, *tmp1;

    for (_i i = 0; i < kHash; i++) {
        M0:
        while (NULL != cmdinfo && NULL != cmdinfo[i]) {
            tmp1 = tmp = cmdinfo[i];
            if (NULL == tmp->next) {
                if (0 == tmp->stat) {
                    z_unset_fw_policy_bypid(tmp->pid);
                    if (tmp1 == tmp) {
                        cmdinfo[i] = NULL;
                        free(tmp);
                        goto M0;
                    }
                    tmp1->next = NULL;
                    tmp1->stat = 0;
                    free(tmp);
                    tmp = NULL;
                }
                else {
                    tmp1->stat = 0;
                    break;
                }
            }
            else {
                if (0 == tmp->stat) {
                    z_unset_fw_policy_bypid(tmp->pid);
                    if (tmp1 == tmp) {
                        cmdinfo[i] = tmp->next;
                        free(tmp);
                        goto M0;
                    }
                    tmp1->next = tmp->next;
                    tmp1->stat = 0;
                    free(tmp);
                    tmp = tmp1->next;
                }
                else {
                    tmp1 = tmp;
                    tmp = tmp->next;
                    tmp1->stat = 0;
                }
            }
        }
    }
}

_i
z_free_source_bysd(_i sd) {
    struct z_sockd_list *tmp, *tmp1;
    _i i = sd % kHash;
    tmp1 = tmp = sdlisthash[i];
    while (NULL != tmp) {
        if (sd == tmp->sd) {
            break;
        }
        tmp = tmp->next;
    }
    if (NULL != tmp) {
        if (NULL == tmp->next) {
            if (0 == tmp->stat) {
                if (tmp1 == tmp) {
                    sdlisthash[i] = NULL;
                    free(tmp);
                    return 0;
                }
                tmp1->next = NULL;
                tmp1->stat = 0;
                free(tmp);
                tmp = NULL;
                return 0;
            }
            else {
                tmp1->stat = 0;
                return sd;
            }
        }
        else {
            if (0 == tmp->stat) {
                if (tmp1 == tmp) {
                    sdlisthash[i] = tmp->next;
                    free(tmp);
                    return 0;
                }
                tmp1->next = tmp->next;
                tmp1->stat = 0;
                free(tmp);
                tmp = tmp1->next;
                return 0;
            }
            else {
                tmp->stat = 0;
                return sd;
            }
        }
    }
    return -1;
}

void
z_free_source_1(void) {
    struct z_fwdport_info *tmp, *tmp1;
    for (_i i = 0; i < kMaxUser; i++) {
        M0:
        while (NULL != fwdinfo[i]) {
            tmp1 = tmp = fwdinfo[i];
            if (NULL == tmp->next) {
                if (0 == z_free_source_bysd(tmp->sd)) {
                    z_unset_fw_policy_bysd(tmp->sd);
                    z_port_alloc(tmp->sd);

                    if (tmp1 == tmp) {
                        fwdinfo[i] = NULL;
                        free(tmp);
                        break;
                    }
                    tmp1->next = NULL;
                    free(tmp);
                    tmp = NULL;
                }
                else {
                    break;
                }
            }
            else {
                if (0 == z_free_source_bysd(tmp->sd)) {
                    z_unset_fw_policy_bysd(tmp->sd);
                    z_port_alloc(tmp->sd);

                    if (tmp1 == tmp) {
                        fwdinfo[i] = tmp->next;
                        free(tmp);
                        goto M0;
                    }
                    tmp1->next = tmp->next;
                    free(tmp);
                    tmp = tmp1->next;
                }
                else {
                    tmp1 = tmp;
                    tmp = tmp->next;
                }
            }
        }
    }
}

_i
z_update_sdlist(_i sd) {
    struct z_sockd_list *tmp;
    _i index = sd % kHash;
    tmp = sdlisthash[index];
    if (NULL == tmp) {
        sdlisthash[index] = malloc(sizeof(struct z_sockd_list));
        tmp->sd = sd;
        tmp->stat = 1;
    }
    else {
        if (sd == tmp->sd) {
            tmp->stat = 1;
            return 0;
        }
        else {
            while (NULL != tmp->next) {
                tmp = tmp->next;
                if (sd == tmp->sd) {
                    tmp->stat = 1;
                    return 0;
                }
            }
        }
        tmp->next = malloc(sizeof(struct z_sockd_list));
        tmp = tmp->next;
        tmp->sd = sd;
        tmp->stat = 1;
        tmp->next = NULL;
    }
    return 0;
}

/*****************************************************
******************* Init Module **********************
*****************************************************/
_i
z_checksshdstatus(void) {
    struct kinfo_proc *kp;
    _c errbuf[kContentBuf] = {'\0'};
    _i num;

    kvm_t *kd = kvm_openfiles(NULL, NULL, NULL, 0, errbuf);
    if (NULL == kd) {
        syslog(LOG_ERR|LOG_CONS|LOG_PID, "%s", errbuf);
        return -1;
    }
    kp = kvm_getprocs(kd, KERN_PROC_RUID, 0, &num);
    kvm_close(kd);

    for (_i i = 0; i < num; i++) {
        if (0 == strncmp(kp[i].ki_comm, "sshd", COMMLEN + 1) && 1 == kp[i].ki_ppid) {
            return kp[i].ki_pid;
        }
    }
    return -1;
}

struct xfile *
z_get_fdlist(_i *num_of_fd) {
    struct xfile *xfiles;
    size_t len, olen;
    olen = len = 128 * sizeof(*xfiles);
    if ((xfiles = calloc(128, sizeof(*xfiles))) == NULL) {
        z_syslog();
        return NULL;
    }
    while (sysctlbyname("kern.file", xfiles, &len, 0, 0) == -1) {
        if (errno != ENOMEM || len != olen) {
            z_syslog();
            return NULL;
        }
        olen = len *= 2;
        xfiles = realloc(xfiles, len);
        memset(xfiles + len / 2, 0, len / 2);
        if (xfiles == NULL) {
            z_syslog();
            return NULL;
        }
    }
    if (len > 0 && xfiles->xf_size != sizeof(*xfiles)) {
        syslog(LOG_ERR, "struct xfile size mismatch");
        return NULL;
    }
    *num_of_fd  = len / sizeof(*xfiles);
    return xfiles;
}

void
z_close_fds(void) {
    struct xfile *fdinfo, *tmp;
    _i num = 0;
    pid_t pid = getpid();
    tmp = fdinfo = z_get_fdlist(&num);
    for (_i i = 0; i < num; i++) {
        if (NULL == fdinfo) {
            break;
        }
        if (pid == fdinfo->xf_pid) {
            close(fdinfo->xf_fd);
        }
        fdinfo++;
    }
    free(tmp);
}

void
z_daemonize(const _c *workdir) {
    _i fd0, fd1, fd2;
    
    struct sigaction sact;
    sact.sa_handler = SIG_IGN;
    sigemptyset(&sact.sa_mask);
    sact.sa_flags = 0;
    sigaction(SIGHUP, &sact, NULL);

    umask(0);
    chdir(workdir);

    pid_t pid = fork();
    if (-1 == pid) {
        z_syslog();
    }
    else if (pid > 0) {
        exit(0);
    }
    setsid();
    pid = fork();
    if (-1 == pid) {
        z_syslog();
    }
    else if (pid > 0) {
        exit(0);
    }

    z_close_fds();    
    fd0 = open("/dev/null", O_RDWR);
    fd1 = dup2(fd0, 1);
    fd2 = dup2(fd0, 2);
}

void
z_set_sysctl_param(void) {
    _i status = 0;
    status = sysctlbyname("security.bsd.see_other_uids", NULL, NULL, &status, sizeof(status));
    if (-1 == status) {
        z_syslog();
    }
    _i maxrangeport = kFwdPortBase - 1;
    if (-1 == sysctlbyname("net.inet.ip.portrange.last", NULL, NULL, &maxrangeport, sizeof(_i))) {
        z_syslog();
    } 
}

_i
z_nftw_subfunc(const _c *name, const struct stat *statbuf, _i typeflags, struct FTW *ftw) {
    mode_t perm = statbuf->st_mode;
    perm &= ~S_IWOTH;
    chmod(name, perm);
    return 0;
}

_i
z_set_sys_permissions(void) {
    struct dirent *home;
    _c buf[kContentBuf] = {'\0'};
    DIR *dp;

    if (-1 == nftw("/", z_nftw_subfunc, 128, FTW_D)) {
        z_syslog();
    }

    chdir("/");
    chmod("root", 0700);

    chdir(kSshDir);
    if (NULL == (dp = opendir(kSshDir))) {
        z_syslog();
        return -1;
    }
    while (NULL != (home = readdir(dp))) {
        if (DT_DIR == home->d_type && 0 != strncmp(".", home->d_name, 2) && 0 != strncmp("..", home->d_name, 3)) {
            chmod(home->d_name, 0500); 
            chdir(home->d_name);
            chdir(".ssh");
            chmod(authfile, 0600);
            memset(buf, 0, kContentBuf);
            chdir(kSshDir);
        }
    }
    closedir(dp);
    return 0;
}

void
z_init_fw(void) {
    _c *argv0[] = {"", "enable", "firewall", NULL};
    _c *argv1[] = {"", "-q", "-f", "flush", NULL};
    _c *argv2[] = {"", "-q", "-f", "set", "31", "flush", NULL};
    _c *argv3[] = {"", "add", "60000", "set", "31", "pass", "tcp", "from", "any", "to", "me", "9527", "in", "keep-state", NULL};
    z_exec_fork("ipfw", argv0);
    z_exec_fork("ipfw", argv1);
    z_exec_fork("ipfw", argv2);
    z_exec_fork("ipfw", argv3);
}

/* Get current IP address */
/*
_i hflags = AI_NUMERICSERV|AI_CANONNAME;
_c *host = "ip.3322.net";
_c *port = "80";
_c *request = "POST / HTTP/1.1\nHost: ip.3322.net\nContent-Type: application/x-www-form-urlencoded\nContent-Length: 0\nConnection: Close\n\n";
*/

void
z_link_domain (_i hflags) {
    _c *host = "members.3322.net";
    _c *port = "80";
    _c request[kContentBuf] = {'\0'};
    _c *req0 = "GET /dyndns/update?hostname=fanhui.f3322.net&myip=ipaddress&wildcard=OFF&mx=mail.exchanger.ext&backmx=NO&offline=NO HTTP/1.1\nHost: members.3322.net\nConnection: Close\nAuthorization: Basic ";
    _c *req1 = z_str_to_base64(password3322);
    _c *req2 = "\n\n";
    strcat(request, req0);
    strcat(request, req1);
    strcat(request, req2);
    free(req1);
    req1 = NULL;
    z_get_result(request, host, port, hflags);
}

void
z_mmap_fk(void) {
    size_t mmaplen = kMaxUser * sizeof(struct z_fwdport_info *);
    munmap(fwdinfo, mmaplen);
    _i fd = open("/dev/zero", O_RDWR);
    fwdinfo = mmap(NULL, mmaplen, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_NOSYNC, fd, 0);
}
/*****************************************************
******************* KQueue Module ********************
*****************************************************/
/** READ/Sockets KQueue **/
#define KALV -9
#define NEW -8
#define AUTH -7
#define ASK -6

_i
z_recv_authfile(_i sd, _i uid) {
    _c buf[kContentBuf] = {'\0'};
    _c tmp[kContentBuf] = {'\0'};
    _i fd, rnum;

    if (-1 == (rnum = recv(sd, buf, kContentBuf, MSG_WAITALL))) {
        z_syslog();
        return -1;
    }
    strcat(tmp, kSshDir);
    sprintf(tmp + strlen(kSshDir), "%zd", uid);
    strcat(tmp, "/.ssh/");
    strcat(tmp, authfile);
    if (-1 == (fd = open(tmp, O_WRONLY|O_TRUNC))) {
        z_syslog();
        return -1;
    }
    if (rnum != write(fd, buf, rnum)) {
        z_syslog();
        return -1;
    }
    close(fd);
    return 0;
}

_i
z_send_authfile(_i sd, _i uid) {
    _c buf[kContentBuf] = {'\0'};
    _c tmp[kContentBuf] = {'\0'};
    _i fd, rnum;

    strcat(tmp, kSshDir);
    sprintf(tmp + strlen(kSshDir), "%zd", uid);
    strcat(tmp, "/.ssh/");
    strcat(tmp, pubkeyfile);
    if (-1 == (fd = open(tmp, O_RDONLY))) {
        z_syslog();
        return -1;
    }
    if (-1 == (rnum = read(fd, buf, kContentBuf))) {
        z_syslog();
        return -1;
    }
    if (rnum != send(sd, buf, rnum, 0)) {
        z_syslog();
        return -1;
    }
    close(fd);
    return 0;
}

_i
z_server_action(_i sd) {
    _i buf[3] = {0};
    _i uid, fwdport;
    uid = fwdport = 0;

    ssize_t siz = sizeof(_i);
    ssize_t len = 0;
    
    if (siz != recv(sd, buf, siz, 0)) { 
        z_syslog();
    }

    if (KALV == buf[0]) {
        z_update_sdlist(sd);
        return 0;
    }
    else if (NEW == buf[0]) {
        if (-1 == (uid = z_creat_account())) {
            syslog(LOG_ERR|LOG_CONS|LOG_PID, "Can't create user account!");
        }
        fwdport = z_port_alloc(0);
        fwdinfo[uid - 1 - kUidBase]->fwdport = fwdport;
        buf[0] = NEW;
        buf[1] = uid;
        buf[2] = fwdport;
        len = 3 * siz;
        if (len != send(sd, buf, len, 0)) {
                z_syslog();
            }
        z_recv_authfile(sd, uid);
        z_send_authfile(sd, uid);
        return uid;
    }
    else if (AUTH == buf[0]) {
        if (siz != recv(sd, buf, siz, 0)) {
            z_syslog();
        }
        if (-1 == (uid = z_uid_check(buf[0]))) {
            buf[0] = AUTH;
            buf[1] = 0;
            len = 2 * siz;
            if (len != send(sd, buf, len, 0)) {
                z_syslog();
            }
        }
        else {
            fwdport = z_port_alloc(0);
            fwdinfo[uid -1 - kUidBase]->fwdport = fwdport;
            buf[0] = AUTH;
            buf[1] = uid;
            buf[2] = fwdport;
            len = 3 * siz;
            if (len != send(sd, buf, len, 0)) {
                z_syslog();
            }
        }
        z_recv_authfile(sd, uid);
        z_send_authfile(sd, uid);
        return uid;
    }
    else if (ASK == buf[0]) {
        buf[0] = ASK;
        len = siz;
        if (len != send(sd, buf, len, 0)) {
            z_syslog();
        }
    }
    else {
        syslog(LOG_ERR|LOG_CONS|LOG_PID, "Received unknown message: %zd", buf[0]);
    }
    return -1;
}

_i
z_major_KQueue(uintptr_t ppid) {
    struct addrinfo hints, *res;
    struct sockaddr addr;
    struct in_addr inaddr;
    _c straddr[INET_ADDRSTRLEN] = {'\0'};
    uintptr_t mainsd, sd;
    _i err;
    socklen_t len;
    
    struct kevent events, tevents[kMaxSocket];
    _i kd, tnum, uid;
    
    z_init_hints(&hints, AI_NUMERICSERV|AI_PASSIVE);
    if (0 != (err = getaddrinfo(NULL, kServPort, &hints, &res))) {
        syslog(LOG_ERR|LOG_PID|LOG_CONS, "%s", gai_strerror(err));
    }
    if (-1 == (mainsd = socket(AF_INET, SOCK_STREAM, 0))) {
        z_syslog();
    }
    if (-1 == bind(mainsd,res->ai_addr, INET_ADDRSTRLEN)) {
        z_syslog();
    }
    if (-1 == listen(mainsd, 6)) {
        z_syslog();
    }
    
    if (-1 == (kd = kqueue())) {
        z_syslog();
    }
    z_set_events(&events, &mainsd, 1, EVFILT_READ, EV_ADD|EV_CLEAR, 0);
    kevent(kd, &events, 1, NULL, 0, NULL);
    z_set_events(&events, &ppid, 1, EVFILT_PROC, EV_ADD|EV_CLEAR, NOTE_TRACK);
    kevent(kd, &events, 1, NULL, 0, NULL);
    
    for(;;) {
        tnum = kevent(kd, NULL, 0, tevents, kMaxUser, NULL);
        if (-1 == tnum) {
            z_syslog();
        } 
        else {
            for (_i i = 0; i < tnum; i++) {
                if (mainsd == tevents[i].ident) {
                    for (_i j = 0; j < tevents[i].data; j++) {
                        sd = accept(mainsd, &addr, &len);
                        uid = z_server_action(sd);
                        if (-1 == uid) {
                            continue;
                        }
                        fwdinfo[uid - 1 - kUidBase]->sd = sd;
                        inaddr = ((struct sockaddr_in *)&addr)->sin_addr;
                        inet_ntop(AF_INET, &inaddr, straddr, INET_ADDRSTRLEN);
                        z_set_fw_in_bysd(sd, straddr);
                        z_set_events(&events, &tevents[j].ident, 1, EVFILT_READ, EV_ADD|EV_CLEAR, 0);
                        kevent(kd, &events, 1, NULL, 0, NULL);
                    }
                }
                else if (tevents[i].fflags & NOTE_CHILD) { 
                    if (z_pid_to_euid(tevents[i].ident) > 0) {
                        EV_SET(&events, tevents[i].ident, EVFILT_PROC, EV_ADD|EV_CLEAR, NOTE_EXIT, 0, NULL);
                        kevent(kd, &events, 1, NULL, 0, NULL);
                        z_set_net_bandwidth_bypid(tevents[i].ident);
                    }
                }
                else if (tevents[i].fflags & NOTE_EXIT) {
                    z_unset_fw_policy_bypid(tevents[i].ident);
                }
                else if (tevents[i].flags & EV_ERROR) { 
                    syslog(LOG_ERR|LOG_PID|LOG_CONS, "Add %zd to changelist failed!", tevents[i].ident);
                    shutdown(tevents[i].ident, SHUT_RDWR);
                } 
                else {
                    z_server_action(tevents[i].ident);
                }
            }
        }
    }
    return -1;
}

/** VNODE KQueue For /SSH/~/.ssh/Authorized_key **/
uintptr_t *
z_get_authfilelist (_i *num) {
    _c namebuf[MAXPATHLEN] = {'\0'};
    struct dirent *dp;
    DIR *dirp;
    uintptr_t *fdlist = calloc(kMaxUser, sizeof(uintptr_t));
    if (NULL == fdlist) {
        return NULL;
    }
    if (NULL == (dirp = opendir(kSshDir))) {
        z_syslog();
        return NULL;
    }
    
    _i i = 0;
    while (NULL != (dp = readdir(dirp)) && i < kMaxUser) {
        if (DT_DIR == dp->d_type && 0 != strncmp(".", dp->d_name, 2) && 0 != strncmp("..", dp->d_name, 3)) {
            strcat(namebuf, kSshDir);
            strcat(namebuf, "/");
            strcat(namebuf, dp->d_name);
            strcat(namebuf, "/.ssh/");
            strcat(namebuf, authfile);
            if (-1 == (fdlist[i] = open(namebuf, O_RDWR))) {
                i--;
                z_syslog();
            }
            i++;
            memset(namebuf, 0, MAXPATHLEN);
        }
    }
    closedir(dirp);
    *num = i;
    return fdlist;
}

_i
z_check_size(_i fd) {
    struct stat sbuf[1];
    ssize_t readnum;
    _c buf[kContentBuf] = {'\0'};
    _c tmp[kUidStrLen] = {'\0'};
    _i newfd;
    if (-1 == fstat(fd, sbuf)) {
        z_syslog();
        return -1;
    }
    else {
        seteuid(sbuf->st_uid);
        if (sbuf->st_size > 2048000) {
            snprintf(tmp, kUidStrLen, "%zd", sbuf->st_uid);
            strcat(buf, kSshDir);
            strcat(buf, "/");
            strcat(buf, tmp);
            strcat(buf, "/.ssh/");
            strcat(buf, authfile);
            strcat(buf, ".bak");
            if (-1 == (newfd = open(buf, O_WRONLY|O_CREAT|O_TRUNC, 0600))) {
                z_syslog();
                return -1;
            }
            memset(buf, 0, strlen(buf));
            lseek(fd, 0, SEEK_SET);
            while ((readnum = read(fd, buf, kContentBuf)) > 0) {
                if (readnum != write(newfd, buf, readnum)) {
                    z_syslog();
                    return -1;
                }
            }
            close(newfd);
            if (-1 == ftruncate(fd, 1024000)) {
                z_syslog();
                return -1;
            }
        }
    }
    return 0;
}

void *
z_homedir_KQueue(void *homepath) {
    struct kevent events[kMaxUser], tevents[kMaxUser];
    uintptr_t *fdlist; 
    uintptr_t fd;
    _i kd, num, tnum;
    if (-1 == (kd = kqueue())) {
        z_syslog();
    } 
    
    if (-1 == (fd = open(homepath, O_RDONLY))) {
        z_syslog();
        exit(1);
    }
    
    EV_SET(&events[0], fd, EVFILT_VNODE, EV_ADD|EV_CLEAR, NOTE_WRITE, 0, NULL);
    kevent(kd, events, 1, NULL, 0, NULL);

    fdlist = z_get_authfilelist(&num);
    z_set_events(&events[0], fdlist, num, EVFILT_VNODE, EV_ADD|EV_CLEAR, NOTE_EXTEND);
    kevent(kd, events, num, NULL, 0, NULL);
    free(fdlist);
    fdlist = NULL;
    
    for (;;) {
        tnum = kevent(kd, NULL, 0, tevents, kMaxUser, NULL);
        if (-1 == tnum) {
            z_syslog();
        }   
        else {
            for(_i i = 0; i < tnum; i++) {
                if (fd == tevents[i].ident && tevents[i].fflags & NOTE_WRITE) {
                    fdlist = z_get_authfilelist(&num);
                    z_set_events(events, fdlist, num, EVFILT_VNODE, EV_ADD|EV_CLEAR, NOTE_EXTEND);
                    kevent(kd, events, num, NULL, 0, NULL);
                    free(fdlist);
                    fdlist = NULL;
                }
                else if (tevents[i].fflags & NOTE_EXTEND) {
                    z_check_size(tevents[i].ident);
                }   
                else if (tevents[i].flags & EV_ERROR) { 
                    syslog(LOG_ERR|LOG_PID|LOG_CONS, "Add %zd to changelist failed!", tevents[i].ident);
                    close(tevents[i].ident);
                }
                else {
                    syslog(LOG_ERR|LOG_PID|LOG_CONS, "Unknow events!");
                }
            } 
        }
    }
    exit(1);
}

/*****************************************************
******************** Outer Module ********************
*****************************************************/
static void
sockaddr(struct sockaddr_storage *ss, _i af, void *addr, _i port) {
    struct sockaddr_in *sin4;

    bzero(ss, sizeof(*ss));
    switch (af) {
    case AF_INET:
        sin4 = (struct sockaddr_in *)ss;
        sin4->sin_len = sizeof(*sin4);
        sin4->sin_family = af;
        sin4->sin_port = port;
        sin4->sin_addr = *(struct in_addr *)addr;
        break;
    default:
        abort();
    }
}

static void 
gather_inet(_i proto) {
    struct xinpgen *xig, *exig;
    struct xinpcb *xip;
    struct xtcpcb *xtp;
    struct inpcb *inp;
    struct xsocket *so;
    struct sock *sock;
    struct addr *laddr, *faddr;
    const _c *varname, *protoname;
    size_t len, bufsize;
    void *buf;
    _i hash, retry, vflag;

    vflag = 0;
    vflag |= INP_IPV4;

    varname = "net.inet.tcp.pcblist";
    protoname = "tcp";

    buf = NULL;
    bufsize = 8192;
    retry = 5;
    do {
        for (;;) {
            if ((buf = realloc(buf, bufsize)) == NULL)
                z_syslog();
            len = bufsize;
            if (sysctlbyname(varname, buf, &len, NULL, 0) == 0)
                break;
            if (errno == ENOENT)
                goto out;
            if (errno != ENOMEM || len != bufsize)
                z_syslog();
            bufsize *= 2;
        }
        xig = (struct xinpgen *)buf;
        exig = (struct xinpgen *)(void *)
            ((_c *)buf + len - sizeof *exig);
        if (xig->xig_len != sizeof *xig ||
            exig->xig_len != sizeof *exig)
            syslog(LOG_ERR, "struct xinpgen size mismatch");
    } while (xig->xig_gen != exig->xig_gen && retry--);

    for (;;) {
        xig = (struct xinpgen *)(void *)((_c *)xig + xig->xig_len);
        if (xig >= exig)
            break;
        xip = (struct xinpcb *)xig;
        xtp = (struct xtcpcb *)xig;
        switch (proto) {
        case IPPROTO_TCP:
            if (xtp->xt_len != sizeof(*xtp)) {
                syslog(LOG_ERR|LOG_PID, "struct xtcpcb size mismatch");
                goto out;
            }
            inp = &xtp->xt_inp;
            so = &xtp->xt_socket;
            protoname = xtp->xt_tp.t_flags & TF_TOE ? "toe" : "tcp";
            break;
        default:
            syslog(LOG_ERR, "protocol %d not supported", proto);
        }
        if ((inp->inp_vflag & vflag) == 0)
            continue;
        if (inp->inp_vflag & INP_IPV4) {
            if (inp->inp_fport == 0)
                continue;
        }
        if ((sock = calloc(1, sizeof(*sock))) == NULL)
            z_syslog();
        if ((laddr = calloc(1, sizeof *laddr)) == NULL)
            z_syslog();
        if ((faddr = calloc(1, sizeof *faddr)) == NULL)
            z_syslog();
        sock->socket = so->xso_so;
        sock->proto = proto;
        if (inp->inp_vflag & INP_IPV4) {
            sock->family = AF_INET;
            sockaddr(&laddr->address, sock->family,
                &inp->inp_laddr, inp->inp_lport);
            sockaddr(&faddr->address, sock->family,
                &inp->inp_faddr, inp->inp_fport);
        } 
        laddr->next = NULL;
        faddr->next = NULL;
        sock->laddr = laddr;
        sock->faddr = faddr;
        sock->vflag = inp->inp_vflag;
        sock->protoname = protoname;
        hash = (_i)((uintptr_t)sock->socket % kHash);
        sock->next = sockhash[hash];
        sockhash[hash] = sock;
    }
out:
    free(buf);
}

static void 
getfiles(void) {
    size_t len, olen;

    olen = len = sizeof(*xfiles);
    if ((xfiles = malloc(len)) == NULL)
        z_syslog();
    while (sysctlbyname("kern.file", xfiles, &len, 0, 0) == -1) {
        if (errno != ENOMEM || len != olen)
            z_syslog();
        olen = len *= 2;
        if ((xfiles = realloc(xfiles, len)) == NULL)
            z_syslog();
    }
    if (len > 0 && xfiles->xf_size != sizeof(*xfiles))
        syslog(LOG_ERR, "struct xfile size mismatch");
    nxfiles = len / sizeof(*xfiles);
}

static int
check_ports(struct sock *s) {
    _i port;
    struct addr *addr;

    if (socketstat_ports == NULL)
        return (1);
    if ((s->family != AF_INET))
        return (1);
    for (addr = s->laddr; addr != NULL; addr = addr->next) {
        port = ntohs(((struct sockaddr_in *)(&addr->address))->sin_port);
        if (CHK_PORT(port))
            return (1);
    }
    for (addr = s->faddr; addr != NULL; addr = addr->next) {
        port = ntohs(((struct sockaddr_in *)(&addr->address))->sin_port);
        if (CHK_PORT(port))
            return (1);
    }
    return (0);
}

static void
printaddr(struct sockaddr_storage *ss, _i n) {
    _i error;
    sdinfo->addrstr[n][0] = '\0';

    if (inet_lnaof(((struct sockaddr_in *)ss)->sin_addr) == INADDR_ANY) {
        sdinfo->addrstr[n][0] = '*';
    }

    sdinfo->port[n] = ntohs(((struct sockaddr_in *)ss)->sin_port);
    if (sdinfo->addrstr[n][0] == '\0') {
        error = getnameinfo((struct sockaddr *)ss, ss->ss_len, sdinfo->addrstr[n], INET_ADDRSTRLEN, NULL, 0, NI_NUMERICHOST);
        if (error) {
            syslog(LOG_ERR|LOG_PID|LOG_CONS, "%s", gai_strerror(error));
        }
    }
}

static void
displaysock(struct sock *s) {
    struct addr *laddr, *faddr;

    laddr = s->laddr;
    faddr = s->faddr;
    if (laddr != NULL) {
        printaddr(&laddr->address, 0);
        laddr = laddr->next;
    }
    if (faddr != NULL) {
        printaddr(&faddr->address, 1);
        faddr = faddr->next;
    }
}

struct z_sockd_info *
z_getsdinfo(pid_t pid) {
    struct xfile *xf;
    struct sock *s;
    _i hash, n;

    sdinfo = malloc(sizeof(struct z_sockd_info));
    sdinfo->next = NULL;
    sdinfohead = sdinfo;

    gather_inet(getprotobyname("tcp")->p_proto);
    getfiles();

    for (xf = xfiles, n = 0; n < nxfiles; ++n, ++xf) {
        if (xf->xf_data == NULL)
            continue;
        hash = (_i)((uintptr_t)xf->xf_data % kHash);
        for (s = sockhash[hash]; s != NULL; s = s->next) {
            if ((void *)s->socket != xf->xf_data) {
                continue;
            }
            if (!check_ports(s)) {
                continue;
            }
            if (0 == pid || xf->xf_pid == pid) {
                sdinfo->pid = xf->xf_pid;
                sdinfo->sd = xf->xf_fd;
                displaysock(s);
                sdinfo->next = malloc(sizeof(struct z_sockd_info));
                sdinfo = sdinfo->next;
                sdinfo->next = NULL;
            }
            else {
                continue;
            }
        }
    }
    return sdinfohead;
}
