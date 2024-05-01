#!/bin/bash

source common.sh

sudo nmcli con down $HOTSPOT_NAME
sudo nmcli con up $DEFAULT_WIFI_NAME