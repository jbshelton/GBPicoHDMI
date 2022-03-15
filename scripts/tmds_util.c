/*
	tmds_util.c

	This program generates the TMDS output data/lookup tables for the Raspberry Pi Pico/RP2040.
	And various other utilities. (Coming soon)

	TO DO:
	-Add TMDS control word packing (right now it only puts 1 10-bit TMDS word inside a uint16_t)
	-Add TMDS audio LUT generation (if necessary)
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <unistd.h>

#define H_ACTIVE 720
#define H_FRONT 32
#define H_PULSE 64
#define H_BACK 96
#define H_TOTAL 912

#define V_ACTIVE 480
#define V_FRONT 13
#define V_PULSE 8
#define V_BACK 38
#define V_TOTAL 539

const uint16_t sync_ctl_states[] = 
{
	0b0000001101010100,
	0b0000000010101011,
	0b0000000101010100,
	0b0000001010101011
};

const uint16_t guardband_states[] = 
{
	0b0000001011001100,
	0b0000000100110011
};

const uint16_t terc4_table[] = 
{
	0b0000001010011100,
	0b0000001001100011,
	0b0000001011100100,
	0b0000001011100010,
	0b0000000101110001,
	0b0000000100011110,
	0b0000000110001110,
	0b0000000100111100,
	0b0000001011001100,
	0b0000000100111001,
	0b0000000110011100,
	0b0000001011000110,
	0b0000001010001110,
	0b0000001001110001,
	0b0000000101100011,
	0b0000001011000011
};


struct tmds_pixel_t
{
	uint8_t color_data_5b;
	uint8_t color_data;
	uint16_t tmds_data;
	int disparity;
};

struct sync_buffer_t
{
	// Normal hblank
	uint16_t *hblank_ch0;
	uint16_t *hblank_ch1;
	uint16_t *hblank_ch2;
	// Entering and most of vblank: no video preamble or guard bands included, falling edge of vsync
	uint16_t *vblank_en_ch0;
	uint16_t *vblank_en_ch1;
	uint16_t *vblank_en_ch2;
	// Vsync: no video preamble or guard bands, with the addition of vsync pulse
	uint16_t *vblank_syn_ch0;
	uint16_t *vblank_syn_ch1;
	uint16_t *vblank_syn_ch2;
	// Exiting vblank: the last hblank of the frame is just a normal hblank, except with the addition of rising edge of vsync
	// (or first hblank before active video data)
	uint16_t *vblank_ex_ch0;
	uint16_t *vblank_ex_ch1;
	uint16_t *vblank_ex_ch2;
};

// Frees the allocated buffers before the program exits to prevent bad stuff from happening.
void free_sync_buffers(struct sync_buffer_t *sync_buffer)
{
	free(sync_buffer->hblank_ch0);
	free(sync_buffer->hblank_ch1);
	free(sync_buffer->hblank_ch2);

	free(sync_buffer->vblank_en_ch0);
	free(sync_buffer->vblank_en_ch1);
	free(sync_buffer->vblank_en_ch2);

	free(sync_buffer->vblank_syn_ch0);
	free(sync_buffer->vblank_syn_ch1);
	free(sync_buffer->vblank_syn_ch2);

	free(sync_buffer->vblank_ex_ch0);
	free(sync_buffer->vblank_ex_ch1);
	free(sync_buffer->vblank_ex_ch2);
}

// Video format (hsync before active video):
// Line 494: enter vsync buffer
// Lines 495-501: during vsync buffer
// Line 502: exit vsync buffer
// During vsync buffer lasts 1 line less than the number of lines per vsync pulse
// because the previous line counts as a line.
// Interrupt/timing scheme (starts at line 1, not line 0):
// Line 481 hblank start interrupt: reconfigure active video transmit as sync transmit
// Line 493 active start interrupt: prepare enter vsync buffer
// Line 494 active start interrupt: prepare during vsync buffer
// Line 501 active start interrupt: prepare exit vsync buffer
// Line 1 hblank start interrupt: reconfigure sync transmit as active video transmit again

// Creates 2 static data buffers: one for hsync, one for vsync.
// Vsync buffer does not include a video data period preamble or guard band
// They have null data during the data island periods.
// Since there will be only one output resolution, this uses global defines.
void create_sync_buffers(struct sync_buffer_t *sync_buffer)
{
	sync_buffer->hblank_ch0 = (uint16_t *)malloc((H_TOTAL-H_ACTIVE)*sizeof(uint16_t));
	sync_buffer->hblank_ch1 = (uint16_t *)malloc((H_TOTAL-H_ACTIVE)*sizeof(uint16_t));
	sync_buffer->hblank_ch2 = (uint16_t *)malloc((H_TOTAL-H_ACTIVE)*sizeof(uint16_t));

	sync_buffer->vblank_en_ch0 = (uint16_t *)malloc((H_TOTAL-H_ACTIVE)*sizeof(uint16_t));
	sync_buffer->vblank_en_ch1 = (uint16_t *)malloc((H_TOTAL-H_ACTIVE)*sizeof(uint16_t));
	sync_buffer->vblank_en_ch2 = (uint16_t *)malloc((H_TOTAL-H_ACTIVE)*sizeof(uint16_t));

	sync_buffer->vblank_syn_ch0 = (uint16_t *)malloc((H_TOTAL-H_ACTIVE)*sizeof(uint16_t));
	sync_buffer->vblank_syn_ch1 = (uint16_t *)malloc((H_TOTAL-H_ACTIVE)*sizeof(uint16_t));
	sync_buffer->vblank_syn_ch2 = (uint16_t *)malloc((H_TOTAL-H_ACTIVE)*sizeof(uint16_t));

	sync_buffer->vblank_ex_ch0 = (uint16_t *)malloc((H_TOTAL-H_ACTIVE)*sizeof(uint16_t));
	sync_buffer->vblank_ex_ch1 = (uint16_t *)malloc((H_TOTAL-H_ACTIVE)*sizeof(uint16_t));
	sync_buffer->vblank_ex_ch2 = (uint16_t *)malloc((H_TOTAL-H_ACTIVE)*sizeof(uint16_t));
	// Format, starting in hblank:
	// Normal sync data for at least 4 pixel clocks
	// Preamble for 8 pixel clocks (TMDS channel 1, channel 2): (data island here)
	// Data island: 0b01, 0b01; Video period: 0b01, 0b00
	// Guard band for 2 pixel clocks (channel 0, 1, 2): (data island here)
	// Video: 0b1011001100, 0b0100110011, 0b1011001100; Data: n/a, 0b0100110011, 0b0100110011
	// Data island period: 64 clocks total, 32 per InfoFrame/packet
	// Guard band for 2 pixel clocks (data island exit)
	// Normal sync data for at least 4 pixel clocks
	// Preamble for 8 pixel clocks (video period here)
	// Guard band for 2 pixel clocks (video period here)
	// Active video data (not included in sync buffers)
	int data_pad = H_FRONT - 10;
	int video_pad = H_BACK - 12; // Data island has 2 guardbands, so we subtract 2 guardbands total (2nd data island one and video data one)
	int j = 0;
	for(int i=0; i<data_pad; i++)
	{
		sync_buffer->hblank_ch0[j] = sync_ctl_states[3]; // Since signals are active low, during this period they're both high
		sync_buffer->hblank_ch1[j] = sync_ctl_states[0]; // Channels 1 and 2 are to be kept low
		sync_buffer->hblank_ch2[j] = sync_ctl_states[0];

		sync_buffer->vblank_en_ch0[j] = sync_ctl_states[3]; // The falling edge of vsync comes later
		sync_buffer->vblank_en_ch1[j] = sync_ctl_states[0];
		sync_buffer->vblank_en_ch2[j] = sync_ctl_states[0];

		sync_buffer->vblank_syn_ch0[j] = sync_ctl_states[1]; // Bit 1 is vsync, so it is to be kept low during this period
		sync_buffer->vblank_syn_ch1[j] = sync_ctl_states[0];
		sync_buffer->vblank_syn_ch2[j] = sync_ctl_states[0];

		sync_buffer->vblank_ex_ch0[j] = sync_ctl_states[1]; // The rising edge of vsync comes later
		sync_buffer->vblank_ex_ch1[j] = sync_ctl_states[0];
		sync_buffer->vblank_ex_ch2[j++] = sync_ctl_states[0];
	}
	for(int i=0; i<8; i++)
	{
		sync_buffer->hblank_ch0[j] = sync_ctl_states[3];
		sync_buffer->hblank_ch1[j] = sync_ctl_states[1];
		sync_buffer->hblank_ch2[j] = sync_ctl_states[1];

		sync_buffer->vblank_en_ch0[j] = sync_ctl_states[3]; // Not yet
		sync_buffer->vblank_en_ch1[j] = sync_ctl_states[1];
		sync_buffer->vblank_en_ch2[j] = sync_ctl_states[1];

		sync_buffer->vblank_syn_ch0[j] = sync_ctl_states[1];
		sync_buffer->vblank_syn_ch1[j] = sync_ctl_states[1];
		sync_buffer->vblank_syn_ch2[j] = sync_ctl_states[1];

		sync_buffer->vblank_ex_ch0[j] = sync_ctl_states[1]; // Not yet
		sync_buffer->vblank_ex_ch1[j] = sync_ctl_states[1];
		sync_buffer->vblank_ex_ch2[j++] = sync_ctl_states[1];
	}
	for(int i=0; i<2; i++)
	{
		// Channel 0: transmits hsync and vsync terc4 encoded with top 2 bits set
		// Channels 1 and 2: transmit guardband
		sync_buffer->hblank_ch0[j] = terc4_table[15];
		sync_buffer->hblank_ch1[j] = guardband_states[1]; //0b0100110011
		sync_buffer->hblank_ch2[j] = guardband_states[1];

		sync_buffer->vblank_en_ch0[j] = terc4_table[15];
		sync_buffer->vblank_en_ch1[j] = guardband_states[1];
		sync_buffer->vblank_en_ch2[j] = guardband_states[1];

		sync_buffer->vblank_syn_ch0[j] = terc4_table[13]; // vsync is still active, so transmit 0b1101
		sync_buffer->vblank_syn_ch1[j] = guardband_states[1];
		sync_buffer->vblank_syn_ch2[j] = guardband_states[1];

		sync_buffer->vblank_ex_ch0[j] = terc4_table[13];
		sync_buffer->vblank_ex_ch1[j] = guardband_states[1];
		sync_buffer->vblank_ex_ch2[j++] = guardband_states[1];
	}
	for(int i=0; i<H_PULSE; i++)
	{
		// Here is where the vsync pulse is allowed to transition (can't remember if it's before or after the hsync pulse though)
		// Channel 0 transmits sync signals terc4 encoded either with bit 3 set or reset, bit 2 is reset for null header
		// Bit 3 reset: 2 and 0, bit 3 set: 10 and 8 (going with bit set first)
		sync_buffer->hblank_ch0[j] = terc4_table[10]; //0b1010
		sync_buffer->hblank_ch1[j] = terc4_table[8]; // Transmit null packets
		sync_buffer->hblank_ch2[j] = terc4_table[8];

		sync_buffer->vblank_en_ch0[j] = terc4_table[8]; //0b1000
		sync_buffer->vblank_en_ch1[j] = terc4_table[8];
		sync_buffer->vblank_en_ch2[j] = terc4_table[8];

		sync_buffer->vblank_syn_ch0[j] = terc4_table[8]; //0b1000
		sync_buffer->vblank_syn_ch1[j] = terc4_table[8];
		sync_buffer->vblank_syn_ch2[j] = terc4_table[8];

		sync_buffer->vblank_ex_ch0[j] = terc4_table[10]; //0b1010
		sync_buffer->vblank_ex_ch1[j] = terc4_table[8];
		sync_buffer->vblank_ex_ch2[j++] = terc4_table[8];
	}
	for(int i=0; i<2; i++)
	{
		sync_buffer->hblank_ch0[j] = terc4_table[15];
		sync_buffer->hblank_ch1[j] = guardband_states[0]; //0b0100110011
		sync_buffer->hblank_ch2[j] = guardband_states[0];

		sync_buffer->vblank_en_ch0[j] = terc4_table[13]; //vsync is now active, so transmit 0b1101
		sync_buffer->vblank_en_ch1[j] = guardband_states[0];
		sync_buffer->vblank_en_ch2[j] = guardband_states[0];

		sync_buffer->vblank_syn_ch0[j] = terc4_table[13]; //vsync is still active
		sync_buffer->vblank_syn_ch1[j] = guardband_states[0];
		sync_buffer->vblank_syn_ch2[j] = guardband_states[0];

		sync_buffer->vblank_ex_ch0[j] = terc4_table[15]; //vsync is no longer active
		sync_buffer->vblank_ex_ch1[j] = guardband_states[0];
		sync_buffer->vblank_ex_ch2[j++] = guardband_states[0];
	}
	for(int i=0; i<video_pad; i++)
	{
		sync_buffer->hblank_ch0[j] = sync_ctl_states[3]; // Since signals are active low, during this period they're both high
		sync_buffer->hblank_ch1[j] = sync_ctl_states[0]; // Channels 1 and 2 are to be kept low
		sync_buffer->hblank_ch2[j] = sync_ctl_states[0];

		sync_buffer->vblank_en_ch0[j] = sync_ctl_states[1]; // vsync is low
		sync_buffer->vblank_en_ch1[j] = sync_ctl_states[0];
		sync_buffer->vblank_en_ch2[j] = sync_ctl_states[0];

		sync_buffer->vblank_syn_ch0[j] = sync_ctl_states[1]; // vsync is low
		sync_buffer->vblank_syn_ch1[j] = sync_ctl_states[0];
		sync_buffer->vblank_syn_ch2[j] = sync_ctl_states[0];

		sync_buffer->vblank_ex_ch0[j] = sync_ctl_states[3]; // vsync is high
		sync_buffer->vblank_ex_ch1[j] = sync_ctl_states[0];
		sync_buffer->vblank_ex_ch2[j++] = sync_ctl_states[0];
	}
	for(int i=0; i<8; i++)
	{
		sync_buffer->hblank_ch0[j] = sync_ctl_states[3];
		sync_buffer->hblank_ch1[j] = sync_ctl_states[1];
		sync_buffer->hblank_ch2[j] = sync_ctl_states[0];

		sync_buffer->vblank_en_ch0[j] = sync_ctl_states[1];
		sync_buffer->vblank_en_ch1[j] = sync_ctl_states[1];
		sync_buffer->vblank_en_ch2[j] = sync_ctl_states[0];

		sync_buffer->vblank_syn_ch0[j] = sync_ctl_states[1];
		sync_buffer->vblank_syn_ch1[j] = sync_ctl_states[1];
		sync_buffer->vblank_syn_ch2[j] = sync_ctl_states[0];

		sync_buffer->vblank_ex_ch0[j] = sync_ctl_states[3];
		sync_buffer->vblank_ex_ch1[j] = sync_ctl_states[1];
		sync_buffer->vblank_ex_ch2[j++] = sync_ctl_states[0];
	}
	for(int i=0; i<2; i++)
	{
		sync_buffer->hblank_ch0[j] = guardband_states[0]; //0b1011001100
		sync_buffer->hblank_ch1[j] = guardband_states[1]; 
		sync_buffer->hblank_ch2[j] = guardband_states[0];

		sync_buffer->vblank_en_ch0[j] = guardband_states[0];
		sync_buffer->vblank_en_ch1[j] = guardband_states[1];
		sync_buffer->vblank_en_ch2[j] = guardband_states[0];

		sync_buffer->vblank_syn_ch0[j] = guardband_states[0];
		sync_buffer->vblank_syn_ch1[j] = guardband_states[1];
		sync_buffer->vblank_syn_ch2[j] = guardband_states[0];

		sync_buffer->vblank_ex_ch0[j] = guardband_states[0];
		sync_buffer->vblank_ex_ch1[j] = guardband_states[1];
		sync_buffer->vblank_ex_ch2[j++] = guardband_states[0];
	}
}

// Creates sync buffers without the data island period.
// Basically, just 190 pixel clocks' worth of normal sync data before the video guardband.
void create_sync_buffers_nodat(struct sync_buffer_t *sync_buffer)
{

}

// little endian
// Input: 8-bit color value.
uint16_t tmds_xor(uint8_t color_data)
{
	uint16_t this_color = (uint16_t)color_data;
	uint16_t tmds_word = (this_color&0x01)<<15;
	this_color = this_color>>1;
	tmds_word = tmds_word>>1;
	for(int i=0; i<8; i++)
	{
		//shifts bit 0 of this_color to bit 15 to be XORed with the previous tmds_word bit shifted left by one
		//so it can be put back, shifted right and XORed again
		tmds_word |= (((this_color&0x01)<<15)^((tmds_word&0x4000)<<1))&0x8000;
		tmds_word = tmds_word>>1;
		this_color = this_color>>1;
	}
	tmds_word = (tmds_word>>7)&0xff; 
	tmds_word |= 0x100;
	return tmds_word;
}

uint16_t tmds_xnor(uint8_t color_data)
{
	uint16_t this_color = (uint16_t)color_data;
	uint16_t tmds_word = (this_color&0x01)<<15;
	this_color = this_color>>1;
	tmds_word = tmds_word>>1;
	for(int i=0; i<8; i++)
	{
		tmds_word |= (~(((this_color&0x01)<<15)^((tmds_word&0x4000)<<1)))&0x8000;
		tmds_word = tmds_word>>1;
		this_color = this_color>>1;
	}
	tmds_word = (tmds_word>>7)&0xff;
	return tmds_word;
}

int ones_count(uint8_t color_data)
{
	uint8_t this_color = color_data;
	int ones_cnt = 0;
	for(int i=0; i<8; i++)
	{
		if((this_color&0x01)==1) 
			ones_cnt++;
		this_color = this_color>>1;
	}
	return ones_cnt;
}

//disparity is a 4-bit signed integer converted to a 4-bit unsigned integer
//Current LUT has 2 words per entry: one for the 3 TMDS words it outputs for the same pixel, and one for the resulting disparity.
void tmds_calc_disparity(struct tmds_pixel_t *tmds_pixel)
{
	int this_disparity = tmds_pixel->disparity;
	int ones_cnt = ones_count(tmds_pixel->color_data);
	int zeros_cnt = 8-ones_cnt;
	uint16_t tmds_word = 0;
	// Is there an excess of ones or is bit 0 equal to 0 and ones_cnt is equal to 4?
	if(ones_cnt>4 || ((((tmds_pixel->color_data)&0x01)==0) && (ones_cnt==4)))
	{
		// If yes, XNOR
		tmds_word = tmds_xnor(tmds_pixel->color_data);
	}
	else
	{
		// If no, XOR
		tmds_word = tmds_xor(tmds_pixel->color_data);
	}
	
	// Is the previous disparity equal to 0 or ones equal to zeroes (4)?
	if(ones_cnt==zeros_cnt || (tmds_pixel->disparity)==0)
	{
		// If yes,
		// Bit 9 out = bit 8 in inverted,
		// Bit 8 out = bit 8 in,
		// XOR word with bit 8 state

		// Is bit 8 reset?
		if((tmds_word&0x100)!=0)
		{
			// If no, 
			// Reset bit 9
			// Add the number of ones minus number of zeroes
			this_disparity = this_disparity+(ones_cnt-zeros_cnt);
			tmds_word = tmds_word&0x1ff;
		}
		else
		{
			// If yes, 
			// Set bit 9 
			// Invert lower 8 bits
			// Add the number of zeroes minus number of ones
			tmds_word = tmds_word^0xff;
			tmds_word |= 0x200;
			this_disparity = this_disparity+(zeros_cnt-ones_cnt);
			// If the disparity is zero
			// If more ones than zeros: disparity is -1*ones_cnt (up to -8)
			// If more zeros than ones: disparity could be 8 (oops)
			// ONLY IF it's all zeros, which could be avoided by inverting the LSB
		}
	}
	else
	{
		// If no,
		// Is the previous disparity more than zero AND there are more ones than zeroes, OR
		// Is the previous disparity less than zero AND there are more zeroes than ones?
		if(((tmds_pixel->disparity)>0 && ones_cnt>4) || ((tmds_pixel->disparity)<0 && ones_cnt<4))
		{
			// If yes,
			// Set bit 9
			// Invert lower 8 bits
			// Add the number of zeros minus number of ones
			// Add 2 to disparity if bit 8 is set
			tmds_word = tmds_word&0x1ff;
			tmds_word = tmds_word^0xff;
			tmds_word |= 0x200;
			this_disparity = this_disparity+(zeros_cnt-ones_cnt);
			if((tmds_word&0x100)!=0)
			{
				this_disparity = this_disparity+2;
			}
		}
		else
		{
			// If no,
			// Reset bit 9
			// Add the number of ones minus the number of zeroes
			// Subtract 2 from disparity if bit 8 is reset
			tmds_word = tmds_word&0x1ff;
			this_disparity = this_disparity+(ones_cnt-zeros_cnt);
			if((tmds_word&0x100)==0)
			{
				this_disparity = this_disparity-2;
			}
		}
	}
	tmds_pixel->disparity = this_disparity;
	tmds_pixel->tmds_data = tmds_word;
	
	// For debugging purposes only
	/*
	if(this_disparity>15 || this_disparity<-16)
	{
		printf("Invalid disparity value: %d\n", this_disparity);
	}
	*/
	/*
	bool valid = true;
	for(int i=0; i<6; i++)
	{
		if(tmds_word==control_states[i])
		{
			valid = false;
		}
	}
	if(valid==false)
	{
		invalid_states++;
		printf("Invalid states: %d\n", invalid_states);
	}
	*/
}

// The disparity should be pre-initialized, in a loop.
// The LUT is 16*32*2 words long, or 4096 bytes.
void tmds_pixel_repeat(uint32_t *lut_buf, struct tmds_pixel_t *tmds_pixel)
{
	int dispy = tmds_pixel->disparity;
	tmds_calc_disparity(tmds_pixel);
	lut_buf[(((tmds_pixel->color_data_5b)<<1)|((((uint32_t)(dispy+8))&0x0f)<<6))&0x3fe] = (uint32_t)(tmds_pixel->tmds_data);
	tmds_calc_disparity(tmds_pixel);
	lut_buf[(((tmds_pixel->color_data_5b)<<1)|((((uint32_t)(dispy+8))&0x0f)<<6))&0x3fe] |= (uint32_t)((tmds_pixel->tmds_data)<<10);
	tmds_calc_disparity(tmds_pixel);
	lut_buf[(((tmds_pixel->color_data_5b)<<1)|((((uint32_t)(dispy+8))&0x0f)<<6))&0x3fe] |= (uint32_t)((tmds_pixel->tmds_data)<<20);
	lut_buf[((((tmds_pixel->color_data_5b)<<1)|((((uint32_t)(dispy+8))&0x0f)<<6))&0x3fe)+1] = ((uint32_t)((tmds_pixel->disparity)+8))<<6;
	
	// For debugging purposes only
	/*
	if((tmds_pixel->disparity)<-8 || (tmds_pixel->disparity)>7)
	{
		printf("Invalid final disparity value: %d\n", tmds_pixel->disparity);
	}
	*/
}

// This converts the GBC/GBA 5bpc colors into 8bpc with no color correction.
uint8_t depth_convert(uint8_t c_in)
{
	uint8_t c_out = (c_in<<3)|((c_in&0x1c)>>2);
	// Invert the LSB if 0xff or 0x00 to prevent disparity from going outside the signed 4-bit limit.
	if(c_out==0xff || c_out==0x00)
	{
		c_out = c_out^0x01;
	}
	return c_out;
}

// Creates the TMDS lookup table, where each entry has 3 separate pixels and an output disparity value.
// Addressed by disparity<<6|color_data<<1 for the TMDS data, and the entry after that is the disparity.
int main()
{
    uint32_t *tmds_lut = (uint32_t *)malloc(0x400*sizeof(uint32_t));
    struct tmds_pixel_t *tmds_pixel = (struct tmds_pixel_t *)malloc(sizeof(struct tmds_pixel_t));
    uint8_t color = 0, color_8b = 0;
    int dispy = 0;
    for(color=0; color<32; color++)
    {
    	for(dispy=-8; dispy<8; dispy++)
    	{
    		color_8b = depth_convert(color);
    		tmds_pixel->color_data_5b = color;
    		tmds_pixel->color_data = color_8b;
    		tmds_pixel->tmds_data = 0;
    		tmds_pixel->disparity = dispy;
    		tmds_pixel_repeat(tmds_lut, tmds_pixel);
    	}
    }

    FILE *pico_tmds_lut = fopen("tmds_lut.bin", "w");
    fwrite(tmds_lut, 1, 4096, pico_tmds_lut);
    fclose(pico_tmds_lut);
    free(tmds_pixel);
    free(tmds_lut);

    struct sync_buffer_t *sync_buffer = (struct sync_buffer_t *)malloc(sizeof(struct sync_buffer_t));
    create_sync_buffers(sync_buffer);

    // Have another function that interleaves all the 10 bit words from the uint16_t arrays into a uint32_t array
    // that's perfectly bit-packed, so that 48 10-bit TMDS words fit into 15 32-bit words
    
    return 0;
}