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
The difference between DVI and HDMI is that HDMI adds the ability to send encoded data over the 3 data channels during the blanking periods, or data island periods\. This includes InfoFrames, which allow the signal source to transmit information about the signal to the sink, such as resolution, framerate, color depth, and audio information like number of channels and sample rate\. Each InfoFrame has a 3\-byte header, and a 31\-byte packet including a checksum byte \(which is calculated by adding all the bytes in the packet together and subtracting that from 256\.\) This means 30 bytes are valid data, so as an example, in order to transmit audio consistently, 6 samples \(24 bytes\) are transmitted during one InfoFrame\. The header is transmitted one bit at a time over channel 0, and the packet data is transmitted over channels 1 and 2\.

The header consists of a packet type byte, a version byte \(for the version of HDMI standard being used,\) and a length byte\. The length byte indicates how many bytes of the packet \(starting from packet byte 1, where byte 0 is the checksum\) are valid data\.

Hold it\! Before I move on to how data is encoded, I need to specify that before the data island period starts, a few things need to happen\. There are actually control periods between the active video data and the data island periods which tell the sink what data period is coming next, which are padded by some extra "control data\." This just happens to be horizontal and vertical sync \(bits 0 and 1 respectively\) on channel 0, with the useful data on channels 1 and 2\. First, the source transmits the extra data \(being normal DVI sync data\) for at least 4 pixel clocks\. Second, the source transmits the control word across channels 1 and 2 for 8 pixel clocks\. If the next data period is the data island, channels 1 and 2 transmit `0b01`, and if the next period is active video data, channel 1 transmits `0b01` and channel 2 transmits `0b00`\. \(2\-bit values refer to the control state lookup table above\.\)

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

If you're confused, here's what the terms mean:
- Hfront/Vfront: Horizontal/vertical front porch \(pixels/lines after the active video data before the sync pulse\)
- Hback/Vback: Horizontal/vertical back porch
- Hpol/Vpol: Horizontal/vertical sync pulse polarity

From the custom specifications, here are the \(rough\) data transmission timings for video outside of vertical blanking on channels 1 and 2:
| Data period | Duration \(in pixel clocks\) |
| ----------- | -------------------------- |
| Data island padding data | 22 |
| Data island preamble | 8 |
| Data island guardband 1 | 2 |
| Hsync pulse and data island | 64 |
| Data island guardband 2 | 2 |
| Video padding data | 84 |
| Video preamble | 8 |
| Video guardband | 2 |
| Active video data | 720 |

And here's the version without any data island periods:
| Data period | Duration \(in pixel clocks\) |
| ----------- | -------------------------- |
| Sync data | 182 |
| Video preamble | 8 |
| Video guardband | 2 |
| Active video data | 720 |

---

### TL;DR of concepts provided \(most important\)
TODO

---

### Initial DVI test program
Because I want to start out small, I want to create a test program that simply displays a solid color and outputs sync signals in DVI mode\. That is, without any preambles or guard bands for the data island periods\. To make it simple, here's how the program will work:
- Have one constant value/color to display that is sent in pairs with opposite parity so the equivalent DC offset is zero
- At the start of the frame, DMA that value for the horizontal resolution
- Chain into sync DMA \(the whole sync period is stored as raw data\)
- Chain back into active display
- Repeat for active vertical lines
- On the last sync period after the active display period, start DMAing from vsync buffer
- On the last hsync pulse before vsync ends, start DMAing from standard hsync buffer

And here's how each DMA will work:
- Active display: increment source \(frame or line buffer,\) constant destination \(PIO FIFO\), chain into sync DMA when done
- Sync: increment source \(output sync data,\) constant destination \(PIO FIFO\), chain into active display DMA when done
- Extra 1: on the last active video line, chain into a reconfigure DMA which switches the video position to just hsync and vsync values
- Extra 2: every 2 lines, chain into a reconfigure DMA which resets the line buffer position \(the output is from a double line buffer\)

For the record, the DMA will transfer data to the PIOs when the FIFOs are either empty or not full\.

In order to reduce DMA channel usage, there will be 2 DMAs for configuration: one that reads from a control data buffer and writes to the main channels, and one that the first DMA chains into that changes the read and write addresses of the first DMA\.

Where literal channel 0 chains into "DMA 1" and that chains into "DMA 2" which normally chains into "DMA 1" again:

Channel 0: finishes displaying active data, chains into channel 3 and DMA 1 \(assuming DMA 1 is already reading from channels 0\-2 configuration buffer\)
- DMA 1: configures DMA 2 to chain back into DMA 1
- DMA 2: configures channel 0
- DMA 1: configures DMA 2 to write to channel 1
- DMA 2: configures channel 1
- DMA 1: configures DMA 2 to write to channel 2
- DMA 2: configures channel 2
- DMA 1: configures DMA 2 to read from DMA 1 configuration buffer and to not chain back into DMA 1
- DMA 2: configures DMA 1 to read from channels 3\-5 configuration buffer

Channel 3: finishes sync, chains into channel 0 and DMA 1
- DMA 1: configures DMA 2 to chain back into DMA 1
- DMA 2: configures channel 3
- DMA 1: configures DMA 2 to write to channel 4
- DMA 2: configures channel 4
- DMA 1: configures DMA 2 to write to channel 5
- DMA 2: configures channel 5
- DMA 1: configures DMA 2 to read from DMA 1 configuration buffer and to not chain back into DMA 1
- DMA 2: configures DMA 1 to read from channels 0\-2 configuration buffer

However, I could handle it with the other CPU core and interrupts, since there's already more than enough time to encode a frame's worth of audio using C code and 2 DMA channels will be handling input capture without any aid from the CPU\. In that case, a DMA interrupt will occur every line if enabled by the CPU, and 4 PWM units will be configured to fire off an interrupt once every frame; one synchronized with the beginning of the last visible line \(or the start of the sync period, I don't know\), one synchronized with the beginning of the line when the vertical sync pulse is active, one synchronized with the beginning of the line when the vertical sync pulse becomes inactive, and one synchronized with the beginning of the line or sync period of the very last line of the frame\. One of those interrupts could also tell the CPU to switch DMA channels 3\-5 to a buffer containing an AVI InfoFrame \(to refresh the video signal information for the HDMI sink\) or an audio InfoFrame \(to refresh the audio information for the sink\.\) The one PWM unit that optionally triggers an interrupt every line is enabled by the CPU when it finishes encoding a single audio block, and disabled when that audio block has been fully transmitted\. When active, channel 3\-5 behavior changes so that instead of transmitting a single line of normal sync data \(including the vertical blanking period\) over and over, it switches to a sync and aux data buffer constructed by the CPU during audio encoding\.

When the CPU finishes encoding a full audio block, it will enable interrupts from DMA channel 3\. When it receives that interrupt, it immediately halts DMA 1 and DMA 2 and switches them over to a set of separate command block buffers, which depending on the current line can be either a vsync inactive buffer or a vsync active buffer\. After the entire audio block has been sent, the CPU leaves the interrupt enabled if it has finished encoding it, or disables it and switches DMA 1 and DMA 2 back to the normal sync buffer if it hasn't finished encoding it\. Because there is a difference between active video and vblank periods in terms of sync output \(which influences how TMDS channel 0 is encoded,\) the CPU will explicitly encode a fixed number of audio blocks to send during vblank\. If 12 samples are sent per line, and 16 lines make up an audio block, then it will encode a total of 2 audio blocks to send during the vsync back porch since it lasts 38 lines, and 2 audio blocks can be sent in 32 lines\. Since 8 or 9 audio blocks are transmitted per frame depending on how many samples are left over, that means 6 or 7 audio blocks will be sent during active video, over 60 or 72 lines\.

For vsync behavior, the first PWM interrupt should be synced with the end of the last active display period\. The CPU will then configure DMA channels 0\-2 to read from a constant address instead of the normal display line buffer\. The second, third and fourth PWM interrupts will also be synced at the end of what would be an active display line, and the CPU will just switch DMA channels 0\-2 between different configurations as to what to display during what would be active video\.

Of course, since I need to use DMA, I need to learn how it works from a programming point of view, and not just a technical point of view\.

---

### How to implement DMA in programming
TODO

---

### HDMI audio test program
Note: Because the InfoFrame packets are wacky, a null byte \(or header checksum?\) has to be sent during the header, and a null byte has to be sent at the end of a packet to pad it to 32 bytes\. In the case of audio, since only 6 samples or 24 bytes are transmitted in a packet in order to consistently send an audio block in 16 lines, it has to be padded with 7 null bytes, and the length in the header has to be set to 24\. I want to create the first DVI test program first before I think more about this, so it is currently TODO
