## Documentation used and produced for this project
This serves as a place where I put \(most of\) the various info that I gathered and compiled in order to work on and complete this project\. It includes some PDFs that I referenced\.

First, I provide the information on the standards, and then about the device itself\!

---

### Color correction
I got my info on color correction from [this site](https://near.sh/articles/video/color-emulation)\. It has information on how to translate RGB555 colors to RGB888 colors directly, how to emulate the GBA LCD colors, and how to emulate the GBC LCD colors\. I have integrated this information into my `tmds_calc.c` program, which generates the lookup tables necessary to allow fast color correction while running on the RP2040\. I can't guarantee whether or not it will actually work, but if it can, the final product will have optional color correction when uploading the firmware to the board\. \(At the moment, I don't think that there will be enough time to perform color correction\.\)

---

### The difference between VGA and DVI
HDMI is built off of the DVI standard, which is essentially a digital version of VGA with support for higher resolutions because of how it is encoded to support higher pixel clocks, as well as some changes to how syncing works in order to work with that transmission method\. The difference specifically being that 8\-bit color values are encoded to 10 bits, and a disparity variable is used to keep track of the difference between the number of zeroes and ones transmitted so that the DC offset of the signal can be kept to a minimum\. In addition to that, there are control signals/values that are transmitted at the end of the visible data along with a data enable signal so the sink knows it's time to listen for the sync signals\. The sync signals also have tighter timing requirements; sync signal transitions need to occur on the same pixel clock\. More details can be found within `tmds_calc.c`\.

Below is a small table of how the control signals are encoded\. The control signals are used on all 3 TMDS \(short for **T**ransition **M**inimized **D**ifferential **S**ignaling\) data channels; specifically on channel 0, they are used for horizontal and vertical sync, and on all 3 channels they are used to indicate whether a data period is either video data or a data island \(which I will cover later\.\) These values are taken straight from the DVI 1\.0 document, which formats the output data as big\-endian; I have converted it to little\-endian for easier understanding\.


| Control state | Output data |
| ------------- | ------------ |
| 0b00 | 0b1101010100 |
| 0b01 | 0b0010101011 |
| 0b10 | 0b0101010100 |
| 0b11 | 0b1010101011 |

---

### The difference between DVI and HDMI
The difference between DVI and HDMI is that HDMI adds the ability to send encoded data over the 3 data channels during the blanking periods, or data island periods\. This includes InfoFrames, which allow the signal source to transmit information about the signal to the sink, such as resolution, framerate, color depth, and audio information like number of channels and sample rate\. Each InfoFrame has a 3\-byte header, and a 28\-byte packet including a checksum byte\. The header is transmitted one bit at a time over channel 0, and the packet data is transmitted over channels 1 and 2\.

The header consists of a packet type byte, a version byte \(for the version of HDMI standard being used,\) and a length byte\. The length byte indicates how many bytes of the packet \(starting from packet byte 1, where byte 0 is the checksum\) are valid data\.

Hold it\! Before I move on to how data is encoded, I need to specify that before the data island period starts, a few things need to happen\. There are actually control periods between the active video data and the data island periods which tell the sink what data period is coming next, which are padded by some extra "control data\." This just happens to be horizontal and vertical sync \(bits 0 and 1 respectively\) on channel 0, with the useful data on channels 1 and 2\. First, the source transmits the extra data for at least 4 pixel clocks \(at this time, I don't think it's useful for anything, just padding\.\) Second, the source transmits the control word across channels 1 and 2 for 8 pixel clocks\. If the next data period is the data island, channels 1 and 2 transmit `0b01`, and if the next period is active video data, channel 1 transmits `0b01` and channel 2 transmits `0b00`\.

These preambles are supplemented by guard bands, which appear before the beginning of active video data, and at the beginning and end of the data island period, and are transmitted for 2 pixel clocks each\.

| Channel | Video guardband | Data island guardband |
| ------- | --------------- | --------------------- |
| 0 | 0b1011001100 | n/a |
| 1 | 0b0100110011 | 0b0100110011 |
| 2 | 0b1011001100 | 0b0100110011 |

---

### Auxiliary data encoding
HDMI uses an encoding method called TERC4 \(short for **T**MDS **E**rror **R**eduction **C**oding **4**\-bit\) to transmit data during the blanking periods, which are also known as data island periods\. TERC4 involves encoding 4 bits into a 10\-bit string to transmit via TMDS\. I don't know the exact algorithm used to do the encoding, but I do have a lookup table of those values\.

| 4\-bit value | 10\-bit encoded value |
| ----------- | ------------------- |
| 0b0000 | 0b1010011100 |
| 0b0001 | 0b1001100011 |
| 0b0010 | 0b1011100100 |
| 0b0011 | 0b1011100010 |
| 0b0100 | 0b0101110001 |
| 0b0101 | 0b0100011110 |
| 0b0110 | 0b0110001110 |
| 0b0111 | 0b0100111100 |
| 0b1000 | 0b1011001100 |
| 0b1001 | 0b0100111001 |
| 0b1010 | 0b0110011100 |
| 0b1011 | 0b1011000110 |
| 0b1100 | 0b1010001110 |
| 0b1101 | 0b1001110001 |
| 0b1110 | 0b0101100011 |
| 0b1111 | 0b1011000011 |

---

### Audio sample clock capture/regeneration theory
Since the audio is transmitted at the TMDS clock frequency, there is no immediate way to reconstruct the sample rate\. The HDMI source does have to send information every frame or two regarding the audio format \(which includes the sample rate,\) but that doesn't mean the sink can always regenerate the audio clock from that\. Instead, an InfoFrame may be sent that tells the sink the relationship between the TMDS clock and the audio sample clock in order to regenerate it\.

In this case, since the TMDS clock is 294MHz and the sample rate is 48KHz, the TMDS clock can simply be divided by 6125 to get the sample clock\. However, the audio reference clock for HDMI is 128 times the sample rate, so the TMDS clock is divided by 375 instead\. Because the relationship between the audio reference clock and the TMDS clock is `128*sample_rate = tmds_clock*(N/CTS)`, the value of N/CTS can simply be 1/375\.

However, because the sample clock is most likely already reconstructed from the information transmitted for the audio in general, this information isn't necessary to reconstruct the clock\.

---

### Signal specs
The output resolution is 720x480p at a pixel clock of 29\.4MHz \(and a TMDS clock of 294MHz respectively\.\) The total area used by the frame is 912x539, and the vertical refresh rate \(framerate\) is \~59\.8086Hz\. It's not the Gameboy's vertical refresh of \~59\.73Hz, but it's only \~0\.1358% faster\. Additionally, the resolution and TMDS clock allows an output clock of 4\.2MHz or 8\.4MHz to be perfectly synchronous with a Gameboy, Gameboy Advance or Gameboy Color\. Below is a table comparing the full specs of this signal to the standard 720x480p 60Hz\.

General info:
| Standard or no | Format code | Hactive | Vactive | Htotal | Hblank | Vtotal | Vblank | H freq \(KHz\) | V freq \(Hz\) | Pixel freq \(MHz\) |
| -------------- | ----------- | ------- | ------- | ------ | ------ | ------ | ------ | -------------- | ------------- | ------------------ |
| Standard | 2, 3 | 720 | 480 | 858 | 138 | 525 | 45 | 31\.4690 | 59\.9400 | 27\.000 |
| Custom | 2, 3 | 720 | 480 | 912 | 192 | 539 | 59 | 32\.2368 | 58\.8086 | 29\.400 |

The custom video resolution extends horizontal blanking by 54 pixels, and vertical blanking by 14 pixels\.

Specific sync info:
| Standard or no | Hfront | Hsync | Hback | Hpol | Vfront | Vsync | Vback | Vpol |
| -------------- | ------ | ----- | ----- | ---- | ------ | ----- | ----- | ---- |
| Standard | 16 | 62 | 60 | N | 9 | 6 | 30 | N |
| Custom | 32 | 64 | 96 | N | 13 | 8 | 38 | N |

If it matters, the sync pulses are all powers of 2 in pixel or line count\.

From the custom specifications, here are the \(rough\) data transmission timings for video outside of vertical blanking on channels 1 and 2:
- Data island padding data: 						22 pixel clocks
- Data island preamble: 							8 pixel clocks
- Data island guardband 1: 							2 pixel clocks
- Horizontal sync pulse and data island duration: 	64 pixel clocks
- Data island guardband 2: 							2 pixel clocks
- Video padding data: 								84 pixel clocks
- Video preamble: 									8 pixel clocks
- Video guardband: 									2 pixel clocks
- Active video data: 								720 pixel clocks
