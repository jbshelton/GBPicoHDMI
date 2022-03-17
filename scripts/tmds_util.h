/*
	tmds_util.h

	Various definitions/declarations of values, structs, and function prototypes for tmds_util.c to make things less messy.
*/
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

struct sync_buffer_32t
{
	uint32_t *hblank_ch0;
	uint32_t *hblank_ch1;
	uint32_t *hblank_ch2;

	uint32_t *vblank_en_ch0;
	uint32_t *vblank_en_ch1;
	uint32_t *vblank_en_ch2;

	uint32_t *vblank_syn_ch0;
	uint32_t *vblank_syn_ch1;
	uint32_t *vblank_syn_ch2;

	uint32_t *vblank_ex_ch0;
	uint32_t *vblank_ex_ch1;
	uint32_t *vblank_ex_ch2;
};

// Function header prototypes
void free_sync_buffers(struct sync_buffer_t *sync_buffer);
void free_sync_buffers(struct sync_buffer_32t *sync_buffer);
void allocate_sync_buffer(uint16_t *buffer);
void allocate_sync_buffer(uint32_t *buffer);
void create_sync_buffers(struct sync_buffer_t *sync_buffer);
void create_sync_buffers_nodat(struct sync_buffer_t *sync_buffer);

void pack_buffer_single(uint16_t *in_buffer, uint32_t *out_buffer)
struct sync_buffer_32t *pack_sync_buffers(struct sync_buffer_t *sync_buffer);

uint16_t tmds_xor(uint8_t color_data);
uint16_t tmds_xnor(uint8_t color_data);
int ones_count(uint8_t color_data);
void tmds_calc_disparity(struct tmds_pixel_t *tmds_pixel);
void tmds_pixel_repeat(uint32_t *lut_buf, struct tmds_pixel_t *tmds_pixel);

uint8_t depth_convert(uint8_t c_in);
