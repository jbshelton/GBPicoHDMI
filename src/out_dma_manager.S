/*
	Output DMA manager
	Monitors the state of output DMA channels and updates the information of the chain DMA channels.

	These 3 chain DMA channels (channels 3, 4 and 5) have a ring size of 16 bytes (4 words) and transfer 4 words in order to reset the main output DMA channels.
	The 3 channels that chain to them (channels 0, 1 and 2) transfer 911 words before they stop and chain to the reset channels.
	Since the output resolution is at max a 3x scale of the Gameboy's resolution (720x480,) each input line is repeated three times, at which point this program switches the line buffer address the chain DMA feeds to the main DMA when it resets.
	This is because on the output, there is a double line buffer, each line being a fully encoded TMDS line. This is necessary so that the CPU has enough time to construct a whole TMDS line, which involves converting pixels from the double frame buffer to TMDS format, repeating them, and adding sync words at the end of the line. In the future, if audio over HDMI is added, the CPU also needs enough time to grab encoded audio from the audio buffer and put that into the signal.
	The resolution is 912 by 539 pixels, including the blanking areas.

	DMA channel 0 DREQ: DREQ_PIO0_TX0 (0)
	DMA channel 1 DREQ: DREQ_PIO0_TX1 (1)
	DMA channel 2 DREQ: DREQ_PIO0_TX2 (2)

	DMA base address: 0x50000000
	All DMAs are set to IRQ_QUIET, which I think sets bit 21 of CHx_CTRL_TRIG.
	DATA_SIZE is always a full word, so the value is 0x2.

	Offset - Name - Value
	0x000 CH0_READ_ADDR = Channel 0 TMDS buffer
	0x004 CH0_WRITE_ADDR = PIO0_RX0
	0x008 CH0_TRANS_CNT = 912
	0x00C CH0_CTRL_TRIG
	TREQ_SEL = DREQ_PIO0_TX0 (0b000000)
	CHAIN_TO = Channel 3 (0b0011)
	INCR_WRITE = 0
	INCR_READ = 1

	0x040 CH1_READ_ADDR = Channel 1 TMDS buffer
	0x044 CH1_WRITE_ADDR = PIO_RX1
	0x048 CH1_TRANS_CNT = 912
	0x04C CH1_CTRL_TRIG
	TREQ_SEL = DREQ_PIO0_TX1 (0b000001)
	CHAIN_TO = Channel 4 (0b0100)
	INCR_WRITE = 0
	INCR_READ = 1

	0x080 CH2_READ_ADDR = Channel 2 TMDS buffer
	0x084 CH2_WRITE_ADDR = PIO_RX2
	0x088 CH2_TRANS_CNT = 912
	0x08C CH2_CTRL_TRIG
	TREQ_SEL = DREQ_PIO0_TX2 (0b000010)
	CHAIN_TO = Channel 5 (0b0101)
	INCR_WRITE = 0
	INCR_READ = 1

	Not sure if I'm going to use these:

	0x0C0 CH3_READ_ADDR
	0x0C4 CH3_WRITE_ADDR = Channel 0 base (0x000)
	0x0C8 CH3_TRANS_CNT = 4
	0x0CC CH3_CTRL_TRIG
	TREQ_SEL = 0x3f (unpaced transfer)
	CHAIN_TO = Channel 0 (0b0000)
	RING_SEL = 1
	RING_SIZE = 16 bytes (0x4)
	INCR_WRITE = 1
	INCR_READ = 1

	0x100 CH4_READ_ADDR
	0x104 CH4_WRITE_ADDR = Channel 1 base (0x040)
	0x108 CH4_TRANS_CNT = 4
	0x10C CH4_CTRL_TRIG
	TREQ_SEL = 0x3f (unpaced transfer)
	CHAIN_TO = Channel 1 (0b0001)
	RING_SEL = 1
	RING_SIZE = 16 bytes (0x4)
	INCR_WRITE = 1
	INCR_READ = 1

	0x140 CH5_READ_ADDR
	0x144 CH5_WRITE_ADDR = Channel 2 base (0x080)
	0x148 CH5_TRANS_CNT = 4
	0x14C CH5_CTRL_TRIG
	TREQ_SEL = 0x3f (unpaced transfer)
	CHAIN_TO = Channel 2 (0b0010)
	RING_SEL = 1
	RING_SIZE = 16 bytes (0x4)
	INCR_WRITE = 1
	INCR_READ = 1

*/
