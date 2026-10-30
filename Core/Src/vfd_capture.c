/**
  ******************************************************************************
  * @file    vfd_capture.c
  * VFD capture: the SPI2 slave DMA buffer, the frame-complete copy made by the EXTI handler, and the
  * rule for which frames are decoded (Stage 4 of docs/REWORK-PLAN.md).
  ******************************************************************************
*/

#include "main.h"
#include "spi.h"
#include "vfd.h"
#include <string.h>


//******************************************************************************

// SPI receive buffer for packets data
volatile uint8_t rx_buffer[PACKET_WIDTH * PACKET_COUNT];

// Flag indicating finish of SPI start-up initialization
volatile uint8_t Init_Completed_flag = 0;

// ---- Capture (Stage 4 of docs/REWORK-PLAN.md) -------------------------------------------------------------------
// The EXTI handler that restarts the SPI2 DMA every 9 ms first copies the frame the DMA has just finished into
// cap_frame[] (only if all 235 bytes arrived). The main loop takes a copy with interrupts briefly off and decodes it if it
// differs from what is decoded already. So the decoder never sees a frame that DMA is half-way through (the old source of
// the rare '?'), and does nothing while the display is unchanged. A frame holding a pattern not in the font table waits to
// be seen twice (CAPTURE_CONFIRM=1: every frame does, Stage 4's first rule). CAPTURE_LEGACY=1 restores decoding rx_buffer on every main-loop pass and the capture pause around drawing.
volatile uint8_t cap_frame[FRAME_BYTES];
volatile uint32_t cap_seq = 0;          // complete frames copied by the EXTI handler
volatile uint32_t cap_short = 0;        // EXTI came before the DMA had the whole frame (frame dropped)
volatile uint32_t cap_unstable = 0;     // frames that differed from the one before (a change, or a bad frame)
volatile uint32_t cap_heldback = 0;     // frames with an unknown pattern, held back until seen twice
volatile uint32_t cap_decodes = 0;      // frames decoded
volatile uint32_t cap_unmatched = 0;    // decodes with at least one character not in the font table ('?')
volatile uint32_t draws = 0;            // redraws that drew something (FULL_REDRAW=1: every 35 ms tick)
volatile uint32_t draw_unmatched = 0;   // redraws with a '?' in G[]: what would reach the screen
volatile uint32_t cap_frame_cyc = 0;    // TIMING_PROBE: DWT cycle count when cap_frame was last filled
volatile uint32_t main_loops = 0;       // passes of the main loop (a measure of idle CPU time)
static uint8_t cap_armed = 0;           // a DMA has been started, so the next EXTI ends a real frame
#if TIMING_PROBE
static uint32_t cap_change_cyc;         // when the newest change was first seen (end of its first frame)
uint32_t cap_decoded_change_cyc;        // the same for the frame Capture_Poll() last decoded (main.c latency probe)
#endif

void Capture_FrameEnd(void)
{
	if (cap_armed && __HAL_DMA_GET_COUNTER(hspi2.hdmarx) == 0) {
		memcpy((void*)cap_frame, (const void*)rx_buffer, FRAME_BYTES);
		cap_seq++;
#if TIMING_PROBE
		cap_frame_cyc = DWT->CYCCNT;
#endif
	}
	else if (cap_armed) {
		cap_short++;
	}
	cap_armed = 1;
}

#if !CAPTURE_LEGACY
// Decode the newest captured frame if it is new (see Capture_FrameEnd and CAPTURE_CONFIRM). Returns 1 if it decoded one.
uint8_t Capture_Poll(void)
{
	static uint8_t now[FRAME_BYTES], prev[FRAME_BYTES], shown[FRAME_BYTES];
	static uint32_t seen = 0;
	static uint8_t have_prev = 0, have_shown = 0;

	if (cap_seq == seen) return 0;
	__disable_irq();
	memcpy(now, (const void*)cap_frame, FRAME_BYTES);
	seen = cap_seq;
#if TIMING_PROBE
	uint32_t at = cap_frame_cyc;
#endif
	__enable_irq();

	uint8_t repeat = have_prev && memcmp(now, prev, FRAME_BYTES) == 0;   // the same bytes as the frame before
	if (!repeat) {
		if (have_prev) cap_unstable++;
		memcpy(prev, now, FRAME_BYTES);
		have_prev = 1;
#if TIMING_PROBE
		cap_change_cyc = at;
#endif
#if CAPTURE_CONFIRM
		return 0;                                                    // Stage 4 rule: wait for the same bytes again
#endif
	}
	if (have_shown && memcmp(now, shown, FRAME_BYTES) == 0) return 0;   // already decoded
	Packets_to_chars(now);
	char text[CHAR_COUNT + 1];
	uint8_t unmatched = Decode_Text(text);
	if (unmatched && !repeat) {                                      // a pattern not in the table: maybe a character the
		cap_heldback++;                                              // meter was changing mid-pass. Show it only if the
		return 0;                                                    // next frame is the same.
	}
	memcpy(shown, now, FRAME_BYTES);
	have_shown = 1;
	Main_Aux_Commit(text, unmatched);
#if TIMING_PROBE
	cap_decoded_change_cyc = cap_change_cyc;
#endif
	return 1;
}
#endif

