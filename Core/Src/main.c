/*
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  *
  * Original code by MickleT, this version modded by Ian Johnston (IanScottJohnston on YouTube),
  * for 4.58" 960x320 TFT LCD (ST7701S) and using LT7680 controller adaptor
  * Visual Studio 2022 with VisualGDB plugin:
  * To upload HEX from VS2022 = BUILD then PROGRAM AND START WITHOUT DEBUGGING
  * Use LIVE WATCH to view variables live debug
  * 
  * For use with a LT7680A-R & 320x960 TFT LCD
  * 
  * This version is for the ADCMT/ADVANTEST R6581(T) DMM unit
  *
  ******************************************************************************
*/


/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "dma.h"
#include "spi.h"
#include "gpio.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "lt7680.h"
#include "timer.h"
#include <stdbool.h>    // bool support, otherwise use _Bool
//#include <stdlib.h> // For rand()
#include "display.h"
#include "vfd.h"
#include "settings.h"
#include "lcd.h"
#include "stm32f1xx_hal.h"
#include <stdlib.h>		// required for float (soft FPU)

/* Variables ---------------------------------------------------------*/
_Bool oneVoltmode = false;
_Bool oneVoltmodepreviousState = false;


// BluePill clone determination/tests
volatile uint32_t dbg_sysclk_hz = 0;
volatile uint32_t dbg_hclk_hz = 0;
volatile uint32_t dbg_pclk1_hz = 0;
volatile uint32_t dbg_pclk2_hz = 0;
volatile uint32_t dbg_clock_source = 0;
volatile uint32_t dbg_hse_ready = 0;
volatile uint32_t dbg_pll_ready = 0;
volatile uint32_t dbg_loop_count = 0;
volatile uint32_t dbg_loop_per_sec = 0;
volatile uint32_t dbg_loop_last_ms = 0;
volatile uint32_t dbg_loop_test_done = 0;
volatile uint32_t boot_ready_ms = 0;	// HAL tick when the live loop starts (boot time, Stage 3 of docs/REWORK-PLAN.md)
volatile uint8_t boot_poweron = 0;		// 1 if this boot was a power-on reset (the panel then gets its reset timing)
volatile uint32_t boot_sleepout_ms = 0;	// ... when the panel set-up reached Sleep Out (its first half is done)
volatile uint32_t boot_lt_ms = 0;		// ... when the LT7680 set-up has finished
volatile uint32_t boot_panel_ms = 0;	// ... when the TFT panel (ST7701S) set-up has finished
volatile uint32_t first_frame_ms = 0;	// ... when the first decoded meter frame has been drawn

// BOOT_TRACE=1 (bench): for the first BOOT_TRACE_MS after reset, record every decoded frame, every draw, and PA12 on
// every tick, for reading over SWD after a power-up (build/s7/boottrace.py)
#ifndef BOOT_TRACE
#define BOOT_TRACE 0
#endif
#if BOOT_TRACE
#define BOOT_TRACE_MS 6000
typedef struct { uint16_t ms; char text[47]; uint8_t ann[3]; } BtFrame;      // G[1..47], Annunc[1..18] as bits
typedef struct { uint16_t ms; uint16_t cells; uint8_t ann; uint8_t full; } BtDraw;
volatile BtFrame bt_frame[40];
volatile BtDraw bt_draw[60];
volatile uint8_t bt_pa12[180];          // one byte per 35 ms tick: PA11 in bit 1, PA12 in bit 0
volatile uint16_t bt_pa12_ms0;          // tick time of bt_pa12[0]
volatile uint8_t bt_frames, bt_draws, bt_ticks, bt_timing_mode;
volatile uint16_t bt_timing_check_ms;
#endif


#if TIMING_PROBE
// Timing probe (read over SWD): CPU cycle counts (DWT->CYCCNT); divide by SystemCoreClock for seconds.
volatile uint32_t tp_draw_last, tp_draw_min = 0xFFFFFFFF, tp_draw_max;   // DisplaySplash..right-edge wipe
volatile uint32_t tp_total_last, tp_total_max;                           // whole timed block incl. settle + button read
volatile uint32_t tp_period_last, tp_period_min = 0xFFFFFFFF, tp_period_max;  // entry-to-entry period of the timed block
volatile uint32_t tp_blocks;                                             // number of timed blocks run
volatile uint32_t tp_main_loops;                                         // passes of the main while(1)
static uint32_t tp_prev_entry;
// Change-to-screen latency (Stage 6): from the end of the first frame that showed a change to the end of the draw.
// The stable-frame rule alone accounts for one frame (~9 ms). Microseconds.
volatile uint32_t tp_lat_last, tp_lat_min = 0xFFFFFFFF, tp_lat_max, tp_lat_sum, tp_lat_n;
static uint32_t tp_lat_from;
static uint8_t tp_lat_pending;
static void TP_Latency(uint32_t end)
{
	if (!tp_lat_pending) return;
	tp_lat_pending = 0;
	tp_lat_last = (end - tp_lat_from) / (SystemCoreClock / 1000000u);
	if (tp_lat_last < tp_lat_min) tp_lat_min = tp_lat_last;
	if (tp_lat_last > tp_lat_max) tp_lat_max = tp_lat_last;
	tp_lat_sum += tp_lat_last;
	tp_lat_n++;
}
#endif

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void RunBluePillSpeedTestOffline(void);


//************************************************************************************************************************************************************
//************************************************************************************************************************************************************

// Main
int main(void) {

	// Reset of all peripherals, Initializes the Flash interface and the Systick.
	HAL_Init();

	// Configure the system clock
	SystemClock_Config();

	// Initialize all configured peripherals (except bit-bang SPI for S7701S LCD glass)
	MX_GPIO_Init();					// I/O pins
	MX_DMA_Init();					// DMA1 Ch.2 & Ch.4
	MX_SPI1_Init();					// SPI1 - LT760A-R
	MX_SPI2_Init();					// SPI2 - VFD
	TIM2_Init();					// Initialize the timer
	Init_Completed_flag = 1;		// start capturing VFD frames now, so stable ones are waiting when the live loop starts

	// Pull CS high and SCLK low immediately after reset
	HAL_GPIO_WritePin(LCD_CS_Port, LCD_CS_Pin, GPIO_PIN_SET);			// Pull CS high
	HAL_GPIO_WritePin(LCD_SCK_Port, LCD_SCK_Pin, GPIO_PIN_RESET);		// CLK pin low

	// Read pin B0 - Set colours for MAIN & AUX
	if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_0) == GPIO_PIN_SET) {
		// B0 high
		MainColourFore = 0xFFFFFF;		// WHite
		AuxColourFore = 0xFFFF00;		// Yellow
	}
	else {
		// B0 low
		MainColourFore = 0xFFFF00;		// Yellow
		AuxColourFore = 0xFFFFFF;		// White
	}

	// Read pin B1 - Set colour for ANNUNCIATORS
	if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_1) == GPIO_PIN_SET) {
		// B1 high
		AnnunColourFore = 0x00FF00;		// Green
	}
	else {
		// B1 low
		AnnunColourFore = 0x00FFFF;		// Cyan
	}

	if (oneVoltmode) {
		HAL_GPIO_TogglePin(GPIOC, TEST_OUT_Pin); // Test LED toggle
	}
		
	// DCV held at power-up (timing-adjust mode) is decided in the main loop at TIMING_MODE_CHECK_MS, not here: this early,
	// on a cold power-up, PA12 still reads high because the meter is not yet driving its key lines.
	timingModsOnBoot = false;

	// Settings first: the panel type picks the panel set-up, and the LT7680 set-up uses the stored TFT timings.
	// (Ian's start-up set up the LT7680 before loading them, so it always started on the built-in defaults.)
	Settings_Load();
	if (strcmp(ADA_BUY, "AdaF") != 0 && strcmp(ADA_BUY, "BuyD") != 0) strcpy(ADA_BUY, "AdaF");	// default panel
	uint8_t buyDisplay = (strcmp(ADA_BUY, "BuyD") == 0);

	// Panel (ST7701S) set-up, first half, then Sleep Out. The panel then needs LCD_SLEEPOUT_MS before its next command,
	// and the LT7680 set-up runs in that time instead of after it. After a power-on reset the panel has just come out of
	// its own reset, so its first command waits LCD_POWERON_CMD_MS and Sleep Out waits LCD_POWERON_SLPOUT_MS (lcd.h);
	// the LT7680 set-up runs first instead. On a warm reset the panel stayed powered: no wait.
	uint8_t poweron = __HAL_RCC_GET_FLAG(RCC_FLAG_PORRST) != RESET;
	__HAL_RCC_CLEAR_RESET_FLAGS();				// so the next warm reset doesn't look like a power-on
	boot_poweron = poweron;
	if (poweron) while (HAL_GetTick() < LCD_POWERON_CMD_MS) { }
	if (buyDisplay) BuyDisplay_Init_Start(); else AdaFruit_Init_Start();
	if (!poweron) {
		LCD_SleepOut();
		boot_sleepout_ms = HAL_GetTick();
	}

	while (HAL_GetTick() < BOOT_SETTLE_MS) { }	// the LT7680 still gets BOOT_SETTLE_MS from reset (lt7680.h)

	// Reset the LT7680 and run subs to setup LT7680 based on Levetop info; retried once. If the controller does not
	// answer at all, blink the test LED (PC13) and keep retrying: nothing else is useful without a display.
	while (LT_Init_LT() & LT_FAULT_NOREPLY) {
		for (uint8_t i = 0; i < 6; i++) {
			HAL_GPIO_TogglePin(GPIOC, TEST_OUT_Pin);
			HAL_Delay(250);
		}
	}

	boot_lt_ms = HAL_GetTick();
#if LT_REGDUMP
	LT_RegDump();					// register readback for SWD (tools/regdump.py)
#endif
#if LT_BUS_TEST && !LT_BUS_HAL
	LT_BusTest();					// register read-backs and the burst-write experiment (Stage 5)
#endif

	// Main loop timer
	SetTimerDuration(35);			// 35 ms timed action set

	ConfigurePWMAndSetBrightness(BACKLIGHTFULL);  // Configure Timer-1 and PWM-1 for backlighting. Settable 0-100%

	if (poweron) {
		while (HAL_GetTick() < LCD_POWERON_SLPOUT_MS) { }
		LCD_SleepOut();
		boot_sleepout_ms = HAL_GetTick();
	}

	// Panel set-up, second half, once LCD_SLEEPOUT_MS have passed since Sleep Out
	while (HAL_GetTick() - boot_sleepout_ms < LCD_SLEEPOUT_MS) { }
	if (buyDisplay) BuyDisplay_Init_Finish(); else AdaFruit_Init_Finish();
	LCD_ReadID();					// panel identity for the timing-adjust page and SWD (about 0.2 ms)
	boot_panel_ms = HAL_GetTick();

	if (lt_fault) {					// LT7680 answers but part of its set-up failed: say so (now the panel is up), then carry on
		char msg[48];
		snprintf(msg, sizeof(msg), "LT7680 init fault %02X after %u tries", lt_fault, lt_init_attempts);
		DisplayFaultLine(msg);
		HAL_Delay(LT_FAULT_SHOW_MS);
		ClearScreen();
	}

//**************************************************************************************************
// Main loop initialize
	
	// Boot diagnostics: the BluePill speed test and the splash screen, one boot screen before the live display starts.
	// Only for the first BOOT_DIAG_SHOWS boots (default 0: never); holding DCV at power-up resets the count so they come back.
#if BOOT_DIAG_SHOWS
	{
		uint8_t boots = BootCountRead();
		if (boots < BOOT_DIAG_SHOWS) {
			BootCountIncrement(boots);
			boot_diag_number = boots + 1;
			RunBluePillSpeedTestOffline(); // measures, shows the boot screen for BOOT_SCREEN_MS
			ClearScreen();
		}
	}
#endif
#if RENDER_VERIFY
	ClearScreen();
	Render_ClearCheck();			// the whole page must now read black
#endif

	LoadCustomGlyphs_LT();			// ohm + sigma glyphs, loaded once (and again after DISPLAY OFF)
#if SDRAM_SOAK
	SdramSoak_Start();				// write the retention test patterns (tools/soak.py)
#endif

#if GLYPH_SELFTEST
	GlyphSelfTest();	// ~90 s of test pages, then normal operation
#endif
#if RENDER_VERIFY && !FULL_REDRAW
	Render_SelfTest(RENDER_SELFTEST_STEPS);	// synthetic screens, each checked against the full redraw
#endif
	
#if TIMING_PROBE
	CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
	DWT->CYCCNT = 0;
	DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
#endif
	boot_ready_ms = HAL_GetTick();
	uint8_t timing_mode_checked = 0;
#if !FULL_REDRAW
	uint8_t render_tick = 0;			// the 35 ms tick asks for a render (1 V mode and the periodic repaint need no new frame)
	uint32_t full_redraw_ms = HAL_GetTick();
#endif

	while (1) {

		// demo float (confirmation of Soft FP)
		//char inputString[] = "123.456";
		//test15 = atof(inputString);

		main_loops++;
#if TIMING_PROBE
		tp_main_loops++;
#endif
#if CAPTURE_LEGACY
		while (SPI1->SR & SPI_SR_BSY);					// Wait until any ongoing TFT SPI transfers are complete
		SPI1->CR1 &= ~SPI_CR1_SPE;						// Disable the TFT SPI peripheral
		DMA1_Channel3->CCR &= ~DMA_CCR_EN;				// Disable the TFT SPI DMA channels
		NVIC_DisableIRQ(SPI1_IRQn);						// Disable TFT SPI NVIC interrupts to prevent any inadvertent triggering

		Packets_to_chars(rx_buffer);	// Convert VFD packets from R6581 to characters
		Main_Aux_R6581();           // Get R6581 VFD drive data

		DMA1_Channel3->CCR |= DMA_CCR_EN;				// Re-enable the TFT DMA channel
		SPI1->CR1 |= SPI_CR1_SPE;						// Re-enable the TFT SPI peripheral by setting its SPE bit
		NVIC_EnableIRQ(SPI1_IRQn);						// Re-enable the TFT SPI NVIC interrupts
		uint8_t fresh = 0;
#else
		uint8_t fresh = Capture_Poll();	// decode a new, stable VFD frame if there is one
#if BOOT_TRACE
		if (fresh && HAL_GetTick() < BOOT_TRACE_MS && bt_frames < 40) {
			volatile BtFrame* f = &bt_frame[bt_frames++];
			f->ms = HAL_GetTick();
			for (int i = 0; i < 47; i++) f->text[i] = G[i + 1];
			for (int i = 0; i < 18; i++) if (Annunc[i + 1]) f->ann[i / 8] |= 1u << (i % 8);
		}
#endif
#if TIMING_PROBE
		if (fresh) { tp_lat_from = cap_decoded_change_cyc; tp_lat_pending = 1; }
#endif
#endif
		(void)fresh;

		task_ready = 1; // Mark tasks as complete so the timer driven code is allowed to run again

#if SDRAM_SOAK
		SdramSoak_Poll();				// read back the retention patterns when due
#endif
#if LT_REGDUMP
		if (lt_regdump_request) {		// set over SWD by tools/regdump.py --again
			LT_RegDump();
			lt_regdump_request = 0;
		}
#if !LT_BUS_HAL && LT_REG_CACHE
		{								// once a second, check the register cache against the chip
			static uint32_t last_check = 0;
			if (HAL_GetTick() - last_check >= 1000) { last_check = HAL_GetTick(); LT_CacheCheck(); }
		}
#endif
#endif

		// Timing-adjust mode: DCV held at power-up shows as PA12 high. Read it where Ian's start-up did (about 2.3 s after
		// power-on); the live display runs until then.
		if (!timing_mode_checked && HAL_GetTick() >= TIMING_MODE_CHECK_MS) {
			timing_mode_checked = 1;
#if BOOT_TRACE
			bt_timing_check_ms = HAL_GetTick();
			bt_timing_mode = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_12) == GPIO_PIN_SET;
#endif
			if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_12) == GPIO_PIN_SET) {
				timingModsOnBoot = true;
#if BOOT_DIAG_SHOWS
				BootCountReset();		// the boot screen comes back for another BOOT_DIAG_SHOWS boots (leaves the timings as loaded)
#endif
				ClearScreen();
			}
		}

		//*******************************************************************************************
		// Timed Action - Check if timer flag is set and tasks are ready and run the LCD sub
		if (timer_flag && task_ready) {
			timer_flag = 0;   // Clear the timer flag
			task_ready = 0;   // Reset task-ready flag    
			
			if (timingModsOnBoot == false) {

#if FULL_REDRAW
				draws++;
				if (!first_frame_ms && cap_decodes) first_frame_ms = HAL_GetTick();	// set as the draw starts; it takes ~7 ms
				if (memchr(&G[1], '?', CHAR_COUNT)) draw_unmatched++;
#if CAPTURE_LEGACY
				// Wait for SPI2 to finish any ongoing transfers
				while (SPI2->SR & SPI_SR_BSY);

				// Pause SPI2 capture: Disable the SPI2 peripheral
				SPI2->CR1 &= ~SPI_CR1_SPE;

				// Pause the DMA channels used by SPI2
				DMA1_Channel4->CCR &= ~DMA_CCR_EN;  // Disable SPI2 TX DMA
				DMA1_Channel5->CCR &= ~DMA_CCR_EN;  // Disable SPI2 RX DMA

				// Also disable the related NVIC interrupts for extra safety
				NVIC_DisableIRQ(SPI2_IRQn);
				NVIC_DisableIRQ(DMA1_Channel4_IRQn);
				NVIC_DisableIRQ(DMA1_Channel5_IRQn);
#endif

				HAL_GPIO_TogglePin(GPIOC, TEST_OUT_Pin); // Test LED toggle
#if TIMING_PROBE
				uint32_t tp_t0 = DWT->CYCCNT;
				if (tp_blocks) { tp_period_last = tp_t0 - tp_prev_entry; if (tp_period_last < tp_period_min) tp_period_min = tp_period_last; if (tp_period_last > tp_period_max) tp_period_max = tp_period_last; }
				tp_prev_entry = tp_t0;
#endif

				DisplaySplash();

				DisplayMain();

				DisplayAux();

				DisplayAnnunciators();

				// Right wipe required to erase some random pixels that appear
				DrawLine(0, 959, 399, 959, 0x00, 0x00, 0x00);	// far right hand vertical line, black, 1 pixel line. (this line hidden!)
				DrawLine(0, 958, 399, 958, 0x00, 0x00, 0x00);	// (this line hidden!)
				DrawLine(0, 957, 399, 957, 0x00, 0x00, 0x00);
				DrawLine(0, 956, 399, 956, 0x00, 0x00, 0x00);
				DrawLine(0, 955, 399, 955, 0x00, 0x00, 0x00);
				DrawLine(0, 954, 399, 954, 0x00, 0x00, 0x00);
				DrawLine(0, 953, 399, 953, 0x00, 0x00, 0x00);
				DrawLine(0, 952, 399, 952, 0x00, 0x00, 0x00);
#if TIMING_PROBE
				tp_draw_last = DWT->CYCCNT - tp_t0; if (tp_draw_last < tp_draw_min) tp_draw_min = tp_draw_last; if (tp_draw_last > tp_draw_max) tp_draw_max = tp_draw_last;
				TP_Latency(tp_t0 + tp_draw_last);
#endif

				// Test only - 400pixel based test lines for viewing the centre line and the left, middle and far right positions.
				// The internal memory is set up as 400x960 but the leftmost 80 pixels are considered overscan and don't show up, thus 320
				//DrawLine(0, 0, 399, 0, 0xFF, 0xFF, 0xFF);		// far left hand vertical line, black, 1 pixel line. 938 not 960 seems to be far right edge!
				//DrawLine(0, 480, 399, 480, 0xFF, 0xFF, 0xFF);	// mid-way
				//DrawLine(0, 959, 399, 959, 0xFF, 0xFF, 0xFF);	// far right
				//DrawLine(199, 0, 199, 959, 0xFF, 0x00, 0x00);	// centred on R6581T horizontally

				//HAL_Delay(6); // Allow the LT7680 sufficient processing time
				Delay_NonBlocking(6);  // Wait 6ms in a non-blocking way
#else
				DisplaySplash();		// only with SPLASH_IN_LOOP
				render_tick = 1;		// drawing happens below, after the button is read
#if BOOT_TRACE
				if (HAL_GetTick() < BOOT_TRACE_MS && bt_ticks < sizeof(bt_pa12)) {
					if (!bt_ticks) bt_pa12_ms0 = HAL_GetTick();
					bt_pa12[bt_ticks++] = (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_11) == GPIO_PIN_SET) << 1 | (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_12) == GPIO_PIN_SET);
				}
#endif
#endif

				// Read pins A11/A12 - Front panel DCV switch momentary - Enable 1VDC mode
				GPIO_PinState pinA11 = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_11);
				GPIO_PinState pinA12 = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_12);

				// 1000mVdc / 1Vdc mode select
				if (pinA11 == GPIO_PIN_SET && pinA12 == GPIO_PIN_RESET) {
					// Button is NOT pressed (normal state)
					oneVoltmodepreviousState = false;
				}
				else if (pinA11 == GPIO_PIN_RESET && pinA12 == GPIO_PIN_RESET && pinA11 == pinA12) {
					// Button is pressed (both pins are the same, and LOW)
					if (!oneVoltmodepreviousState) {
						// Toggle the mode on the first detection of the press (only if the 1V mode is built in; see ONE_VOLT_MODE in display.h)
#if ONE_VOLT_MODE
						oneVoltmode = !oneVoltmode;
#endif
					}
					// Update the previous state
					oneVoltmodepreviousState = true;
				}

				// Wait until SPI1 finishes any ongoing transactions
				while (SPI1->SR & SPI_SR_BSY);

#if TIMING_PROBE && FULL_REDRAW
				tp_total_last = DWT->CYCCNT - tp_t0; if (tp_total_last > tp_total_max) tp_total_max = tp_total_last; tp_blocks++;
#endif
#if CAPTURE_LEGACY
				// Resume SPI2 capture:
				// Re-enable DMA channels first
				DMA1_Channel4->CCR |= DMA_CCR_EN;
				DMA1_Channel5->CCR |= DMA_CCR_EN;

				// Re-enable SPI2 by setting its SPE bit
				SPI2->CR1 |= SPI_CR1_SPE;

				// Re-enable the NVIC interrupts
				NVIC_EnableIRQ(SPI2_IRQn);
				NVIC_EnableIRQ(DMA1_Channel4_IRQn);
				NVIC_EnableIRQ(DMA1_Channel5_IRQn);
#endif

			} else {
				TimingAdjust_Poll();		// timing-adjust mode (DCV held at power-up), settings.c
			}
			

		}
#if !FULL_REDRAW
		//*******************************************************************************************
		// Redraw (Stage 6): draw what changed as soon as a new frame is decoded. The 35 ms tick still reads the DCV
		// button (1 V mode changes the text without a new frame) and repaints everything every FULL_REDRAW_MS.
		if (timingModsOnBoot == false && (fresh || render_tick)) {
			uint8_t all = 0;
			render_tick = 0;
#if FULL_REDRAW_MS
			if (HAL_GetTick() - full_redraw_ms >= FULL_REDRAW_MS) { all = 1; full_redraw_ms = HAL_GetTick(); }
#endif
#if TIMING_PROBE
			uint32_t tp_t0 = DWT->CYCCNT;
#endif
#if BOOT_TRACE
			uint32_t bt_cells0 = render_cells, bt_ann0 = render_ann;
#endif
			uint8_t drew = Render_Update(all);
#if BOOT_TRACE
			if (drew && HAL_GetTick() < BOOT_TRACE_MS && bt_draws < 60) {
				volatile BtDraw* d = &bt_draw[bt_draws++];
				d->ms = HAL_GetTick(); d->cells = render_cells - bt_cells0; d->ann = render_ann - bt_ann0; d->full = all;
			}
#endif
#if TIMING_PROBE
			uint32_t tp_t1 = DWT->CYCCNT;
			tp_total_last = tp_t1 - tp_t0; if (tp_total_last > tp_total_max) tp_total_max = tp_total_last;
			if (drew) {
				tp_draw_last = tp_total_last; if (tp_draw_last < tp_draw_min) tp_draw_min = tp_draw_last; if (tp_draw_last > tp_draw_max) tp_draw_max = tp_draw_last;
				if (tp_blocks) { tp_period_last = tp_t0 - tp_prev_entry; if (tp_period_last < tp_period_min) tp_period_min = tp_period_last; if (tp_period_last > tp_period_max) tp_period_max = tp_period_last; }
				tp_prev_entry = tp_t0;
				tp_blocks++;
				TP_Latency(tp_t1);
			}
			else tp_lat_pending = 0;     // the decoded change did not alter the text
#endif
			if (drew) {
				if (!first_frame_ms && cap_decodes) first_frame_ms = HAL_GetTick();
				draws++;
				if (memchr(&G[1], '?', CHAR_COUNT)) draw_unmatched++;
			}
		}
#if RENDER_VERIFY
		{
			static uint32_t last_verify = 0;
			if (timingModsOnBoot == false && HAL_GetTick() - last_verify >= RENDER_VERIFY_MS) {
				rv_checks++;
				if (Render_Verify()) rv_mismatch++;
				last_verify = HAL_GetTick();
			}
		}
#endif
#endif

	}

}


void RunBluePillSpeedTestOffline(void)
{
	// BluePill clone determination/test - standalone loop speed test when no comms is available
	// Loops per sec is displayed on the TFT for 2secs at boot.
	// Examples: Good board = 1497606, bad board = TBA

	dbg_loop_count = 0;
	dbg_loop_per_sec = 0;
	dbg_loop_test_done = 0;

	uint32_t test_start_ms = HAL_GetTick();

	while ((HAL_GetTick() - test_start_ms) < 1000)
	{
		dbg_loop_count++;
	}

	dbg_loop_per_sec = dbg_loop_count;
	dbg_loop_test_done = 1;

	DisplayBootScreen(boot_diag_number, BOOT_DIAG_SHOWS);   // speed result + credits + timing settings + boot n/N

	HAL_Delay(BOOT_SCREEN_MS);
}


// System Clock Configuration
void SystemClock_Config(void) {
	RCC_OscInitTypeDef RCC_OscInitStruct = { 0 };
	RCC_ClkInitTypeDef RCC_ClkInitStruct = { 0 };

	//Initializes the RCC Oscillators according to the specified parameters in the RCC_OscInitTypeDef structure.
	RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
	RCC_OscInitStruct.HSEState = RCC_HSE_ON;
	RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
	RCC_OscInitStruct.HSIState = RCC_HSI_ON;
	RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
	RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
	RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
	if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
	{
		Error_Handler();
	}

	// Initializes the CPU, AHB and APB buses clocks
	RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
		| RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
	RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
	RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
	RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
	RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

	if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
	{
		Error_Handler();
	}
}


// delay
void Delay_NonBlocking(uint32_t delayMs) {
	uint32_t startTime = HAL_GetTick();
	while ((HAL_GetTick() - startTime) < delayMs) {
		// Do nothing, just wait for time to pass
	}
}


// This function is executed in case of error occurrence.
void Error_Handler(void) {
	// User can add his own implementation to report the HAL error return state
	__disable_irq();
	while (1)
	{
	}
	/* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t* file, uint32_t line)
{
	/* USER CODE BEGIN 6 */
	/* User can add his own implementation to report the file name and line number,
	   ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
	   /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */

