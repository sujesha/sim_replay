#!/bin/sh

#This script prints the name of the test hard-disk for use by the replay module
mountpoint=""
sudo cat /etc/fstab | grep -e $mountpoint | awk '{print $1}' | awk -F "/" '{print $3}' | sed 's/[0-9]//'


#It is assumed that the biggest hard-disk attached to this machine is the
#	test hard-disk, hence the name of that disk is the returned output.
#	However, if this is not the correct criteria, change the condition here.
#sudo /sbin/fdisk -l | grep -e "Disk /dev" | sort -k 3 |head -n 1 |awk '{print $2}' | awk -F ":" '{print $1}'|awk -F "/" '{print $3}'

