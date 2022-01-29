// use the GNU ARM assembler(?) (what is the proper assembler to use?)
// spoiler alert, this is the first time I've programmed in ARM assembly, but hopefully some of the things I've learned from GB ASM can carry over

.global SeparatePixel
.global CreateAddr
.global GetTMDS
.global GetDisparity

.code 16 // this is THUMB assembly so the width is 16 bits

.macro SeparatePixel red, green, blue, mask, pixel
	and \red, \mask, \pixel
	and \green, \mask, \pixel {, lsr#5}
	and \blue, \mask, \pixel {, lsr#10}
.endm
	
.macro CreateAddr $red_addr, $green_addr, $blue_addr, $red, $green, $blue, $red_disparity, $green_disparity, $blue_disparity
	orr \red_addr, \red, \red_disparity {, lsl#5}
	orr \green_addr, \green, \green_disparity {, lsl#5}
	orr \blue_addr, \blue, \blue_disparity {, lsl#5}
.endm
	
.macro GetTMDS $red_tmds, $green_tmds, $blue_tmds, $red_addr, $green_addr, $blue_addr, $tmds_lut
	ldr \red_tmds, [\tmds_lut+\red_addr]
	ldr \green_tmds, [\tmds_lut+\green_addr]
	ldr \blue_tmds, [\tmds_lut+\blue_addr]
.endm
	
.macro GetDisparity $red_disparity, $green_disparity, $blue_disparity, $red_tmds, $green_tmds, $blue_tmds
	mov \red_disparity, \red_tmds {, lsr#24}
	mov \green_disparity, \green_tmds {, lsr#24}
	mov \blue_disparity, \blue_tmds {, lsr#24}
.endm
