/*
 * main.c
 *
 *  Created on: 21 Oct 2016
 *      Author: Ander Juaristi
 */
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <signal.h>
#include "libudev.h"

static int stop = 0;

static void sighandler(int s)
{
	fprintf(stderr, "Received %d. Stopping.\n", s);
	stop = 1;
}

static void traverse_list(struct udev_enumerate *udev_enum)
{
	const char *name;
	struct udev_list_entry *entry = NULL;
	if (!udev_enum)
		return;

	udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(udev_enum)) {
		name = udev_list_entry_get_name(entry);
		if (name)
			printf("%s\n", name);
		else
			printf("<null>\n");
	}
}

static void print_subsystems(struct udev *udev)
{
	int retval;
	struct udev_enumerate *udev_enum = NULL;
	if (!udev)
		return;

	udev_enum = udev_enumerate_new(udev);
	if (!udev_enum)
		return;

	retval = udev_enumerate_scan_subsystems(udev_enum);
	if (retval < 0) {
		fprintf(stderr, "ERROR: could not scan subsystems (%d)\n", retval);
		return;
	}

	traverse_list(udev_enum);
	udev_enumerate_unref(udev_enum);
}

static void __print_media(const char *dirname)
{
	printf("\t/%s\n", dirname);
}

static void __print_dev(const char *dirname)
{
	if (strncmp(dirname, "sd", 2) == 0)
		printf("\t/%s\n", dirname);
}

static void print_directory(const char *dirname, void (*callback_func)(const char *))
{
	DIR *dir;
	struct dirent *dirent;

	printf("%s:\n", dirname);

	dir = opendir(dirname);
	if (!dir) {
		fprintf(stderr, "Could not open directory\n");
		return;
	}

	while ((dirent = readdir(dir)))
		callback_func(dirent->d_name);
}

static void receive_devices(struct udev_monitor *monitor)
{
	int fd;
	struct udev_device *device;
	fd_set fds;
	struct timeval tv;
	int ret;

	fd = udev_monitor_get_fd(monitor);
	while (!stop) {
		FD_ZERO(&fds);
		FD_SET(fd, &fds);
		tv.tv_sec = 0;
		tv.tv_usec = 0;

		ret = select(fd + 1, &fds, NULL, NULL, &tv);

		if (ret > 0 && FD_ISSET(fd, &fds)) {
			device = udev_monitor_receive_device(monitor);
			if (device) {
				printf("-----------------------------\n");
				printf("Node: %s\n", udev_device_get_devnode(device));
				printf("Subsystem: %s\n", udev_device_get_subsystem(device));
				printf("Devtype: %s\n", udev_device_get_devtype(device));
				printf("Action: %s\n", udev_device_get_action(device));
				print_directory("/media", __print_media);
				print_directory("/dev", __print_dev);
				printf("-----------------------------\n");
				udev_device_unref(device);
			} else {
				fprintf(stderr, "ERROR: could not receive device\n");
				stop = 1;
			}
		}
	}
}

int main(int argc, char **argv)
{
	int retval;
	struct sigaction sig;
	struct udev_monitor *monitor = NULL;
	struct udev *udev = udev_new();

	if (!udev) {
		fprintf(stderr, "ERROR: could not create a udev library context\n");
		goto end;
	}

	sig.sa_handler = sighandler;
	sig.sa_flags = SA_RESETHAND;
	sigemptyset(&sig.sa_mask);
	sigaction(SIGINT, &sig, NULL);

	if (argc == 1) {
		print_subsystems(udev);
		goto end;
	}

	monitor = udev_monitor_new_from_netlink(udev, "udev");
	if (!monitor) {
		fprintf(stderr, "ERROR: could not create an udev monitor\n");
		goto end;
	}
	retval = udev_monitor_filter_add_match_subsystem_devtype(monitor, argv[1], NULL);
	if (retval) {
		fprintf(stderr, "ERROR: could not set up subsystem filter (%d)\n", retval);
		goto end;
	}
	retval = udev_monitor_enable_receiving(monitor);
	if (retval) {
		fprintf(stderr, "ERROR: could not enable event source (%d)\n", retval);
		goto end;
	}

	receive_devices(monitor);

end:
	if (monitor)
		udev_monitor_unref(monitor);
	if (udev)
		udev_unref(udev);

	return 0;
}
