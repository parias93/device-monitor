/*
 * netlink.c
 *
 *  Created on: 2 Nov 2016
 *      Author: Ander Juaristi
 */
#include <unistd.h>
#include "libmnl.h"

struct _netlink_st {
	struct mnl_socket *nlsock;
};

void netlink_open()
{
	struct mnl_socket *nlsock;
	const int on = 1;
	unsigned int group = 2;
	pid_t pid = getpid();

	nlsock = mnl_socket_open2(NETLINK_KOBJECT_UEVENT, SOCK_CLOEXEC);

	if (mnl_socket_bind(nlsock, group, pid) != 0)
		fprintf(stderr, "ERROR: mnl_socket_bind()\n");
	setsockopt(mnl_socket_get_fd(nlsock), SOL_SOCKET, SO_PASSCRED, &on, sizeof(on));
}
