/*
	tmds_util.c

	This program generates the TMDS output data/lookup tables for the Raspberry Pi Pico/RP2040.
	And various other utilities.

	TO DO:
	-Add TMDS audio LUT generation (if necessary)

	The HDMI InfoFrame buffers will still have the standard sync data tacked onto them,
	but will just transmit the relevant information during the hsync pulse so channel 0
	is ORed with whatever current bit of the header is shifted left 2 bits, and channels 1
	and 2 will carry the little-endian TERC4 encoded data.
	So before the unpacked buffer is freed after it is copied to the packed buffer, inject
	the data into it and pack it into a different buffer. (Though encoding the sync and header
	bits in TERC4 will have to be done manually.)
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <unistd.h>
#include "tmds_util.h"

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

// OR these with the InfoFrame header bits.
// 0 = during vsync, 1 = during active video (in the hblank interval, during the hsync pulse)
const uint8_t sync_masks[] = 
{
	0b00001000,
	0b00001010
};

// Creates the TMDS lookup table, where each entry has 3 separate pixels and an output disparity value (stored in 2 separate words.)
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

    FILE *pico_tmds_lut = fopen("tmds_lut.bin", "wb");
    fwrite(tmds_lut, 4, 1024, pico_tmds_lut);
    fclose(pico_tmds_lut);
    free(tmds_pixel);
    free(tmds_lut);
    // These functions create the sync buffers with the null packets and with no packets.
    // They do everything automatically, including packing the data and writing it to files.
    create_sync_buffers();
    create_sync_buffers_nodat();
    // Now create the AVI (video) InfoFrame.
    // Creates both hsync and during vsync variants.

    create_avi_infoframe(); // Also writes them to files and frees the structs.
    // Create a solid line that can be used to get a solid color on the screen.
    // Black, white, red, green, blue, magenta, cyan, or yellow can be made with different combinations.
    // The create_solid_line() function also writes it to a file.
    struct tmds_pixel_t *solid_pixel = (struct tmds_pixel_t *)malloc(sizeof(struct tmds_pixel_t));
    solid_pixel->color_data_5b = 0x00;
    char *pixel_name = (char *)malloc(32);
    sprintf(pixel_name, "pixel_0x00.bin");
    create_solid_line(pixel_name, solid_pixel);
    solid_pixel->color_data_5b = 0x1f;
    sprintf(pixel_name, "pixel_0xff.bin");
    create_solid_line(pixel_name, solid_pixel);
    free(pixel_name);
    free(solid_pixel);
    
    return 0;
}

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

	free(sync_buffer);
}

void free_sync_buffers_32(struct sync_buffer_32_t *sync_buffer)
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

	free(sync_buffer);
}

void allocate_sync_buffer(uint16_t **buffer)
{
	*buffer = (uint16_t *)malloc((H_TOTAL-H_ACTIVE)*sizeof(uint16_t));

	return;
}

void allocate_sync_buffer_32(uint32_t **buffer)
{
	*buffer = (uint32_t *)malloc((((H_TOTAL-H_ACTIVE)*10)/32)*sizeof(uint32_t)); // Whoops! Initially forgot to put the multiplication factor there.

	return;
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
void create_sync_buffers()
{
	struct sync_buffer_t *sync_buffer = (struct sync_buffer_t *)malloc(sizeof(struct sync_buffer_t));

	allocate_sync_buffer(&(sync_buffer->hblank_ch0));
	allocate_sync_buffer(&(sync_buffer->hblank_ch1));
	allocate_sync_buffer(&(sync_buffer->hblank_ch2));

	allocate_sync_buffer(&(sync_buffer->vblank_en_ch0));
	allocate_sync_buffer(&(sync_buffer->vblank_en_ch1));
	allocate_sync_buffer(&(sync_buffer->vblank_en_ch2));

	allocate_sync_buffer(&(sync_buffer->vblank_syn_ch0));
	allocate_sync_buffer(&(sync_buffer->vblank_syn_ch1));
	allocate_sync_buffer(&(sync_buffer->vblank_syn_ch2));

	allocate_sync_buffer(&(sync_buffer->vblank_ex_ch0));
	allocate_sync_buffer(&(sync_buffer->vblank_ex_ch1));
	allocate_sync_buffer(&(sync_buffer->vblank_ex_ch2));
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
		sync_buffer->vblank_en_ch1[j] = sync_ctl_states[0]; // Program throws a segmentation fault here because it accesses the wrong address, wtf???
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
	char buffer_name[] = "nm";
	create_sync_files(buffer_name, sync_buffer);

	return;
}

// Creates sync buffers without the data island period.
// Basically, just 190 pixel clocks' worth of normal sync data before the video guardband.
// No data island preamble, no data island guardbands, just the video preamble and guard band.
void create_sync_buffers_nodat()
{
	struct sync_buffer_t *sync_buffer = (struct sync_buffer_t *)malloc(sizeof(struct sync_buffer_t));

	allocate_sync_buffer(&(sync_buffer->hblank_ch0));
	allocate_sync_buffer(&(sync_buffer->hblank_ch1));
	allocate_sync_buffer(&(sync_buffer->hblank_ch2));

	allocate_sync_buffer(&(sync_buffer->vblank_en_ch0));
	allocate_sync_buffer(&(sync_buffer->vblank_en_ch1));
	allocate_sync_buffer(&(sync_buffer->vblank_en_ch2));

	allocate_sync_buffer(&(sync_buffer->vblank_syn_ch0));
	allocate_sync_buffer(&(sync_buffer->vblank_syn_ch1));
	allocate_sync_buffer(&(sync_buffer->vblank_syn_ch2));

	allocate_sync_buffer(&(sync_buffer->vblank_ex_ch0));
	allocate_sync_buffer(&(sync_buffer->vblank_ex_ch1));
	allocate_sync_buffer(&(sync_buffer->vblank_ex_ch2));

	int video_pad = (H_TOTAL - H_ACTIVE) - 10; // Subtract the 10 pixels used for video preamble and guardband
	int sync_pad = ((H_TOTAL - H_ACTIVE) - H_BACK) - H_PULSE;
	int j = 0;
	for(int i=0; i<sync_pad; i++)
	{
		sync_buffer->hblank_ch0[j] = sync_ctl_states[3];
		sync_buffer->hblank_ch1[j] = sync_ctl_states[0];
		sync_buffer->hblank_ch2[j] = sync_ctl_states[0];

		sync_buffer->vblank_en_ch0[j] = sync_ctl_states[3];
		sync_buffer->vblank_en_ch1[j] = sync_ctl_states[0];
		sync_buffer->vblank_en_ch2[j] = sync_ctl_states[0];

		sync_buffer->vblank_syn_ch0[j] = sync_ctl_states[1];
		sync_buffer->vblank_syn_ch1[j] = sync_ctl_states[0];
		sync_buffer->vblank_syn_ch2[j] = sync_ctl_states[0];

		sync_buffer->vblank_ex_ch0[j] = sync_ctl_states[1];
		sync_buffer->vblank_ex_ch1[j] = sync_ctl_states[0];
		sync_buffer->vblank_ex_ch2[j++] = sync_ctl_states[0];
	}
	for(int i=0; i<H_PULSE; i++)
	{
		sync_buffer->hblank_ch0[j] = sync_ctl_states[2]; 
		sync_buffer->hblank_ch1[j] = sync_ctl_states[0];
		sync_buffer->hblank_ch2[j] = sync_ctl_states[0];

		sync_buffer->vblank_en_ch0[j] = sync_ctl_states[0];
		sync_buffer->vblank_en_ch1[j] = sync_ctl_states[0];
		sync_buffer->vblank_en_ch2[j] = sync_ctl_states[0];

		sync_buffer->vblank_syn_ch0[j] = sync_ctl_states[0]; 
		sync_buffer->vblank_syn_ch1[j] = sync_ctl_states[0];
		sync_buffer->vblank_syn_ch2[j] = sync_ctl_states[0];

		sync_buffer->vblank_ex_ch0[j] = sync_ctl_states[2];
		sync_buffer->vblank_ex_ch1[j] = sync_ctl_states[0];
		sync_buffer->vblank_ex_ch2[j++] = sync_ctl_states[0];
	}
	// Only these last two remain the same
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
		sync_buffer->hblank_ch0[j] = guardband_states[0];
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

	char buffer_name[] = "nd";
	create_sync_files(buffer_name, sync_buffer);

	return;
}

// Packs a single channel into a buffer. Used to reduce copy and pasting.
// Takes pointers to a uint16_t input buffer and uint32_t output buffer.
// Buffer size is in multiples of 16 pixels.
void pack_buffer_single(uint16_t *in_buffer, uint32_t *out_buffer, int buffer_size)
{
	int in_pos = 0, out_pos = 0;
	uint32_t temp_word = 0;
	for(int i=0; i<buffer_size; i++)
	{
		temp_word = ((uint32_t)(in_buffer[in_pos++]));
		temp_word |= ((uint32_t)(in_buffer[in_pos++]))<<10;
		temp_word |= ((uint32_t)(in_buffer[in_pos++]))<<20;
		temp_word |= (((uint32_t)(in_buffer[in_pos]))&0x03)<<30;
		out_buffer[out_pos++] = temp_word;
		// Next word has bottom 2 bits cut off
		temp_word = ((uint32_t)(in_buffer[in_pos++]))>>2;
		temp_word |= ((uint32_t)(in_buffer[in_pos++]))<<8;
		temp_word |= ((uint32_t)(in_buffer[in_pos++]))<<18;
		temp_word |= (((uint32_t)(in_buffer[in_pos]))&0x0f)<<28;
		out_buffer[out_pos++] = temp_word;
		// Next word has bottom 4 bits cut off
		temp_word = ((uint32_t)(in_buffer[in_pos++]))>>4;
		temp_word |= ((uint32_t)(in_buffer[in_pos++]))<<6;
		temp_word |= ((uint32_t)(in_buffer[in_pos++]))<<16;
		temp_word |= (((uint32_t)(in_buffer[in_pos]))&0x3f)<<26;
		out_buffer[out_pos++] = temp_word;
		// Next word has bottom 6 bits cut off
		temp_word = ((uint32_t)(in_buffer[in_pos++]))>>6;
		temp_word |= ((uint32_t)(in_buffer[in_pos++]))<<4;
		temp_word |= ((uint32_t)(in_buffer[in_pos++]))<<14;
		temp_word |= (((uint32_t)(in_buffer[in_pos]))&0xff)<<24;
		out_buffer[out_pos++] = temp_word;
		// Next word has bottom 8 bits cut off- this allows the 30 other bits to be filled in
		temp_word = ((uint32_t)(in_buffer[in_pos++]))>>8;
		temp_word |= ((uint32_t)(in_buffer[in_pos++]))<<2;
		temp_word |= ((uint32_t)(in_buffer[in_pos++]))<<12;
		temp_word |= (((uint32_t)(in_buffer[in_pos]))&0x3ff)<<22;
		out_buffer[out_pos++] = temp_word;
	}
	return;
}

// Creates the files for the hblank stuff.
// Copying and pasting is the bane of my existance but at the moment I don't know a better way to do this.
// Also packs the data from the sync buffers. 16 10-bit TMDS words fit into 5 32-bit words.
// There are 192 TMDS words per buffer channel, so they would fit it ((192/16)=12)*5 = 60 32-bit words.
// All variations take up a total of 2880 bytes in RAM.
void create_sync_files(char *name, struct sync_buffer_t *sync_buffer)
{
	struct sync_buffer_32_t *pack_buffer = (struct sync_buffer_32_t *)malloc(sizeof(struct sync_buffer_32_t));

	allocate_sync_buffer_32(&(pack_buffer->hblank_ch0));
	allocate_sync_buffer_32(&(pack_buffer->hblank_ch1));
	allocate_sync_buffer_32(&(pack_buffer->hblank_ch2));

	allocate_sync_buffer_32(&(pack_buffer->vblank_en_ch0));
	allocate_sync_buffer_32(&(pack_buffer->vblank_en_ch1));
	allocate_sync_buffer_32(&(pack_buffer->vblank_en_ch2));

	allocate_sync_buffer_32(&(pack_buffer->vblank_syn_ch0));
	allocate_sync_buffer_32(&(pack_buffer->vblank_syn_ch1));
	allocate_sync_buffer_32(&(pack_buffer->vblank_syn_ch2));

	allocate_sync_buffer_32(&(pack_buffer->vblank_ex_ch0));
	allocate_sync_buffer_32(&(pack_buffer->vblank_ex_ch1));
	allocate_sync_buffer_32(&(pack_buffer->vblank_ex_ch2));

	// 16 TMDS words fit into 5 32-bit words. There are 192 pixels during hblank in total, so the buffers are 60 words each.
	pack_buffer_single(sync_buffer->hblank_ch0, pack_buffer->hblank_ch0, 12);
	pack_buffer_single(sync_buffer->hblank_ch1, pack_buffer->hblank_ch1, 12);
	pack_buffer_single(sync_buffer->hblank_ch1, pack_buffer->hblank_ch2, 12);

	pack_buffer_single(sync_buffer->vblank_en_ch0, pack_buffer->vblank_en_ch0, 12);
	pack_buffer_single(sync_buffer->vblank_en_ch1, pack_buffer->vblank_en_ch1, 12);
	pack_buffer_single(sync_buffer->vblank_en_ch2, pack_buffer->vblank_en_ch2, 12);

	pack_buffer_single(sync_buffer->vblank_syn_ch0, pack_buffer->vblank_syn_ch0, 12);
	pack_buffer_single(sync_buffer->vblank_syn_ch1, pack_buffer->vblank_syn_ch1, 12);
	pack_buffer_single(sync_buffer->vblank_syn_ch2, pack_buffer->vblank_syn_ch2, 12);

	pack_buffer_single(sync_buffer->vblank_ex_ch0, pack_buffer->vblank_ex_ch0, 12);
	pack_buffer_single(sync_buffer->vblank_ex_ch1, pack_buffer->vblank_ex_ch1, 12);
	pack_buffer_single(sync_buffer->vblank_ex_ch2, pack_buffer->vblank_ex_ch2, 12);

    free_sync_buffers(sync_buffer); // Frees the struct too. Works properly.

    char file_name[32];
	
	sprintf(file_name, "hblank_ch0_%s.bin", name);
	FILE *hblank_ch0 = fopen(file_name, "wb");
	fwrite(pack_buffer->hblank_ch0, 4, 60, hblank_ch0);
	fclose(hblank_ch0);

	sprintf(file_name, "hblank_ch1_%s.bin", name);
	FILE *hblank_ch1 = fopen(file_name, "wb");
	fwrite(pack_buffer->hblank_ch1, 4, 60, hblank_ch1);
	fclose(hblank_ch1);

	sprintf(file_name, "hblank_ch2_%s.bin", name);
	FILE *hblank_ch2 = fopen(file_name, "wb");
	fwrite(pack_buffer->hblank_ch2, 4, 60, hblank_ch2);
	fclose(hblank_ch2);
	// Enter
	sprintf(file_name, "vblank_en_ch0_%s.bin", name);
	FILE *vblank_en_ch0 = fopen(file_name, "wb");
	fwrite(pack_buffer->vblank_en_ch0, 4, 60, vblank_en_ch0);
	fclose(vblank_en_ch0);

	sprintf(file_name, "vblank_en_ch1_%s.bin", name);
	FILE *vblank_en_ch1 = fopen(file_name, "wb");
	fwrite(pack_buffer->vblank_en_ch1, 4, 60, vblank_en_ch1);
	fclose(vblank_en_ch1);

	sprintf(file_name, "vblank_en_ch2_%s.bin", name);
	FILE *vblank_en_ch2 = fopen(file_name, "wb");
	fwrite(pack_buffer->vblank_en_ch2, 4, 60, vblank_en_ch2);
	fclose(vblank_en_ch2);
	// Sync
	sprintf(file_name, "vblank_syn_ch0_%s.bin", name);
	FILE *vblank_syn_ch0 = fopen(file_name, "wb");
	fwrite(pack_buffer->vblank_syn_ch0, 4, 60, vblank_syn_ch0);
	fclose(vblank_syn_ch0);

	sprintf(file_name, "vblank_syn_ch1_%s.bin", name);
	FILE *vblank_syn_ch1 = fopen(file_name, "wb");
	fwrite(pack_buffer->vblank_syn_ch1, 4, 60, vblank_syn_ch1);
	fclose(vblank_syn_ch1);

	sprintf(file_name, "vblank_syn_ch2_%s.bin", name);
	FILE *vblank_syn_ch2 = fopen(file_name, "wb");
	fwrite(pack_buffer->vblank_syn_ch2, 4, 60, vblank_syn_ch2);
	fclose(vblank_syn_ch2);
	// Exit
	sprintf(file_name, "vblank_ex_ch0_%s.bin", name);
	FILE *vblank_ex_ch0 = fopen(file_name, "wb");
	fwrite(pack_buffer->vblank_ex_ch0, 4, 60, vblank_ex_ch0);
	fclose(vblank_ex_ch0);

	sprintf(file_name, "vblank_ex_ch1_%s.bin", name);
	FILE *vblank_ex_ch1 = fopen(file_name, "wb");
	fwrite(pack_buffer->vblank_ex_ch1, 4, 60, vblank_ex_ch1);
	fclose(vblank_ex_ch1);

	sprintf(file_name, "vblank_ex_ch2_%s.bin", name);
	FILE *vblank_ex_ch2 = fopen(file_name, "wb");
	fwrite(pack_buffer->vblank_ex_ch2, 4, 60, vblank_ex_ch2);
	fclose(vblank_ex_ch2);

	free_sync_buffers_32(pack_buffer);
	// Program never gets to this point because only the first array of the sync_buffer_32_t pack_buffer can be freed (hblank_ch0)
	return;
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

	return;
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

	return;
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

void create_avi_infoframe()
{
	struct infoframe_header_t *packet_header = (struct infoframe_header_t *)malloc(sizeof(struct infoframe_header_t));
	struct infoframe_header_t *packet_header_v = (struct infoframe_header_t *)malloc(sizeof(struct infoframe_header_t));
    struct infoframe_packet_t *info_packet = (struct infoframe_packet_t *)malloc(sizeof(struct infoframe_packet_t));

	packet_header->terc4_r_header = (uint16_t *)malloc(32*sizeof(uint16_t));
	packet_header->terc4_en_header = (uint32_t *)malloc(10*sizeof(uint32_t));
	packet_header_v->terc4_r_header = (uint16_t *)malloc(32*sizeof(uint16_t));
	packet_header_v->terc4_en_header = (uint32_t *)malloc(10*sizeof(uint32_t));

	info_packet->terc4_r_ch1 = (uint16_t *)malloc(32*sizeof(uint16_t));
	info_packet->terc4_en_ch1 = (uint32_t *)malloc(10*sizeof(uint32_t));
	info_packet->terc4_r_ch2 = (uint16_t *)malloc(32*sizeof(uint16_t));
	info_packet->terc4_en_ch2 = (uint32_t *)malloc(10*sizeof(uint32_t));
	info_packet->packet_data  = (uint8_t *)malloc(31);

	packet_header->packet_type = AVI_PACKET_TYPE;
	packet_header->version = HDMI_VERSION;
	packet_header->packet_length = AVI_PACKET_LENGTH;
	packet_header->header_checksum = AVI_HEADER_CHECKSUM;

	packet_header_v->packet_type = AVI_PACKET_TYPE;
	packet_header_v->version = HDMI_VERSION;
	packet_header_v->packet_length = AVI_PACKET_LENGTH;
	packet_header_v->header_checksum = AVI_HEADER_CHECKSUM;

	info_packet->packet_checksum = 0x02; // VIC
	for(int i=0; i<31; i++)
	{
		info_packet->packet_data[i] = 0;
	}
	info_packet->packet_data[3] = 0x02;

	uint8_t header_byte = packet_header->packet_type;
	int j = 0;
	for(int i=0; i<8; i++)
	{
		packet_header->terc4_r_header[j] = terc4_table[((header_byte&0x01)<<2)|sync_masks[1]];
		packet_header_v->terc4_r_header[j++] = terc4_table[((header_byte&0x01)<<2)|sync_masks[0]];
		header_byte = header_byte>>1;
	}
	header_byte = packet_header->version;
	for(int i=0; i<8; i++)
	{
		packet_header->terc4_r_header[j] = terc4_table[((header_byte&0x01)<<2)|sync_masks[1]];
		packet_header_v->terc4_r_header[j++] = terc4_table[((header_byte&0x01)<<2)|sync_masks[0]];
		header_byte = header_byte>>1;
	}
	header_byte = packet_header->packet_length;
	for(int i=0; i<8; i++)
	{
		packet_header->terc4_r_header[j] = terc4_table[((header_byte&0x01)<<2)|sync_masks[1]];
		packet_header_v->terc4_r_header[j++] = terc4_table[((header_byte&0x01)<<2)|sync_masks[0]];
		header_byte = header_byte>>1;
	}
	header_byte = packet_header->header_checksum;
	for(int i=0; i<8; i++)
	{
		packet_header->terc4_r_header[j] = terc4_table[((header_byte&0x01)<<2)|sync_masks[1]];
		packet_header_v->terc4_r_header[j++] = terc4_table[((header_byte&0x01)<<2)|sync_masks[0]];
		header_byte = header_byte>>1;
	}

	info_packet->terc4_r_ch1[0] = terc4_table[((info_packet->packet_checksum)&0x0f)];
	info_packet->terc4_r_ch2[0] = terc4_table[((info_packet->packet_checksum)&0xf0)>>4];
	for(int i=1; i<32; i++)
	{
		info_packet->terc4_r_ch1[i] = terc4_table[((info_packet->packet_data[i-1])&0x0f)];
		info_packet->terc4_r_ch2[i] = terc4_table[((info_packet->packet_data[i-1])&0xf0)>>4];
	}

	pack_buffer_single(packet_header->terc4_r_header, packet_header->terc4_en_header, 2);
	pack_buffer_single(packet_header_v->terc4_r_header, packet_header_v->terc4_en_header, 2);
	pack_buffer_single(info_packet->terc4_r_ch1, info_packet->terc4_en_ch1, 2);
	pack_buffer_single(info_packet->terc4_r_ch2, info_packet->terc4_en_ch2, 2);

	FILE *terc4_header = fopen("terc4_hblank_ch0.bin", "wb");
	fwrite(packet_header->terc4_en_header, 4, 10, terc4_header);
	fclose(terc4_header);

	FILE *terc4_header_v = fopen("terc4_vsync_ch0.bin", "wb");
	fwrite(packet_header_v->terc4_en_header, 4, 10, terc4_header_v);
	fclose(terc4_header_v);

	FILE *terc4_ch1 = fopen("terc4_blank_ch1.bin", "wb");
	fwrite(info_packet->terc4_en_ch1, 4, 10, terc4_ch1);
	fclose(terc4_ch1);

	FILE *terc4_ch2 = fopen("terc4_blank_ch2.bin", "wb");
	fwrite(info_packet->terc4_en_ch2, 4, 10, terc4_ch2);
	fclose(terc4_ch2);

	free(packet_header->terc4_r_header);
	free(packet_header->terc4_en_header);
	free(packet_header);

	free(packet_header_v->terc4_r_header);
	free(packet_header_v->terc4_en_header);
	free(packet_header_v);

	free(info_packet->terc4_r_ch1);
	free(info_packet->terc4_en_ch1);
	free(info_packet->terc4_r_ch2);
	free(info_packet->terc4_en_ch2);
	free(info_packet->packet_data);
	free(info_packet);

	return;
}

// Requires at least the 5 bit color of the tmds pixel to be initialized.
// Creates a file with the data inside it.
void create_solid_line(char *name, struct tmds_pixel_t *pixel)
{
	uint16_t *tmds_r_line = (uint16_t *)malloc(720*sizeof(uint16_t));
	uint32_t *tmds_en_line = (uint32_t *)malloc(225*sizeof(uint32_t));
	pixel->color_data = depth_convert(pixel->color_data_5b);
	for(int i=0; i<720; i++)
	{
		tmds_calc_disparity(pixel);
		tmds_r_line[i] = pixel->tmds_data;
	}
	pack_buffer_single(tmds_r_line, tmds_en_line, 45);
	free(tmds_r_line);

	FILE *tmds_line = fopen(name, "wb");
	fwrite(tmds_en_line, 4, 225, tmds_line);
	fclose(tmds_line);
	free(tmds_en_line);

	return;
}
