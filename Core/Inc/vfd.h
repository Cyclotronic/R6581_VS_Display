/**
  ******************************************************************************
  * @file    vfd.h
  * @brief   VFD capture (vfd_capture.c) and decode (vfd_decode.c)
  ******************************************************************************
*/

#ifndef VFD_H
#define VFD_H

#include <stdint.h>
#include "main.h"

// ---- Capture (vfd_capture.c), Stage 4 of docs/REWORK-PLAN.md ----------------------------------------------------
// CAPTURE_LEGACY=1 restores decoding rx_buffer on every main-loop pass and the capture pause around drawing.
#ifndef CAPTURE_LEGACY
#define CAPTURE_LEGACY 0
#endif
#define FRAME_BYTES (PACKET_WIDTH * PACKET_COUNT)
// Each captured frame is one whole pass of the meter's display, so it is decoded at once. Only a frame with a pattern not
// in the font table waits to be seen twice. CAPTURE_CONFIRM=1 restores Stage 4's rule (every frame seen twice), which
// freezes the display when the reading changes every pass (0.1 PLC and faster).
#ifndef CAPTURE_CONFIRM
#define CAPTURE_CONFIRM 0
#endif
extern volatile uint8_t rx_buffer[FRAME_BYTES];   // SPI2 DMA target (restarted by the EXTI handler every frame)
extern volatile uint8_t Init_Completed_flag;      // set once start-up is done: the EXTI handler may run the capture
extern volatile uint32_t cap_seq, cap_short, cap_unstable, cap_heldback, cap_decodes, cap_unmatched;
extern volatile uint32_t draws, draw_unmatched, main_loops;
#if TIMING_PROBE
extern uint32_t cap_decoded_change_cyc;           // DWT count when the change Capture_Poll() last decoded was first seen
#endif
void Capture_FrameEnd(void);                      // EXTI handler: keep the frame the DMA just finished
uint8_t Capture_Poll(void);                       // main loop: decode a new stable frame; returns 1 if it did

// ---- Decode (vfd_decode.c) ----------------------------------------------------------------------------------------
extern char G[48];                                // G[1..18] main line, G[19..47] aux line
extern _Bool Annunc[19];                          // Annunc[1..18], left to right
void Packets_to_chars(const volatile uint8_t* buf);
void Main_Aux_R6581(void);
uint8_t Decode_Text(char* text);                  // chars[] to text[1..47]; returns the count of unknown patterns
void Main_Aux_Commit(const char* text, uint8_t unmatched);

#endif // VFD_H
