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
		out_word |= ((reg_word&0x01)|(inv_word&0x02)<<20);
		reg_word = reg_word>>1;
		inv_word = inv_word>>1;
		out_word = out_word>>2;
	}
	out_word |= unsd<<24;
	//format = nnnDDDDDNPNPNPNPNPNPNPNPNPNP
	//n = not used
	//D = disparity
	//N = negative TMDS data
	//P = positive TMDS data
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

int main(int argc, const char * argv[])
{
    uint32_t tmds_lut[256][32];
    for(uint16_t i=0; i<256; i++)
    {
    	for(int j=-16; j<16; j++)
    	{
    		tmds_lut[i][j+16] = tmds_calc_disparity(i, j);
    	}
    }
}