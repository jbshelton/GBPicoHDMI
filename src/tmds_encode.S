// Edit: I'm probably going to use arm-none-eabi or something since I'll be integrating this with a little bit of C code to make things easier
// spoiler alert, this is the first time I've programmed in ARM assembly, but hopefully some of the things I've learned from GB ASM can carry over

// load and store multiple cycle count is 1+number of registers to load/store

.global SeparatePixel
.global GetTMDSDisparity
.global PackTMDS

.code 16 // this is THUMB assembly so the width is 16 bits(?) (the processor only accepts THUMB instructions by default so idk)

	// Mask is 0x1f<<1
.macro SeparatePixel red, green, blue, mask, pixel
	and \red, \mask, [\pixel, lsl#1]
	and \green, \mask, [\pixel, lsr#4]
	and \blue, \mask, [pixel, lsr#9]
.endm
	
	// If possible, the macros will just make it easier to do things in an unrolled loop
	// Err, more human-friendly

	// Disparity is already shifted into the correct position when retrieving it from the TMDS LUT

.macro GetTMDSDisparity channel, disparity, chaddr, tmds_lut, tmds
	orr \chaddr, \channel, \disparity
	add \chaddr, \chaddr, \tmds_lut
	ldmia \chaddr, {\tmds, \disparity} // 3 cycles
.endm

	// Takes the left and right amounts for packing the current tripled pixel.
	// Changes them by 2.
.macro PackTMDS lshift, rshift, pix_0, pix_1
	lsr \pix_0 \rshift
	orr \pix_0, \pix_0, [\pix_1 lsl \lshift]
	add \rshift, \rshift, #2
	sub \lshift, \lshift, #2 // could be the wrong way right now
	// if it's 4 cycles total, 4*15=60 clock cycles for this macro to pack all values needed.
.endm

	// Output pixel is repeated 3 times, but disparity is still needed for it.
	// The same line is repeated 3 times, but the already encoded line is enough since disparity is reset at the end of a line
	// At 912x539, the CPU has up to 912*10/240 = 38 clock cycles to encode a color channel, or 608 clock cycles to pack
	// 16 of those values into 15 32-bit words.
	// 27360 total cycles for encoding all the pixels for a line at the most- a good way to encode a line is to have 3 buffers
	// of 240 color values (1 value per word) created with SeparatePixel, and then clobber those buffers with encoded TMDS.
	// (Perhaps do it 16 pixels at a time because of packing.)
	// Each entry of the LUT contains 3 properly-encoded repeated pixels for the input value and a 4-bit output disparity,
	// which is already shifted into position where it can be ORed with the next color value.

