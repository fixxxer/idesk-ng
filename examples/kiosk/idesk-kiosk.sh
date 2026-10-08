#!/bin/sh
# Starts idesk-ng in kiosk mode and restarts it if it ever dies.
# Install as root:  install -m 755 idesk-kiosk.sh /usr/local/bin/idesk-kiosk
# and call it from the kiosk user's session startup (~/.xsession, the window
# manager's autostart, ...). Keep this file root-owned: whoever can edit it
# can remove --kiosk.
while true; do
    idesk-ng --kiosk
    sleep 2        # avoid a tight loop if it fails at startup
done
