#!/bin/bash

source common.sh

echo "disconnect current connectction"
sudo nmcli device disconnect wlan0

echo "host $HOTSPOT_SSID with password $HOTSPOT_PASSWD as $HOTSPOT_NAME"
sudo nmcli device wifi hotspot ifname wlan0 con-name $HOTSPOT_NAME ssid $HOTSPOT_SSID band bg password $HOTSPOT_PASSWD
sudo nmcli con up $HOTSPOT_NAME