Toshiba TC358746AXBG / TC358748XBG - parallel to CSI-2 or CSI-2 to parallel
https://toshiba.semicon-storage.com/info/TC358746AXBG_datasheet_en_20201214.pdf?did=30612&prodName=TC358746AXBG
datasheet confidential - 118 pages

Toshiba TC358743 - similar in i.MX Kernel HDMI-RX to MIPI CSI2-TX Bridge
https://github.com/phytec/linux-phytec/blob/v5.15.71/drivers/media/i2c/tc358743.c
https://github.com/phytec/linux-phytec/blob/v5.15.71/Documentation/devicetree/bindings/media/i2c/tc358743.txt

I2C: 0x0e, MSB/LSB

write 32 bit register:
ADDRESS ADDR_VAL[15:8] ADDR_VAL[7:0] DATA[15:8] DATA[7:0] DATA[31:24] DATA[23:16]

read 32 bit register:
ADDRESS DATA[15:8] DATA[7:0] DATA[31:24] DATA[23:16]
------------------------------------------------------------

i2cdetect -y 2  # I2C3 on SOD5 tester CSI1
#> 0x0e

# 0x0000 - Chip and Revision ID - read
i2ctransfer -y 2 w2@0x0e  0x00 0x00 r2
#> 0x44 0x01

# 0x0002 - System Control Register - read
i2ctransfer -y 2 w2@0x0e  0x00 0x02 r2
#> 0x00 0x02

# 0x0002 - System Control Register - write 0x0000
i2ctransfer -y 2 w4@0x0e  0x00 0x02  0x00 0x01  # reset
i2ctransfer -y 2 w4@0x0e  0x00 0x02  0x00 0x00  # normal

# 0x0002 - System Control Register - read
i2ctransfer -y 2 w2@0x0e  0x00 0x02 r2
#> 0x00 0x00

# 0x0004 - Configuration Control Register - read
i2ctransfer -y 2 w2@0x0e  0x00 0x04 r2
#> 0x80 0x04

# 0x0004 - Configuration Control Register - write
i2ctransfer -y 2 w4@0x0e  0x00 0x04  0x80 0x37
	# Parallel Data Format Option = Mode 0 = 0
	# CSI-2 Data Lane = 4
	# Vvalid Polarity Control = 1
	# Hvalid Polarity Control = 1
i2ctransfer -y 2 w4@0x0e  0x00 0x04  0x00 0x37
	# Parallel Out = 0
i2ctransfer -y 2 w4@0x0e  0x00 0x04  0x00 0x77
	# Parallel Port Enable = 1

# 0x0008 - Data Format Control Register - read
i2ctransfer -y 2 w2@0x0e  0x00 0x08 r2
#> 0x00 0x00

# 0x0008 - Data Format Control Register - write
i2ctransfer -y 2 w4@0x0e  0x00 0x08  0x00 0x30  # PDFormat = RGB888 = 3





rmmod tc358748
i2ctransfer -y 2 w4@0x0e  0x00 0x02  0x00 0x00  # normal
i2ctransfer -y 2 w2@0x0e  0x00 0x00 r2

i2ctransfer -y 2 w6@0x0e  0x02 0x04 0x12 0x34 0x56 0x78
i2ctransfer -y 2 w6@0x0e  0x02 0x10 0x11 0x33 0x55 0x77
i2ctransfer -y 2 w2@0x0e  0x02 0x04 r4
i2ctransfer -y 2 w2@0x0e  0x02 0x10 r4

------------------------------------------------------------
nVidia driver https://github.com/avionic-design/linux-l4t/blob/meerkat/l4t-r21-5/drivers/media/i2c/tc358748.c#L1010
	tc358748_probe():
SYSCTL 0x0002 = 1  # reset
msleep 50
SYSCTL 0x0002 = 0
CONFCTL 0x0004 = 0x04
tc358748_set_pll()

	tc358748_setup():
SYSCTL 0x0002 = 1  # reset
msleep 50
SYSCTL 0x0002 = 0

	# tc358748_set_pll()
# V4L2_MBUS_FMT_RGB888_1X24:
# pdformat = 3
# bpp = 24
csi_bus = 38'028'180 (lub 76'056'360)
csi_rate = bpp * pixelclock = 24 * 12'676'060 Hz = 304'225'440 bps
csi_lane_rate = csi_rate / num_data_lanes = 304'225'440 / 4 = 76'056'360 (min 62'500'000, max 1G)
rate = csi_lane_rate

PLLCTL0 0x0016 = (PRD << 12) | FBD = (1 << 12) | 383
PLLCTL1 0x0018 = (FRS << 10) | (2 << 8) | (1 << 1) | (1 << 0) = (3 << 10) | (2 << 8) | (1 << 1) | (1 << 0)
msleep 20  # wait for PLL to lock

sclk_div = clk_div = FRS > 2 ? 2 : FRS;
sclk_div = clk_div = 2
CLKCTL 0x0020 = (2 << 4) | (2 << 2) | 2  # podejrzane to jest
PLLCTL1 0x0018 |= (1 << 4)  # turn on clocks
	# end tc358748_set_pll()

CONFCTL ?
FIFOCTL ?
DATAFMT ?
WORDCNT ?

hsbyte_clk = csi_lane_rate / 8 = 76'056'360 / 8 = 9'507'045
hsbyte_clk / 2 = 9'507'045 / 2 = 4'753'522,5
linecnt = 0,000'100 (> 100us) / (1 / 4'753'522,5) = 475,35 -> + 1 = 476
LINEINITCNT 0x0210 = linecnt
lptxtime = 0,000'000'050 (> 50ns) / hsbyte_clk = 0,000'000'050 (> 50ns) / (1 / 9'507'045) = 0,47535225 -> 1
LPTXTIMECNT 0x0214 = lptxtime
tclk_prepare = (38ns - 95ns) / hsbyte_clk = 0,000'000'038 / (1 / 9'507'045) = 0,36126771 -> 1
tclk_zero = 300ns / hsbyte_clk - tclk_prepare = 0,000'000'038 / (1 / 9'507'045) - 1 = 2,8521135 - 1 -> 3 - 1 = 2
TCLK_HEADERCNT = (tclk_zero << 8) | tclk_prepare = (3 << 8) | 2
tclk_trail = 60ns / hsbyte_clk = 0,000'000'060 / (1 / 9'507'045) = 0,5704227 -> 1
TCLK_TRAILCNT = tclk_trail
ths_prepare = 
ths_zero = 
THS_HEADERCNT = (ths_zero << 8) | ths_prepare
t_wakeup = 
TWAKEUP = t_wakeup
tclk_post = 
TCLK_POSTCNT = tclk_post
ths_trail = 
THS_TRAILCNT = ths_trail
HSTXVREGCNT = 5
HSTXVREGEN = (((1 << num_data_lanes) - 1) << 1) | 1
TXOPTIONCNTRL = 1
STARTCNTRL = 1
CSI_START = 1
CSI_CONFW = (5 << 29) | (3 << 24) | ((4 - 1) << 1)

DBG_LCNT = 480 - 1
DBG_WIDTH = (800 - 1) * 24 / 8 = 2397
# DBG_WIDTH = 800 * 24 / 8 - 1 = 2399  # lub
DBG_VBLANK = 2 - 1  # Vsync = 2

# ----------------------------- Debug registers
i2ctransfer -y 2 w2@0x0e 0x00 0x80 r2

i2ctransfer -y 2 w2@0x0e 0x00 0xe0 r2
i2ctransfer -y 2 w2@0x0e 0x00 0xe2 r2
i2ctransfer -y 2 w2@0x0e 0x00 0xe4 r2
i2ctransfer -y 2 w2@0x0e 0x00 0xe8 r2




# ----------------------------- Debug registers

Inner PLL nie wymaga REFCLK - PLL bierze zegar z PCLK/4
RGB888
HSync i VSync active low
REFCLK zwarty do masy
? PCLK - normal or inverted
CSI-2 D-PHY (1 clock lane + 4 data lanes, 1 lane = 2 lines)

# ----------------------------- PLL and clock

	CSI clock dla PCLK = 12'676'060 Hz
Pixel clock:           800 * 525 * 30,181095238 = 12'676'060 Hz
Bandwich:              12'676'060 * 24 = 304'225'440 bps
Data Rate Per Line:    304'225'440 / 4 = 76'056'360 bps
MIPI D-PHY Clock Rate: 76'056'360 / 2 = 38'028'180 Hz    # / 2 - Double Data Rate (?)

CSITxClk is obtained by dividing pll_clk by 2.
Pll_clk = CSITxClk * 2
Pll_clk = 38'028'180 * 2 = 76'056'360 Hz   # dla DDR - Double Data Rate
Pll_clk = 76'056'360 * 2 = 152'112'720 Hz  # bez DDR

In CSI 2 Tx mode, RefClk can be tie to ground.
In this case, PClk / 4 will be used to drive PLL, Figure 5-3.
REFCLK = 12'676'060 / 4 = 3'169'015 Hz

REFCLK * ((FBD + 1) / (PRD + 1)) * (1 / (2 ^ FRS)) = Pll_clk
3'169'015 * ((383 + 1) / (1 + 1)) * (1 / (2 ^ 3)) =  76'056'360  # z DDR
3'169'015 * ((383 + 1) / (1 + 1)) * (1 / (2 ^ 2)) = 152'112'720  # bez DDR
see 'Toshiba PLL calculation.ods'

For FRS (HSCK = pll_clk):
Frequency range setting (post divider) for HSCK frequency
2’b00: 500MHz – 1GHz HSCK frequency
2’b01: 250MHz – 500MHz HSCK frequency
2’b10: 125 MHz – 250MHz HSCK frequency
2’b11: 62.5MHz – 125MHz HSCK frequency

example in datasheet:
16600000 * (255 + 1) / (7 + 1) * (1 / (2 ^ 1)) = 265600000
16600000 * (319 + 1) / (5 + 1) * (1 / (2 ^ 2)) = 221333333



# -----------------------------

	dla 12'600'000 Hz (użyć 12'676'060 Hz)
Pixel clock:           800 * 525 * 30 = 12'600'000 Hz
Bandwich:              12'600'000 * 24 = 302'400'000 bps
Data Rate Per Line:    302'400'000 / 4 = 75'600'000 bps
MIPI D-PHY Clock Rate: 75'600'000 / 2 = 37'800'000 Hz    # / 2 - Double Data Rate (?)

Kalkulator Texas Instrument Imager-Serializer nie dzieli przez 2

Example for 2592x1944x15 YUV422 15fps:
2844 * 1968 * 15 = 83954880
83954880 * 16 = 1343278080
1343278080 / 2 = 671639040
671639040 / 2 = 335'819'520





4.6.1 Enable and Disable Parallel Input (Video)
0x0032[15] (FrmStop), 0x0032[14] (RstPtr) and 0x0004[6] (PP_En)
1 Start Video to TC358746A
2 Clear RstPtr and FrmStop to 1’b0
3 Set PP_En to 1’b1


# ----------------------------- To Do

- STM32 restartuje Toshibę po pojawieniu się PCLK z FPGA - około 1s
- power sequence dla Toshiby nie zgodne z datasheetem - włączanie napięć po kolei

# ----------------------------- Links

NXP MIPI CSI-2
https://www.nxp.com/docs/en/application-note/AN13573.pdf

i.MX 8MP Dual ISI channels for single CSI
https://www.nxp.com/docs/en/application-note/AN13430.pdf

MIPI–CSI2 Peripheral on i.MX6 MPUs
https://www.nxp.com/docs/en/application-note/AN5305.pdf

https://martin.hinner.info/vga/timing.html
https://patchwork.kernel.org/project/linux-media/patch/20190619152838.25079-3-m.felsch@pengutronix.de/#22725185
https://www.cnblogs.com/biglucky/p/4157488.html
https://www.spinics.net/lists/linux-media/msg180342.html
https://github.com/avionic-design/linux-l4t/blob/meerkat/l4t-r21-5/drivers/media/i2c/tc358748.c#L157
https://community.nxp.com/t5/i-MX-Processors/IMX8MM-MIPI-CSI/td-p/1048942
	TC9590XBG_Rev1.0_20181227.pdf

https://forums.raspberrypi.com/viewtopic.php?t=287424
https://community.nxp.com/t5/i-MX-Processors/Parallel-to-mipi-csi2-issue-for-toshiba-chip-TC358748/td-p/544245?profile.language=en
https://community.nxp.com/t5/i-MX-Processors-Knowledge-Base/Debug-steps-for-customer-MIPI-sensor-docx/ta-p/1120244
https://community.nxp.com/t5/i-MX-Processors/imx6d-camera-device-tree/m-p/610760
https://lists.freebsd.org/pipermail/freebsd-doc/2006-March/009918.html
ADV7480

https://community.nxp.com/t5/i-MX-Processors/Trouble-with-TC358748-Parallel-to-CSI2-video-bridge/td-p/1846172?attachment-id=174022

https://community.nxp.com/t5/i-MX-Processors/How-to-calculate-MIPI-CSI-2-required-operating-frequency/m-p/331051
https://community.nxp.com/t5/i-MX-Processors/MIPI-CSI2-Required-Operating-Frequency-Calculation/m-p/431947


V4L2 capture
https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/v4l2.html
https://vocal.com/video/overview-of-video-capture-in-linux/
https://www.researchgate.net/figure/V4L2-image-capture-operation-flowchart_fig1_324172011
http://v4l.videotechnology.com/dwg/v4l2.pdf

VIDIOC_DQBUF
https://www.nxp.com/docs/en/reference-manual/IMX_REFERENCE_MANUAL.pdf

https://www2.baslerweb.com/en/downloads/software-downloads/basler-camera-enablement-package-for-nxp-s-i-mx-8m-plus-applications-processor/

https://github.com/avionic-design/linux-l4t/blob/meerkat/l4t-r21-5/drivers/media/i2c/tc358748.c#L793
https://community.nxp.com/t5/i-MX-Processors/TC358743-HDMI-CSI-Bridge-Driver-for-iMX8QM/m-p/1279555
https://github.com/torvalds/linux/blob/v6.2/drivers/media/i2c/tc358746.c


nVidia camera development
https://docs.nvidia.com/jetson/archives/r34.1/DeveloperGuide/text/SD/CameraDevelopment.html
https://elinux.org/Jetson/Cameras


	Kernel debug
echo 3 > /sys/module/mxc_mipi_csi/parameters/debug
echo 3 > /sys/module/mxc_mipi_csi2_yav/parameters/debug
echo 0xff > /sys/class/video4linux/video0/dev_debug
echo 0xff > /sys/class/video4linux/v4l-subdev1/dev_debug

dmesg
# dmesg | grep 'video0\|videodev'

	V4L2 capture driver debug

echo 0xff > /sys/class/video4linux/video0/dev_debug; \
gst-launch-1.0 v4l2src num-buffers=1 device=/dev/video0 ! video/x-raw,width=64,height=64 ! multifilesink location=image.raw; \
dmesg | \
tail -20

/home/p2119/linux-imx-v5.15.71_2.2.2-phy/include/uapi/linux/videodev2.h
#define V4L2_BUF_FLAG_MAPPED			0x00000001
#define V4L2_BUF_FLAG_QUEUED			0x00000002

============================================================
	Driver debug
https://forums.developer.nvidia.com/t/how-to-check-v4l2-dbg-message-in-the-console/57749/12

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "debug level (0-3)");

echo 1 > /sys/module/tc358840/parameters/debug

v4l2_dbg(1, debug, sd, "%s: no valid signal\n", __func__);

============================================================






gst-launch-1.0 v4l2src num-buffers=1 device=/dev/video0 ! video/x-raw,width=640,height=480 ! multifilesink location=image.raw
gst-launch-1.0 v4l2src num-buffers=1 device=/dev/video0 ! video/x-raw,width=640,height=480,format=RGB ! multifilesink location=image.raw
hexdump -n384 image.raw
ll image.raw  # 640 * 480 * 3 = 921600

gst-launch-1.0 v4l2src num-buffers=1 device=/dev/video0 ! video/x-raw,width=640,height=480,format=RGB ! jpegenc ! multifilesink location=image.jpeg

gst-launch-1.0 v4l2src num-buffers=1 device=/dev/video0 ! video/x-raw,width=640,height=480,format=YUV422 ! jpegenc ! multifilesink location=image.jpeg

scp root@192.168.3.11:/root/image.jpeg . && xdg-open image.jpeg

echo "alias j='gst-launch-1.0 v4l2src num-buffers=1 device=/dev/video0 ! video/x-raw,width=640,height=480,format=RGB ! jpegenc ! multifilesink location=image.jpeg'" >> .bashrc
echo "alias t='gst-launch-1.0 v4l2src device=/dev/video0 ! video/x-raw,width=640,height=480,framerate=30/1,format=RGB ! videoconvert ! vpuenc_h264 ! mpegtsmux ! tcpserversink port=8888 host=0.0.0.0'" >> .bashrc



	# i.MX send video - TCP server h264
gst-launch-1.0 v4l2src device=/dev/video0 ! video/x-raw,width=640,height=480,framerate=30/1,format=RGB ! videoconvert ! vpuenc_h264 ! mpegtsmux ! tcpserversink port=8888 host=0.0.0.0

	# PC receive - TCP server h264
gst-launch-1.0 -v tcpclientsrc port=8888 host=192.168.3.11 ! tsdemux ! h264parse ! openh264dec ! fpsdisplaysink sync=false


	# i.MX send video - RTSP server h265
./test-launch "v4l2src device=/dev/video0 ! video/x-raw,width=640,height=480,framerate=30/1,format=RGB ! videoconvert ! vpuenc_hevc ! rtph265pay name=pay0"

	# PC receive - RTSP h265
gst-launch-1.0 rtspsrc location=rtsp://192.168.3.11:8554/test latency=0 ! rtph265depay ! h265parse ! avdec_h265 ! decodebin ! fpsdisplaysink sync=false



dmesg | grep '\-\-\-'

[   12.673114] input fmt RGB4                                                                       
[   12.675819] output fmt RGB3                                                                      

v4l2-ctl -d /dev/video0 --set-fmt-video=pixelformat=RGB3

	Image test - 8 color bars
The 75% Colour Bars or EBU/IBA 100/0/75/0 Colour Bars pattern
https://en.wikipedia.org/wiki/EBU_colour_bars

https://en.wikipedia.org/wiki/SMPTE_color_bars

white   #FFFFFF
yellow  #F0FF03
cyan    #0FFFFF
green   #00FF03
magenta #FF00FC
red     #F00000
blue    #0F00FC
black   #000000
