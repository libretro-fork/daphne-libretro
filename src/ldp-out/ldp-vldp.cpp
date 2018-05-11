/*
* ldp-vldp.cpp
*
* Copyright (C) 2001 Matt Ownby
*
* This file is part of DAPHNE, a laserdisc arcade game emulator
*
* DAPHNE is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* DAPHNE is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

// ldp-vldp.c
// by Matt Ownby

// pretends to be an LDP, but uses the VLDP library for output
// (for people who have no laserdisc player)

#ifdef WIN32
#define _CRT_SECURE_NO_WARNINGS 1
#define _CRT_NONSTDC_NO_DEPRECATE 1
#pragma warning (disable:4100)	// disable the warning about unreferenced formal parameters (MSVC++)
#pragma warning (disable:4786) // disable warning about truncating to 255 in debug info
#define strcasecmp stricmp
#endif


#ifdef DEBUG
#include <assert.h>
#endif
// 
#include <stdlib.h>
#include <time.h>
#include <set>
#include "../io/conout.h"
#include "../io/error.h"
#include "../video/video.h"
#include "../timer/timer.h"
#include "../daphne.h"	// for get_quitflag, set_quitflag
#include "../io/homedir.h"
#include "../io/input.h"
#include "../io/fileparse.h"
#include "../io/mpo_mem.h"
#include "../io/numstr.h"	// for debug
#include "../io/network.h"	// to query amount of RAM the system has (get_sys_mem)
#include "../game/game.h"
#include "../video/rgb2yuv.h"
#include "ldp-vldp.h"
#include "framemod.h"
#include "ldp-vldp-gl.h"	// for OpenGL callbacks
#include "ldp-vldp-gp2x.h"	// for GP2X callbacks (if no gp2x, this is harmless)
#include "../vldp2/vldp/vldp.h"	// to get the vldp structs
#include "../video/palette.h"
#include "../video/SDL_DrawText.h"
#include "../video/blend.h"

// 2017.02.01 - RJS - Logging and libretro needs.
#include "../../main_android.h"
#include "../../libretro/libretro_daphne.h"
#include "../../libretro/libretro.h"
#include "SDL_surface.h"

extern "C" {
	extern DECLSPEC SDL_SW_YUVTexture * SDLCALL SDL_RJS_SW_CreateYUVBuffer(Uint32 format, Uint32 target_format, int w, int h);
	extern DECLSPEC void SDLCALL SDL_SW_DestroyYUVTexture(SDL_SW_YUVTexture * swdata);
}

#define API_VERSION 11

static const unsigned int FREQ1000 = AUDIO_FREQ * 1000;	// let compiler compute this ...

// video overlay stuff
Sint32 g_vertical_offset = 0;	// (used a lot, we only want to calc it once)

double g_dPercentComplete01 = 0.0;	// send by child thread to indicate how far our parse is complete
bool g_bGotParseUpdate = false;	// if true, it means we've received a parse update from VLDP
bool g_take_screenshot = false;	// if true, a screenshot will be taken at the next available opportunity
unsigned int g_vertical_stretch = 0;
unsigned int g_filter_type = FILTER_NONE;	// what type of filter to use on our data (if any)

// these are globals because they are used by our callback functions
static SDL_Rect g_no_clip_rect = { 0, 0, 0, 0 };
SDL_Rect *g_screen_clip_rect = &g_no_clip_rect;	// used a lot, we only want to calculate once

// 2017.02.14 - RJS - changed from apk version using textures to surfaces for RA
// 2017.06.22 - RJS - changed to using SW textures directly
// 2017.10.13 - RJS - changed to a array of textures so we can render one, have one waiting, build one, and an extra one.
// APK SDL_Texture *g_hw_overlay = NULL;
// SURFACE SDL_Surface *g_hw_overlay = NULL;
// ALPHA SDL_SW_YUVTexture * g_hw_overlay = NULL;
#define VIDEO_BUFFER_AMOUNT 4
typedef enum
{
	VB_STATE_USEABLE,
	VB_STATE_FILLING,
	VB_STATE_WAITING,
	VB_STATE_RENDERING,
	VB_STATE_RENDERING_DONE,
	VB_STATE_AMOUNT,
	VB_STATE_NOTHING	= -1
} VIDEO_BUFFER_STATE;

typedef struct
{
	VIDEO_BUFFER_STATE	buffer_state;
	SDL_SW_YUVTexture *	video_buffer;
} VIDEO_BUFFER;

// We're not going to use a usable queue since we'll just crawl the queues looking for a STATE_USEABLE.  Ordered
// usable queues aren't necessary.
// int g_vb_usable_top = 0;
// int g_vb_usable_queue[VIDEO_BUFFER_AMOUNT] = { 0, 1, 2, 3 };

int g_vb_filling_queue = -1;

int g_vb_waiting_top	= -1;
int g_vb_waiting_next	= 0;
int g_vb_waiting_queue[VIDEO_BUFFER_AMOUNT] = { -1, -1, -1, -1 };

// int g_vb_rendering_queue = -1;

VIDEO_BUFFER g_hw_overlay[VIDEO_BUFFER_AMOUNT] = { { VB_STATE_USEABLE, NULL }, { VB_STATE_USEABLE, NULL }, { VB_STATE_USEABLE, NULL }, { VB_STATE_USEABLE, NULL } };

// FOR_DEBUG #define LOGVBSYS(STRIN) ((void)__android_log_print(ANDROID_LOG_INFO, "RA...CoreVB", "fb: %d\t\tw_top: %d\tw_next: %d\tw_q: [0]:%d\t[1]:%d\t[2]:%d\t[3]:%d\t Buffer: [0]:%d\t[1]:%d\t[2]:%d\t[3]:%d  "STRIN, g_vb_filling_queue, g_vb_waiting_top, g_vb_waiting_next, g_vb_waiting_queue[0], g_vb_waiting_queue[1], g_vb_waiting_queue[2], g_vb_waiting_queue[3], g_hw_overlay[0].buffer_state, g_hw_overlay[1].buffer_state, g_hw_overlay[2].buffer_state, g_hw_overlay[3].buffer_state))
#define LOGVBSYS(STRIN)


// ********************************************************************************************************************************
// ********************************************************************************************************************************
bool initialize_vb(Uint32 format, Uint32 target_format, int w, int h)
{
	LOGVBSYS("INITIALIZATION, top.");

	for (int i = 0; i < VIDEO_BUFFER_AMOUNT; i++)
	{
		g_hw_overlay[i].buffer_state = VB_STATE_USEABLE;
	#ifdef __ANDROID__
		if (g_hw_overlay[i].video_buffer != NULL)	LOGI("daphne-libretro: In initialize_vb, BUFFER being allocated twice, LEAK!");
	#endif
		g_hw_overlay[i].video_buffer = SDL_RJS_SW_CreateYUVBuffer(format, target_format, w, h);
		if (g_hw_overlay[i].video_buffer == NULL) return false;

		// g_vb_usable_queue[i]	= i;
		g_vb_waiting_queue[i]	= -1;
	}

	// g_vb_usable_top			= 0;
	g_vb_filling_queue		= -1;
	g_vb_waiting_top		= -1;
	// g_vb_rendering_queue	= -1;
	g_vb_waiting_next		= 0;

	LOGVBSYS("INITIALIZATION, bottom.");
	return true;
}

// ********************************************************************************************************************************
// ********************************************************************************************************************************
void teardown_vb()
{
	LOGVBSYS("TEARDOWN, top.");

	for (int i = 0; i < VIDEO_BUFFER_AMOUNT; i++)
	{
		g_hw_overlay[i].buffer_state = VB_STATE_USEABLE;
		if (g_hw_overlay[i].video_buffer != NULL) SDL_SW_DestroyYUVTexture(g_hw_overlay[i].video_buffer);
		g_hw_overlay[i].video_buffer = NULL;

		// g_vb_usable_queue[i] = i;
		g_vb_waiting_queue[i] = -1;
	}

	// g_vb_usable_top = 0;
	g_vb_filling_queue		= -1;
	g_vb_waiting_top		= -1;
	// g_vb_rendering_queue	= -1;
	g_vb_waiting_next		= 0;

	LOGVBSYS("TEARDOWN, bottom.");
}

// ********************************************************************************************************************************
// ********************************************************************************************************************************
SDL_SW_YUVTexture * get_vb_next_usable(int * vb_ndx)
{
	LOGVBSYS("getUSABLE, top, before RECLAIM.");

	// Reclaim already rendered buffers.
	for (int i = 0; i < VIDEO_BUFFER_AMOUNT; i++)
	{
		if (g_hw_overlay[i].buffer_state == VB_STATE_RENDERING_DONE)
		{
			g_hw_overlay[i].buffer_state = VB_STATE_USEABLE;
		}
	}

	LOGVBSYS("getUSABLE, after RECLAIM.");

	// Crawl all buffers looking for a STATE_USABLE buffer.
	int vb_next;
	for (vb_next = 0; vb_next < VIDEO_BUFFER_AMOUNT; vb_next++)
	{
		if (g_hw_overlay[vb_next].buffer_state == VB_STATE_USEABLE) break;
	}

	// We could consider a stall and wait for a usable video buffer as one should be in the 
	// middle of rendering.  Though a better solution might be to allow more buffers.
	if ((vb_next >= VIDEO_BUFFER_AMOUNT) &&
		(g_vb_waiting_top == g_vb_waiting_next))
	{
		g_vb_waiting_next--;
		if (g_vb_waiting_next < 0) g_vb_waiting_next = VIDEO_BUFFER_AMOUNT - 1;
		vb_next = g_vb_waiting_next;
		g_hw_overlay[(g_vb_waiting_queue[vb_next])].buffer_state = VB_STATE_USEABLE;
		LOGVBSYS("getUSABLE, NO USABLE FOUND, eating most recent from WAIT_Q!");
	}

	// Check for and deal with no more usable buffers.
	if (vb_next >= VIDEO_BUFFER_AMOUNT)
	{
		// This means we have no usable buffers.  Debug why, consider making VIDEO_BUFFER_AMOUNT larger, or
		// frame skip making the g_first_waiting USABLE and the next WAITING the g_first_waiting.  In the
		// initial implimentation were just going to log so possible errors can be attacked.
		LOGVBSYS("getUSABLE, NO USEABLE BUFFERS, exiting with NULL!");
		if (vb_ndx != NULL) *vb_ndx = -1;
		return NULL;
	}

	// We are going to transition this found usable buffer to a buffer being STATE_FILLING.

	// Make sure and deal with a buffer already being filled which should never happen.
	if (g_vb_filling_queue >= 0)
	{
		// This means we are filling two buffers which should never happen.  In the
		// initial implimentation were just going to log so possible errors can be attacked.
		LOGVBSYS("getUSABLE, FILLING TWO+ buffers, exiting with NULL!");
		if (vb_ndx != NULL) *vb_ndx = -1;
		return NULL;
	}

	// Change state to filling and add to the single entry filling queue.
	if (vb_ndx != NULL) *vb_ndx = vb_next;
	g_vb_filling_queue = vb_next;
	g_hw_overlay[vb_next].buffer_state = VB_STATE_FILLING;
	LOGVBSYS("getUSABLE, bottom, found USABLE, moved to FILLING.");
	return(g_hw_overlay[vb_next].video_buffer);
}

// ********************************************************************************************************************************
// ********************************************************************************************************************************
SDL_SW_YUVTexture * get_vb_filling(int * vb_ndx)
{
	LOGVBSYS("getFILLING, top.");

	// Intialize returned ndx.
	if (vb_ndx != NULL) *vb_ndx = -1;

	// Make sure we have a filling queue and that it's tagged as such.
	if (g_vb_filling_queue < 0)
	{
		LOGVBSYS("getFILLING, nothing in FILLING_Q, exiting NULL!");
		return NULL;
	}
	if (g_hw_overlay[g_vb_filling_queue].buffer_state != VB_STATE_FILLING)
	{
		LOGVBSYS("getFILLING, FILLING buffer wrong state, exiting NULL!");
		return NULL;
	}

	// Return a good queue.
	if (vb_ndx != NULL) *vb_ndx = g_vb_filling_queue;
	LOGVBSYS("getFILLING, bottom.");
	return(g_hw_overlay[g_vb_filling_queue].video_buffer);
}

// ********************************************************************************************************************************
// ********************************************************************************************************************************
void set_vb_filling_done(int vb_ndx)
{
	LOGVBSYS("setFILLING_DONE, top.");

	// Check the input ndx and make sure it's a filling queue.
	if (vb_ndx >= VIDEO_BUFFER_AMOUNT)
	{
		LOGVBSYS("setFILLING_DONE, input ndx wrong, exiting!");
		return;
	}
	if (g_hw_overlay[vb_ndx].buffer_state != VB_STATE_FILLING)
	{
		LOGVBSYS("setFILLING_DONE, FILLING buffer wrong state, exiting!");
		return;
	}

	// Clear the filling queue.
	g_vb_filling_queue = -1;
	LOGVBSYS("setFILLING_DONE, CLEARING FILLING_Q.");

	// Move from filling queue to waiting queue.

	// Handle situation where the next pointer and top pointer are the same.  This denotes a
	// filled waiting buffer.  We'll try to reclaim the most recent buffer since we don't
	// want to do an atomic by reclaiming the least recent buffer as that should be being
	// picked up by the presentation thread.
	if (g_vb_waiting_top == g_vb_waiting_next)
	{
		LOGVBSYS("setFILLING_DONE, WAITING_Q FILLED, need buffer, eating most recent!");
		g_vb_waiting_next--;
		if (g_vb_waiting_next < 0) g_vb_waiting_next = VIDEO_BUFFER_AMOUNT - 1;
	}

	// Calculate next waiting queue position.
	int vb_next_waiting_ndx = g_vb_waiting_next + 1;
	if (vb_next_waiting_ndx >= VIDEO_BUFFER_AMOUNT) vb_next_waiting_ndx = 0;
	if (vb_next_waiting_ndx == g_vb_waiting_top)
	{
		LOGVBSYS("setFILLING_DONE, WAITING_Q now FILLED!");
	}

	// While we are letting the presentation thread handle g_vb_waiting_top, on first entry, we are not.
	if (g_vb_waiting_top < 0) g_vb_waiting_top = g_vb_waiting_next;

	// Put filled buffer into waiting list.
	g_hw_overlay[vb_ndx].buffer_state = VB_STATE_WAITING;
	g_vb_waiting_queue[g_vb_waiting_next] = vb_ndx;
	g_vb_waiting_next = vb_next_waiting_ndx;

	LOGVBSYS("setFILLING_DONE, bottom.");
}

// ********************************************************************************************************************************
// ********************************************************************************************************************************
SDL_SW_YUVTexture * get_vb_waiting(int * vb_ndx)
{
	LOGVBSYS("getWAITING, top.");

	// Make sure soemthing is waiting.
	if (vb_ndx != NULL) *vb_ndx = -1;
	if (g_vb_waiting_top < 0)
	{
		LOGVBSYS("getWAITING, nothing is WAITING, exiting NULL!");
		return NULL;
	}

	int vb_rendering_ndx = g_vb_waiting_top;
	int vb_new_top = g_vb_waiting_top + 1;
	if (vb_new_top >= VIDEO_BUFFER_AMOUNT) vb_new_top = 0;

	// Check if there is another buffer waiting.  If not don't increment to next buffer.  We'll
	// try to keep this one.
	if ((g_vb_waiting_queue[vb_new_top] >= 0) &&
		(g_hw_overlay[(g_vb_waiting_queue[vb_new_top])].buffer_state == VB_STATE_WAITING))
	{
		g_vb_waiting_top = vb_new_top;
		LOGVBSYS("getWAITING, new top WAITING buffer.");
	} else {
		g_vb_waiting_top	= -1;
		g_vb_waiting_next	= 0;
		LOGVBSYS("getWAITING, NO new top WAITING buffer.");
	}
	
	g_hw_overlay[(g_vb_waiting_queue[vb_rendering_ndx])].buffer_state = VB_STATE_RENDERING;
	
	SDL_SW_YUVTexture * vb_rendering = g_hw_overlay[(g_vb_waiting_queue[vb_rendering_ndx])].video_buffer;
	if (vb_ndx != NULL) *vb_ndx = vb_rendering_ndx;

	LOGVBSYS("getWAITING, bottom, valid RENDERING buffer found.");
	return(vb_rendering);
}

// ********************************************************************************************************************************
// ********************************************************************************************************************************
void set_vb_rendering_done(int vb_ndx)
{
	LOGVBSYS("setRENDERING_DONE, top.");

	if (vb_ndx < 0) return;

	int buffer_ndx = g_vb_waiting_queue[vb_ndx];
	if (buffer_ndx < 0)
	{
		LOGVBSYS("setRENDERING_DONE, given rendering buffer is INVALID!");
		return;
	}

	if (g_hw_overlay[buffer_ndx].buffer_state != VB_STATE_RENDERING)
	{
		LOGVBSYS("setRENDERING_DONE, given buffer is not in RENDERING state!");
		return;
	}

	g_hw_overlay[buffer_ndx].buffer_state = VB_STATE_RENDERING_DONE;
	LOGVBSYS("setRENDERING_DONE, bottom, buffer set to DONE.");

	// 2017.11.06 - RJS - This shouldn't be necessary, but make for easier debugging.
	g_vb_waiting_queue[vb_ndx] = -1;
}



struct yuv_buf g_blank_yuv_buf;	// this will contain a blank YUV overlay suitable for search/seek blanking
Uint8 *g_line_buf = NULL;	// temp sys RAM for doing calculations so we can do fastest copies to slow video RAM
Uint8 *g_line_buf2 = NULL;	// 2nd buf
Uint8 *g_line_buf3 = NULL;	// 3rd buf

////////////////////////////////////////

// 2 pixels of black in YUY2 format (different for big and little endian)
#if SDL_BYTEORDER == SDL_LIL_ENDIAN
#define YUY2_BLACK 0x7f007f00
#else
#define YUY2_BLACK 0x007f007f
#endif

/////////////////////////////////////////

// We have to dynamically load the .DLL/.so file due to incompatibilities between MSVC++ and mingw32 library files
// These pointers and typedefs assist us in doing so.

typedef const struct vldp_out_info *(*initproc)(const struct vldp_in_info *in_info);
initproc pvldp_init;	// pointer to the init proc ...

// pointer to all functions the VLDP exposes to us ...
const struct vldp_out_info *g_vldp_info = NULL;

// info that we provide to the VLDP DLL
struct vldp_in_info g_local_info;

/////////////////////////////////////////

ldp_vldp::ldp_vldp()
{
	m_bIsVLDP = true;	// this is VLDP, so this value is true ...
	blitting_allowed = true;	// blitting is allowed until we get to a certain point in initialization
	m_target_mpegframe = 0;	// mpeg frame # we are seeking to
	m_mpeg_path = "";
	m_cur_mpeg_filename = "";
	m_file_index = 0; // # of mpeg files in our list
	m_framefile = ".txt";
	m_framefile = g_game->get_shortgamename() + m_framefile;	// create a sensible default framefile name
	m_bFramefileSet = false;
	m_altaudio_suffix = "";	// no alternate audio by default
	m_audio_file_opened = false;
	m_cur_ldframe_offset = 0;
	m_blank_on_searches = false;
	m_blank_on_skips = false;

	// RJS HERE
	LOGI("In ldp_vldp constructor.");
	m_seek_frames_per_ms = 0;
	m_min_seek_delay = 0;

	m_vertical_stretch = 0;

	m_testing = false;	// don't run tests by default

	m_bPreCache = m_bPreCacheForce = false;
	m_mPreCachedFiles.clear();

	m_uSoundChipID = 0;

	enable_audio1();	// both channels should be on by default
	enable_audio2();
}

ldp_vldp::~ldp_vldp()
{
	pre_shutdown();
}

// called when daphne starts up
bool ldp_vldp::init_player()
{
	LOGI("daphne-libretro: In ldp_vldp::init_player, top of routine.");

	bool result = false;
	bool need_to_parse = false;	// whether we need to parse all video

	g_vertical_stretch = m_vertical_stretch;  // callbacks don't have access to m_vertical_stretch
	
	LOGI("daphne-libretro: In ldp_vldp::init_player, before load_vldp_lib.");

	// load the .DLL first in case we call any of its functions elsewhere
	if (load_vldp_lib())
	{
		LOGI("daphne-libretro: In ldp_vldp::init_player, after load_vldp_lib.");

		// try to read in the framefile
		if (read_frame_conversions())
		{
			LOGI("daphne-libretro: In ldp_vldp::init_player, after read_frame_conversions.");

			// just a sanity check to make sure their frame file is correct
			if (first_video_file_exists())
			{				
				LOGI("daphne-libretro: In ldp_vldp::init_player, after first_video_file_exists.");

				// if the last video file has not been parsed, assume none of them have been
				// This is safe because if they have been parsed, it will just skip them
				if (!last_video_file_parsed())
				{
					LOGI("daphne-libretro: In ldp_vldp::init_player, in last_video_file_parsed if block.");

					printnotice("Press any key to parse your video file(s). This may take a while. Press ESC if you'd rather quit.");
					need_to_parse = true;
				}
				
				LOGI("daphne-libretro: In ldp_vldp::init_player, before audio_init.");
				if (audio_init() && !get_quitflag())
				{
					LOGI("daphne-libretro: In ldp_vldp::init_player, after successful audio_init.");
					// if our game is using video overlay,
					// AND if we're not doing tests that an overlay would interfere with
					// we'll use our slower callback

					// 201x.xx.xx - RJS - since the change of moving the LDP thread to start earlier, the video overlays were not
					// initialized (video_init) yet, however this flag can be
					// if (g_game->get_active_video_overlay() && !m_testing)
					if (g_game->does_game_use_video_overlay() && !m_testing)
					{
						g_local_info.prepare_frame = prepare_frame_callback_with_overlay;
					}
					
					// otherwise we can draw the frame much faster w/o worrying about
					// video overlay
					else
					{
						g_local_info.prepare_frame = prepare_frame_callback_without_overlay;
					}
					g_local_info.display_frame = display_frame_callback;
#ifdef GP2X
					// gp2x receives a yuy2 frame directly
					g_local_info.prepare_yuy2_frame = prepare_gp2x_frame_callback;
					g_local_info.display_yuy2_frame = display_gp2x_frame_callback;
#endif // GP2X
					g_local_info.report_parse_progress = report_parse_progress_callback;
					g_local_info.report_mpeg_dimensions = report_mpeg_dimensions_callback;
					g_local_info.render_blank_frame = blank_overlay;
					g_local_info.blank_during_searches = m_blank_on_searches;
					g_local_info.blank_during_skips = m_blank_on_skips;
					g_local_info.GetTicksFunc = GetTicksFunc;

#ifdef USE_OPENGL
					// if we're using openGL, then we have a different set of callbacks ...
					if (get_use_opengl())
					{
						g_local_info.prepare_frame = prepare_frame_GL_callback;
						g_local_info.display_frame = display_frame_GL_callback;
						g_local_info.report_mpeg_dimensions = report_mpeg_dimensions_GL_callback;
						g_local_info.render_blank_frame = render_blank_frame_GL_callback;
						if (!init_vldp_opengl())
						{
							printerror("OpenGL v2.0 initialization failed.");
							set_quitflag();
						}
					}
#endif
					LOGI("daphne-libretro: In ldp_vldp::init_player, before pvldp_init.");

					g_vldp_info = pvldp_init(&g_local_info);

					LOGI("daphne-libretro: In ldp_vldp::init_player, after pvldp_init. vldp_info: %d", (int) g_vldp_info);
					
					// if we successfully made contact with VLDP ...
					if (g_vldp_info != NULL)
					{
						// make sure we are using the API that we expect
						if  (g_vldp_info->uApiVersion == API_VERSION)
						{
							// this number is used repeatedly, so we calculate it once
							g_vertical_offset = g_game->get_video_row_offset();

							// if testing has been requested then run them ...
							if (m_testing)
							{
								list<string> lstrPassed, lstrFailed;
								run_tests(lstrPassed, lstrFailed);
								// run releasetest to see actual results now ...
								printline("Run releasetest to see printed results!");
								set_quitflag();
							}

							// bPreCacheOK will be true if precaching succeeds or is never attempted
							bool bPreCacheOK = true;

							// If precaching has been requested, do it now.
							// The check for RAM requirements is done inside the
							//  precache_all_video function, so we don't need to worry about that here.
							if (m_bPreCache)
							{
								LOGI("daphne-libretro: In ldp_vldp::init_player, before precache_all_video.");
								bPreCacheOK = precache_all_video();
							}

							// if we need to parse all the video
							if (need_to_parse)
							{
								LOGI("daphne-libretro: In ldp_vldp::init_player, before parse_all_video.");
								parse_all_video();
							}

							LOGI("daphne-libretro: In ldp_vldp::init_player, before bPreCacheOK.  bPreCacheOK: %d", bPreCacheOK);

							// if precaching succeeded or we didn't request precaching
							if (bPreCacheOK)
							{
								blitting_allowed = false;	// this is the point where blitting isn't allowed anymore

								LOGI("daphne-libretro: In ldp_vldp::init_player, in bPreCacheOK, before open_and_block.");

								// open first file so that
								// we can draw video overlay even if the disc is not playing
								printline("LDP-VLDP INFO : opening video file . . .");
								printline(m_mpeginfo[0].name.c_str());
								if (open_and_block(m_mpeginfo[0].name))
								{
									LOGI("daphne-libretro: In ldp_vldp::init_player, in bPreCacheOK, after successful open_and_block.");

									// although we just opened a video file, we have not opened an audio file,
									// so we want to force a re-open of the same video file when we do a real search,
									// in order to ensure that the audio file is opened also.
									m_cur_mpeg_filename = "";

									// set MPEG size ASAP in case different from NTSC default
									m_discvideo_width = g_vldp_info->w;
									m_discvideo_height = g_vldp_info->h;

									if (is_sound_enabled())
									{
										struct sounddef soundchip;
										soundchip.type = SOUNDCHIP_VLDP;
										// no further parameters necessary
										m_uSoundChipID = add_soundchip(&soundchip);
									}

									result = true;

									LOGI("daphne-libretro: In ldp_vldp::init_player, in bPreCacheOK, end of successful open_and_block.");
								}
								else
								{
									printline("LDP-VLDP ERROR : first video file could not be opened!");
								}
							} // end if it's ok to proceed
							// else precaching failed
							else
							{
								printerror("LDP-VLDP ERROR : precaching failed");
							}
						} // end if API matches up
						else
						{
							printerror("VLDP library is the wrong version!");
						}
						
					} // end if reading the frame conversion file worked
					else
					{
						printline("LDP-VLDP ERROR : vldp_init returned NULL (which shouldn't ever happen)");
					}
				} // if audio init succeeded
				else
				{
					// only report an audio problem if there is one
					if (!get_quitflag())
					{
						printline("Could not initialize VLDP audio!");
					}
					
					// otherwise report that they quit
					else
					{
						printline("VLDP : Quit requested, shutting down!");
					}
				} // end if audio init failed or if user opted to quit instead of parse
			} // end if first file was present (sanity check)
			// else if first file was not found, we do just quit because an error is printed elsewhere
		} // end if framefile was read in properly
		else
		{
			// if the user didn't specify a framefile from the command-line, then give them a little hint.
			if (!m_bFramefileSet)
			{
				printline("You must specify a -framefile argument when using VLDP.");
			}
			// else the other error messages are more than sufficient
		}
	} // end if .DLL was loaded properly
	else
	{
		printline("Could not load VLDP dynamic library!!!");
	}
	
	// if init didn't completely finish, then we need to shutdown everything
	if (!result)
	{
		shutdown_player();
	}

	LOGI("daphne-libretro: In ldp_vldp::init_player, bottom of routine.");
	return result;
}

void ldp_vldp::shutdown_player()
{
	// if VLDP has been loaded
	if (g_vldp_info)
	{
		g_vldp_info->shutdown();
		g_vldp_info = NULL;
	}
	
	if (is_sound_enabled())
	{
		if (!delete_soundchip(m_uSoundChipID))
		{
			printline("ldp_vldp::shutdown_player WARNING : sound chip could not be deleted");
		}
	}
	free_vldp_lib();
	audio_shutdown();
	free_yuv_overlay();	// de-allocate overlay if we have one allocated ...
	
#ifdef USE_OPENGL
	free_gl_resources();
#endif

}

bool ldp_vldp::open_and_block(const string &strFilename)
{
	LOGI("daphne-libretro: In ldp_vldp::open_and_block, top of routine.  Filename: %s  Path: %s", strFilename.c_str(), m_mpeg_path.c_str());

	bool bResult = false;

	// during parsing, blitting is allowed
	// NOTE: we want this here so that it is true before the initial open command is called.
	//  Otherwise we'd put it inside wait_for_status ...
	blitting_allowed = true;

	// see if this filename has been precached
	map<string, unsigned int>::const_iterator mi = m_mPreCachedFiles.find(strFilename);

	// if the file has not been precached and we are able to open it
	if ((mi == m_mPreCachedFiles.end() &&
			(g_vldp_info->open((m_mpeg_path + strFilename).c_str())))
			// OR if the file has been precached and we are able to refer to it
		|| (g_vldp_info->open_precached(mi->second, (m_mpeg_path + strFilename).c_str())))
	{
		LOGI("daphne-libretro: In ldp_vldp::open_and_block, before wait_for_status.");
		bResult = wait_for_status(STAT_STOPPED);
		LOGI("daphne-libretro: In ldp_vldp::open_and_block, after wait_for_status.  bResult: %d", bResult);
		if (bResult)
		{
			m_cur_mpeg_filename = strFilename;
		}
	}

	blitting_allowed = false;

	LOGI("daphne-libretro: In ldp_vldp::open_and_block, bottom of routine.  bResult: %d", bResult);

	return bResult;
}

bool ldp_vldp::precache_and_block(const string &strFilename)
{
	bool bResult = false;

	// during precaching, blitting is allowed
	// NOTE: we want this here so that it is true before the initial open command is called.
	//  Otherwise we'd put it inside wait_for_status ...
	blitting_allowed = true;

	if (g_vldp_info->precache((m_mpeg_path + strFilename).c_str()))
	{
		bResult = wait_for_status(STAT_STOPPED);
	}

	blitting_allowed = false;

	return bResult;
}

bool ldp_vldp::wait_for_status(unsigned int uStatus)
{
	LOGI("daphne-libretro: In ldp_vldp::wait_for_status, top of function.  uStatus: %d  g_vldp_info status: %d", uStatus, g_vldp_info->status);

	bool bResult = false;

	while (g_vldp_info->status == STAT_BUSY)
	{
		LOGI("daphne-libretro: In ldp_vldp::wait_for_status, in STAT_BUSY loop.  uStatus: %d  g_vldp_info status: %d", uStatus, g_vldp_info->status);

		// if we got a parse update, then show it ...
		if (g_bGotParseUpdate)
		{
			LOGI("daphne-libretro: In ldp_vldp::wait_for_status, in STAT_BUSY loop.  uStatus: %d  g_vldp_info status: %d  g_bGotParseUpdate: %d", uStatus, g_vldp_info->status, g_bGotParseUpdate);

			// redraw screen blitter before we display it
			update_parse_meter();
			vid_blank();
			vid_blit(get_screen_blitter(), 0, 0);
			vid_flip();
			g_bGotParseUpdate = false;
		}

		SDL_check_input();	// so that windows events are handled
		make_delay(20);	// be nice to CPU
	}

	// if opening succeeded
	if ((unsigned int) g_vldp_info->status == uStatus)
	{
		bResult = true;
	}

	LOGI("daphne-libretro: In ldp_vldp::wait_for_status, bottom of function.  bResult: %d", bResult);

	return bResult;
}

bool ldp_vldp::nonblocking_search(char *frame)
{
	
	bool result = false;
	string filename = "";
	string oggname = "";
	Uint16 target_ld_frame = (Uint16) atoi(frame);
	Uint64 u64AudioTargetPos = 0;	// position in audio to seek to (in samples)
	unsigned int seek_delay_ms = 0;	// how many ms this seek must be delayed (to simulate laserdisc lag)
	
	audio_pause();	// pause the audio before we seek so we don't have overrun
	
	// do we need to compute seek_delay_ms?
	// (This is best done sooner than later so get_current_frame() is more accurate
	if (m_seek_frames_per_ms > 0)
	{
		// this value should be approximately the last frame we displayed
		// it doesn't get changed to the new frame until the seek is complete
		Uint16 cur_frame = get_current_frame();
		unsigned int frame_delta = 0;	// how many frames we're skipping
		
		// if we're seeking forward
		if (target_ld_frame > cur_frame)
		{
			frame_delta = target_ld_frame - cur_frame;
		}
		else
		{
			frame_delta = cur_frame - target_ld_frame;
		}
		
		seek_delay_ms = (unsigned int) (frame_delta / m_seek_frames_per_ms);

#ifdef DEBUG
		string msg = "frame_delta is ";
		msg += numstr::ToStr(frame_delta);
		msg += " and seek_delay_ms is ";
		msg += numstr::ToStr(seek_delay_ms);
		printline(msg.c_str());
#endif
		
	}

	// make sure our seek delay does not fall below our minimum
	if (seek_delay_ms < m_min_seek_delay) seek_delay_ms = m_min_seek_delay;

	m_target_mpegframe = mpeg_info(filename, target_ld_frame); // try to get a filename

	// if we can convert target frame into a filename, do it!
	if (filename != "")
	{
		result = true;	// now we assume success unless we fail
		
		// if the file to be opened is different from the one we have opened
		// OR if we don't yet have a file open ...
		// THEN open the file! :)
		if (filename != m_cur_mpeg_filename)
		{
			// if we were able to successfully open the video file
			if (open_and_block(filename))
			{
				result = true;

				// this is done inside open_and_block now ...
				//m_cur_mpeg_filename = filename; // make video file our new current file
				
				// if sound is enabled, try to open an audio stream to match the video stream
				if (is_sound_enabled())
				{
					// try to open an optional audio file to go along with video
					oggize_path(oggname, filename);
					m_audio_file_opened = open_audio_stream(oggname.c_str());
				}
			}
			else
			{
				outstr("LDP-VLDP.CPP : Could not open video file ");
				printline(filename.c_str());
				result = false;
			}
		}

		// if we're ok so far, try the search
		if (result)
		{
			// IMPORTANT : 'uFPKS' _must_ be queried AFTER a new mpeg has been opened,
			//  because sometimes a framefile can include mpegs that have different framerates
			//  from each other.
			unsigned int uFPKS = g_vldp_info->uFpks;

			m_discvideo_width = g_vldp_info->w;
			m_discvideo_height = g_vldp_info->h;

			// IMPORTANT : this must come before the optional FPS adjustment takes place!!!
			u64AudioTargetPos = get_audio_sample_position(m_target_mpegframe);

			if (!need_frame_conversion())
			{
				// If the mpeg's FPS and the disc's FPS differ, we need to adjust the mpeg frame
				// NOTE: AVOID this if you can because it makes seeking less accurate
				if (g_game->get_disc_fpks() != uFPKS)
				{
#ifndef GP2X	// we want to show a msg w/ floating point stuff in it, which is too much for gp2x to handle
					string s = "NOTE: converting FPKS from " + numstr::ToStr(g_game->get_disc_fpks()) +
						" to " + numstr::ToStr(uFPKS) + ". This may be less accurate.";
					printline(s.c_str());
#endif
					m_target_mpegframe = (m_target_mpegframe * uFPKS) / g_game->get_disc_fpks();
				}
			}

			// try to search to the requested frame
			if (g_vldp_info->search((Uint16) m_target_mpegframe, seek_delay_ms))
			{
				// if we have an audio file opened, do an audio seek also
				if (m_audio_file_opened)
				{
					result = seek_audio(u64AudioTargetPos);
				}
			}
			else
			{
				printline("LDP-VLDP.CPP : Search failed in video file");
			}
		}
		// else opening the file failed
	}
	// else mpeg_info() wasn't able to provide a filename ...
	else
	{
		printline("LDP-VLDP.CPP ERROR: frame could not be converted to file, probably due to a framefile error.");
		outstr("Your framefile must begin no later than frame ");
		printline(frame);
		printline("This most likely is your problem!");
	}
	
	return(result);
}

// it should be safe to assume that if this function is getting called, that we have not yet got a result from the search
int ldp_vldp::get_search_result()
{
	int result = SEARCH_BUSY;	// default to no change
	
	// if search is finished and has succeeded
	if (g_vldp_info->status == STAT_PAUSED)
	{
		result = SEARCH_SUCCESS;
	}
	
	// if the search failed
	else if (g_vldp_info->status == STAT_ERROR)
	{
		result = SEARCH_FAIL;
	}
	
	// else it's busy so we just wait ...
	
	return result;
}

unsigned int ldp_vldp::play()
{
	unsigned int result = 0;
	string ogg_path = "";
	bool bOK = true;	// whether it's ok to issue the play command
	
	// if we haven't opened any mpeg file yet, then do so now
	if (m_cur_mpeg_filename == "")
	{
		bOK = open_and_block(m_mpeginfo[0].name);	// get the first mpeg available in our list
		if (bOK)
		{
			// this is done inside open_and_block now ...
			//m_cur_mpeg_filename = m_mpeginfo[0].name;
			
			// if sound is enabled, try to load an audio stream to go with video stream ...
			if (is_sound_enabled())
			{			
				// try to open an optional audio file to go along with video
				oggize_path(ogg_path, m_mpeginfo[0].name);
				m_audio_file_opened = open_audio_stream(ogg_path.c_str());
			}
			
		}
		else
		{
			outstr("LDP-VLDP.CPP : in play() function, could not open mpeg file ");
			printline(m_mpeginfo[0].name.c_str());
		}
	} // end if we haven't opened a file yet
	
	// we need to keep this separate in case an mpeg is already opened
	if (bOK)
	{
#ifdef DEBUG
		// we always expect this to be true, because we've just played :)
		assert(m_uElapsedMsSincePlay == 0);
#endif
		audio_play(0);
		if (g_vldp_info->play(0))
		{
			result = GET_TICKS();
		}
	}

	if (!result)
	{
		printline("VLDP ERROR : play command failed!");
	}
	
	return result;
}

// skips forward a certain # of frames during playback without pausing
// Caveats: Does not work with an mpeg of the wrong framerate, does not work with an mpeg
//  that uses fields, and does not skip across file boundaries.
// Returns true if skip was successful
bool ldp_vldp::skip_forward(Uint16 frames_to_skip, Uint16 target_frame)
{
	bool result = false;
	
	target_frame = (Uint16) (target_frame - m_cur_ldframe_offset);	// take offset into account
	// this is ok (and possible) because we don't support skipping across files

	unsigned int uFPKS = g_vldp_info->uFpks;
	unsigned int uDiscFPKS = g_game->get_disc_fpks();

	// We don't support skipping on mpegs that differ from the disc framerate
	if (uDiscFPKS == uFPKS)
	{
		// make sure they're not using fields
		// UPDATE : I don't see any reason why using fields would be a problem anymore,
		//  but since I am not aware of any mpegs that use fields that require skipping,
		//  I am leaving this restriction in here just to be safe.
		if (!g_vldp_info->uses_fields)
		{
			// advantage to this method is no matter how many times we skip, we won't drift because we are using m_play_time as our base
			// if we have an audio file opened
			if (m_audio_file_opened)
			{
				//Uint64 u64AudioTargetPos = (((Uint64) target_frame) * FREQ1000) / uDiscFPKS;
				Uint64 u64AudioTargetPos = get_audio_sample_position(target_frame);
	
				// seek and play if seeking was successful
				if (seek_audio(u64AudioTargetPos))
				{
					audio_play(m_uElapsedMsSincePlay);
				}
			}
			// else we have no audio file open, but that's ok ...
	
			// if VLDP was able to skip successfully
			if (g_vldp_info->skip(target_frame))
			{
				result = true;
			}
			else
			{
				printline("LDP-VLDP ERROR : video skip failed");
			}
		}
		else
		{
			printline("LDP-VLDP ERROR : Skipping not supported with mpegs that use fields (such as this one)");
		}
	}
	else
	{
		string s = "LDP-VLDP ERROR : Skipping not supported when the mpeg's framerate differs from the disc's (" +
			numstr::ToStr(uFPKS / 1000.0) + " vs " + numstr::ToStr(uDiscFPKS / 1000.0) + ")";
		printline(s.c_str());
	}
	
	return result;
}

void ldp_vldp::pause()
{
#ifdef DEBUG
	string s = "ldp_vldp::pause() : g_vldp_info's current frame is " + numstr::ToStr(g_vldp_info->current_frame) +
		" (" + numstr::ToStr(m_cur_ldframe_offset + g_vldp_info->current_frame) + " adjusted)";
	printline(s.c_str());
#endif
	g_vldp_info->pause();
	audio_pause();
}

bool ldp_vldp::change_speed(unsigned int uNumerator, unsigned int uDenominator)
{
	bool bResult = false;

	// if we aren't doing 1X, then stop the audio (this can be enhanced later)
	if ((uNumerator != 1) || (uDenominator != 1))
	{
		audio_pause();
	}
	// else the audio should be played at the correct location
	else
	{
		string filename;	// dummy, not used

		// calculate where our audio position should be
		unsigned int target_mpegframe = mpeg_info(filename, get_current_frame());
		Uint64 u64AudioTargetPos = get_audio_sample_position(target_mpegframe);

		// try to get the audio playing again
		if (seek_audio(u64AudioTargetPos))
		{
			audio_play(m_uElapsedMsSincePlay);
		}
		else
		{
			printline("WARNING : trying to seek audio after playing at 1X failed");
		}
	}

	if (g_vldp_info->speedchange(m_uFramesToSkipPerFrame, m_uFramesToStallPerFrame))
	{
		bResult = true;
	}

	return bResult;
}

void ldp_vldp::think()
{
#ifdef USE_OPENGL
	// IMPORTANT: this must come before we update VLDP's uMsTimer to ensure that OpenGL has a chance to draw any pending frames before
	//  a new frame comes in.
	if (get_use_opengl())
	{
		ldp_vldp_gl_think(m_uVblankCount);
	}
#endif

	// VLDP relies on this number
	// (m_uBlockedMsSincePlay is only non-zero when we've used blocking seeking)
	g_local_info.uMsTimer = m_uElapsedMsSincePlay + m_uBlockedMsSincePlay;

}

#ifdef DEBUG
// This function tests to make sure VLDP's current frame is the same as our internal current frame.
unsigned int ldp_vldp::get_current_frame()
{
	Sint32 result = 0;

	// safety check
	if (!g_vldp_info) return 0;

	unsigned int uFPKS = g_vldp_info->uFpks;

	// the # of frames that have advanced since our search
	unsigned int uOffset = g_vldp_info->current_frame - m_target_mpegframe;

	unsigned int uStartMpegFrame = m_target_mpegframe;

	// since the mpeg's beginning does not correspond to the laserdisc's beginning, we add the offset
	result = m_cur_ldframe_offset + uStartMpegFrame + uOffset;

	// if we're out of bounds, just set it to 0
	if (result <= 0)
	{
		result = 0;
	}

	// if we got a legitimate frame
	else
	{
		// FIXME : THIS CODE HAS BUGS IN IT, I HAVEN'T TRACKED THEM DOWN YET HEHE
		// if the mpeg's FPS and the disc's FPS differ, we need to adjust the frame that we return
		if (g_game->get_disc_fpks() != uFPKS)
		{
			return m_uCurrentFrame;
			//result = (result * g_game->get_disc_fpks()) / uFPKS;
		}
	}

	// if there's even a slight mismatch, report it ...
	if ((unsigned int) result != m_uCurrentFrame)
	{
		// if VLDP is ahead, that is especially disturbing
		if ((unsigned int) result > m_uCurrentFrame)
		{
			string s = "ldp-vldp::get_current_frame() [vldp ahead]: internal frame is " + numstr::ToStr(m_uCurrentFrame) +
				", vldp's current frame is " + numstr::ToStr(result);
			printline(s.c_str());

			s = "g_local_info.uMsTimer is " + numstr::ToStr(g_local_info.uMsTimer) + ", which means frame offset " +
				numstr::ToStr((g_local_info.uMsTimer * uFPKS) / 1000000);
#ifndef GP2X
			s += " (" + numstr::ToStr(g_local_info.uMsTimer * uFPKS * 0.000001) + ") ";
#endif
			printline(s.c_str());
			s = "m_uCurrentOffsetFrame is " + numstr::ToStr(m_uCurrentOffsetFrame) + ", m_last_seeked frame is " + numstr::ToStr(m_last_seeked_frame);
			printline(s.c_str());
			unsigned int uMsCorrect = ((result - m_last_seeked_frame) * 1000000) / uFPKS;
			s = "correct elapsed ms is " + numstr::ToStr(uMsCorrect);
#ifndef GP2X
			// show float if we have decent FPU
			s += " (" + numstr::ToStr((result - m_last_seeked_frame) * 1000000.0 / uFPKS) + ") ";
#endif
			s += ", which is frame offset " + numstr::ToStr((uMsCorrect * uFPKS) / 1000000);
#ifndef GP2X
			// show float result if we have a decent FPU
			s += " (" + numstr::ToStr(uMsCorrect * uFPKS * 0.000001) + ")";
#endif
			printline(s.c_str());

		}

		// This will be behind more than 1 frame (legitimately) playing at faster than 1X, 
		//  so uncomment this with that in mind ...
		/*
		// else if VLDP is behind more than 1 frame, that is also disturbing
		else if ((m_uCurrentFrame - result) > 1)
		{
			string s = "ldp-vldp::get_current_frame() [vldp behind]: internal frame is " + numstr::ToStr(m_uCurrentFrame) +
				", vldp's current frame is " + numstr::ToStr(result);
			printline(s.c_str());
		}

		// else vldp is only behind 1 frame, that is to be expected at times due to threading issues ...
		*/

	}

	return m_uCurrentFrame;
}
#endif

// takes a screenshot of the current frame + any video overlay
void ldp_vldp::request_screenshot()
{
	g_take_screenshot = true;
}

void ldp_vldp::set_search_blanking(bool enabled)
{
	m_blank_on_searches = enabled;
}

void ldp_vldp::set_skip_blanking(bool enabled)
{
	m_blank_on_skips = enabled;
}

void ldp_vldp::set_seek_frames_per_ms(double value)
{
	m_seek_frames_per_ms = value;
}

void ldp_vldp::set_min_seek_delay(unsigned int value)
{
	m_min_seek_delay = value;
}

// sets the name of the frame file
void ldp_vldp::set_framefile(const char *filename)
{
	m_bFramefileSet = true;
	m_framefile = filename;
}

// sets alternate soundtrack
void ldp_vldp::set_altaudio(const char *audio_suffix)
{
	m_altaudio_suffix = audio_suffix;
}

// sets alternate soundtrack
void ldp_vldp::set_vertical_stretch(unsigned int value)
{
	m_vertical_stretch = value;
}

void ldp_vldp::test_helper(unsigned uIterations)
{
	// We aren't calling think_delay because we want to have a lot of milliseconds pass quickly without actually waiting.

	// make a certain # of milliseconds elapse ....
	for (unsigned int u = 0; u < uIterations; u++)
	{
		pre_think();
	}

}

void ldp_vldp::run_tests(list<string> &lstrPassed, list<string> &lstrFailed)
{
	// these tests just basically stress the VLDP .DLL to make sure it will never crash
	// under any circumstances
	string s = "";
	bool result = false;

	// if we have at least 1 entry in our framefile
	if (m_file_index > 0)
	{
		string path = m_mpeginfo[0].name;	// create full pathname to file

		// TEST #1 : try opening and playing without seeking
		s = "VLDP TEST #1 (playing and skipping)";
		if (open_and_block(path) == 1)
		{
			g_local_info.uMsTimer = GET_TICKS();
			if (g_vldp_info->play(g_local_info.uMsTimer) == 1)
			{
				test_helper(1000);
				if (g_vldp_info->skip(5) == 1)
				{
					lstrPassed.push_back(s);
				}
				else
				{
					lstrFailed.push_back(s + " (opened and played, but could not skip)");
				}
			}
			else lstrFailed.push_back(s + " (opened but could not play)");
		}
		else lstrFailed.push_back(s);

		// TEST #2 : try opening the file repeatedly
		// This should not cause any problems
		s = "VLDP TEST #2 (opening repeatedly)";
		result = true;
		for (int i = 0; i < 10; i++)
		{
			if (!open_and_block(path))
			{
				result = false;
				break;
			}
		}
		if (result) lstrPassed.push_back(s);
		else lstrFailed.push_back(s);

		// TEST #3 : try opening the file and seeking to an illegal frame
		s = "VLDP TEST #3 (seeking to illegal frame)";
		if (!g_vldp_info->search_and_block(65534, 0))
		{
			lstrPassed.push_back(s);
		}
		else lstrFailed.push_back(s);
		
		// TEST #4 : try seeking to a (hopefully) legitimate frame
		s = "VLDP TEST #4 (seeking to legit frame)";
		if (g_vldp_info->search_and_block(50, 0) == 1)
		{
			lstrPassed.push_back(s);
			//test_helper(1000);
		}
		else
		{
			lstrFailed.push_back(s);
		}
		
		// TEST #5 : play to the end of the file, then try playing again to see what happens
		s = "VLDP TEST #5 (playing to end of file, then playing again)";
		if (open_and_block(path) == 1)
		{
			g_local_info.uMsTimer = m_uElapsedMsSincePlay = m_uBlockedMsSincePlay = 0;

			if (g_vldp_info->play(g_local_info.uMsTimer) == 1)
			{
				// wait for file to end ...
				while ((g_vldp_info->status == STAT_PLAYING) && (!get_quitflag()))
				{
					SDL_check_input();
					test_helper(250);
				}
				
				if (g_vldp_info->play(g_local_info.uMsTimer) == 1)
				{
					lstrPassed.push_back(s);
					test_helper(1000);
				}
				else
				{
					lstrFailed.push_back(s + " - the second time around");
				}
			}
			else lstrFailed.push_back(s + "opened file, but failed to play it)");
		}
		else lstrFailed.push_back(s);

		// TEST #6 : play to the end of the file, then try seeking to see what happens
		s = "VLDP TEST #6 (play to end of file, then seek)";
		if (open_and_block(path) == 1)
		{
			g_local_info.uMsTimer = m_uElapsedMsSincePlay = m_uBlockedMsSincePlay = 0;

			if (g_vldp_info->play(g_local_info.uMsTimer) == 1)
			{
				// wait for file to end ...
				while ((g_vldp_info->status == STAT_PLAYING) && (!get_quitflag()))
				{
					SDL_check_input();
					test_helper(250);
				}
				
				if (g_vldp_info->search_and_block(50, 0) == 1)
				{
					lstrPassed.push_back(s);
					test_helper(1000);
				}
				else
				{
					lstrFailed.push_back(s + "(failed the second time around)");
				}
			}
			else lstrFailed.push_back(s + "(opened file, but failed to play it)");
		}
		else lstrFailed.push_back(s);

		// TEST #7 : open, seek, play, all testing timing
		s = "VLDP TEST #7 (seeking, playing with timing tested)";
		if (open_and_block(path) == 1)
		{
			g_local_info.uMsTimer = m_uElapsedMsSincePlay = m_uBlockedMsSincePlay = 0;
			
			// search to beginning frame ...
			if (g_vldp_info->search_and_block(0, 0) == 1)
			{
				test_helper(1);	// make 1 ms elapse ...
				SDL_Delay(1);	// give VLDP thread a chance to make its own updates ...
				
				// make sure that frame is what we expect it to be ...
				if (g_vldp_info->current_frame == 0)
				{
					test_helper(50);	// make 50 ms elapse
					SDL_Delay(1);	// give VLDP thread a chance to make updates

					// make sure current frame has not changed
					if (g_vldp_info->current_frame == 0)
					{
						g_local_info.uMsTimer = m_uElapsedMsSincePlay = m_uBlockedMsSincePlay = 0;
						
						// so now we start playing ...
						if (g_vldp_info->play(0) == 1)
						{
							test_helper(32);	// pause 32 ms (right before frame should change)
							SDL_Delay(50);	// vldp thread blah blah ...

							// current frame still should not have changed
							if (g_vldp_info->current_frame == 0)
							{
								test_helper(1);	// 1 more ms, frame should change now
								SDL_Delay(50);	// don't fail simply due to the cpu being overloaded
								if (g_vldp_info->current_frame == 1)
								{
									lstrPassed.push_back(s);
								}
								else lstrFailed.push_back(s + " (frame didn't change to 1)");
							}
							else lstrFailed.push_back(s + " (frame changed to " + numstr::ToStr(g_vldp_info->current_frame) + " after play)");
						}
						else lstrFailed.push_back(s + " (play failed)");
					}
					else lstrFailed.push_back(s + " (current frame changed from 0 after seek)");
				}
				else lstrFailed.push_back(s + " (opened, but current frame was not 0)");
			}
			else lstrFailed.push_back(s + " (opened but could not search)");
		}
		else lstrFailed.push_back(s);
		
	} // end if there was 1 file in the framefile
	else lstrFailed.push_back("VLDP TESTS (Framefile had no entries)");
}

// handles VLDP-specific command line args
bool ldp_vldp::handle_cmdline_arg(const char *arg)
{
	bool result = true;
	
	if (strcasecmp(arg, "-blend")==0)
	{
		g_filter_type |= FILTER_BLEND;
	}
	else if (strcasecmp(arg, "-scanlines")==0)
	{
		g_filter_type |= FILTER_SCANLINES;
	}
	// should we run a few VLDP tests when the player is initialized?
	else if (strcasecmp(arg, "-vldptest")==0)
	{
		m_testing = true;
	}
	// should we precache all video streams to RAM?
	else if (strcasecmp(arg, "-precache")==0)
	{
		m_bPreCache = true;
	}
	// even if we don't have enough RAM, should we still precache all video streams to RAM?
	else if (strcasecmp(arg, "-precache_force")==0)
	{
		m_bPreCache = true;
		m_bPreCacheForce = true;
	}
	
	// else it's unknown
	else
	{
		result = false;
	}
	
	return result;
}


//////////////////////////////////

// loads the VLDP dynamic library, returning true on success
bool ldp_vldp::load_vldp_lib()
{
	bool result = false;

#ifndef STATIC_VLDP	// if we're loading VLDP dynamically
#ifndef DEBUG
    m_dll_instance = M_LOAD_LIB(vldp2);	// load VLDP2.DLL or libvldp2.so
#else
	m_dll_instance = M_LOAD_LIB(vldp2_dbg);
#endif
	
    // If the handle is valid, try to get the function address. 
    if (m_dll_instance)
    {
		pvldp_init = (initproc) M_GET_SYM(m_dll_instance, "vldp_init");
		
		// if init function was found
		if (pvldp_init)
		{
			result = true;
		}
		else
		{
			printerror("VLDP LOAD ERROR : vldp_init could not be loaded");
		}
	}
	else
	{
		printerror("ERROR: could not open the VLDP2 dynamic library (file not found maybe?)");
	}

#else // else if we're loading VLDP statically
	pvldp_init = vldp_init;
	result = true;
#endif // STATIC_VLDP
	
	return result;
}

// frees the VLDP dynamic library if we loaded it in
void ldp_vldp::free_vldp_lib()
{
#ifndef STATIC_VLDP	// if we loaded vldp dynamically, then we need to unload it properly...

	// don't free if the library was never opened
	if (m_dll_instance)
	{
		M_FREE_LIB(m_dll_instance);
	}

#endif // STATIC_VLDP
}




// read frame conversions in from LD-frame to mpeg-frame data file
bool ldp_vldp::read_frame_conversions()
{
	LOGI("daphne-libretro: In read_frame_conversions, top of function.");

	struct mpo_io *p_ioFileConvert;
	string s = "";
	string frame_string = "";
	bool result = false;
	string framefile_path;
	
	framefile_path = m_framefile;
	LOGI("daphne-libretro: In read_frame_conversions, framefile_path: %s", framefile_path.c_str());

	p_ioFileConvert = mpo_open(framefile_path.c_str(), MPO_OPEN_READONLY);
	
	// if the file was not found in the relative directory, try looking for it in the framefile directory
	if (!p_ioFileConvert)
	{
		LOGI("daphne-libretro: In read_frame_conversions, attempt to open framefile in relative directory failed, framefile_path: %s", framefile_path.c_str());
		framefile_path = g_homedir.get_framefile(framefile_path);	// add directory to front of path
		LOGI("daphne-libretro: In read_frame_conversions, attempting framefile directory, framefile_path: %s", framefile_path.c_str());
		p_ioFileConvert = mpo_open(framefile_path.c_str(), MPO_OPEN_READONLY);
	}
	
	// if the framefile was opened successfully
	if (p_ioFileConvert)
	{
		LOGI("daphne-libretro: In read_frame_conversions, framefile opened successfully.");
		MPO_BYTES_READ bytes_read = 0;
		// 2017.12.28 - RJS - Is this ever freed?  Or saved off?
		char *ff_buf = (char *) MPO_MALLOC((unsigned int) (p_ioFileConvert->size+1));	// add an extra byte to null terminate
		if (ff_buf != NULL)
		{
			if (mpo_read(ff_buf, (unsigned int) p_ioFileConvert->size, &bytes_read, p_ioFileConvert))
			{
				// if we successfully read in the whole framefile
				if (bytes_read == p_ioFileConvert->size)
				{
					string err_msg = "";
					
					ff_buf[bytes_read] = 0;	// NULL terminate the end of the file to be safe
					
					// if parse was successful
					if (parse_framefile(
						(const char *) ff_buf,
						framefile_path.c_str(),
						m_mpeg_path,
						&m_mpeginfo[0],
						m_file_index,
						sizeof(m_mpeginfo) / sizeof(struct fileframes),
						err_msg))
					{
						outstr("Framefile parse succeeded. Video/Audio directory is: ");
						printline(m_mpeg_path.c_str());
						result = true;

						LOGI("Framefile parse succeeded.  Video/Audio directory is: %s", framefile_path.c_str());
						LOGI("---BEGIN FRAMEFILE CONTENTS---");
						LOGI("%s", ff_buf);
						LOGI("---END FRAMEFILE CONTENTS---");
					}
					else
					{
						printerror("Framefile Parse Error");
						printline(err_msg.c_str());
						err_msg = "Mpeg Path : " + m_mpeg_path;
						printline(err_msg.c_str());
						printline("---BEGIN FRAMEFILE CONTENTS---");
						// print the entire contents of the framefile to make it easier to us to debug newbie problems using their daphne_log.txt
						printline(ff_buf);
						printline("---END FRAMEFILE CONTENTS---");
					}
				}
				else printerror("ldp-vldp.cpp : framefile read error");
			}
			else printerror("ldp-vldp.cpp : framefile read error");
		}
		else printerror("ldp-vldp.cpp : mem alloc error");
		mpo_close(p_ioFileConvert);
	}
	else
	{
		LOGI("daphne-libretro: In read_frame_conversions, framefile opened UNsuccessfully.");
		s = "Could not open framefile : " + m_framefile;
		printerror(s.c_str());
	}
	
	LOGI("daphne-libretro: In read_frame_conversions, bottom of function.  Boolean result: %d", result);
	return result;
}

// if file does not exist, we print an error message
bool ldp_vldp::first_video_file_exists()
{
	string full_path = "";
	bool result = false;
	
	// if we have at least one file
	if (m_file_index)
	{
		full_path = m_mpeg_path;
		full_path += m_mpeginfo[0].name;
		if (mpo_file_exists(full_path.c_str()))
		{
			result = true;
		}
		else
		{
			full_path = "Could not open file : " + full_path;	// keep using full_path just because it's convenient
			printerror(full_path.c_str());
		}
	}
	else
	{
		printerror("ERROR : Framefile seems empty, it's probably invalid");
		printline("Read the documentation to learn how to create framefiles.");
	}
	
	return result;
}

// returns true if the last video file has been parsed
// This is so we don't parse_all_video if all files are already parsed
bool ldp_vldp::last_video_file_parsed()
{
	string full_path = "";
	bool result = false;
	
	// if we have at least one file
	if (m_file_index > 0)
	{
		full_path = m_mpeg_path;
		full_path += m_mpeginfo[m_file_index-1].name;
		full_path.replace(full_path.length() - 3, 3, "dat");	// replace pre-existing suffix (which is probably .m2v) with 'dat'
		
		if (mpo_file_exists(full_path.c_str()))
		{
			result = true;
		}
	}
	// else there is a problem with the frame file so return false
	
	return result;
}

// opens (and closes) all video files, forcing any unparsed video files to get parsed
void ldp_vldp::parse_all_video()
{
	unsigned int i = 0;

	for (i = 0; i < m_file_index; i++)
	{
		// if the file can be opened...
		if (open_and_block(m_mpeginfo[i].name))
		{
			g_vldp_info->search_and_block(0, 0);	// search to frame 0 to render it so the user has something to watch while he/she waits
			think();	// let opengl have a chance to display the frame
		}
		else
		{
			outstr("LDP-VLDP: Could not parse video because file ");
			outstr(m_mpeginfo[i].name.c_str());
			printline(" could not be opened.");
			break;
		}
	}
}

bool ldp_vldp::precache_all_video()
{
	bool bResult = true;

	unsigned int i = 0;
	string full_path = "";
	mpo_io *io = NULL;

	// to make sure the total file size is correct
	// (it's legal for a framefile to have the same file listed more than once)
	set<string> sDupePreventer;

	MPO_UINT64 u64TotalBytes = 0;

	// first compute file size ...
	for (i = 0; i < m_file_index; i++)
	{
		full_path = m_mpeg_path + m_mpeginfo[i].name;	// create full pathname to file
		
		// if this file's size hasn't already been taken into account
		if (sDupePreventer.find(m_mpeginfo[i].name) == sDupePreventer.end())
		{
			io = mpo_open(full_path.c_str(), MPO_OPEN_READONLY);
			if (io)
			{
				u64TotalBytes += io->size;
				mpo_close(io);	// we're done with this file ...
				sDupePreventer.insert(m_mpeginfo[i].name);	// we've used it now ...
			}
			// else file can't be opened ...
			else
			{
				outstr("LDP-VLDP: when precaching, the file ");
				outstr(full_path.c_str());
				printline(" cannot be opened.");
				bResult = false;
				break;
			}
		} // end if the filesize hasn't been taken into account
		// else the filesize has already been taken into account
	}

	// if we were able to compute the file size ...
	if (bResult)
	{
		const unsigned int uFUDGE = 256;	// how many megs we assume the OS needs in addition to our application running
		unsigned int uReqMegs = (unsigned int) ((u64TotalBytes / 1048576) + uFUDGE);
		unsigned int uMegs = get_sys_mem();

		// if we have enough memory (accounting for OS overhead, which may need to increase in the future)
		//  OR if the user wants to force precaching despite our check ...
		if ((uReqMegs < uMegs) || (m_bPreCacheForce))
		{
			for (i = 0; i < m_file_index; i++)
			{
				// if the file in question has not yet been precached
				if (m_mPreCachedFiles.find(m_mpeginfo[i].name) == m_mPreCachedFiles.end())
				{
					// try to precache and if it fails, bail ...
					if (precache_and_block(m_mpeginfo[i].name))
					{
						// store the index of the file that we last precached
						m_mPreCachedFiles[m_mpeginfo[i].name] = g_vldp_info->uLastCachedIndex;
					}
					else
					{
						full_path = m_mpeg_path + m_mpeginfo[i].name;

						outstr("LDP-VLDP: precaching of file ");
						outstr(full_path.c_str());
						printline(" failed.");
						bResult = false;
					}
				} // end if file has not been precached
				// else file has already been precached, so don't precache it again
			}
		}
		else
		{
			printline( ((string) "Not enough memory to precache video stream.  You have about " +
				numstr::ToStr(uMegs) + " but need " +
				numstr::ToStr(uReqMegs)).c_str());
			bResult = false;
		}
	}

	return bResult;
}

Uint64 ldp_vldp::get_audio_sample_position(unsigned int uTargetMpegFrame)
{
	Uint64 u64AudioTargetPos = 0;

	if (!need_frame_conversion())
	{
		u64AudioTargetPos = (((Uint64) uTargetMpegFrame) * FREQ1000) / g_game->get_disc_fpks();
		// # of samples to seek to in the audio stream
	}
	// If we are already doing a frame conversion elsewhere, we don't want to do it here again twice
	//  but we do need to set the audio to the correct time
	else
	{
		u64AudioTargetPos = (((Uint64) uTargetMpegFrame) * FREQ1000) / get_frame_conversion_fpks();
	}

	return u64AudioTargetPos;
}


// returns # of frames to seek into file, and mpeg filename
// if there's an error, filename is ""
// WARNING: This assumes the mpeg and disc are running at exactly the same FPS
// If they aren't, you will need to calculate the actual mpeg frame to seek to
// The reason I don't return time here instead of frames is because it is more accurate to
//  return frames if they are at the same FPS (which hopefully they are hehe)
Uint16 ldp_vldp::mpeg_info (string &filename, Uint16 ld_frame)
{
	LOGI("In mpeg_info, top of routine, ld_frame: %d", ld_frame);

	unsigned int index = 0;
	Uint16 mpeg_frame = 0;	// which mpeg frame to seek (assuming mpeg and disc have same FPS)
	filename = "";	// blank 'filename' means error, so we default to this condition for safety reasons
	
	// find the mpeg file that has the LD frame inside of it
	LOGI("In mpeg_info, before framefile calculation, ld_frame: %d  index+1: %d  m_file_index: %d  m_mpeginfo[index+1]: %d", ld_frame, index+1, m_file_index, m_mpeginfo[index+1].frame);
	while ((index+1 < m_file_index) && (ld_frame >= m_mpeginfo[index+1].frame))
	{
		index = index + 1;
	}
	LOGI("In mpeg_info, after framefile calculation, index: %d", index);

	// make sure that the frame they've requested comes after the first frame in our framefile
	if (ld_frame >= m_mpeginfo[index].frame)
	{
		// make sure a filename exists (when can this ever fail? verify!)
		if (m_mpeginfo[index].name != "")
		{
			filename = m_mpeginfo[index].name;
			mpeg_frame = (Uint16) (ld_frame - m_mpeginfo[index].frame);
			m_cur_ldframe_offset = m_mpeginfo[index].frame;
		}
		else
		{
			printline("VLDP error, no filename found");
			mpeg_frame = 0;
		}
	}
	// else frame is out of range ...
	
	LOGI("In mpeg_info, bottom of routine, ld_frame: %d  filename: %s", ld_frame, filename.c_str());
	return(mpeg_frame);
}

////////////////////////////////////////////////////////////////////////////////////////

// puts the yuv_callback into a blocking state
// This is necessary if the gamevid ever becomes invalid for a period of time (ie it gets free'd and re-allocated in seektest)
// timeout is how many ms to wait before giving up
// returns true if it got the lock or false if it couldn't get a lock
bool ldp_vldp::lock_overlay(Uint32 timeout)
{
	bool bRes = false;

	if (g_vldp_info)
	{
		bRes = g_vldp_info->lock(timeout) == VLDP_TRUE;
	}
	// else g_vldp_info is NULL which means the init function hasn't been called yet probably

	return bRes;
}

// releases the yuv_callback from its blocking state
bool ldp_vldp::unlock_overlay(Uint32 timeout)
{
/*
	Uint32 time = refresh_ms_time();
	
	mutex_lock_request = false;
	
	// sleep until the yuv callback acknowledges that it has control once again
	while (mutex_lock_acknowledge)
	{
		SDL_Delay(0);
		
		// if we've timed out
		if (elapsed_ms_time(time) > timeout)
		{
			printline("LDP_VLDP : unlock_overlay timed out while trying to unlock");
			break;
		}
	}
	
	return (!mutex_lock_acknowledge);
	*/

	return (g_vldp_info->unlock(timeout) == VLDP_TRUE);
}

bool ldp_vldp::parse_framefile(const char *pszInBuf, const char *pszFramefileFullPath,
							   string &sMpegPath, struct fileframes *pFrames, unsigned int &frame_idx, unsigned int max_frames, string &err_msg)
{
	bool result = false;
	const char *pszPtr = pszInBuf;
	unsigned int line_number = 0;	// for debugging purposes
	char ch = 0;
	
	frame_idx = 0;
	err_msg = "";
	
	// read absolute or relative directory
	pszPtr = read_line(pszPtr, sMpegPath);
	
	// if there are no additional lines
	if (!pszPtr)
	{
		// if there is at least 1 line
		if (sMpegPath.size() > 0)
		{
			err_msg = "Framefile only has 1 line in it. Framefiles must have at least 2 lines in it.";
		}
		else err_msg = "Framefile appears to be empty. Framefile must have at least 2 lines in it.";
		return false;	// normally I only like to have 1 return per function, but this is a good spot to return..
	}
	
	++line_number;	// if we get this far, it means we've read our first line
	
	// If sMpegPath is NOT an absolute path (doesn't begin with a unix '/' or have the second char as a win32 ':')
	//  then we want to use the framefile's path for convenience purposes
	// (this should be win32 and unix compatible)
	if ((sMpegPath[0] != '/') && (sMpegPath[0] != '\\') && (sMpegPath[1] != ':'))
	{
		string path = "";
		
		// try to isolate the path of the framefile
		if (get_path_of_file(pszFramefileFullPath, path))
		{
			// put the path of the framefile in front of the relative path of the mpeg files
			// This will allow people to move the location of their mpegs around to
			// different directories without changing the framefile at all.
			// For example, if the framefile and the mpegs are in the same directory,
			// then the framefile's first line could be "./"
			sMpegPath = path + sMpegPath;
		}
	}
	// else it is an absolute path, so we ignore the framefile's path
	
	string s = "";
	// convert all \'s to /'s to be more unix friendly (doesn't hurt win32)
	for (unsigned int i = 0; i < sMpegPath.length(); i++)
	{
		ch = sMpegPath[i];
		if (ch == '\\') ch = '/';
		s += ch;
	}
	sMpegPath = s;
	
	// Clean up after the user if they didn't end the path with a '/' character
	if (ch != '/')
	{
		sMpegPath += "/";	// windows will accept / as well as \, so we're ok here
	}
	
	string word = "", remaining = "";
	result = true;	// from this point, we should assume success
	Sint32 frame = 0;
	
	// parse through entire file
	while (pszPtr != NULL)
	{
		pszPtr = read_line(pszPtr, s);	// read in a line
		++line_number;
		
		// if we can find the first word (frame #)
		if (find_word(s.c_str(), word, remaining))
		{
			// check for overflow
			if (frame_idx >= max_frames)
			{
				err_msg = "Framefile has too many entries in it."
					" You can increase the value of MAX_MPEG_FILES and recompile.";
				result = false;
				break;
			}

			frame = numstr::ToInt32(word.c_str());	// this should work, even with whitespace after it

			// If frame is valid AND we are able to find the name of the 
			// (a non-integer will be converted to 0, so we need to make sure it really is supposed to be 0)
			if (((frame != 0) || (word == "0"))
				&& find_word(remaining.c_str(), word, remaining))
			{
				pFrames[frame_idx].frame = (Sint32) frame;
				pFrames[frame_idx].name = word;
				++frame_idx;
			}
			else
			{
				// This error has been stumping self-proclaimed "experienced emu'ers"
				// so I am going to extra effort to make it clear to them what the
				// problem is.
				err_msg = "Expected a number followed by a string, but on line " +
					numstr::ToStr(line_number) + ", found this: " + s + "(";
				// print hex values of bad string to make troubleshooting easier
				for (size_t idx = 0; idx < s.size(); idx++)
				{
					err_msg += "0x" + numstr::ToStr(s[idx], 16) + " ";
				}
				
				err_msg += ")";
				result = false;
				break;
			}
		}
		// else it is probably just an empty line, so we can ignore it
		
	} // end while file hasn't been completely parsed
	
	// if we ended up with no entries AND didn't get any error, then the framefile is bad
	if ((frame_idx == 0) && (result == true))
	{
		err_msg = "Framefile appears to not have any entries in it.";
		result = false;
	}
	
	return result;
}


// RJS HERE - prepare frame
//******************************************************************************
//******************************************************************************
int prepare_frame_callback_with_overlay(struct yuv_buf *src)
{
	// LOGI("daphne-libretro: In prepare_frame_callback_with_overlay, top of routine.");
	int result = VLDP_FALSE;
	
	void * g_hw_overlay_pixels	= NULL;
	SDL_Rect g_hw_overlay_rect	= { 0, 0, 0, 0 };
	// v0.01 int nPitch = DAPHNE_VIDEO_W * DAPHNE_VIDEO_ByPP;
	int nPitch = 0;

	SDL_SW_YUVTexture * sw_overlay = NULL;
	sw_overlay = get_vb_next_usable(NULL);
	if (sw_overlay)
	{
		g_hw_overlay_pixels = sw_overlay->pixels;
		g_hw_overlay_rect.w = sw_overlay->w;
		g_hw_overlay_rect.h = sw_overlay->h;
		nPitch = sw_overlay->w * SDL_BYTESPERPIXEL(sw_overlay->format);
	}

	{
		// 20xx.xx.xx - RJS - Since I haven't dived all the way through the system, I'm pretty sure this is 
		// where the LDP thread will get the Main Threads "screen".  Since I'm unsure, if I use
		// SWITCH below, that's where I switched from Texture back to origional Surface.  Here goes . . .

		// if (sw_overlay) LOGI("daphne-libretro: In prepare_frame_callback_with_overlay, before get_finished_video_overlay.  g_game: %d  pitch: %d ByPP: %d", (int) g_game, nPitch, SDL_BYTESPERPIXEL(sw_overlay->format));
		// else  LOGI("daphne-libretro: In prepare_frame_callback_with_overlay, before get_finished_video_overlay.  g_game: %d  pitch: %d sw_overlay: NULLd", (int)g_game, nPitch);

		SDL_Surface *gamevid = g_game->get_finished_video_overlay();	// This could change at any time (double buffering, for example)
		if (gamevid == NULL)
		{
			// LOGI("daphne-libretro: In prepare_frame_callback_with_overlay, gamevid is NULL, you'll likely get a blank screen.");
			return result;
		}
		// LOGI("daphne-libretro: In prepare_frame_callback_with_overlay, gamevid Surface data.  h: %d  w: %d  pitch: %d  addr: %d", gamevid->h, gamevid->w, gamevid->pitch, (int)gamevid->pixels);
		// LOGI("daphne-libretro: In prepare_frame_callback_with_overlay, gamevid is not NULL.");

		if (nPitch == 0) nPitch = gamevid->w * SDL_BYTESPERPIXEL(gamevid->format->format);

		Uint8 * gamevid_pixels = (Uint8 *)gamevid->pixels;
		// LOGI("daphne-libretro: In prepare_frame_callback_with_overlay, before overlay size check.  gamevid->w: %d  Lshifted 1: %d  g_hw_overlay_rect.w: %d", gamevid->w, gamevid->w << 1, g_hw_overlay_rect.w);

		if ((gamevid->w << 1) == g_hw_overlay_rect.w)
		{
			// adjust for vertical offset
			// We use _half_ of the requested vertical offset because the mpeg video is twice
			// the size of the overlay
			// LOGI("daphne-libretro: In prepare_frame_callback_with_overlay, before adjusting for vertical offset.  gamevid->w: %d  g_vertical_offset: %d  g_vertical_stretch: %d", gamevid->w, g_vertical_offset, g_vertical_stretch);
			gamevid_pixels = (Uint8 *) gamevid_pixels - (gamevid->w * (g_vertical_offset - g_vertical_stretch));
			
			unsigned int row = 0;
			unsigned int col = 0;

			Uint32 w_double = g_hw_overlay_rect.w << 1;	// twice the overlay width, to avoid calculating this more than once
			Uint32 h_half = g_hw_overlay_rect.h >> 1;	// half of the overlay height, to avoid calculating this more than once
			
			t_yuv_color* yuv_palette = get_yuv_palette();
			unsigned int channel0_pitch = nPitch;	// this val gets used a lot so we put it into a var

			Uint8 *dst_ptr;
			dst_ptr = (Uint8 *)g_hw_overlay_pixels;

			Uint8 *Y	= (Uint8 *) src->Y;
			Uint8 *Y2	= (Uint8 *) src->Y + g_hw_overlay_rect.w;
			Uint8 *U	= (Uint8 *) src->U;
			Uint8 *V	= (Uint8 *) src->V;

			// if letterbox removal is active, shift video down to compensate
			for (unsigned int skip = 0; skip < g_vertical_stretch; skip += 2)
			{
				Y	+= (g_hw_overlay_rect.w * 4);
				Y2	+= (g_hw_overlay_rect.w * 4);
				U	+= g_hw_overlay_rect.w;
				V	+= g_hw_overlay_rect.w;
			}
			
			// do 2 rows at a time
			for (row = 0; row < h_half; row++)
			{
				// calculate this here to avoid calculating too often
				int adjusted_row = ((int) row) - g_vertical_offset;
				bool row_in_range = ((adjusted_row >= 0) && (adjusted_row < gamevid->h));
				t_yuv_color *palette = NULL;
				
				// do 4 bytes at a time, for twice the width of the overlay since we're using YUY2
				for (col = 0; col < w_double; col += 4)
				{
					// if we can safely draw from the video overlay
					if (row_in_range) palette = &yuv_palette[*gamevid_pixels];
					
					// If we are out of range, OR if the current color is transparent,
					//  then draw the mpeg video pixel instead of the video overlay
					// (if palette is NULL, the compiler shouldn't try to dereference palette->transparent)
					if ((palette == NULL) || palette->transparent)
					{
						unsigned int Y_chunk = *((Uint16 *) Y);
						unsigned int Y2_chunk = *((Uint16 *) Y2);
						unsigned int V_chunk = *V;
						unsigned int U_chunk = *U;
						
#if SDL_BYTEORDER == SDL_LIL_ENDIAN
						//Little-Endian (Intel)
						*((Uint32 *) (g_line_buf + col)) = (Y_chunk & 0xFF) | (U_chunk << 8) |
							((Y_chunk & 0xFF00) << 8) | (V_chunk << 24);
						*((Uint32 *) (g_line_buf2 + col)) = (Y2_chunk & 0xFF) | (U_chunk << 8) |
							((Y2_chunk & 0xFF00) << 8) | (V_chunk << 24);
#else						
						//Big-Endian (Mac)			
						*((Uint32 *) (g_line_buf + col)) = ((Y_chunk & 0xFF00) << 16) | ((U_chunk) << 16) |
							((Y_chunk & 0xFF) << 8) | (V_chunk);
						*((Uint32 *) (g_line_buf2 + col)) = ((Y2_chunk & 0xFF00) << 16) | ((U_chunk) << 16) |
							((Y2_chunk & 0xFF) << 8) | (V_chunk);												
#endif
					}
					
					// if we have an overlay pixel to be drawn
					else
					{
#if SDL_BYTEORDER == SDL_LIL_ENDIAN							
						//Little-Endian (Intel)
						*((Uint32 *) (g_line_buf + col)) = 
							*((Uint32 *) (g_line_buf2 + col)) = palette->y | (palette->u << 8)
							| (palette->y << 16) | (palette->v << 24);						
#else					
						//Big-Endian (Mac)
						*((Uint32 *) (g_line_buf + col)) = 
							*((Uint32 *) (g_line_buf2 + col)) = (palette->y << 24) | (palette->u << 16)
							| (palette->y << 8) | (palette->v);
#endif
						
					}
					Y += 2;
					Y2 += 2;
					U++;
					V++;
					gamevid_pixels++;
				}

				// if we're not doing scanlines
				if (!(g_filter_type & FILTER_SCANLINES))
				{
					// no filtering at all
					if (!(g_filter_type & FILTER_BLEND))
					{
						memcpy(dst_ptr, g_line_buf, (g_hw_overlay_rect.w << 1));
						memcpy(dst_ptr + channel0_pitch, g_line_buf2, (g_hw_overlay_rect.w << 1));
					}
					else
					{
						g_blend_func();	// blend the two lines into g_line_buf3
						// this won't affect video overlay because it is already doubled in size anyway
						memcpy(dst_ptr, g_line_buf3, (g_hw_overlay_rect.w << 1));
						memcpy(dst_ptr + channel0_pitch, g_line_buf3, (g_hw_overlay_rect.w << 1));
					}
				}
				// if we're doing scanlines
				else
				{
					// do a black YUY2 line (the first line should be black to workaround nvidia bug)
					for (int i = 0; i < (g_hw_overlay_rect.w << 1); i += 4)
					{
						*((Uint32 *) (dst_ptr + i)) = YUY2_BLACK;	// this value is black in YUY2 mode
					}
					
					if (g_filter_type & FILTER_BLEND)
					{
						g_blend_func();	// blend the two lines into g_line_buf3
						// this won't affect video overlay because it is already doubled in size anyway
						memcpy(dst_ptr + channel0_pitch, g_line_buf3, (g_hw_overlay_rect.w << 1));
					}
					else
					{
						memcpy(dst_ptr + channel0_pitch, g_line_buf, (g_hw_overlay_rect.w << 1));	// this could be g_line_buf2 also
					}
				}

				dst_ptr += (channel0_pitch << 1);	// we've done 2 rows, so skip a row
				Y += g_hw_overlay_rect.w;	// we've done 2 vertical Y pixels, so skip a row
				Y2 += g_hw_overlay_rect.w;
			}	
			
			// if we've been instructed to take a screenshot, do so now that the overlay is in place
			if (g_take_screenshot)
			{
				g_take_screenshot = false;
				// 2017.02.14 - RJS REMOVE - changes back from apk texutres to surfaces
				// APK take_screenshot(g_hw_overlay);
			}
		} // end if sanity check passed
		
		// if sanity check failed
		else
		{
			static bool warned = false;
			
			// newbies might appreciate knowing why their video overlay isn't working, so
			// let's give them some instruction in the logfile.  We only want to print this once
			// to avoid spamming, hence the 'warned' boolean
			if (!warned)
			{
				// if the game does not dynamically reallocate its overlay size (most of them don't)
				if (!g_game->is_overlay_size_dynamic())
				{
					char s[81];
					printline("WARNING : Your MPEG doesn't match your video overlay's resolution.");
					printline("Video overlay will not work!");
					sprintf(s, "Your MPEG's size is %d x %d, and needs to be %d x %d", g_hw_overlay_rect.w, g_hw_overlay_rect.h, (gamevid->w << 1), (gamevid->h << 1));
					printline(s);
				}
				// else, there is no problem at all, so don't alarm the user
				warned = true;
			}
			
			if (g_game->is_overlay_size_dynamic())
			{
				// MPEG size has changed, so drop a hint to any game
				// that resizes dynamically.
				g_game->set_video_overlay_needs_update(true);
			}
		} // end sanity check
		
		result = VLDP_TRUE;	// we were successful (we return successful even if overlay part failed because we want to render _something_)
	} // end if locking the overlay was successful
	
	// LOGI("daphne-libretro: In prepare_frame_callback_with_overlay, bottom of routine.");
	return result;
}

//******************************************************************************
//******************************************************************************
int prepare_frame_callback_without_overlay(struct yuv_buf *buf)
{
	int result = VLDP_FALSE;
	
	// if locking the video overlay is successful
	void * g_hw_overlay_pixels = NULL;
	int nPitch = 0;

	SDL_SW_YUVTexture * sw_overlay = NULL;
	sw_overlay = get_vb_next_usable(NULL);

	if (sw_overlay)
	{
		g_hw_overlay_pixels = sw_overlay->pixels;

		int overlay_w = 0;
		int overlay_h = 0;
		overlay_h = sw_overlay->h;
		overlay_w = sw_overlay->w;

		buf2overlay_YUY2((Uint8 *)g_hw_overlay_pixels, nPitch, overlay_h, overlay_w, buf);

		// if we've been instructed to take a screenshot, do so now that the overlay is in place
		if (g_take_screenshot)
		{
			g_take_screenshot = false;
			// 2017.02.14 - RJS REMOVE - changes back from apk texutres to surfaces
			// APK take_screenshot(g_hw_overlay);
		}

		result = VLDP_TRUE;
	}
	
	return result;
}

//*********************************************************************************************************************************
//*********************************************************************************************************************************
// displays the frame as fast as possible
// RJS NOTE - *** video frames ***
extern "C" {
	extern DECLSPEC int SDLCALL SDL_RJS_SW_CopyYUVToRGB(SDL_SW_YUVTexture * swdata, const SDL_Rect * srcrect, Uint32 target_format, int w, int h, void *pixels, int pitch);
}
// extern retro_video_refresh_t cb_videorefresh;
// extern jboolean retro_run_once;

void display_frame_callback(struct yuv_buf *buf)
{
	// 2017.08.30 - RJS - This should be what's in the APK version.  Unfortunatly, my solutions are all over
	// the fucking place but I believe this is it.  And yes, textH is meaningless.
	// APK int textH = 0;
	// APK SDL_QueryTexture(g_hw_overlay, NULL, NULL, NULL, &textH);
	// APK int nRC = 0;
	// APK nRC = SDL_RenderClear(gp_ldp_renderer);
	// APK nRC = SDL_RenderCopy(gp_ldp_renderer, g_hw_overlay, NULL, NULL);
	// APK SDL_RenderPresent(gp_ldp_renderer);

	// 2017.08.30 - RJS - This is direct from Daphne source which is SDL1 unlike this source which is SDL2.
	// SDL1 SDL_DisplayYUVOverlay(g_hw_overlay, g_screen_clip_rect);

	// 2017.08.24 - RJS - Going to mimic SDL_SW_CopyYUVToRGB to allow a memory buffer.  _RJS_ should be removed once
	// proven.  Out of pure laziness, "texture" will mean the buffer in these new routines.  Also, not sure if this needs to be
	// added to dynapis.  Function is: SDL_RJS_SW_CopyYUVToRGB

	// LOGI("daphne-libretro: In display_frame_callback, top of routine.  buf: %d", (int)buf);

	int vb_ndx = -1;
	SDL_SW_YUVTexture * sw_overlay = NULL;
	sw_overlay = get_vb_filling(&vb_ndx);

	// I'll bet sw_ovelay is NULL here.  Testing code.
	// LOGI("daphne-libretro: In display_frame_callback, after get_vb_filling.  sw_overlay: %d  buf: %d", (int)sw_overlay, (int)buf);
	if (sw_overlay == NULL)
	{
		LOGI("In display_frame_callback, sw_overlay is NULL!  Should be very, very rare.  Exiting.");
		return;
	}

	SDL_Rect full_rect;
	full_rect.x = 0;
	full_rect.y = 0;
	full_rect.w = sw_overlay->w;
	full_rect.h = sw_overlay->h;

	// LOGI("daphne-libretro: In display_frame_callback, before SDL_RJS_SW_CopyYUVToRGB.  sw_overlay: %d  w: %d  h: %d", (int)sw_overlay, sw_overlay->w, sw_overlay->h);
	SDL_RJS_SW_CopyYUVToRGB(sw_overlay, &full_rect, sw_overlay->target_format, sw_overlay->w, sw_overlay->h, sw_overlay->planes[0], sw_overlay->pitches[0]);

	// LOGI("daphne-libretro: In display_frame_callback, done FILLING.");
	set_vb_filling_done(vb_ndx);

	// RJS HERE - vidbuffer
	/*
	if ((cb_videorefresh) && (retro_run_once))
	{
		// LOGI("daphne-libretro: In display_frame_callback, before cb_videorefresh.  g_hw_overlay: %d  w: %d  h: %d  pixels: %d", (int)g_hw_overlay, g_hw_overlay->w, g_hw_overlay->h, (int) g_hw_overlay->pixels);
		LOGI("daphne-libretro: In %s, writing VIDEO buffer.", __func__);
		cb_videorefresh(g_hw_overlay->pixels, g_hw_overlay->w, g_hw_overlay->h, g_hw_overlay->w * DAPHNE_VIDEO_ByPP);
		// LOGI("daphne-libretro: In display_frame_callback, after cb_videorefresh.");
	}
	*/
}

//*********************************************************************************************************************************
//*********************************************************************************************************************************
// This function converts the YV12-formatted 'src' to a YUY2-formatted overlay (which Xbox-Daphne may be using)
// copies the contents of src into dst
// assumes destination overlay is locked and *IMPORTANT* assumes src and dst are the same resolution
// RJS CHANGE - Convert SDL_Overlay
// void buf2overlay_YUY2(SDL_Overlay *dst, struct yuv_buf *src)
void buf2overlay_YUY2(Uint8 *out_pixels, Uint16 in_pitch, int in_h, int in_w, struct yuv_buf *src)
{
	// RJS START - from overlays to textures, remember this assumes an already locked texture
	SDL1_Overlay dst_mem;
	SDL1_Overlay *dst;
	dst = &dst_mem;
	dst->h			= in_h;
	dst->w			= in_w;
	dst->pitches	= in_pitch;
	dst->pixels		= out_pixels;
	// RJS END
		
	unsigned int channel0_pitch = dst->pitches;
	Uint8 *dst_ptr = dst->pixels;			
	Uint8 *Y = (Uint8 *) src->Y;
	Uint8 *Y2 = ((Uint8 *) src->Y) + dst->w;
	Uint8 *U = (Uint8 *) src->U;
	Uint8 *V = (Uint8 *) src->V;
	int col, row;
	
	// do 2 rows at a time
	for (row = 0; row < (dst->h >> 1); row++)
	{
		// do 4 bytes at a time, but only iterate for w_half
		for (col = 0; col < (dst->w << 1); col += 4)
		{
			unsigned int Y_chunk = *((Uint16 *) Y);
			unsigned int Y2_chunk = *((Uint16 *) Y2);
			unsigned int V_chunk = *V;
			unsigned int U_chunk = *U;
			
#if SDL_BYTEORDER == SDL_LIL_ENDIAN
			
			//Little-Endian (PC)
			*((Uint32 *) (g_line_buf + col)) = (Y_chunk & 0xFF) | (U_chunk << 8) |
				((Y_chunk & 0xFF00) << 8) | (V_chunk << 24);
			*((Uint32 *) (g_line_buf2 + col)) = (Y2_chunk & 0xFF) | (U_chunk << 8) |
				((Y2_chunk & 0xFF00) << 8) | (V_chunk << 24);
			
#else
			
			//Big-Endian (Mac)
			*((Uint32 *) (g_line_buf + col)) = ((Y_chunk & 0xFF00) << 16) | ((U_chunk) << 16) |
				((Y_chunk & 0xFF) << 8) | (V_chunk);
			*((Uint32 *) (g_line_buf2 + col)) = ((Y2_chunk & 0xFF00) << 16) | ((U_chunk) << 16) |
				((Y2_chunk & 0xFF) << 8) | (V_chunk);	
			
#endif
			
			Y += 2;
			Y2 += 2;
			U++;
			V++;
		}
		
		// if we're not doing scanlines
		if (!(g_filter_type & FILTER_SCANLINES))
		{
			// no filtering at all
			if (!(g_filter_type & FILTER_BLEND))
			{
				memcpy(dst_ptr, g_line_buf, (dst->w << 1));
				memcpy(dst_ptr + channel0_pitch, g_line_buf2, (dst->w << 1));
			}
			else
			{
				g_blend_func();	// blend the two lines into g_line_buf3
				// this won't affect video overlay because it is already doubled in size anyway
				memcpy(dst_ptr, g_line_buf3, (dst->w << 1));
				memcpy(dst_ptr + channel0_pitch, g_line_buf3, (dst->w << 1));
			}
		}
		
		// if we're doing scanlines
		else
		{
			// do a black YUY2 line first
			// (on nvidia it makes the top line too bright if the black line doesn't come first!)
			for (int i = 0; i < (dst->w << 1); i+=4)
			{
				*((Uint32 *) (dst_ptr + i)) = YUY2_BLACK;	// this value is black in YUY2 mode
			}
			
			if (g_filter_type & FILTER_BLEND)
			{
				g_blend_func();	// blend the two lines into g_line_buf3
				// this won't affect video overlay because it is already doubled in size anyway
				memcpy(dst_ptr + channel0_pitch, g_line_buf3, (dst->w << 1));
			}
			else
			{
				memcpy(dst_ptr + channel0_pitch, g_line_buf, (dst->w << 1));	// this could also be g_line_buf2
			}			
		}
		
		dst_ptr += (channel0_pitch << 1);	// we've done 2 rows, so skip a row
		Y += dst->w;	// we've done 2 vertical Y pixels, so skip a row
		Y2 += dst->w;
	}
}

///////////////////

Uint32 g_parse_start_time = 0;	// when mpeg parsing began approximately ...
double g_parse_start_percentage = 0.0;	// the first percentage report we received ...
bool g_parsed = false;	// whether we've received any data at all ...

// this should be called from parent thread
void update_parse_meter()
{
	// if we have some data collected
	if (g_dPercentComplete01 >= 0)
	{
		double elapsed_s = 0;	// how many seconds have elapsed since we began this ...
		double total_s = 0;	// how many seconds the entire operation is likely to take
		double remaining_s = 0;	// how many seconds are remaining
		
		double percent_complete = g_dPercentComplete01 * 100.0;	// switch it from [0-1] to [0-100]

		elapsed_s = (elapsed_ms_time(g_parse_start_time)) * 0.001;	// compute elapsed seconds
		double percentage_accomplished = percent_complete - g_parse_start_percentage;	// how much 'percentage' points we've accomplished

		total_s = (elapsed_s * 100.0) / percentage_accomplished;	// 100 signifies 100 percent (I got this equation by doing some algebra on paper)

		// as long as percent_complete is always 100 or lower, total_s will always be >= elapsed_s, so no checking necessary here
		remaining_s = total_s - elapsed_s;

		SDL_Surface *screen = get_screen_blitter();	// the main screen that we can draw on ...

		SDL_FillRect(screen, NULL, 0);	// erase previous stuff on the screen blitter

		// if we have some progress to report ...
		if (remaining_s > 0)
		{
			char s[160];
			int half_h = screen->h >> 1;	// calculations to center message on screen ...
			int half_w = screen->w >> 1;
			sprintf(s, "Video parsing is %02.f percent complete, %02.f seconds remaining.\n", percent_complete, remaining_s);
			SDLDrawText(s, screen, FONT_SMALL, (half_w-((strlen(s)/2)*FONT_SMALL_W)), half_h-FONT_SMALL_H);

// now draw a little graph thing ...
SDL_Rect clip = screen->clip_rect;
const int THICKNESS = 10;	// how thick our little status bar will be
clip.y = (clip.h - THICKNESS) / 2;	// where to start our little status bar
clip.h = THICKNESS;
clip.y += FONT_SMALL_H + 5;	// give us some padding

SDL_FillRect(screen, &clip, SDL_MapRGB(screen->format, 255, 255, 255));	// draw a white bar across the screen ...

clip.x++;	// move left boundary in 1 pixel
clip.y++;	// move upper boundary down 1 pixel
clip.w -= 2;	// move right boundary in 1 pixel
clip.h -= 2;	// move lower boundary in 1 pixel

SDL_FillRect(screen, &clip, SDL_MapRGB(screen->format, 0, 0, 0));	// fill inside with black

clip.w = (Uint16)((screen->w * g_dPercentComplete01) + 0.5) - 1;	// compute how wide our progress bar should be (-1 to take into account left pixel border)

// go from full red (hardly complete) to full green (fully complete)
SDL_FillRect(screen, &clip, SDL_MapRGB(screen->format,
	(Uint8)(255 * (1.0 - g_dPercentComplete01)),
	(Uint8)(255 * g_dPercentComplete01),
	0));
		}
	}
}

// percent_complete is between 0 and 1
// a negative value means that we are starting to parse a new file NOW
void report_parse_progress_callback(double percent_complete_01)
{
	g_dPercentComplete01 = percent_complete_01;
	g_bGotParseUpdate = true;
	g_parsed = true;	// so we can know to re-create the overlay

	// if a new parse is starting
	if (percent_complete_01 < 0)
	{
		// NOTE : this would be a good place to automatically free the yuv overlay
		// BUT it was causing crashes... free_yuv_overlay here if you find any compatibility problems on other platforms
		g_parse_start_time = refresh_ms_time();
		g_parse_start_percentage = 0;
	}
}

///////////////////
extern unsigned int g_draw_width, g_draw_height;

// this always gets called before the draw_callback and always after report_parse_progress callback
void report_mpeg_dimensions_callback(int width, int height)
{
	LOGI("daphne-libretro: In report_mpeg_dimensions_callback, top of routine.  w: %d  h: %d", width, height);

	unsigned int uTimer = refresh_ms_time();

	// if we haven't blitted this information to the screen, then wait for other thread to do so before we continue ...
	while ((g_bGotParseUpdate) && (elapsed_ms_time(uTimer) < 3000))
	{
		make_delay(1);
	}

	LOGI("daphne-libretro: In report_mpeg_dimensions_callback, clip_rect_xy: %d %d  clip_rect_wh: %d %d  draw_wh: %d %d", g_screen_clip_rect->x, g_screen_clip_rect->y, g_screen_clip_rect->w, g_screen_clip_rect->h, g_draw_width, g_draw_height);

	// if draw width is less than the screen width
	if (g_draw_width < (unsigned int) g_screen_clip_rect->w)
	{
		// center horizontally
		unsigned int uDiff = g_screen_clip_rect->w - g_draw_width;
		g_screen_clip_rect->x += (uDiff / 2);
		g_screen_clip_rect->w = g_draw_width;
	}

	// if draw height is less than the screen height
	if (g_draw_height < (unsigned int) g_screen_clip_rect->h)
	{
		// center vertically
		unsigned int uDiff = g_screen_clip_rect->h - g_draw_height;
		g_screen_clip_rect->y += (uDiff / 2);
		g_screen_clip_rect->h = g_draw_height;
	}

	// if we drew some stuff on the screen, then we need to free the overlay and re-create it
	if (g_parsed)
	{
		free_yuv_overlay();
		g_parsed = false;
	}

	// if an overlay exists, but its dimensions are wrong, we need to de-allocate it
	int g_hw_overlay_w = 0;
	int g_hw_overlay_h = 0;
	if (g_hw_overlay[0].video_buffer != NULL)
	{
		// 2017.02.14 - RJS CHANGE - changes back from apk texutres to surfaces
		// APK SDL_QueryTexture(g_hw_overlay, NULL, NULL, &g_hw_overlay_w, &g_hw_overlay_h);
		g_hw_overlay_h = g_hw_overlay[0].video_buffer->h;
		g_hw_overlay_w = g_hw_overlay[0].video_buffer->w;

		if ((g_hw_overlay_w != width) || (g_hw_overlay_h != height))
		{
			free_yuv_overlay();
		}
	}

	// blitting is not allowed once we create the YUV overlay ...
	g_ldp->set_blitting_allowed(false);

	// if our overlay has been de-allocated, or if we never had one to begin with ... then allocate it now
	if (g_hw_overlay[0].video_buffer == NULL)
	{
		// create overlay, taking into account any letterbox removal we're doing
		// (*4 because our pixels are *2 the height of the graphics, AND we're doing it at the top and bottom)

		// 201x.xx.xx - RJS - SDL2 has gotten rid of overlays and now uses textures
		// ORIG g_hw_overlay = SDL_CreateYUVOverlay (width, height - (g_vertical_stretch * 4), SDL_YUY2_OVERLAY, get_screen());
		// APK g_hw_overlay = SDL_CreateTexture(gp_ldp_renderer, SDL_PIXELFORMAT_YUY2, SDL_TEXTUREACCESS_STREAMING, width, height - (g_vertical_stretch * 4));
		//
		// 2017.02.14 - RJS - changing the apk version back to surfaces since we don't have a renderer
		// 2017.06.22 - RJS - So while YUV isn't an RGB surface, the "withFormat" routine will take in a pixel format for YUV.  Well, that doesn't work and
		// SDL2 doesn't let you do surfaces that are not RGB.  You can do textures which means you need a SDL_WINDOW and SDL_RENDERER which are to heavy weight.
		// If for some reason we need to go back to window/renderer then the "APK" version above is a good starting point.
		// SURFACE g_hw_overlay = SDL_CreateRGBSurfaceWithFormat(0, width, height - (g_vertical_stretch * 4), DAPHNE_VIDEO_ByPP, SDL_PIXELFORMAT_YUY2);
		// SURFACE LOGI("daphne-libretro: In report_mpeg_dimensions_callback, SDL_CreateRGBSurfaceWithFormat, with  W: %d  H: %d  BPP: %d  FORMAT: %d", width, height, DAPHNE_VIDEO_ByPP, SDL_PIXELFORMAT_YUY2);
		// v0.01 g_hw_overlay = SDL_RJS_SW_CreateYUVBuffer(SDL_PIXELFORMAT_YUY2, SDL_PIXELFORMAT_RGB565, DAPHNE_VIDEO_W, DAPHNE_VIDEO_H);
		if (! initialize_vb(SDL_PIXELFORMAT_YUY2, SDL_PIXELFORMAT_RGB565, width, height - (g_vertical_stretch * 4)))
		{
			LOGI("daphne-libretro: In report_mpeg_dimensions_callback, g_hw_overlay allocate FAILED!?");
			printline("ldp-vldp.cpp : YUV overlay creation failed!");
			set_quitflag();
		} else {
			LOGI("daphne-libretro: In report_mpeg_dimensions_callback, g_hw_overlay allocated.");
			// 2017.09.21 - RJS - used to tell if rendering was SW or HW, this is left to RA
			// if overlay was successfully created, then indicate in the log whether it is HW accelerated or not
			string msg = "YUV overlay is done in RetroArch.";
			printline(msg.c_str());	// add it to the daphne_log.txt
		}
		
		// we don't need to check whether these buffers have been allocated or not because this is checked for earlier
		// when we check to see if g_hw_overlay has been allocated
		LOGI("daphne-libretro: In report_mpeg_dimensions_callback, allocating blank yuv.  w: %d  h: %d", width, height);
		g_blank_yuv_buf.Y_size = width*height;
		g_blank_yuv_buf.Y = MPO_MALLOC(g_blank_yuv_buf.Y_size);
		memset(g_blank_yuv_buf.Y, 0, g_blank_yuv_buf.Y_size);	// blank Y color
		g_blank_yuv_buf.UV_size = g_blank_yuv_buf.Y_size >> 2;
		g_blank_yuv_buf.U = MPO_MALLOC(g_blank_yuv_buf.UV_size);
		memset(g_blank_yuv_buf.U, 127, g_blank_yuv_buf.UV_size);	// blank U color
		g_blank_yuv_buf.V = MPO_MALLOC(g_blank_yuv_buf.UV_size);
		memset(g_blank_yuv_buf.V, 127, g_blank_yuv_buf.UV_size);	// blank V color
		
		// yuy2 needs twice as much space across for lines
		g_line_buf = MPO_MALLOC(width * 2);
		g_line_buf2 = MPO_MALLOC(width * 2);
		g_line_buf3 = MPO_MALLOC(width * 2);
	}
	// else g_hw_overlay exists, so we don't need to re-allocate it
	
	// This is some one-time setup stuff that will be used by the blending functions
	// It will be used by both the prepare w/ overlay and prepare w/o overlay functions
	if (g_filter_type & FILTER_BLEND)
	{
		g_blend_line1 = g_line_buf;
		g_blend_line2 = g_line_buf2;
		g_blend_dest = g_line_buf3;
		// 2017.02.14 - RJS - changes back from apk texutres to surfaces
		// APK SDL_QueryTexture(g_hw_overlay, NULL, NULL, &g_hw_overlay_w, &g_hw_overlay_h);
		// SURFACE g_hw_overlay_h = g_hw_overlay->h;
		// SURFACE g_hw_overlay_w = g_hw_overlay->w;
		g_hw_overlay_h = DAPHNE_VIDEO_H;
		g_hw_overlay_w = DAPHNE_VIDEO_W;

		g_blend_iterations = g_hw_overlay_w << 1;
#ifdef DEBUG
		assert(((g_blend_iterations % 8) == 0) && (g_blend_iterations >= 8));	// blend MMX does 8 bytes at a time
#endif
	}
	LOGI("daphne-libretro: In report_mpeg_dimensions_callback, bottom of routine.");
	
}

void free_yuv_overlay()
{
	teardown_vb();
	
	// free line bufs
	MPO_FREE(g_line_buf);
	MPO_FREE(g_line_buf2);
	MPO_FREE(g_line_buf3);
	
	// free blank buf ...
	MPO_FREE(g_blank_yuv_buf.Y);
	MPO_FREE(g_blank_yuv_buf.U);
	MPO_FREE(g_blank_yuv_buf.V);
}

// makes the laserdisc video black while drawing game's video overlay on top
void blank_overlay()
{
	LOGI("daphne-libretro: In blank_overlay, top of routine.");

	// So the buffers passed into there routines isn't good.  We need to figure out why that is.
	// It's like the buffer is in YUV when it's being displayed.  Should be hw_overlay onto buffer then
	// all the RGB-ified.

	// only do this if the HW overlay has already been allocated

	LOGI("daphne-libretro: In blank_overlay, before if.  video_buffer: %d", (int)g_hw_overlay[0].video_buffer);
	if (g_hw_overlay[0].video_buffer != NULL)
	{
		/*
		memset(g_blank_yuv_buf.Y, 0, g_blank_yuv_buf.Y_size);		// blank Y color
		memset(g_blank_yuv_buf.U, 127, g_blank_yuv_buf.UV_size);	// blank U color
		memset(g_blank_yuv_buf.V, 127, g_blank_yuv_buf.UV_size);	// blank V color
		*/

		LOGI("daphne-libretro: In blank_overlay, before prepare_frame.");
		g_local_info.prepare_frame(&g_blank_yuv_buf);

		LOGI("daphne-libretro: In blank_overlay, before display_frame.");
		g_local_info.display_frame(&g_blank_yuv_buf);
	}

	LOGI("daphne-libretro: In blank_overlay, bottom of routine.");
}
