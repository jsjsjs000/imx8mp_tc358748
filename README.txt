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


media-ctl -p; ls /dev/video*; ls /dev/media*

v4l2-ctl -d0 --list-devices
v4l2-ctl -d0 --list-formats


	# usefull aliases for i.MX - writing to .bashrc
echo "alias a='media-ctl -p; ls /dev/video*; ls /dev/media*'" >> .bashrc
echo "alias re='reboot'" >> .bashrc
echo "alias g='gst-launch-1.0 -vvv v4l2src device=/dev/video0 ! video/x-raw,format=YUY2,width=640,height=480 ! autovideosink'" >> .bashrc


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


gst-launch-1.0 -vvv v4l2src device=/dev/video0 ! video/x-raw,format=YUY2,width=640,height=480 ! autovideosink
gst-launch-1.0 -vvv v4l2src device=/dev/video0 ! video/x-raw,format=RGB,width=640,height=480 ! autovideosink

gst-launch-1.0 videotestsrc ! video/x-raw,width=640,height=480 ! vpuenc_h264 ! mpegtsmux ! tcpserversink port=8888 host=0.0.0.0
gst-launch-1.0 v4l2src device=/dev/video0 ! video/x-raw,width=640,height=480 ! vpuenc_h264 ! mpegtsmux ! tcpserversink port=8888 host=0.0.0.0
gst-launch-1.0 -v tcpclientsrc port=8888 host=192.168.3.11 ! tsdemux ! h264parse ! openh264dec ! glimagesink sync=false


gst-device-monitor-1.0

v4l2-ctl --device /dev/video0 --get-fmt-video
v4l2-ctl --device /dev/video0 --set-fmt-video=width=640,height=480,pixelformat=YUY2 --stream-mmap --stream-count=1 --stream-to=image.bin
v4l2-ctl --device /dev/video0 --set-fmt-video=width=640,height=480,pixelformat=RGB --stream-mmap --stream-count=1 --stream-to=image.bin
v4l2-ctl --device /dev/video0 --set-fmt-video=width=640,height=480 --stream-mmap --stream-count=1 --stream-to=image.bin


setup-pipeline-csi1 -f SGRBG8_1X8 -s 640x480 -o '(0,0)' -c 640x480
#> No camera found on CSI1     !!!!!!!!!!!!!!!!!!!! $$

setup-pipeline-csi1 -f Y8_1X8 -s 64x64 -o '(0,0)' -c 64x64
#> No camera found on CSI1     !!!!!!!!!!!!!!!!!!!! $$
gst-launch-1.0 v4l2src num-buffers=1 device=/dev/video0 ! video/x-raw,width=64,height=64 ! multifilesink location=image.bin


dmesg | grep "No remote pad found!"
[    2.891892] : mipi_csis_imx8mp_phy_reset, No remote pad found!
[    6.670282] mxc_isi.0: is_entity_link_setup, No remote pad found!


# --------------------------------------------------------------------------------

media-ctl -p

media-ctl -r
media-ctl -l "'tc358748 2-000e':0->'mxc-mipi-csi2.0':0[1]"


i.MX 8 GStreamer User Guide
https://community.nxp.com/pwmxy87654/attachments/pwmxy87654/imx-processors%40tkb/15/2/i.MX8GStreamerUserGuide.pdf


ADV7180 driver

clock
  - dokładna częstotliwość
  - (non)continues clock
#define ADV_DEBUG registers

i2c_wr32(sd, TXOPTIONCNTRL, 0); i2c_wr32(sd, TXOPTIONCNTRL, MASK_CONTCLKMODE);

https://gist.github.com/olesia-kochergina/c2af863c250c748c3c58dbb7acfe84bf

https://community.nxp.com/t5/i-MX-Processors/Trouble-with-TC358748-Parallel-to-CSI2-video-bridge/td-p/1846172

https://github.com/avionic-design/linux-l4t/blob/meerkat/l4t-r21-5/drivers/media/i2c/tc358748.c#L793

https://community.nxp.com/t5/i-MX-Processors/Host-Driver-not-registring-V4L2-subdevices-after-bridge/m-p/1359414


# ---------------------------------------- AR051

gst-launch-1.0 v4l2src device=/dev/video0 ! video/x-bayer,format=grbg,depth=8,width=1920,height=1080,framerate=60/1,pixel-aspect-ratio=1/1 ! bayer2rgbneon ! autovideosink

gst-launch-1.0 v4l2src device=/dev/video0 ! video/x-bayer,format=grbg,depth=8,width=1920,height=1080,framerate=60/1,pixel-aspect-ratio=1/1 ! bayer2rgbneon ! vpuenc_h264 ! mpegtsmux ! tcpserversink port=8888 host=0.0.0.0

	# PC:
gst-launch-1.0 -v tcpclientsrc port=8888 host=192.168.3.11 ! tsdemux ! h264parse ! openh264dec ! glimagesink sync=false


gst-launch-1.0 v4l2src num-buffers=1 device=/dev/video0 ! video/x-bayer,format=grbg,depth=8,width=2592,height=1944 ! bayer2rgbneon ! videoconvert ! jpegenc ! multifilesink location=image.jpg
	# PC:
scp root@192.168.3.11:/root/image.jpg . && xdg-open image.jpg


setup-pipeline-csi1 -f SGRBG8_1X8 -s 1920x1080 -o '(336,432)' -c 1920x1080

gst-launch-1.0 v4l2src num-buffers=1 device=/dev/video0 ! video/x-bayer,format=grbg,depth=8,width=1920,height=1080 ! bayer2rgbneon ! videoconvert ! jpegenc ! multifilesink location=image.jpg

gst-launch-1.0 v4l2src num-buffers=1 device=/dev/video0 ! video/x-bayer,format=grbg,depth=8,width=1920,height=1080 ! multifilesink location=image.bin
	# PC:
scp root@192.168.3.11:/root/image.bin . && ghex image.bin &



setup-pipeline-csi1 -f SGRBG8_1X8 -s 800x480 -o '(896,732)' -c 800x480

gst-launch-1.0 v4l2src device=/dev/video0 ! video/x-bayer,format=grbg,depth=8,width=800,height=480,framerate=60/1,pixel-aspect-ratio=1/1 ! bayer2rgbneon ! vpuenc_h264 ! mpegtsmux ! tcpserversink port=8888 host=0.0.0.0


setup-pipeline-csi1 -f SGRBG8_1X8 -s 640x480 -o '(0,0)' -c 640x480
gst-launch-1.0 v4l2src device=/dev/video0 ! video/x-bayer,format=grbg,depth=8,width=640,height=480,framerate=60/1,pixel-aspect-ratio=1/1 ! bayer2rgbneon ! vpuenc_h264 ! mpegtsmux ! tcpserversink port=8888 host=0.0.0.0

	# 64x64
setup-pipeline-csi1 -f SGRBG8_1X8 -s 64x64 -o '(0,0)' -c 64x64
gst-launch-1.0 v4l2src num-buffers=1 device=/dev/video0 ! video/x-bayer,format=grbg,depth=8,width=64,height=64 ! multifilesink location=image.bin

#> Setting pipeline to PAUSED ...
#> Pipeline is live and does not need PREROLL ...
#> Pipeline is PREROLLED ...
#> Setting pipeline to PLAYING ...
#> New clock: GstSy[   28.301518] bypass csc
#> stemClock
#> [   28.304896] input fmt RGB4
#> [   28.308599] output fmt GRBG
#> [   28.311426] mxc-isi 32e00000.isi: input_size(648,486), output_size(64,64)
#> [   28.870227] get_swap_device: Bad swap file entry 10020f1b0d180e1b

	# 64x64 mono
setup-pipeline-csi1 -f Y8_1X8 -s 64x64 -o '(0,0)' -c 64x64
gst-launch-1.0 v4l2src num-buffers=1 device=/dev/video0 ! video/x-bayer,format=grbg,depth=8,width=64,height=64 ! multifilesink location=image.bin

	# 64x64 mono RAW
setup-pipeline-csi1 -f Y8_1X8 -s 64x64 -o '(0,0)' -c 64x64
#> Setting Y8_1X8/64x64 (0,0)/64x64 for ar0521 2-0036

gst-launch-1.0 v4l2src num-buffers=1 device=/dev/video0 ! video/x-raw,width=64,height=64 ! multifilesink location=image.bin
#> RGB4 to YUYV - file 8KB - 64*64*2=8192

scp root@192.168.3.11:/root/image.bin . && ghex image.bin &

	# w AR0521:
[    2.890677] : mipi_csis_imx8mp_phy_reset, No remote pad found!


# ------------------------------------------------------------
	CSI-2 debug
kernel/drivers/mxc/mipi/mxc_mipi_csi2.c:275
mipi_csi2_write(info, 0x00000001, CSI2_PHY_TST_CTRL0);

Debug steps for customer MIPI sensor Data Flow
google: Debug steps for customer MIPI sensor.docx

# ------------------------------------------------------------ Unknown $$$$
https://community.nxp.com/t5/i-MX-Processors/IMX8MP-HDMI-input-Convert-to-MIPI-CSI-debug-issue/m-p/1458690/highlight/true
https://www.kernel.org/doc/html/v5.13/driver-api/media/csi2.html
https://community.infineon.com/t5/USB-superspeed-peripherals/How-can-I-debug-the-MIPI-CSI-2-RX-interface/td-p/75860
https://www.kernel.org/doc/html/v4.15/media/kapi/csi2.html

https://www.ezurio.com/resources/software-announcements/hdmi-input-for-i-mx8-boards-via-mipi-csi

# ------------------------------------------------------------ debug V4L2 driver:
/home/p2119/linux-imx-v5.15.71_2.2.2-phy/drivers/media/v4l2-core/v4l2-ioctl.c
static void v4l_print_buffer(const void *arg, bool write_only)

alias up_k='scp ~/linux-imx-v5.15.71_2.2.2-phy/arch/arm64/boot/Image root@192.168.3.11:/boot/'

	# ok:
video0: VIDIOC_STREAMON: type=vid-cap-mplane
videodev: v4l2_poll: video0: poll: 00000000 00000039
videodev: v4l2_poll: video0: poll: 00000041 00000039
video0: VIDIOC_DQBUF: 00:00:30.838612 index=0, type=vid-cap-mplane, request_fd=0, flags=0x00002001, field=none, sequence=1, memory=mmap
plane 0: bytesused=8192, data_offset=0x00000000, offset/userptr=0x0, length=8192
timecode=00:00:00 type=0, flags=0x00000000, frames=0, userbits=0x00000000
video0: VIDIOC_QBUF: 00:00:00.000000 index=0, type=vid-cap-mplane, request_fd=0, flags=0x00002003, field=none, sequence=0, memory=mmap
plane 0: bytesused=8192, data_offset=0x00000000, offset/userptr=0x0, length=8192
timecode=00:00:00 type=0, flags=0x00000000, frames=0, userbits=0x00000000
video0: VIDIOC_STREAMOFF: type=vid-cap-mplane
video0: VIDIOC_REQBUFS: count=0, type=vid-cap-mplane, memory=mmap
videodev: v4l2_release: video0: release

	# not ok:
video0: VIDIOC_STREAMON: type=vid-cap-mplane
videodev: v4l2_poll: video0: poll: 00000000 00000039
videodev: v4l2_poll: video0: poll: 00000000 00000039
video0: VIDIOC_STREAMOFF: type=vid-cap-mplane
video0: VIDIOC_REQBUFS: count=0, type=vid-cap-mplane, memory=mmap
videodev: v4l2_release: video0: release
