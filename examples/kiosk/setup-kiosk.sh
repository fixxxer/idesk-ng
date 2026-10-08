#!/bin/sh
# One-time setup of the kiosk user's configuration. Run as root.
# Usage: setup-kiosk.sh <kiosk-user> <folder-with-ideskrc-and-icons>
#
# The folder is copied to ~/.config/idesktop/ (ideskrc and the .lnk icons).
# Afterwards the kiosk user can read everything but change nothing.
set -e
user="$1"; src="$2"
[ -n "$user" ] && [ -d "$src" ] || { echo "usage: $0 <user> <config-folder>"; exit 1; }
home=$(getent passwd "$user" | cut -d: -f6)

mkdir -p "$home/.config" "$home/Desktop"
rm -rf "$home/.config/idesktop"
cp -r "$src" "$home/.config/idesktop"

# root owns the configuration, the Desktop folder and the home itself, so
# the user can't rename or replace them. Directories 755, files 644.
chown root:root "$home" "$home/.config" "$home/Desktop"
chown -R root:root "$home/.config/idesktop"
chmod 755 "$home" "$home/.config" "$home/Desktop"
chmod -R u=rwX,go=rX "$home/.config/idesktop"
echo "Done. $user can read but not change $home/.config/idesktop or $home/Desktop."
echo "Note: with a root-owned home the session can't write its own files"
echo "(.Xauthority, caches...). Point those at a tmpfs or give the user a"
echo "writable subfolder if your session needs it."
