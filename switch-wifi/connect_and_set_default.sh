#!/bin/bash

if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <SSID> <Password>"
    exit 1
fi

WIFI_SSID=$1
WIFI_PWD=$2

if sudo nmcli device wifi connect "$WIFI_SSID" password "$WIFI_PWD"; then
    echo "Successfully connected to $WIFI_SSID."
    
    ORG_STR="ctrl_interface=DIR=/var/run/wpa_supplicant GROUP=netdev\nupdate_config=1"
    # Construct the wpa_supplicant.conf entry
    ENTRY="network={\n\tssid=\"$WIFI_SSID\"\n\tpsk=\"$WIFI_PWD\"\n}"
    
    # Write to wpa_supplicant.conf
    echo -e $ORG_STR | sudo tee /etc/wpa_supplicant/wpa_supplicant.conf > /dev/null
    echo -e $ENTRY | sudo tee -a /etc/wpa_supplicant/wpa_supplicant.conf > /dev/null

    echo "Wi-Fi settings written to wpa_supplicant.conf."
else
    echo "Failed to connect to $WIFI_SSID. Please check the SSID and password."
    exit 1
fi