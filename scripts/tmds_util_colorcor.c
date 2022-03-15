/*
	
	OLD, OUTDATED PROGRAM
	ONLY KEPT FOR POTENTIAL FUTURE REFERENCE/ARCHIVING PURPOSES

	tmds_util.c

	This program generates the TMDS output data/lookup tables for the Raspberry Pi Pico/RP2040.
	And various other utilities.
	It also generates color correction lookup tables for "emulating" the GBC and GBA LCD colors.
	They may go unused in the final device because of timing constraints.

	TO DO:
	-Add TMDS control word LUT/generation
	-Add TMDS audio LUT generation

	Traditionally:
	-Separate color channels from shared word into different words (1 pixel only)
	-Get results individually
	-Convert into TMDS individually and put into different buffers one at a time

	Variables needed per channel: raw color data, last TMDS data, TMDS data buffer address
	Universal variables needed: raw pixel data, framebuffer address
	Total predicted registers used for video encoding: 11
	pix = fbuffer[fbaddr++];

	raw_r = pix&0xff, pix>>=8;
	raw_r |= (last_r&0x1f000000)>>16; //2 instructions?
	last_r = tmds_lut[raw_r];
	rbuffer[raddr++] = last_r; //2 instructions?

	raw_g = pix&0xff, pix>>=8;
	raw_g |= (last_g&0x1f000000)>>16;
	last_g = tmds_lut[raw_g];
	gbuffer[gaddr++] = last_g;

	raw_b = pix;
	raw_b |= (last_b&0x1f000000)>>16;
	last_b = tmds_lut[raw_b];
	bbuffer[baddr++] = last_b;

	6 instructions per channel = 18 instructions per pixel

	Or do this (assuming 24bpp input):
	pix = fbuffer[fbaddr++]; //This is done for however wide the framebuffer is horizontally, then the line buffer is used
	raw_cc = pix&0xff, pix>>=8;
	lbuffer[lbaddr++] = pix; //This is repeated for horizontal width and then reset
	raw_cc |= (cc_tmds&0x1f000000)>>16; //cc_tmds is init'd to zero
	cc_tmds = tmds_lut[raw_cc];
	tlbuffer[tladdr++] = cc_tmds;

	On the other 2 color channels:
	lbaddr = 0;
	loop point:
	pix = lbuffer[lbaddr++];
	raw_cc = pix&0xff, pix>>=8;
	lbuffer[lbaddr++] = pix;
	raw_cc |= (cc_tmds&0x1f000000)>>16;
	cc_tmds = tmds_lut[raw_cc];
	tlbuffer[tladdr++] = cc_tmds;
*/
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <unistd.h>

struct tmds_pixel_t
{
	uint8_t color_data;
	uint16_t tmds_data;
	int disparity;
};

// THIS IS OUTDATED! THE PIOS CAN PERFORM THIS ON THE FLY, SO THIS IS NOT NECESSARY!!!
uint32_t tmds_interleave(uint16_t tmds_data, int disparity)
{
	uint8_t unsd = ((uint8_t)(disparity+16))&0x1f;
	uint32_t reg_word = (uint32_t)tmds_data;
	uint32_t inv_word = (~reg_word)<<1;
	uint32_t out_word = 0;
	for(int i=0; i<10; i++)
	{
		out_word |= (((reg_word&0x01)|(inv_word&0x02))<<20);
		reg_word = reg_word>>1;
		inv_word = inv_word>>1;
		out_word = out_word>>2;
	}
	out_word |= unsd<<24;
	//format = nnnDDDDD-nnnnNPNP-NPNPNPNP-NPNPNPNP (little endian)
	//n = not used
	//D = disparity
	//N = negative TMDS data
	//P = positive TMDS data
	//Disparity is stored in the top byte.
}

// little endian
// Input: 5-bit color value, right aligned.
uint16_t tmds_xor(uint8_t color_data)
{
	uint16_t this_color = (uint16_t)color_data;
	uint16_t tmds_word = (this_color&0x01)<<14;
	this_color = this_color>>1;
	for(int i=0; i<8; i++)
	{
		//shifts bit 0 of this_color to bit 14 to be XORed with tmds_word
		//so it can be put back, shifted right and XORed again
		tmds_word |= ((((this_color&0x01)<<14)^(tmds_word&0x4000))<<1)&0x8000;
		tmds_word = tmds_word>>1;
		this_color = this_color>>1;
	}
	tmds_word = tmds_word>>6; //this is probably 6 since how I did the shifts it occupies the upper 10 bits instead of 9
	tmds_word |= 0x100;
	return tmds_word;
}

uint16_t tmds_xnor(uint8_t color_data)
{
	uint16_t this_color = (uint16_t)color_data;
	uint16_t tmds_word = (this_color&0x01)<<14;
	this_color = this_color>>1;
	for(int i=0; i<8; i++)
	{
		//shifts bit 0 of this_color to bit 14 to be XNORed with tmds_word
		//so it can be put back, shifted right and XNORed again
		tmds_word |= (~(((this_color&0x01)<<14)^(tmds_word&0x4000))<<1)&0x8000;
		tmds_word = tmds_word>>1;
		this_color = this_color>>1;
	}
	tmds_word = tmds_word>>6;
	return tmds_word;
}

// Return the signed value of the difference between 1s and 0s.
// Positive = more 1s, negative = more 0s.
/*
int bit_diff_s(uint16_t tmds_data)
{
	int ones_cnt = (int)ones_count(tmds_data);
	int zeros_cnt = (int)zeros_count(tmds_data);
	int difference = ones_cnt-zeros_cnt;
	return difference;
}
*/

uint8_t ones_count(uint16_t color_data)
{
	uint16_t this_color = color_data;
	uint8_t ones_cnt = 0;
	for(int i=0; i<9; i++)
	{
		if((this_color&0x01)==1) 
			ones_cnt++;
		this_color = this_color>>1;
	}
	return ones_cnt;
}

uint8_t bit_diff(uint16_t tmds_data)
{
	int ones_cnt = (int)ones_count(tmds_data);
	int zeros_cnt = 9-ones_cnt;
	uint8_t difference = (uint8_t)abs(ones_cnt-zeros_cnt);
	return difference;
}

//disparity is a 5-bit signed integer converted to a 5-bit unsigned integer
//takes a color channel value and interleaves it into a TMDS word with disparity
//If the signed value is 0, then assume the output is how much disparity to add for that color value.
//Which means that disparity multiplied by 3 is the result of encoding a pixel.
//If it isn't 0, then the added disparity is the difference between the old and new values.
//Current LUT has 2 words per entry: one for the 3 TMDS words it outputs for the same pixel, and one for the resulting disparity.
void tmds_calc_disparity(struct tmds_pixel_t *tmds_pixel)
{
	//for the LUT, there are 2^(8+5) -> 8192 entries; 32 for each color channel value
	//since disparity is a signed 5-bit integer from -16 to 15
	int this_disparity = tmds_pixel->disparity;
	uint8_t ones_cnt = ones_count(tmds_pixel->color_data);
	uint16_t tmds_word = 0;
	uint8_t difference = 0;
	// Is there an excess of ones or is bit 0 equal to 0? If yes, then XNOR
	if(ones_cnt>4 || !((tmds_pixel->color_data)&0x01))
	{
		tmds_word = tmds_xnor(tmds_pixel->color_data);
	}
	// Is there an excess of zeroes? If yes, then XOR
	if(ones_cnt<4)
	{
		tmds_word = tmds_xor(tmds_pixel->color_data);
	}
	difference = bit_diff(tmds_word);
	if(ones_cnt==4 || !(tmds_pixel->disparity))
	{
		if(tmds_word&0x100)
		{
			this_disparity = this_disparity+(int)difference;
		}
		else
		{
			tmds_word = (~tmds_word)&0x3ff;
			this_disparity = this_disparity-(int)difference;
		}
	}
	else
	{
		if(((tmds_pixel->disparity)>0 && ones_cnt>4) || ((tmds_pixel->disparity)<0 && ones_cnt<4))
		{
			if(tmds_word&0x100)
			{
				tmds_word = ((~tmds_word)&0x2ff)|(tmds_word&0x100);
				this_disparity = (this_disparity-(int)difference)+2;
			}
			else
			{
				tmds_word = ((~tmds_word)&0x2ff);
				this_disparity = this_disparity-(int)difference;
			}
		}
		else
		{
			if(tmds_word&0x100)
			{
				this_disparity = this_disparity+(int)difference;
			}
			else
			{
				this_disparity = (this_disparity+(int)difference)-2;
			}
		}
	}
	tmds_pixel->disparity = this_disparity;
	tmds_pixel->tmds_data = tmds_word;
}

// The disparity should be pre-initialized, in a loop.
// The LUT is 32*32*2 words long, or 8192 bytes.
void tmds_pixel_repeat(uint32_t *lut_buf, struct tmds_pixel_t *tmds_pixel)
{
	tmds_calc_disparity(tmds_pixel);
	lut_buf[((tmds_pixel->color_data)<<1)|(((tmds_pixel->disparity)+16)<<6)] = tmds_pixel->tmds_data;
	tmds_calc_disparity(tmds_pixel);
	lut_buf[((tmds_pixel->color_data)<<1)|(((tmds_pixel->disparity)+16)<<6)] |= (tmds_pixel->tmds_data)<<10;
	tmds_calc_disparity(tmds_pixel);
	lut_buf[((tmds_pixel->color_data)<<1)|(((tmds_pixel->disparity)+16)<<6)] |= (tmds_pixel->tmds_data)<<20;
	lut_buf[(((tmds_pixel->color_data)<<1)|(((tmds_pixel->disparity)+16)<<6))+1] = (tmds_pixel->disparity)+16;
}

//Color correction algorithms to use for generating the LUT: https://near.sh/articles/video/color-emulation
//Input colors are (I assume) 0 to 31
//Output gamma is 2.2
//LCD gamma is 4.0
//1/gamma is ~0.455
//The end multiplied value is ~59683.661
//Simplified:
//-Colors are divided by 31 and raised to the power of 4
//-Color bleed is calculated(?)
//-Value is divided by 255
//-That value then gets something a bit less than its square root calculated
//-Then finally gets multiplied by an end value

//This may require a whole lookup table for color correction
//Which means 32768 * 32-bit words = 131072 bytes per LUT
//Or 3 * 32 * 1024 bytes for all channels, 98304 bytes total
//Which the RP2040 doesn't have if a single buffer is used.
/*
void gba_lcd_correct(uint8_t r_in, uint8_t g_in, uint8_t b_in, uint8_t *r_out, uint8_t *g_out, uint8_t *b_out)
{
	double lcdGamma = 4.0, outGamma = 2.2;
	double lb = pow(((double)b_in / 31.0), lcdGamma);
	double lg = pow(((double)g_in / 31.0), lcdGamma);
	double lr = pow(((double)r_in / 31.0), lcdGamma);
	*r_out = (uint8_t)pow((((50*lg)+(255*lr))/255), (1/outGamma))*((0xffff*255)/280);
	*g_out = (uint8_t)pow((((30*lb)+(230*lg)+(10*lr))/255), (1/outGamma))*((0xffff*255)/280);
	*b_out = (uint8_t)pow((((220*lb)+(10*lg)+(50*lr))/255), (1/outGamma))*((0xffff*255)/280);
}

void gbc_lcd_correct(uint8_t r_in, uint8_t g_in, uint8_t b_in, uint8_t *r_out, uint8_t *g_out, uint8_t *b_out)
{
	int R = ((int)r_in*26 + (int)g_in*4 + (int)b_in*2);
	int G = ((int)g_in*24 + (int)b_in*8);
	int B = ((int)r_in*6 + (int)g_in*4 + (int)b_in*22);
	R = min(960, R)>>2;
	G = min(960, G)>>2;
	B = min(960, B)>>2;
	*r_out = (uint8_t)R;
	*g_out = (uint8_t)G;
	*b_out = (uint8_t)B;
}
*/

// This converts the GBC/GBA 5bpc colors into 8bpc with no color correction.
uint8_t depth_convert(uint8_t c_in)
{
	uint8_t c_out = (c_in<<3)|((c_in&0x1c)>>2);
	return c_out;
}

int main()
{
    /*
    uint8_t color_lut_gba[3][32][1024]; //this should probably be malloc
    uint8_t color_lut_gbc[3][32][1024];
    uint16_t lut_addr_r, lut_addr_g, lut_addr_b;
    uint8_t r_a, g_a, b_a, r_c, g_c, b_c;
    for(uint8_t r=0; r<32; r++)
    {
    	for(uint8_t g=0; g<32; g++)
    	{
    		for(uint8_t b=0; b<32; b++)
    		{
    			gba_lcd_correct(r, g, b, &r_a, &g_a, &b_a);
    			gbc_lcd_correct(r, g, b, &r_c, &g_c, &b_c);
    			lut_addr_r = g_a<<5|b_a;
    			lut_addr_g = b_a<<5|r_a;
    			lut_addr b = r_a<<5|g_a;
    			color_lut_gba[0][r][lut_addr_r] = r_a;
    			color_lut_gba[1][g][lut_addr_g] = g_a;
    			color_lut_gba[2][b][lut_addr_b] = b_a;
    			lut_addr_r = g_c<<5|b_c;
    			lut_addr_g = b_c<<5|r_c;
    			lut_addr b = r_c<<5|g_c;
    			color_lut_gbc[0][r][lut_addr_r] = r_c;
    			color_lut_gbc[1][g][lut_addr_g] = g_c;
    			color_lut_gbc[2][b][lut_addr_b] = b_c;
    		}
    	}
    }
    //Since the color correction could be applied, there needs to be 32*256 entries of 32-bit words
    uint32_t tmds_lut[32][256];
    for(uint16_t i=0; i<256; i++)
    {
    	for(int j=-16; j<16; j++)
    	{
    		tmds_lut[j+16][i] = tmds_calc_disparity(i, j);
    	}
    }
	*/
	//Try to find out which combinations of disparities and data lead to it zeroing out
	//with 2 of the same TMDS words and one different word.
    uint32_t tmds_word = 0;
    int disp_0 = 0, disp_1 = 0;
    uint16_t c_value = 0;
    for(uint16_t i=0; i<32; i++)
    {
    	c_value = (uint16_t)depth_convert((uint8_t)i);
    	tmds_word = tmds_calc_disparity(c_value, 0);
    	disp_0 = (((int)((tmds_word&0x1f000000)>>24))-16)*2;
    	tmds_word = tmds_calc_disparity(c_value, disp_0);
    	disp_1 = ((int)((tmds_word&0x1f000000)>>24))-16;
    	printf("Disparity for value %2x: %2d initial, %2d final\n", c_value, disp_0, disp_1);
    }

    return 0;
}