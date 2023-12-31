/*
 * POSIX file descriptor based virtual network interface feature for
 * LKL Copyright (c) 2015,2016 Ryo Nakamura, Hajime Tazaki
 *
 * Author: Ryo Nakamura <upa@wide.ad.jp>
 *         Hajime Tazaki <thehajime@gmail.com>
 *         Octavian Purdila <octavian.purdila@intel.com>
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#ifdef __FreeBSD__
#include <sys/syslimits.h>
#else
#include <limits.h>
#endif
#include <fcntl.h>
#include <sys/poll.h>
#include <sys/uio.h>

#include "virtio.h"
#include "virtio_net_fd.h"

//#define NETDBG 1

struct lkl_netdev_fd {
	struct lkl_netdev dev;
	/* file-descriptor based device */
	int fd_rx;
	int fd_tx;
	/*
	 * Controlls the poll mask for fd. Can be acccessed concurrently from
	 * poll, tx, or rx routines but there is no need for syncronization
	 * because:
	 *
	 * (a) TX and RX routines set different variables so even if they update
	 * at the same time there is no race condition
	 *
	 * (b) Even if poll and TX / RX update at the same time poll cannot
	 * stall: when poll resets the poll variable we know that TX / RX will
	 * run which means that eventually the poll variable will be set.
	 */
	int poll_tx, poll_rx;
	/* controle pipe */
	int pipe[2];

	unsigned long cmp_to_mon;
};

static int fd_net_tx(struct lkl_netdev *nd, struct iovec *iov, int cnt)
{
	int ret;
	struct lkl_netdev_fd *nd_fd =
		container_of(nd, struct lkl_netdev_fd, dev);

	iov->iov_base += nd_fd->cmp_to_mon;

#ifdef NETDBG
	printf("FD TX %p %p %d\n", nd, iov, cnt);

	char *p = (char *) iov->iov_base;
	for(int i = 0; i < iov->iov_len; i++)
		printf("%02x ", p[i]);
	printf("\n");
#endif



//	char good[74]={0x58, 0x9c, 0xfc, 0x00, 0x54, 0x7a, 0xca, 0xfe, 0x00, 0x00, 0x00, 0x01, 0x08, 0x00, 0x45, 0x00, 0x00, 0x3c, 0xdf, 0x6a, 0x40, 0x00, 0x40, 0x06, 0x31, 0x39, 0x0a, 0x0b, 0x0b, 0x02, 0x0a, 0x0b, 0x0b, 0x01, 0x83, 0xf4, 0x13, 0x88, 0xda, 0xc6, 0x55, 0xba, 0x00, 0x00, 0x00, 0x00, 0xa0, 0x02, 0x72, 0x10, 0xe9, 0xf0, 0x00, 0x00, 0x02, 0x04, 0x05, 0xb4, 0x04, 0x02, 0x08, 0x0a, 0x87, 0x24, 0x72, 0xc6, 0x00, 0x00, 0x00, 0x00, 0x01, 0x03, 0x03, 0x05};

	do {
//		if(iov->iov_len == 74) {
//			  memcpy(iov->iov_base, good, 74);
//		}
		ret = writev(nd_fd->fd_tx, iov, cnt);
	} while (ret == -1 && errno == EINTR);

	if (ret < 0) {
		if (errno != EAGAIN) {
			perror("write to fd netdev fails");
		} else {
			char tmp = 0;

			nd_fd->poll_tx = 1;
			if (write(nd_fd->pipe[1], &tmp, 1) <= 0)
				perror("virtio net fd pipe write");
		}
	}
#ifdef NETDBG
	printf("TX return %d\n", ret);
#endif
	return ret;
}

static int fd_net_rx(struct lkl_netdev *nd, struct iovec *iov, int cnt)
{
	int ret = 0;
	struct lkl_netdev_fd *nd_fd =
		container_of(nd, struct lkl_netdev_fd, dev);

#ifdef NETDBG
	printf("RX %d %p %d %d %p\n", nd_fd->fd_rx, nd, iov->iov_len, cnt, iov->iov_base);
#endif

	iov->iov_base += nd_fd->cmp_to_mon;

	do {
		ret = readv(nd_fd->fd_rx, (struct iovec *)iov, cnt);
	} while (ret == -1 && errno == EINTR);

#ifdef NETDBG
	char *p = (char *) iov->iov_base;
	for(int i = 0; i < ret; i++)
		printf("%02x ", p[i]);
	printf("\n");
#endif

	if (ret < 0) {
		if (errno != EAGAIN) {
			perror("virtio net fd read");
		} else {
			char tmp = 0;

			nd_fd->poll_rx = 1;
			if (write(nd_fd->pipe[1], &tmp, 1) < 0)
				perror("virtio net fd pipe write");
		}
	}
#ifdef NETDBG
	printf("RX return %d\n", ret);
#endif
	return ret;
}

static int fd_net_poll(struct lkl_netdev *nd)
{
	struct lkl_netdev_fd *nd_fd =
		container_of(nd, struct lkl_netdev_fd, dev);
	struct pollfd pfds[3] = {
		{
			.fd = nd_fd->fd_rx,
		},
		{
			.fd = nd_fd->fd_tx,
		},
		{
			.fd = nd_fd->pipe[0],
			.events = POLLIN,
		},
	};
	int ret;

	if (nd_fd->poll_rx)
		pfds[0].events |= POLLIN|POLLPRI;
	if (nd_fd->poll_tx)
		pfds[1].events |= POLLOUT;

	do {
		ret = poll(pfds, 3, -1);
	} while (ret == -1 && errno == EINTR);

	if (ret < 0) {
		perror("virtio net fd poll");
		return 0;
	}

	if (pfds[2].revents & (POLLHUP|POLLNVAL))
		return LKL_DEV_NET_POLL_HUP;

	if (pfds[2].revents & POLLIN) {
		char tmp[PIPE_BUF];

		ret = read(nd_fd->pipe[0], tmp, PIPE_BUF);
		if (ret == 0)
			return LKL_DEV_NET_POLL_HUP;
		if (ret < 0)
			perror("virtio net fd pipe read");
	}

	ret = 0;

	if (pfds[0].revents & (POLLIN|POLLPRI)) {
		nd_fd->poll_rx = 0;
		ret |= LKL_DEV_NET_POLL_RX;
	}

	if (pfds[1].revents & POLLOUT) {
		nd_fd->poll_tx = 0;
		ret |= LKL_DEV_NET_POLL_TX;
	}

	return ret;
}

static void fd_net_poll_hup(struct lkl_netdev *nd)
{
	struct lkl_netdev_fd *nd_fd =
		container_of(nd, struct lkl_netdev_fd, dev);

	/* this will cause a POLLHUP / POLLNVAL in the poll function */
	close(nd_fd->pipe[0]);
	close(nd_fd->pipe[1]);
}

static void fd_net_free(struct lkl_netdev *nd)
{
	struct lkl_netdev_fd *nd_fd =
		container_of(nd, struct lkl_netdev_fd, dev);

	close(nd_fd->fd_rx);
	close(nd_fd->fd_tx);
	free(nd_fd);
}

struct lkl_dev_net_ops fd_net_ops =  {
	.tx = fd_net_tx,
	.rx = fd_net_rx,
	.poll = fd_net_poll,
	.poll_hup = fd_net_poll_hup,
	.free = fd_net_free,
};

struct lkl_netdev *lkl_register_netdev_fd(int fd_rx, int fd_tx)
{
	struct lkl_netdev_fd *nd;

	nd = malloc(sizeof(*nd));
	if (!nd) {
		fprintf(stderr, "fdnet: failed to allocate memory\n");
		/* TODO: propagate the error state, maybe use errno for that? */
		return NULL;
	}

	memset(nd, 0, sizeof(*nd));

	nd->fd_rx = fd_rx;
	nd->fd_tx = fd_tx;
	if (pipe(nd->pipe) < 0) {
		perror("pipe");
		free(nd);
		return NULL;
	}

	if (fcntl(nd->pipe[0], F_SETFL, O_NONBLOCK) < 0) {
		perror("fnctl");
		close(nd->pipe[0]);
		close(nd->pipe[1]);
		free(nd);
		return NULL;
	}

	nd->dev.ops = &fd_net_ops;
	return &nd->dev;
}
