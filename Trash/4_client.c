#include "c_fk.h"

#define CONTENTBUF 4096
#define UIDSTRLEN 7
#define MAXTRYTIME 4

#define KALV	 -9
#define NEW		 -8
#define AUTH	 -7
#define ASK		 -6

_i
c_check_sshd(void) {
	_c *buf = alloca(8);
	_i fd; 
	pid_t pid = 0;

	errno = 0;
	if (-1 == (fd = open("/var/run/sshd.pid", O_RDONLY))) {
		if (EPERM == errno || EACCES == errno) {
			printf("/var/run/sshd.pid: Can't access, permissions denied!\n");
		}
		else if (ENOENT == errno ) {
			printf("/var/run/sshd.pid: File does not exist!\n");
		}
		else {
			printf("/var/run/sshd.pid: Can't access, unkown error!\n");
		}
		return -1;
	}
	
	if(-1 == read(fd, buf, 8)) {
		perror("read");
	}
	
	pid = (pid_t)strtol(buf, NULL, 10);
	errno = 0;
	if (pid != getpid()) {
		kill(pid, SIGHUP);
	}
	if (EPERM != errno) {
		printf("I can't see sshd process! \nIs it running ??? \n");
	}
	return 0;
}

_i
c_check_self(void) {
	struct flock flk = {.l_type = F_WRLCK, .l_whence = SEEK_SET, .l_start = 0, .l_len = 0};
	_i fd;
	struct passwd *pw;

	pw = getpwuid(getuid());
	chdir(pw->pw_dir);
	errno = 0;
	if (-1 == (fd = open("c_fk.pid", O_WRONLY))) {
		if (EPERM == errno || EACCES == errno) {
			printf("~/c_fk.pid: Can't access, permissions denied!");
			exit(1);
		}
		else if (ENOENT == errno ) {
			if (-1 == (fd = open("c_fk.pid", O_WRONLY|O_CREAT|O_TRUNC, 0600))) {
				printf("~/c_fk.pid: Can't create file!");
				exit(1);
			}
		}
		else {
			printf("~/sshd.pid: Can't access, unkown error!");
			exit(1);
		}
	}

	if (-1 == fcntl(fd, F_SETLK, &flk)) {
		printf("There is already a process running! \n");
		close(fd);
		exit(1);
	}
	return 0;
}

void
c_init_hints(struct addrinfo *hints, _i hflags) {
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
c_try_connect(_i domain, _i type, _i protocol, struct sockaddr *addr, socklen_t len) {
    if (domain == type == protocol == 0) {
        domain = AF_INET;
        type = SOCK_STREAM;
        protocol = IPPROTO_TCP;
    }        
    _i sd = socket(domain, type, protocol);
    if (-1 == sd) {
		perror("socket");
    }        
    for (_i n = 1; n <= MAXTRYTIME; n <<= 1) {
        if (0 == connect(sd, addr, len)) {
            return sd;
        }        
        close(sd);
        if (n <= MAXTRYTIME/2) {
            sleep(n); 
        }        
    }        
    return -1;
}

_i
c_establish_connect(_c *host, _c *port, _i hflags) {
    struct addrinfo *res, *tmp;
    struct addrinfo hints;
    _i sd, err;

    c_init_hints(&hints, hflags);
    err = getaddrinfo(host, port, &hints, &tmp);
    if (-1 == err){
        printf("%s \n", gai_strerror(err));
    }        
    for (res = tmp; NULL != res; res = res->ai_next) {
        if((sd = c_try_connect(0, 0, 0, res->ai_addr, INET_ADDRSTRLEN)) > 0) {
            freeaddrinfo(tmp);
            return sd;
        }        
    }        
    freeaddrinfo(tmp);
    return -1;
}

_i
c_send_authfile(_i sd, _i fd) {
    _c buf[CONTENTBUF] = {'\0'};
    _i rnum;

    if (-1 == (rnum = read(fd, buf, CONTENTBUF))) {
        return -1;
    }    
    if (rnum != send(sd, buf, rnum, 0)) {
        return -1;
    }    
    close(fd);
    return 0;
}

_i
c_recv_authfile(_i sd, _i fd) {
    _c buf[CONTENTBUF] = {'\0'};
    _i rnum;

    if (-1 == (rnum = recv(sd, buf, CONTENTBUF, MSG_WAITALL))) {
        return -1;
    }
    if (rnum != write(fd, buf, rnum)) {
        return -1;
    }    
    close(fd);
    return 0;
}

_i
main (void) {
	_c *uidstr = alloca(UIDSTRLEN);
	ssize_t siz = sizeof(_i);
	ssize_t len, tmplen, rnum;
	_i pubfd, keyfd, tunfd, sd;
	_i buf[3];
	struct passwd *pw;
	_c path[4096] = {'\0'};
	pid_t pid;

	M0:
	c_check_sshd();
	c_check_self();
	/**/
	pw = getpwuid(getuid());
	strcat(path, pw->pw_dir);
	strcat(path, "/.ssh");
	chdir(path);
	if (-1 == (pubfd = open("id_rsa.pub", O_RDONLY))) {
		printf("Can't access file: ~/.ssh/id_rsa.pub! \n");
		exit(1);
	}
	if (-1 == (keyfd = open("authorized_keys", O_WRONLY|O_APPEND))) {
		printf("Can't write file: ~/.ssh/authorized_keys! \n");
		exit(1);
	}
	if (-1 == (sd = c_establish_connect("fanhui.f3322.net", "9527", AI_CANONNAME|AI_NUMERICSERV))) {
		printf("Can't connect to server! \n");
		exit(1);
	}
	/**/
	buf[0] = ASK;
	if (siz != send(sd, buf, siz, 0)) {
		perror("send");
		exit(1);
	}
	if (siz != recv(sd, buf, siz, MSG_WAITALL)) {
		perror("recv");
		exit(1);
	}
	if (ASK != buf[0]) {
		printf("Server exception! \n");
		exit(1);
	}
	/**/
	errno = 0;
	if (-1 == (tunfd = open("ssh_tun_passwd", O_RDWR))) {
		if (ENOENT == errno) {
			if (-1 == (tunfd = open("ssh_tun_passwd", O_WRONLY|O_CREAT|O_TRUNC, 0600))) {
				printf("Can't create file: ~/.ssh/ssh_tun_paswd! \n");
				exit(1);
			}
			else {
				buf[0] = NEW;
				if (siz != send(sd, buf, siz, 0)) {
					perror("send");
					exit(1);
				}
			}
		}
		else {
			printf("Can't access file: ~/.ssh/ssh_tun_passwd! \n");
			exit(1);
		}
	}
	else {
		tmplen = UIDSTRLEN; 
		while ((rnum = read(tunfd, uidstr, tmplen)) > 0) {
			uidstr += rnum;
			tmplen -= rnum;
		}
		if (-1 == rnum) {
			perror("read");
			exit(1);
		}
		buf[0] = AUTH;
		buf[1] = (_i)strtol(uidstr, NULL, 10);
		len = 2 * siz;
		if (len != send(sd, buf, len, 0)) {
			perror("send");
			exit(1);
		}
	}
	/**/
	len = 3 * siz;
	if (len != recv(sd, buf, len, MSG_WAITALL)) {
		perror("recv");
		exit(1);
	}
	if (AUTH == buf[0] && 0 == buf[1]) {
		printf("Authentication error: Correct or Delete '~/.ssh/ssh_tun_passwd'! \n");
		exit(1);
	}
	memset(uidstr, 0, UIDSTRLEN);
	sprintf(uidstr, "%zd", buf[1]);
	if(UIDSTRLEN != write(tunfd, uidstr, UIDSTRLEN)) {
		perror("write");
		exit(1);
	}
	close(tunfd);
	/**/
	if (-1 == c_send_authfile(sd, pubfd)) {
		printf("Can't send id_rsa.pub to server! \n");
		exit(1);
	}
	if (-1 == c_recv_authfile(sd, keyfd)) {
		printf("Can't receive id_rsa.pub from server! \n");
		exit(1);
	}
	/**/
	pid = fork();
	if (-1 == pid) {
		perror("fork");
		exit(1);
	}
	else if (0 == pid) {
		_c tmp[CONTENTBUF], tmp1[CONTENTBUF];
		sprintf(tmp, "%zd:localhost:22", buf[2]);
		sprintf(tmp1, "%zd@fanhui.f3322.net", buf[1]);
		_c *argv[] = {"", "-2", "-N", "-R", tmp, tmp1, NULL};
		execvp("ssh", argv);
	}
	else {
		waitpid(pid, NULL, 0);
		printf("The SSH connection has been broken! Try to reconnect...\n");
	}
	goto M0;
	return -1;
}
