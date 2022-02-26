// Edit: I'm probably going to use arm-none-eabi or something since I'll be integrating this with a little bit of C code to make things easier
// spoiler alert, this is the first time I've programmed in ARM assembly, but hopefully some of the things I've learned from GB ASM can carry over

.global SeparatePixel
.global CreateAddr
.global GetTMDS
.global GetDisparity

.code 16 // this is THUMB assembly so the width is 16 bits(?) (the processor only accepts THUMB instructions by default so idk)

.macro SeparatePixel $red, $green, $blue, $mask, $pixel
	and \red, \mask, \pixel
	and \green, \mask, \pixel {, lsr#5}
	and \blue, \mask, \pixel {, lsr#10}
.endm
	
	// If possible, the macros will just make it easier to do things in an unrolled loop
	// Err, more human-friendly

.macro CreateAddr $chaddr, $channel, $disparity
	orr \chaddr, \channel, \disparity {, lsl#5}
	lsl \chaddr #1
.endm
	
.macro GetTMDSandDisparity $tmds, $disparity, $chaddr, $tmds_lut
	ldr \tmds, [\tmds_lut+\chaddr]
	inc \chaddr // Is there a way to increment this after the ldr?
	ldr \disparity, [\tmds_lut+\chaddr]
.endm

	// Output pixel is repeated 3 times, but disparity is still needed for it
	// The same line is repeated 3 times, but the already encoded line is enough since disparity is reset at the end of a line
	// In 912x539 mode, the CPU has 2733*10 =  clock cycles to encode 720 pixels- almost 38 cycles per pixel. 
	// It might be easier to have a LUT that outputs disparity values assuming the same word is sent 3 times, and time might be saved if
	// disparity is in a separate word instead of being stored with the resulting TMDS data.
	// 6 MOVs plus a store multiple still needs to be done to clone the data, adding to the ~18 instructions to make the pixel.
	// This results in ~25 instructions to encode one pixel out of the 38 available.
	// An even faster way to encode the data would be to send 2 of the same TMDS data words, then send the third one to make the effective disparity equal to zero.
	// Or if there's enough memory, have the LUT output 3 separate TMDS words for the same color channel value.

.macro

// 

