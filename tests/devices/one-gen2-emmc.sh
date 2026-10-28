set -e
# An MPC One Gen2 (user report, 2026-10-28, df -h): no /nvme; /synths on eMMC (mmcblk0p11), /storage a loop image, /data full for non-root
# (5% reserved), the SD card /media/MPC (mmcblk1p1) and a USB stick (sda1, noexec vfat). The card is listed FIRST, but it can be pulled out:
# the default must be an internal location (told by the mount's device, not by a path prefix).
S=/data/Settings/MPC; mkdir -p $S /media/MPC/Synths /synths/Synths /storage/Synths
printf '<?xml version="1.0" encoding="UTF-8"?>\r\n<PROPERTIES>\r\n  <VALUE name="SynthContentLocations">\r\n    <SynthContentLocations>\r\n      <Location>/media/MPC/Synths</Location>\r\n      <Location>/synths/Synths</Location>\r\n      <Location>/storage/Synths</Location>\r\n    </SynthContentLocations>\r\n  </VALUE>\r\n  <VALUE name="pluginList-arm-64bit">\r\n    <KNOWNPLUGINS/>\r\n  </VALUE>\r\n</PROPERTIES>\r\n' > $S/MPC.settings
printf '/dev/mmcblk0p11 /synths ext4 rw,relatime 0 0\n/dev/loop0p1 /storage exfat rw,relatime 0 0\n/dev/mmcblk0p12 /data ext4 rw,relatime 0 0\n/dev/mmcblk1p1 /media/MPC exfat rw,relatime 0 0\n/dev/sda1 /media/B89A-DFD0 vfat rw,noexec 0 0\n' > /tmp/pm-mounts
printf "#!/bin/sh\nexit 0\n" > /usr/bin/systemctl; chmod +x /usr/bin/systemctl
