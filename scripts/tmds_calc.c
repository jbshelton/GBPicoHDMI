/*
	tmds_calc.c

	This program generates the TMDS output data/lookup tables for the Raspberry Pi Pico/RP2040.
	It also generates color correction lookup tables for "emulating" the GBC and GBA LCD colors.
	They may go unused in the final device because of timing constraints.

	TO DO:
	-Add TMDS control word LUT/generation
	-Add TMDS audio LUT generation
*/
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <unistd.h>

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

//little endian
uint16_t tmds_xor(uint8_t color_data);
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

uint16_t tmds_xnor(uint8_t color_data);
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

uint8_t bit_diff(uint16_t tmds_data)
{
	int ones_cnt = (int)ones_count(tmds_data);
	int zeros_cnt = 9-ones_cnt;
	uint8_t difference = (uint8_t)abs(ones_cnt-zeros_cnt);
	return difference;
}

uint8_t ones_count(uint16_t color_data)
{
	uint16_t this_color = color_data;
	uint8_t ones_cnt = 0;
	for(int i=0; i<16; i++)
	{
		if((this_color&0x01)==1) 
			ones_cnt++;
		this_color = this_color>>1;
	}
	return ones_cnt;
}

//disparity is a 5-bit signed integer converted to a 5-bit unsigned integer
//takes a color channel value and interleaves it into a TMDS word with disparity
uint32_t tmds_calc_disparity(uint16_t color_data, int disparity)
{
	//for the LUT, there are 2^(8+5) -> 8192 entries; 32 for each color channel value
	//since disparity is a signed 5-bit integer from -16 to 15
	int this_disparity = disparity;
	uint16_t this_color = color_data;
	uint8_t ones_cnt = ones_count(this_color);
	uint16_t tmds_word = 0;
	uint8_t difference = 0;
	if(ones_cnt>4 || (this_color&0x01)==0)
	{
		tmds_word = tmds_xnor(this_color);
	}
	if(ones_cnt<4)
	{
		tmds_word = tmds_xor(this_color);
	}
	difference = bit_diff(tmds_word);
	if(ones_cnt==4 || disparity==0)
	{
		if((tmds_word&0x100)==1)
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
		if((disparity>0 && ones_cnt>4) || (disparity<0 && ones<4))
		{
			if((tmds_word&0x100)==1)
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
			if((tmds_word&0x100)==1)
			{
				this_disparity = this_disparity+(int)difference;
			}
			else
			{
				this_disparity = (this_disparity+(int)difference)-2;
			}
		}
	}
	uint32_t out_word = tmds_interleave(tmds_word, this_disparity);
	return out_word;
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
void gba_lcd_correct(uint8_t r_in, uint8_t g_in, uint8_t b_in, uint8_t *r_out, uint8_t *g_out, uint8_t *b_out)
{
	double lcdGamma = 4.0, outGamma = 2.2;
	double lb = pow(((double)b_in / 31.0), lcdGamma);
	double lg = pow(((double)g_in / 31.0), lcdGamma);
	double lr = pow(((double)r_in / 31.0), lcdGamma);
	r_out = (uint8_t)pow((((50*lg)+(255*lr))/255), (1/outGamma))*((0xffff*255)/280);
	g_out = (uint8_t)pow((((30*lb)+(230*lg)+(10*lr))/255), (1/outGamma))*((0xffff*255)/280);
	b_out = (uint8_t)pow((((220*lb)+(10*lg)+(50*lr))/255), (1/outGamma))*((0xffff*255)/280);
}

void gbc_lcd_correct(uint8_t r_in, uint8_t g_in, uint8_t b_in, uint8_t *r_out, uint8_t *g_out, uint8_t *b_out)
{
	int R = ((int)r_in*26 + (int)g_in*4 + (int)b_in*2);
	int G = ((int)g_in*24 + (int)b_in*8);
	int B = ((int)r_in*6 + (int)g_in*4 + (int)b_in*22);
	R = min(960, R)>>2;
	G = min(960, G)>>2;
	B = min(960, B)>>2;
	r_out = (uint8_t)R;
	g_out = (uint8_t)G;
	b_out = (uint8_t)B;
}

//This converts the 5bpc colors into 8bpc with no color correction
void depth_convert(uint8_t r_in, uint8_t g_in, uint8_t b_in, uint8_t *r_out, uint8_t *g_out, uint8_t *b_out)
{
	r_out = (r_in<<3)|((r_in&0x1c)>>2);
	g_out = (g_in<<3)|((g_in&0x1c)>>2);
	b_out = (b_in<<3)|((b_in&0x1c)>>2);
}

int main(int argc, const char * argv[])
{
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
}