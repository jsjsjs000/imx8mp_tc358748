	# copy Device Tree to Linux Kernel
cp device-tree/imx8mp-tc358748-i2c3.dtso ~/linux-imx-v5.15.71_2.2.2-phy/arch/arm64/boot/dts/freescale/overlays/

nano ~/linux-imx-v5.15.71_2.2.2-phy/arch/arm64/boot/dts/freescale/overlays/Makefile
--------------------------------------------------------------------------------
dtb-y += imx8mp-tc358748-i2c3.dtbo
--------------------------------------------------------------------------------

vi /boot/bootenv.txt
--------------------------------------------------------------------------------
overlays=imx8mp-tc358748-i2c3.dtbo
--------------------------------------------------------------------------------

  # copy .config from Yocto to Linux Kernel standalone
cp ~/linux-imx-v5.15.71_2.2.2-phy/.config ~/linux-imx-v5.15.71_2.2.2-phy/.config_original  # backup original .config
cp ~/phyLinux/build/linux-imx/.config.new ~/linux-imx-v5.15.71_2.2.2-phy/.config

	# usefull aliases
alias co='make -j $(nproc)'
# alias co='make -j $(nproc) LOCALVERSION="-bsp-yocto-nxp-i.mx8mp-pd23.1.0" EXTRAVERSION=""'
alias upimg='scp arch/arm64/boot/Image root@192.168.3.11:/boot/'
alias up='ssh root@192.168.3.11 " \
  mkdir -p /lib/modules/5.15.71-bsp-yocto-nxp-i.mx8mp-pd23.1.0/kernel/drivers/media/i2c/" && \
  scp tc358748.ko root@192.168.3.11:/lib/modules/5.15.71-bsp-yocto-nxp-i.mx8mp-pd23.1.0/kernel/drivers/media/i2c/'
alias up_dtso='scp ~/linux-imx-v5.15.71_2.2.2-phy/arch/arm64/boot/dts/freescale/overlays/imx8mp-tc358748-i2c3.dtbo root@192.168.3.11:/boot/'
alias up_all='ssh root@192.168.3.11 " \
  mkdir -p /lib/modules/5.15.71-bsp-yocto-nxp-i.mx8mp-pd23.1.0/kernel/drivers/media/i2c/" && \
  scp drivers/media/i2c/tc358748.ko root@192.168.3.11:/lib/modules/5.15.71-bsp-yocto-nxp-i.mx8mp-pd23.1.0/kernel/drivers/media/i2c/ && \
  scp ~/linux-imx-v5.15.71_2.2.2-phy/arch/arm64/boot/dts/freescale/overlays/imx8mp-tc358748-i2c3.dtbo root@192.168.3.11:/boot/'
alias re='ssh root@192.168.3.11 reboot'
alias mod='ssh root@192.168.3.11 "modprobe -r tc358748; sleep 1; modprobe tc358748; ls /dev/video*; ls /dev/csi*; ls /dev/media*; ls /dev/cam-*; ls /dev/v4l*; media-ctl -p"'

clear; co && up && mod

depmod
insmod /lib/modules/5.15.71-bsp-yocto-nxp-i.mx8mp-pd23.1.0/kernel/drivers/media/i2c/tc358748.ko
rmmod tc358748
lsmod
dmesg | grep -i tc358748
dmesg | tail

i2cdetect -y 2  # I2C3
#> 0e as UU

dmesg | grep -i 'tc358748\|video\|csi\|isi\|media\|mx8-img-md\|mxc-m\|v4l'

dtc -I dtb -O dts /boot/imx8mp-tc358748-i2c3.dtbo

cat /proc/device-tree/soc\@0/bus\@30800000/i2c\@30a40000/tc358748\@0e/status
cat /proc/device-tree/soc\@0/bus\@30800000/i2c\@30a40000/tc358748\@0e/refclk | hexdump -e '1/4 "0x%08X" "\n"'
#> 0x80969800 -> bigendian 0x989680 = 10 MHz

cat /proc/device-tree/soc\@0/bus\@30800000/i2c\@30a40000/tc358748\@0e/refclk  | hexdump -e '1/1 "0x%02X "'


scp ~/linux-imx-v5.15.71_2.2.2-phy/arch/arm64/boot/dts/freescale/overlays/imx8mp-tc358748-i2c3.dtbo root@192.168.3.11:/boot/
