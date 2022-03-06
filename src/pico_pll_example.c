/*
	An example I made for the RPi Pico PLL.
	Non-functional, but just to provide some context.
*/

#include <stdio.h>
#include "pico/stdlib.h"

int main()
{
	stdio_init_all();

	uint32_t sys_clock_khz = 294000;

	uint vco_freq_out, post_div1_out, post_div2_out;

	bool clock_valid = check_sys_clock_khz(sys_clock_khz, &vco_freq_out, &post_div1_out, &post_div2_out);
	
	if(clock_valid==false)
	{
		printf("Oh no! This clock doesn't work.\n");
		exit(1);
	}
	else
	{
		printf("Hurray! Setting VCO to %d, div1 to %d, and div2 to %d\n", vco_freq_out, post_div1_out, post_div2_out);
		
		set_sys_clock_pll((uint32_t)vco_freq_out, post_div1_out, post_div2_out);
	}
	while(1)
	{
		do_nothing();
	}
}