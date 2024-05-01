#!/bin/bash

echo "install package"
sudo apt update
sudo apt install apache2 php libapache2-mod-php
sudo apt-get install avahi-daemon

echo "copy page to html"
sudo cp wifi_info.php /var/www/html

echo "setup script"
sudo cp connect_and_set_default.sh /usr/bin/

echo "setting permission of www"
# Define the file path
FILE="/etc/sudoers.d/wifi-web"

# Check if the sudoers file already exists to avoid duplicate entries
if [ -f "$FILE" ]; then
    echo "File $FILE already exists."
else
    # Create the sudoers file for www-data with the appropriate permissions
    echo "www-data ALL=(ALL) NOPASSWD: /usr/bin/connect_and_set_default.sh" | sudo tee $FILE > /dev/null

    # Set the correct file permissions
    sudo chmod 0440 $FILE

    # Validate the syntax of the new sudoers file to ensure no syntax errors
    sudo visudo -c -f $FILE

    if [ $? -eq 0 ]; then
        echo "Sudoers file has been created and validated successfully."
    else
        echo "Error in sudoers syntax. Please check the file manually."
    fi
fi