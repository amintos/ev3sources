#!/bin/bash

. ./sdcardDev

sync
sudo umount ${SDCARD_PATH}1
sudo umount ${SDCARD_PATH}2

