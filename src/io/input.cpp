/*
 * input.cpp
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

// Handles SDL input functions (low-level keyboard/joystick input)
#ifdef WIN32
#pragma warning (disable:4996)
#endif

#include <time.h>
#include "input.h"
#include "conout.h"
#include "homedir.h"
#include "../video/video.h"
#include "../video/SDL_Console.h"
#include "../daphne.h"
#include "../timer/timer.h"
#include "../game/game.h"
#include "../game/thayers.h"
#include "../ldp-out/ldp.h"
#include "fileparse.h"

#include "../../main_android.h"

#ifdef UNIX
#include <fcntl.h>	// for non-blocking i/o
#endif

#include <queue>	// STL queue for coin queue

using namespace std;

// Win32 doesn't use strcasecmp, it uses stricmp (lame)
#ifdef WIN32
#define strcasecmp stricmp
#endif

const int JOY_AXIS_MID = (int) (32768 * (0.75));		// how far they have to move the joystick before it 'grabs'

SDL_Joystick *G_joystick = NULL;	// pointer to joystick object
bool g_use_joystick = true;	// use a joystick by default
// RJS ADD - invert controls
bool g_invert_joystick = false;
bool g_consoledown = false;			// whether the console is down or not
bool g_alt_pressed = false;	// whether the ALT key is presssed (for ALT-Enter combo)
unsigned int idle_timer; // added by JFA for -idleexit

const double STICKY_COIN_SECONDS = 0.125;	// how many seconds a coin acceptor is forced to be "depressed" and how many seconds it is forced to be "released"
Uint32 g_sticky_coin_cycles = 0;	// STICKY_COIN_SECONDS * get_cpu_hz(0), cannot be calculated statically
queue<struct coin_input> g_coin_queue;	// keeps track of coin input to guarantee that coins don't get missed if the cpu is busy (during seeks for example)
Uint64 g_last_coin_cycle_used = 0;	// the cycle value that our last coin press used

// the ASCII key words that the parser looks at for the key values
// NOTE : these are in a specific order, corresponding to the enum in daphne.h
const char *g_key_names[] = 
{
	"KEY_UP", "KEY_LEFT", "KEY_DOWN", "KEY_RIGHT", "KEY_START1", "KEY_START2", "KEY_BUTTON1",
	"KEY_BUTTON2", "KEY_BUTTON3", "KEY_COIN1", "KEY_COIN2", "KEY_SKILL1", "KEY_SKILL2", 
	"KEY_SKILL3", "KEY_SERVICE", "KEY_TEST", "KEY_RESET", "KEY_SCREENSHOT", "KEY_QUIT", "KEY_PAUSE",
	"KEY_CONSOLE", "KEY_TILT",
	// RJS ADD - coin start switch
	"KEY_COINSTART"
};

// default key assignments, in case .ini file is missing
// Notice each switch can have two keys assigned to it
// NOTE : These are in a specific order, corresponding to the enum in daphne.h
int g_key_defs[SWITCH_COUNT][2] =
{
	// RJS START
	/*
	{ SDLK_UP, SDLK_KP8 },		// up
	{ SDLK_LEFT, SDLK_KP4 },	// left
	{ SDLK_DOWN, SDLK_KP2 },	// down
	{ SDLK_RIGHT,  SDLK_KP6 },	// right
	*/
	{ SDLK_UP, SDLK_KP_8 },		// up
	{ SDLK_LEFT, SDLK_KP_4 },	// left
	{ SDLK_DOWN, SDLK_KP_2 },	// down
	{ SDLK_RIGHT,  SDLK_KP_6 },	// right
	// RJS END
	{ SDLK_1,	0 }, // 1 player start
	{ SDLK_2,	0 }, // 2 player start
	{ SDLK_SPACE, SDLK_LCTRL }, // action button 1
	{ SDLK_LALT,	0 }, // action button 2
	{ SDLK_LSHIFT,	0 }, // action button 3
	{ SDLK_5, SDLK_c }, // coin chute left
	{ SDLK_6, 0 }, // coin chute right
	{ SDLK_KP_DIVIDE, 0 }, // skill easy
	{ SDLK_KP_MULTIPLY, 0 },	// skill medium
	{ SDLK_KP_MINUS, 0 },	// skill hard
	{ SDLK_9, 0 }, // service coin
	{ SDLK_F2, 0 },	// test mode
	{ SDLK_F3, 0 },	// reset cpu
	{ SDLK_F12, SDLK_F11 },	// take screenshot
	{ SDLK_ESCAPE, SDLK_q },	// Quit DAPHNE
	{ SDLK_p, 0 },	// pause game
	{ SDLK_BACKQUOTE, 0 },	// toggle console
	{ SDLK_t, 0 },	// Tilt/Slam switch
	// RJS ADD - coin start; special switch, in Lair allows coin; coin; start 1
	{ 0, 0 }
};

////////////

#ifndef GP2X
// RJS CHANGE START
/*
// added by Russ
// global button mapping array. just hardcoded room for 10 buttons max
int joystick_buttons_map[10] = {
	SWITCH_BUTTON1,	// button 1
	SWITCH_BUTTON2,	// button 2
	SWITCH_BUTTON3,	// button 3
	SWITCH_BUTTON1,	// button 4
	SWITCH_COIN1,		// button 5
	SWITCH_START1,		// button 6
	SWITCH_BUTTON1,	// button 7
	SWITCH_BUTTON1,	// button 8
	SWITCH_BUTTON1,	// button 9
	SWITCH_BUTTON1,	// button 10
};
*/

// Columns 1 and 2 from SDL_gamecontroller.h enum, column 3 from keycode_to_SDL in SDL_sysjoystick.c.
/*
    SDL_CONTROLLER_BUTTON_INVALID		= -1
    SDL_CONTROLLER_BUTTON_A				= 00	= AKEYCODE_BUTTON_A
    SDL_CONTROLLER_BUTTON_B				= 01	= AKEYCODE_BUTTON_B
    SDL_CONTROLLER_BUTTON_X				= 02	= AKEYCODE_BUTTON_X
    SDL_CONTROLLER_BUTTON_Y				= 03	= AKEYCODE_BUTTON_Y
    SDL_CONTROLLER_BUTTON_BACK			= 04	= AKEYCODE_BUTTON_SELECT or AKEYCODE_BACK (NV added)
    SDL_CONTROLLER_BUTTON_GUIDE			= 05	= AKEYCODE_BUTTON_MODE
    SDL_CONTROLLER_BUTTON_START			= 06	= AKEYCODE_BUTTON_START
    SDL_CONTROLLER_BUTTON_LEFTSTICK		= 07	= AKEYCODE_BUTTON_THUMBL
    SDL_CONTROLLER_BUTTON_RIGHTSTICK	= 08	= AKEYCODE_BUTTON_THUMBR
    SDL_CONTROLLER_BUTTON_LEFTSHOULDER	= 09	= AKEYCODE_BUTTON_L1
    SDL_CONTROLLER_BUTTON_RIGHTSHOULDER	= 10	= AKEYCODE_BUTTON_R1
    SDL_CONTROLLER_BUTTON_DPAD_UP		= 11	= AKEYCODE_DPAD_UP
    SDL_CONTROLLER_BUTTON_DPAD_DOWN		= 12	= AKEYCODE_DPAD_DOWN
    SDL_CONTROLLER_BUTTON_DPAD_LEFT		= 13	= AKEYCODE_DPAD_LEFT
    SDL_CONTROLLER_BUTTON_DPAD_RIGHT	= 14	= AKEYCODE_DPAD_RIGHT
    SDL_CONTROLLER_BUTTON_DPAD_CENTER	= 15	= AKEYCODE_DPAD_CENTER	(NV Added)
    SDL_CONTROLLER_BUTTON_SEARCH		= 16	= AKEYCODE_SEARCH		(NV Added)
    SDL_CONTROLLER_BUTTON_MAX			= 17	= AKEYCODE_ (BUTTON_L2, R2, C, Z) (DPAD_CENTER) (BUTTON_1 to 16)  *** unsupported ***
*/

// Column 1 and 2 taken from input.h, other columns are from enum in game.h 
/*
								01		02		03		04		05		06		07		08		09		10		11		12		13		14		15		16		17		18		19		20		21		22		23
								LAIR	LAIR2	ACE		ACE91	CLIFF	GTG		SUPERD	THAYERS	ASTRON	GALAXY	ESH		LAIREU	BADLNDS	STRRIDR	BEGA	INTSTLR	SAE		MACH3	UVT		BADLNDP	DLE1	DLE2	GPWORLD
	SWITCH_UP			= 00	JoyU	xxxxxxx	JoyU	xxxxxxx	JoyU	xxxxxxx	xxxxxxx	xxxxxxx	JoyD	xxxxxxx	xxxxxxx	xxxxxxx	xxxxxxx	xxxxxxx	JoyU	xxxxxxx	xxxxxxx	xxxxxxx	xxxxxxx	xxxxxxx	xxxxxxx	xxxxxxx	
	SWITCH_LEFT			= 01	JoyL			JoyL			JoyL							JoyL											JoyL															JoyL
	SWITCH_DOWN			= 02	JoyD			JoyD			JoyD							JoyU											JoyD															
	SWITCH_RIGHT		= 03	JoyR			JoyR			JoyR							JoyR											JoyR															JoyR
	SWITCH_START1		= 04	1Plr			1Plr			Feet1S1							1Plr											1Plr															Start
	SWITCH_START2		= 05	2Plr			2Plr			Feet2S2							2Plr											2Plr
	SWITCH_BUTTON1		= 06	Sword			Fire			Hand1							Guns											PriBut															Shift
	SWITCH_BUTTON2		= 07									Feet1S1							Missle											SecBut															Gas
	SWITCH_BUTTON3		= 08	Score			Score																							TriBut															Brake
	SWITCH_COIN1		= 09	Coin1			Coin1			Coin1							Coin1											Coin1															Coin1
	SWITCH_COIN2		= 10	Coin2			Coin2			Coin2							Coin2											Coin2															Coin2
	SWITCH_SKILL1		= 11	Cadet			Cadet
	SWITCH_SKILL2 		= 12	Capt			Capt
	SWITCH_SKILL3		= 13	Ace				Ace
	SWITCH_SERVICE		= 14	DiagTgl			DiagTgl			SvcTgl							DiagSvc											DiagSvc															DiagSvc
	SWITCH_TEST			= 15									TestTgl							TestMd																											Test
	SWITCH_RESET		= 16
	SWITCH_SCREENSHOT	= 17
	SWITCH_QUIT			= 18
	SWITCH_PAUSE		= 19
	SWITCH_CONSOLE		= 20
	SWITCH_TILT			= 21									ClBit5?
	SWITCH_COINSTART	= 22	Macro			Macro											Macro
	SWITCH_COUNT		= 23
	SWITCH_NOTHING		= 24
*/

#define MAX_DEFINED_JOYSTICK_BUTTONS	17
#define MAX_DEFINED_GAME_TYPES			24

int joystick_buttons_map_default[MAX_DEFINED_JOYSTICK_BUTTONS] =
{
	SWITCH_BUTTON1,		// button 00
	SWITCH_BUTTON2,		// button 01
	SWITCH_BUTTON3,		// button 02
	SWITCH_BUTTON1,		// button 03
	SWITCH_COIN1,		// button 04
	SWITCH_START1,		// button 05
	SWITCH_BUTTON1,		// button 06
	SWITCH_BUTTON1,		// button 07
	SWITCH_BUTTON1,		// button 08
	SWITCH_BUTTON1,		// button 09
	SWITCH_BUTTON1,		// button 10
	SWITCH_BUTTON1,		// button 11
	SWITCH_BUTTON1,		// button 12
	SWITCH_BUTTON1,		// button 13
	SWITCH_BUTTON1,		// button 14
	SWITCH_BUTTON1,		// button 15
	SWITCH_BUTTON1		// button 16
};

// "Continue Play" button might be missing and would likely be Plr2 (START2).
int joystick_buttons_map_astron[MAX_DEFINED_JOYSTICK_BUTTONS] =
{
						//				Android
	SWITCH_BUTTON1,		// button 00	A			Fire
	SWITCH_BUTTON1,		// button 01	B			Fire
	SWITCH_BUTTON2,		// button 02	X			-
	SWITCH_BUTTON2,		// button 03	Y			-
	SWITCH_COINSTART,	// button 04	SELECT/BACK	Plr1
	SWITCH_NOTHING,		// button 05	MODE		-
	SWITCH_START1,		// button 06	START		Plr1
	SWITCH_COIN1,		// button 07	THUMBL		Coin1
	SWITCH_START1,		// button 08	THUMBR		Plr1
	SWITCH_NOTHING,		// button 09	L1			-
	SWITCH_NOTHING,		// button 10	R1			-
	SWITCH_UP,			// button 11	DPAD_U		U
	SWITCH_DOWN,		// button 12	DPAD_D		D
	SWITCH_LEFT,		// button 13	DPAD_L		L
	SWITCH_RIGHT,		// button 14	DPAD_R		R
	SWITCH_BUTTON1,		// button 15	DPAD_C		Fire
	SWITCH_BUTTON2		// button 16	SEARCH		-
};


// Plr2 button is missing (START2).
int joystick_buttons_map_ace[MAX_DEFINED_JOYSTICK_BUTTONS] =
{
						//				Android
	SWITCH_BUTTON1,		// button 00	A			Fire
	SWITCH_SKILL3,		// button 01	B			Skill: Space Ace
	SWITCH_SKILL1,		// button 02	X			Skill: Cadet
	SWITCH_SKILL2,		// button 03	Y			Skill: Captian
	SWITCH_COINSTART,	// button 04	SELECT/BACK	Plr1
	SWITCH_BUTTON3,		// button 05	MODE		Score SW
	SWITCH_START1,		// button 06	START		Plr1
	SWITCH_COIN1,		// button 07	THUMBL		Coin1
	SWITCH_START1,		// button 08	THUMBR		Plr1
	SWITCH_BUTTON3,		// button 09	L1			Score SW
	SWITCH_BUTTON3,		// button 10	R1			Score SW
	SWITCH_UP,			// button 11	DPAD_U		U
	SWITCH_DOWN,		// button 12	DPAD_D		D
	SWITCH_LEFT,		// button 13	DPAD_L		L
	SWITCH_RIGHT,		// button 14	DPAD_R		R
	SWITCH_BUTTON1,		// button 15	DPAD_C		Fire
	SWITCH_NOTHING		// button 16	SEARCH		-
};

// Plr2 button is missing (START2).
int joystick_buttons_map_ace91[MAX_DEFINED_JOYSTICK_BUTTONS] =
{
						//				Android
	SWITCH_BUTTON1,		// button 00	A			Fire
	SWITCH_BUTTON1,		// button 01	B			Fire
	SWITCH_BUTTON1,		// button 02	X			Fire
	SWITCH_BUTTON1,		// button 03	Y			Fire
	SWITCH_COINSTART,	// button 04	SELECT/BACK	Plr1
	SWITCH_NOTHING,		// button 05	MODE		Score SW
	SWITCH_START1,		// button 06	START		Plr1
	SWITCH_COIN1,		// button 07	THUMBL		Coin1
	SWITCH_START1,		// button 08	THUMBR		Plr1
	SWITCH_BUTTON2,		// button 09	L1			Score SW
	SWITCH_NOTHING,		// button 10	R1			Score SW
	SWITCH_UP,			// button 11	DPAD_U		U
	SWITCH_DOWN,		// button 12	DPAD_D		D
	SWITCH_LEFT,		// button 13	DPAD_L		L
	SWITCH_RIGHT,		// button 14	DPAD_R		R
	SWITCH_BUTTON1,		// button 15	DPAD_C		Fire
	SWITCH_BUTTON1		// button 16	SEARCH		Fire
};

// Plr2 button missing (START2).
int joystick_buttons_map_lair[MAX_DEFINED_JOYSTICK_BUTTONS] =
{
						//				Android
	SWITCH_BUTTON1,		// button 00	A			Sword
	SWITCH_BUTTON1,		// button 01	B			Sword
	SWITCH_BUTTON1,		// button 02	X			Sword
	SWITCH_BUTTON3,		// button 03	Y			Score SW
	SWITCH_COINSTART,	// button 04	SELECT/BACK	Plr1
	SWITCH_BUTTON3,		// button 05	MODE		Score SW
	SWITCH_START1,		// button 06	START		Plr1
	SWITCH_COIN1,		// button 07	THUMBL		Coin1
	SWITCH_START1,		// button 08	THUMBR		Plr1
	SWITCH_BUTTON1,		// button 09	L1			Sword
	SWITCH_BUTTON1,		// button 10	R1			Sword
	SWITCH_UP,			// button 11	DPAD_U		U
	SWITCH_DOWN,		// button 12	DPAD_D		D
	SWITCH_LEFT,		// button 13	DPAD_L		L
	SWITCH_RIGHT,		// button 14	DPAD_R		R
	SWITCH_BUTTON1,		// button 15	DPAD_C		Sword
	SWITCH_BUTTON1		// button 16	SEARCH		Sword
};

// Plr2 button missing (START2).
int joystick_buttons_map_lair2[MAX_DEFINED_JOYSTICK_BUTTONS] =
{
						//				Android
	SWITCH_BUTTON1,		// button 00	A			Sword
	SWITCH_BUTTON1,		// button 01	B			Sword
	SWITCH_BUTTON1,		// button 02	X			Sword
	SWITCH_BUTTON3,		// button 03	Y			Score SW
	SWITCH_COINSTART,	// button 04	SELECT/BACK	Plr1
	SWITCH_BUTTON3,		// button 05	MODE		Score SW
	SWITCH_START1,		// button 06	START		Plr1
	SWITCH_COIN1,		// button 07	THUMBL		Coin1
	SWITCH_START1,		// button 08	THUMBR		Plr1
	SWITCH_BUTTON1,		// button 09	L1			Sword
	SWITCH_BUTTON1,		// button 10	R1			Sword
	SWITCH_UP,			// button 11	DPAD_U		U
	SWITCH_DOWN,		// button 12	DPAD_D		D
	SWITCH_LEFT,		// button 13	DPAD_L		L
	SWITCH_RIGHT,		// button 14	DPAD_R		R
	SWITCH_BUTTON1,		// button 15	DPAD_C		Sword
	SWITCH_BUTTON1		// button 16	SEARCH		Sword
};

// Plr2 button is missing (START2) 
int joystick_buttons_map_cliff[MAX_DEFINED_JOYSTICK_BUTTONS] =
{
						//				Android
	SWITCH_BUTTON1,		// button 00	A			Hands
	SWITCH_BUTTON1,		// button 01	B			Hands
	SWITCH_BUTTON2,		// button 02	X			Feet/Plr1
	SWITCH_BUTTON2,		// button 03	Y			Feet/Plr1
	SWITCH_COINSTART,	// button 04	SELECT/BACK	Plr1
	SWITCH_NOTHING,		// button 05	MODE		-
	SWITCH_START1,		// button 06	START		Plr1
	SWITCH_COIN1,		// button 07	THUMBL		Coin1
	SWITCH_START1,		// button 08	THUMBR		Plr1
	SWITCH_NOTHING,		// button 09	L1			-
	SWITCH_NOTHING,		// button 10	R1			-
	SWITCH_UP,			// button 11	DPAD_U		U
	SWITCH_DOWN,		// button 12	DPAD_D		D
	SWITCH_LEFT,		// button 13	DPAD_L		L
	SWITCH_RIGHT,		// button 14	DPAD_R		R
	SWITCH_BUTTON1,		// button 15	DPAD_C		Hands
	SWITCH_BUTTON2		// button 16	SEARCH		Feet
};

// No buttons missing.  Plr1 is Start from game.
int joystick_buttons_map_gpworld[MAX_DEFINED_JOYSTICK_BUTTONS] =
{
						//				Android
	SWITCH_BUTTON2,		// button 00	A			Gas
	SWITCH_BUTTON3,		// button 01	B			Brake
	SWITCH_BUTTON1,		// button 02	X			Shift
	SWITCH_NOTHING,		// button 03	Y			-
	SWITCH_COINSTART,	// button 04	SELECT/BACK	Plr1
	SWITCH_NOTHING,		// button 05	MODE		-
	SWITCH_START1,		// button 06	START		Plr1
	SWITCH_COIN1,		// button 07	THUMBL		Coin1
	SWITCH_START1,		// button 08	THUMBR		Plr1
	SWITCH_NOTHING,		// button 09	L1			-
	SWITCH_NOTHING,		// button 10	R1			-
	SWITCH_BUTTON1,		// button 11	DPAD_U		Shift
	SWITCH_BUTTON3,		// button 12	DPAD_D		Brake
	SWITCH_LEFT,		// button 13	DPAD_L		L
	SWITCH_RIGHT,		// button 14	DPAD_R		R
	SWITCH_BUTTON2,		// button 15	DPAD_C		Gas
	SWITCH_BUTTON2		// button 16	SEARCH		Gas
};

int joystick_buttons_map_bega[MAX_DEFINED_JOYSTICK_BUTTONS] =
{
						//				Android			BB			RB
	SWITCH_BUTTON1,		// button 00	A				Fire		-
	SWITCH_BUTTON2,		// button 01	B				Barrier		Brake
	SWITCH_BUTTON3,		// button 02	X				Teleport	Gas
	SWITCH_BUTTON1,		// button 03	Y				Fire		-
	SWITCH_COINSTART,	// button 04	SELECT/BACK		C1/Plr1		C1/Plr1
	SWITCH_NOTHING,		// button 05	MODE			-			-
	SWITCH_START1,		// button 06	START			Plr1		Plr1
	SWITCH_COIN1,		// button 07	THUMBL			Coin1		Coin1
	SWITCH_START1,		// button 08	THUMBR			Plr1		Plr1
	SWITCH_NOTHING,		// button 09	L1				-			-
	SWITCH_NOTHING,		// button 10	R1				-			-
	SWITCH_UP,			// button 11	DPAD_U			U			U
	SWITCH_DOWN,		// button 12	DPAD_D			D			D
	SWITCH_LEFT,		// button 13	DPAD_L			L			L
	SWITCH_RIGHT,		// button 14	DPAD_R			R			R
	SWITCH_BUTTON3,		// button 15	DPAD_C			Teleport	Gas
	SWITCH_BUTTON2		// button 16	SEARCH			Barrier		Brake
};

// Plr2 missing should be START2 (or maybe COIN2).
int joystick_buttons_map_esh[MAX_DEFINED_JOYSTICK_BUTTONS] =
{
						//				Android
	SWITCH_BUTTON1,		// button 00	A			Action
	SWITCH_BUTTON1,		// button 01	B			Action
	SWITCH_BUTTON1,		// button 02	X			Action
	SWITCH_BUTTON1,		// button 03	Y			Action
	SWITCH_COINSTART,	// button 04	SELECT/BACK	Plr1
	SWITCH_NOTHING,		// button 05	MODE		-
	SWITCH_START1,		// button 06	START		Plr1
	SWITCH_COIN1,		// button 07	THUMBL		Coin1
	SWITCH_START1,		// button 08	THUMBR		Plr1
	SWITCH_NOTHING,		// button 09	L1			-
	SWITCH_NOTHING,		// button 10	R1			-
	SWITCH_UP,			// button 11	DPAD_U		U
	SWITCH_DOWN,		// button 12	DPAD_D		D
	SWITCH_LEFT,		// button 13	DPAD_L		L
	SWITCH_RIGHT,		// button 14	DPAD_R		R
	SWITCH_BUTTON1,		// button 15	DPAD_C		Action
	SWITCH_BUTTON1		// button 16	SEARCH		Action
};

// Plr2 missing should be (START2).
int joystick_buttons_map_interstellar[MAX_DEFINED_JOYSTICK_BUTTONS] =
{
						//				Android
	SWITCH_BUTTON1,		// button 00	A			Fazer
	SWITCH_BUTTON2,		// button 01	B			Burn
	SWITCH_BUTTON1,		// button 02	X			Fazer
	SWITCH_BUTTON2,		// button 03	Y			Burn
	SWITCH_COINSTART,	// button 04	SELECT/BACK	Plr1
	SWITCH_NOTHING,		// button 05	MODE		-
	SWITCH_START1,		// button 06	START		Plr1
	SWITCH_COIN1,		// button 07	THUMBL		Coin1
	SWITCH_START1,		// button 08	THUMBR		Plr1
	SWITCH_NOTHING,		// button 09	L1			-
	SWITCH_NOTHING,		// button 10	R1			-
	SWITCH_UP,			// button 11	DPAD_U		U
	SWITCH_DOWN,		// button 12	DPAD_D		D
	SWITCH_LEFT,		// button 13	DPAD_L		L
	SWITCH_RIGHT,		// button 14	DPAD_R		R
	SWITCH_BUTTON1,		// button 15	DPAD_C		Fazer
	SWITCH_BUTTON2		// button 16	SEARCH		Burn
};

// Plr2 missing should be (START2).  BUTTON3 seems to be only used for UvT.
int joystick_buttons_map_mach3[MAX_DEFINED_JOYSTICK_BUTTONS] =
{
						//				Android
	SWITCH_BUTTON1,		// button 00	A			Gun
	SWITCH_BUTTON2,		// button 01	B			Missle
	SWITCH_BUTTON1,		// button 02	X			Gun
	SWITCH_BUTTON2,		// button 03	Y			Missle
	SWITCH_COINSTART,	// button 04	SELECT/BACK	Plr1
	SWITCH_NOTHING,		// button 05	MODE		-
	SWITCH_START1,		// button 06	START		Plr1
	SWITCH_COIN1,		// button 07	THUMBL		Coin1
	SWITCH_START1,		// button 08	THUMBR		Plr1
	SWITCH_NOTHING,		// button 09	L1			-
	SWITCH_NOTHING,		// button 10	R1			-
	SWITCH_UP,			// button 11	DPAD_U		U
	SWITCH_DOWN,		// button 12	DPAD_D		D
	SWITCH_LEFT,		// button 13	DPAD_L		L
	SWITCH_RIGHT,		// button 14	DPAD_R		R
	SWITCH_BUTTON1,		// button 15	DPAD_C		Gun
	SWITCH_BUTTON2		// button 16	SEARCH		Missle
};

// Plr2 missing should be (START2).
int joystick_buttons_map_uvt[MAX_DEFINED_JOYSTICK_BUTTONS] =
{
						//				Android
	SWITCH_BUTTON3,		// button 00	A			Gun
	SWITCH_BUTTON2,		// button 01	B			Right
	SWITCH_BUTTON1,		// button 02	X			Left
	SWITCH_BUTTON3,		// button 03	Y			Gun
	SWITCH_COINSTART,	// button 04	SELECT/BACK	Plr1
	SWITCH_NOTHING,		// button 05	MODE		-
	SWITCH_START1,		// button 06	START		Plr1
	SWITCH_COIN1,		// button 07	THUMBL		Coin1
	SWITCH_START1,		// button 08	THUMBR		Plr1
	SWITCH_BUTTON1,		// button 09	L1			-
	SWITCH_BUTTON2,		// button 10	R1			-
	SWITCH_UP,			// button 11	DPAD_U		U
	SWITCH_DOWN,		// button 12	DPAD_D		D
	SWITCH_LEFT,		// button 13	DPAD_L		L
	SWITCH_RIGHT,		// button 14	DPAD_R		R
	SWITCH_BUTTON3,		// button 15	DPAD_C		Gun
	SWITCH_BUTTON2		// button 16	SEARCH		Right (yes, no Left on remotes)
};

// Plr2 missing should be (START2).
int joystick_buttons_map_sdq[MAX_DEFINED_JOYSTICK_BUTTONS] =
{
						//				Android
	SWITCH_BUTTON1,		// button 00	A			Action
	SWITCH_BUTTON1,		// button 01	B			Action
	SWITCH_BUTTON1,		// button 02	X			Action
	SWITCH_BUTTON1,		// button 03	Y			Action
	SWITCH_COINSTART,	// button 04	SELECT/BACK	Plr1
	SWITCH_NOTHING,		// button 05	MODE		-
	SWITCH_START1,		// button 06	START		Plr1
	SWITCH_COIN1,		// button 07	THUMBL		Coin1
	SWITCH_START1,		// button 08	THUMBR		Plr1
	SWITCH_NOTHING,		// button 09	L1			-
	SWITCH_NOTHING,		// button 10	R1			-
	SWITCH_UP,			// button 11	DPAD_U		U
	SWITCH_DOWN,		// button 12	DPAD_D		D
	SWITCH_LEFT,		// button 13	DPAD_L		L
	SWITCH_RIGHT,		// button 14	DPAD_R		R
	SWITCH_BUTTON1,		// button 15	DPAD_C		Action
	SWITCH_BUTTON1		// button 16	SEARCH		Action
};

// Plr2 missing should be (START2).
int joystick_buttons_map_badlands[MAX_DEFINED_JOYSTICK_BUTTONS] =
{
						//				Android
	SWITCH_BUTTON1,		// button 00	A			Action
	SWITCH_BUTTON1,		// button 01	B			Action
	SWITCH_BUTTON1,		// button 02	X			Action
	SWITCH_BUTTON1,		// button 03	Y			Action
	SWITCH_COINSTART,	// button 04	SELECT/BACK	Plr1
	SWITCH_NOTHING,		// button 05	MODE		-
	SWITCH_START1,		// button 06	START		Plr1
	SWITCH_COIN1,		// button 07	THUMBL		Coin1
	SWITCH_START1,		// button 08	THUMBR		Plr1
	SWITCH_START2,		// button 09	L1			-
	SWITCH_START2,		// button 10	R1			-
	SWITCH_NOTHING,		// button 11	DPAD_U		U
	SWITCH_NOTHING,		// button 12	DPAD_D		D
	SWITCH_NOTHING,		// button 13	DPAD_L		L
	SWITCH_NOTHING,		// button 14	DPAD_R		R
	SWITCH_BUTTON1,		// button 15	DPAD_C		Action
	SWITCH_START2		// button 16	SEARCH		Action
};

// 
int joystick_buttons_map_tq[MAX_DEFINED_JOYSTICK_BUTTONS] =
{
						//				Android
	SWITCH_NOTHING,		// button 00	A			Action
	SWITCH_NOTHING,		// button 01	B			Action
	SWITCH_NOTHING,		// button 02	X			Action
	SWITCH_NOTHING,		// button 03	Y			Action
	SWITCH_COINSTART,	// button 04	SELECT/BACK	Plr1
	SWITCH_NOTHING,		// button 05	MODE		-
	SWITCH_START1,		// button 06	START		Plr1
	SWITCH_COIN1,		// button 07	THUMBL		Coin1
	SWITCH_START1,		// button 08	THUMBR		Plr1
	SWITCH_NOTHING,		// button 09	L1			-
	SWITCH_NOTHING,		// button 10	R1			-
	SWITCH_NOTHING,		// button 11	DPAD_U		U
	SWITCH_NOTHING,		// button 12	DPAD_D		D
	SWITCH_NOTHING,		// button 13	DPAD_L		L
	SWITCH_NOTHING,		// button 14	DPAD_R		R
	SWITCH_NOTHING,		// button 15	DPAD_C		Action
	SWITCH_NOTHING		// button 16	SEARCH		Action
};

// Hard coding 23 is stupid, see enum of game types in game.h, no new games are likely so leaving it.
int * joystick_buttons_map_table[MAX_DEFINED_GAME_TYPES] =
{
	joystick_buttons_map_default,		// 00 GAME_UNDEFINED
	joystick_buttons_map_lair,			// 01 GAME_LAIR
	joystick_buttons_map_lair2,			// 02 GAME_LAIR2
	joystick_buttons_map_ace,			// 03 GAME_ACE
	joystick_buttons_map_ace91,			// 04 GAME_ACE91
	joystick_buttons_map_cliff,			// 05 GAME_CLIFF
	joystick_buttons_map_default,		// 06 GAME_GTG
	joystick_buttons_map_sdq,			// 07 GAME_SUPERD
	joystick_buttons_map_tq,			// 08 GAME_THAYERS
	joystick_buttons_map_astron,		// 09 GAME_ASTRON
	joystick_buttons_map_astron,		// 10 GAME_GALAXY
	joystick_buttons_map_esh,			// 11 GAME_ESH
	joystick_buttons_map_default,		// 12 GAME_LAIREURO
	joystick_buttons_map_badlands,		// 13 GAME_BADLANDS
	joystick_buttons_map_default,		// 14 GAME_STARRIDER
	joystick_buttons_map_bega,			// 15 GAME_BEGA
	joystick_buttons_map_interstellar,	// 16 GAME_INTERSTELLAR
	joystick_buttons_map_ace,			// 17 GAME_SAE
	joystick_buttons_map_mach3,			// 18 GAME_MACH3
	joystick_buttons_map_uvt,			// 19 GAME_UVT
	joystick_buttons_map_badlands,		// 20 GAME_BADLANDP
	joystick_buttons_map_lair,			// 21 GAME_DLE1
	joystick_buttons_map_lair,			// 22 GAME_DLE2
	joystick_buttons_map_gpworld		// 22 GAME_GPWORLD
};

// RJS CHANGE END

#else
// RJS START ADD - to keep up with non-GP2X
#define MAX_DEFINED_JOYSTICK_BUTTONS	18
#define MAX_DEFINED_GAME_TYPES			23
// RJS END

// button mapping for gp2x
// RJS CHANGE
// int joystick_buttons_map[18] =
int joystick_buttons_map_default[MAX_DEFINED_JOYSTICK_BUTTONS] =
{
	SWITCH_UP,	// 0 (up)
	SWITCH_UP,	// 1 (up-left)
	SWITCH_LEFT,	// 2 (left)
	SWITCH_DOWN,	// 3 (down-left)
	SWITCH_DOWN,	// 4 (down)
	SWITCH_DOWN,	// 5 (down-right)
	SWITCH_RIGHT,	// 6 (right)
	SWITCH_UP,	// 7 (up-right)
	SWITCH_START1,	// 8 (start)
	SWITCH_COIN1,	// 9 (select)
	SWITCH_QUIT,	// 10 (left)
	SWITCH_PAUSE,	// 11 (right)
	SWITCH_BUTTON1,	// 12 (A)
	SWITCH_BUTTON2, // 13 (B)
	SWITCH_BUTTON3,	// 14 (X)
	SWITCH_CONSOLE,	// 15 (Y)
	SWITCH_BUTTON1, // 16 is vol +
	SWITCH_BUTTON1 // 17 is vol -
};

// RJS START ADD - to keep up with non-GP2X
// Hard coding 23 is stupid, see enum of game types in game.h, no new games are likely so leaving it.
int * joystick_buttons_map_table[MAX_DEFINED_GAME_TYPES] =
{
	joystick_buttons_map_default,		// 00 GAME_UNDEFINED
	joystick_buttons_map_default,		// 01 GAME_LAIR
	joystick_buttons_map_default,		// 02 GAME_LAIR2
	joystick_buttons_map_default,		// 03 GAME_ACE
	joystick_buttons_map_default,		// 04 GAME_ACE91
	joystick_buttons_map_default,		// 05 GAME_CLIFF
	joystick_buttons_map_default,		// 06 GAME_GTG
	joystick_buttons_map_default,		// 07 GAME_SUPERD
	joystick_buttons_map_default,		// 08 GAME_THAYERS
	joystick_buttons_map_default,		// 09 GAME_ASTRON
	joystick_buttons_map_default,		// 10 GAME_GALAXY
	joystick_buttons_map_default,		// 11 GAME_ESH
	joystick_buttons_map_default,		// 12 GAME_LAIREURO
	joystick_buttons_map_default,		// 13 GAME_BADLANDS
	joystick_buttons_map_default,		// 14 GAME_STARRIDER
	joystick_buttons_map_default,		// 15 GAME_BEGA
	joystick_buttons_map_default,		// 16 GAME_INTERSTELLAR
	joystick_buttons_map_default,		// 17 GAME_SAE
	joystick_buttons_map_default,		// 18 GAME_MACH3
	joystick_buttons_map_default,		// 19 GAME_UVT
	joystick_buttons_map_default,		// 20 GAME_BADLANDP
	joystick_buttons_map_default,		// 21 GAME_DLE1
	joystick_buttons_map_default		// 22 GAME_DLE2
};
// RJS END
#endif

// Mouse button to key mappings
// Added by ScottD for Singe
int mouse_buttons_map[5] =
{
	SWITCH_BUTTON1,  // 0 (Left Button)
	SWITCH_BUTTON3,  // 1 (Middle Button)
	SWITCH_BUTTON2,  // 2 (Right Button)
	SWITCH_BUTTON1,  // 3 (Wheel Up)
	SWITCH_BUTTON2   // 4 (Wheel Down)
};

////////////

// RJS START ADD - new routine for getting the joystick map and invert controls
bool input_isinverted()
{
	return g_invert_joystick;
}

void input_invert_controls(bool fInvert)
{
	g_invert_joystick = fInvert;
}

int * get_joystick_buttons_map()
{
	int nGameType = GAME_UNDEFINED;
	if (g_game) nGameType = g_game->get_game_type();
	if (nGameType >= MAX_DEFINED_GAME_TYPES) nGameType = GAME_UNDEFINED;

	LOGI("In get_joystick_buttons_map, game type being used for input: %d", nGameType);

	int * pnReturnTable = joystick_buttons_map_table[nGameType];
	return (pnReturnTable);
}
// RJS END

void CFG_Keys()
{
	struct mpo_io *io;
	string cur_line = "";
	string key_name = "", sval1 = "", sval2 = "", sval3 = "", eq_sign = "";
	int val1 = 0, val2 = 0, val3 = 0;
//	bool done = false;

	// find where the dapinput ini file is (if the file doesn't exist, this string will be empty)
	string strDapInput = g_homedir.find_file("dapinput.ini", true);

	io = mpo_open(strDapInput.c_str(), MPO_OPEN_READONLY);
	if (io)
	{
		printline("Remapping input ...");

		cur_line = "";

		// read lines until we find the keyboard header or we hit EOF
		while (strcasecmp(cur_line.c_str(), "[KEYBOARD]") != 0)
		{
			read_line(io, cur_line);
			if (io->eof)
			{
				printline("CFG_Keys() : never found [KEYBOARD] header, aborting");
				break;
			}
		}

		// go until we hit EOF, or we break inside this loop (which is the expected behavior)
		while (!io->eof)
		{
			// if we read in something besides a blank line
			if (read_line(io, cur_line) > 0)
			{
				bool corrupt_file = true;	// we use this to avoid doing multiple if/else/break statements

				// if we are able to read in the key name
				if (find_word(cur_line.c_str(), key_name, cur_line))
				{
					if (strcasecmp(key_name.c_str(), "END") == 0) break;	// if we hit the 'END' keyword, we're done

					// equals sign
					if (find_word(cur_line.c_str(), eq_sign, cur_line) && (eq_sign == "="))
					{
						if (find_word(cur_line.c_str(), sval1, cur_line))
						{
							if (find_word(cur_line.c_str(), sval2, cur_line))
							{
								if (find_word(cur_line.c_str(), sval3, cur_line))
								{
									val1 = atoi(sval1.c_str());
									val2 = atoi(sval2.c_str());
									val3 = atoi(sval3.c_str());
									corrupt_file = false;	// looks like we're good

									bool found_match = false;
									// RJS ADD - get jopystick buttons map
									int * joystick_buttons_map = get_joystick_buttons_map();

									for (int i = 0; i < SWITCH_COUNT; i++)
									{
										// if we can match up a key name (see list above) ...
										if (strcasecmp(key_name.c_str(), g_key_names[i])==0)
										{
											g_key_defs[i][0] = val1;
											g_key_defs[i][1] = val2;

											// if zero then no mapping necessary, just use default, if any
											if (val3 > 0) joystick_buttons_map[val3 - 1] = i;
											found_match = true;
											break;
										}
									}

									// if the key line was unknown
									if (!found_match)
									{
										cur_line = "CFG_Keys() : Unrecognized key name " + key_name;
										printline(cur_line.c_str());
										corrupt_file = true;
									}

								}
								else printline("CFG_Keys() : Expected 3 integers, only found 2");
							}
							else printline("CFG_Keys() : Expected 3 integers, only found 1");
						}
						else printline("CFG_Keys() : Expected 3 integers, found none");
					} // end equals sign
					else printline("CFG_Keys() : Expected an '=' sign, didn't find it");
				} // end if we found key_name
				else printline("CFG_Keys() : Weird unexpected error happened");	// this really shouldn't ever happen

				if (corrupt_file)
				{
					printline("CFG_Keys() : input remapping file was not in proper format, so we are aborting");
					break;
				}
			} // end if we didn't find a blank line
			// else it's a blank line so we just ignore it
		} // end while not EOF

		mpo_close(io);
	} // end if file was opened successfully
}

int SDL_input_init()
// initializes the keyboard (and joystick if one is present)
// returns 1 if successful, 0 on error
// NOTE: Video has to be initialized BEFORE this is or else it won't work
{

	int result = 0;

	// make sure the coin queue is empty (it should be, this is just a safety check)
	while (!g_coin_queue.empty())
	{
		g_coin_queue.pop();
	}
	g_sticky_coin_cycles = (Uint32) (STICKY_COIN_SECONDS * get_cpu_hz(0));	// only needs to be calculated once

	if (SDL_InitSubSystem(SDL_INIT_JOYSTICK) >= 0)
	{
		// if joystick usage is enabled
		if (g_use_joystick)
		{
			// if there is at least 1 joystick and we are authorized to use the joystick for input
			if (SDL_NumJoysticks() > 0)
			{
				// RJS NOTE - function helps choose joystick
				G_joystick = SDL_JoystickOpen(0);	// FIXME: right now we automatically choose the first joystick
				if (G_joystick != NULL)
				{
					printline("Joystick #0 was successfully opened");
				}
				else
				{
					printline("Error opening joystick!");
				}
			}
			else
			{
				printline("No joysticks detected");
			}
		}
		// notify user that their attempt to disable the joystick is successful
		else
		{
			printline("Joystick usage disabled");
		}
		
		CFG_Keys();	// NOTE : for some freak reason, this should not be done BEFORE the joystick is initialized, I don't know why!
		result = 1;
	}
	else
	{
		printline("Input initialization failed!");
	}

	idle_timer = refresh_ms_time(); // added by JFA for -idleexit

#ifdef UNIX
	// enable non-blocking stdin (see conin.cpp UNIX stdin stuff)
	/*int iRes =*/ fcntl(fileno(stdin), F_SETFL, O_NONBLOCK);
#endif

	// if the mouse is disabled, then filter mouse events out ...
	if (!g_game->getMouseEnabled())
	{
		FilterMouseEvents(true);
	}

	return(result);

}

void FilterMouseEvents(bool bFilteredOut)
{
	int iState = SDL_ENABLE;

	if (bFilteredOut)
	{
		iState = SDL_IGNORE;
	}

	SDL_EventState(SDL_MOUSEMOTION, iState);
	SDL_EventState(SDL_MOUSEBUTTONDOWN, iState);
	SDL_EventState(SDL_MOUSEBUTTONUP, iState);
}

// does any shutting down necessary
// 1 = success, 0 = failure
int SDL_input_shutdown(void)
{
	SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
	return(1);
}

// checks to see if there is incoming input, and acts on it
void SDL_check_input()
{
	// LOGI("daphne-libretro: In SDL_check_input, top of routine.");

	SDL_Event event;

	while ((SDL_PollEvent (&event)) && (!get_quitflag()))
	{
		// LOGI("daphne-libretro: In SDL_check_input, new event.  type: %d  key: %d", event.type, event.key.keysym.sym);
		// if they press the tilda key to bring down the console
		// this is somewhat of a hacked if statement but I can't see
		// a better way based on the SDL_Console API ...
		if ((event.type == SDL_KEYDOWN) && (event.key.keysym.sym == SDLK_BACKQUOTE))
		{
			// we must not bring down the console if blitting is not allowed
			if (g_ldp->is_blitting_allowed())
			{
#ifdef CPU_DEBUG
				toggle_console();
#endif
			}
		}
		// if we've got the console down, process events
		else if (g_consoledown)
		{
			ConsoleEvents(&event);
		}
		// else handle events normally
		else
		{
			process_event(&event);
		}
	}
	check_console_refresh();

	// added by JFA for -idleexit
	if (get_idleexit() > 0 && elapsed_ms_time(idle_timer) > get_idleexit()) set_quitflag();

	// if the coin queue has something entered into it
	if (!g_coin_queue.empty())
	{
		struct coin_input coin = g_coin_queue.front();	// examine the next element in the queue to be considered

		// NOTE : when cpu timers are flushed, the coin queue is automatically "reshuffled"
		// so it is safe not to check to see whether the cpu timers were flushed here

		// if it's safe to activate the coin
		if (get_total_cycles_executed(0) > coin.cycles_when_to_enable)
		{
			// if we're supposed to enable this coin
			if (coin.coin_enabled)
			{
				g_game->input_enable(coin.coin_val);
			}
			// else we are supposed to disable this coin
			else
			{
				g_game->input_disable(coin.coin_val);
			}
			g_coin_queue.pop();	// remove coin entry from queue
		}
		// else it's not safe to activate the coin, so we just wait
	}
	// else the coin queue is empty, so we needn't do anything ...

	// LOGI("daphne-libretro: In SDL_check_input, bottom of routine.");
}

#ifdef CPU_DEBUG
void toggle_console()
{
	if (get_console_initialized())
	{
		// if console is down, get rid of it
		if (g_consoledown)
		{
			g_consoledown = false;
			SDL_EnableUNICODE(0);
			display_repaint();
		}
		// if console is not down, display it
		else
		{
			g_consoledown = true;
			SDL_EnableUNICODE(1);
		}
	}
}
#endif

// RJS NOTE - event processing here
// processes incoming input
void process_event(SDL_Event *event)
{
	LOGI("In process_event, top of routine.  type: %d  jbutton: %d  keysym: %d", event->type, event->jbutton.button, event->key.keysym.sym);
	unsigned int i = 0;

	switch (event->type)
	{
		case SDL_KEYDOWN:
			reset_idle(); // added by JFA for -idleexit
			if (g_game->get_game_type() != GAME_THAYERS)
			{
				process_keydown(event->key.keysym.sym);
			}
			// We use a special case for Thayers Quest
			else
			{
				thayers *l_thayers = dynamic_cast<thayers *>(g_game);
				// cast game class to a thayers class so we can call a thayers-specific function

				// make sure cast succeeded
				if (l_thayers)
				{
					l_thayers->process_keydown(event->key.keysym.sym);
				}
				// else cast failed, and we would crash if we tried to call process_keydown
				// cast would fail if g_game is not a thayers class
			}
			break;
		case SDL_KEYUP:
			// MPO : since con_getkey doesn't catch the key up event, we shouldn't call reset_idle here.
			//reset_idle(); // added by JFA for -idleexit

			if (g_game->get_game_type() != GAME_THAYERS)
			{
				process_keyup(event->key.keysym.sym);
			}
			// We use a special case for Thayers Quest
			else
			{
				thayers *l_thayers = dynamic_cast<thayers *>(g_game);
				// cast game class to thayers class so we can call a thayers-specific function

				// make sure cast succeeded
				if (l_thayers)
				{
					l_thayers->process_keyup(event->key.keysym.sym);
				}
				// else cast failed and we would crash if we tried to call process_keyup
				// cast would fail if g_game is not a thayers class
			}
			break;
		case SDL_JOYAXISMOTION:
			//reset_idle(); // added by JFA for -idleexit
			// reset_idle removed here because the analog controls were registering
			// even when the joystick was not in use.  Joystick buttons still reset the idle.
			process_joystick_motion(event);
			break;
		case SDL_JOYHATMOTION:
			// only process events for the first hat
			if ( event->jhat.hat == 0 )
			{
				reset_idle();
				process_joystick_hat_motion(event);
			}
			break;
		case SDL_JOYBUTTONDOWN:
		{
			LOGI("In process_event, in SDL_JOYBUTTONDOWN. button: %d", event->jbutton.button);
			reset_idle(); // added by JFA for -idleexit

			// RJS ADD - get joystick buttons map
			int * joystick_buttons_map = get_joystick_buttons_map();

			// added by Russ
			// loop through buttons and look for a press
			// RJS CHANGE
			// for (i = 0; i < (sizeof(joystick_buttons_map) / sizeof(int)); i++) {
			for (i = 0; i < MAX_DEFINED_JOYSTICK_BUTTONS; i++) {
				if (event->jbutton.button == i) {
					input_enable((Uint8)joystick_buttons_map[i]);
					break;
				}
			}
			LOGI("In process_event, in SDL_JOYBUTTONDOWN, end of finding button. maxbuttons: %d  foundbutton: %d", MAX_DEFINED_JOYSTICK_BUTTONS, i);

			break;
		}

		case SDL_JOYBUTTONUP:
		{
			LOGI("In process_event, in SDL_JOYBUTTONUP. button: %d", event->jbutton.button);
			reset_idle(); // added by JFA for -idleexit

			// RJS ADD - get jopystick buttons map
			int * joystick_buttons_map = get_joystick_buttons_map();

			// added by Russ
			// RJS CHANGE
			// for (i = 0; i < (sizeof(joystick_buttons_map) / sizeof(int)); i++) {
			for (i = 0; i < MAX_DEFINED_JOYSTICK_BUTTONS; i++) {
				if (event->jbutton.button == i) {
					input_disable((Uint8)joystick_buttons_map[i]);
					break;
				}
			}
			LOGI("In process_event, in SDL_JOYBUTTONUP, end of finding button. maxbuttons: %d  foundbutton: %d", MAX_DEFINED_JOYSTICK_BUTTONS, i);

			break;
		}

		case SDL_MOUSEBUTTONDOWN:
			// added by ScottD
			// loop through buttons and look for a press
			for (i = 0; i < (sizeof(mouse_buttons_map) / sizeof(int)); i++) {
				if (event->button.button == i) {
					input_enable((Uint8)mouse_buttons_map[i]);
					break;
				}
			}
			break;
		case SDL_MOUSEBUTTONUP:
			// added by ScottD
			for (i = 0; i < (sizeof(mouse_buttons_map) / sizeof(int)); i++) {
				if (event->button.button == i) {
					input_disable((Uint8)mouse_buttons_map[i]);
					break;
				}
			}
			break;
		case SDL_MOUSEMOTION:
			// added by ScottD
			g_game->OnMouseMotion(event->motion.x, event->motion.y, event->motion.xrel, event->motion.yrel);
			break;
		case SDL_QUIT:
			// if they are trying to close the window
			set_quitflag();
			break;
		default:
			break;
	}

	// added by JFA for -idleexit
	if (get_idleexit() > 0 && elapsed_ms_time(idle_timer) > get_idleexit()) set_quitflag();
	LOGI("In process_event, bottom of routine.");
}

// if a key is pressed, we go here
// RJS CHANGE
// void process_keydown(SDLKey key)
void process_keydown(SDL_Keycode key)
{
	// go through each key def (defined in enum in daphne.h) and check to see if the key entered matches
	// If we have a match, the switch to be used is the value of the index "move"
	for (Uint8 move = 0; move < SWITCH_COUNT; move++)
	{
		if ((key == g_key_defs[move][0]) || (key == g_key_defs[move][1]))
		{
			input_enable(move);
		}
	}
	
	// check for ALT-ENTER here
	if ((key == SDLK_LALT) || (key == SDLK_RALT))
	{
		g_alt_pressed = true;
	}
	else if ((key == SDLK_RETURN) && (g_alt_pressed))
	{
		vid_toggle_fullscreen();
	}
	// end ALT-ENTER check
}

// if a key is released, we go here
// RJS CHANGE
// void process_keyup(SDLKey key)
void process_keyup(SDL_Keycode key)
{
	// go through each key def (defined in enum in daphne.h) and check to see if the key entered matches
	// If we have a match, the switch to be used is the value of the index "move"
	for (Uint8 move = 0; move < SWITCH_COUNT; move++)
	{
		if ((key == g_key_defs[move][0]) || (key == g_key_defs[move][1]))
		{
			input_disable(move);
		}
	}

	// if they are releasing an ALT key	
	if ((key == SDLK_LALT) || (key == SDLK_RALT))
	{
		g_alt_pressed = false;
	}
}

// processes movements of the joystick
void process_joystick_motion(SDL_Event *event)
{

	static int x_axis_in_use = 0;	// true if joystick is left or right
	static int y_axis_in_use = 0;	// true if joystick is up or down

	// if they are moving along the verticle axis
	if (event->jaxis.axis == 1)
	{
		// if they're moving up
		if (event->jaxis.value < -JOY_AXIS_MID)
		{
			input_enable(SWITCH_UP);
			y_axis_in_use = 1;
		}
		// if they're moving down
		else if (event->jaxis.value > JOY_AXIS_MID)
		{
			input_enable(SWITCH_DOWN);
			y_axis_in_use = 1;
		}

		// if they just barely stopped moving up or down
		else if (y_axis_in_use == 1)
		{
			input_disable(SWITCH_UP);
			input_disable(SWITCH_DOWN);
			y_axis_in_use = 0;
		}
	} // end verticle axis

	// horizontal axis
	else
	{
		// if they're moving right
		if (event->jaxis.value > JOY_AXIS_MID)
		{
			input_enable(SWITCH_RIGHT);
			x_axis_in_use = 1;
		}
		// if they're moving left
		else if (event->jaxis.value < -JOY_AXIS_MID)
		{
			input_enable(SWITCH_LEFT);
			x_axis_in_use = 1;
		}
		// if they just barely stopped moving right or left
		else if (x_axis_in_use == 1)
		{
			input_disable(SWITCH_RIGHT);
			input_disable(SWITCH_LEFT);
			x_axis_in_use = 0;
		}
	} // end horizontal axis
}

// processes movement of the joystick hat
void process_joystick_hat_motion(SDL_Event *event)
{

	static Uint8 prev_hat_position = SDL_HAT_CENTERED; 

	if ( ( event->jhat.value & SDL_HAT_UP ) && !( prev_hat_position & SDL_HAT_UP ) )
	{
		// hat moved to the up position
		input_enable(SWITCH_UP);
	}
	else if ( !( event->jhat.value & SDL_HAT_UP ) && ( prev_hat_position & SDL_HAT_UP ) )
	{ 
		// up hat released
		input_disable(SWITCH_UP);
	}

	if ( ( event->jhat.value & SDL_HAT_RIGHT ) && !( prev_hat_position & SDL_HAT_RIGHT ) )
	{
		// hat moved to the right position
		input_enable(SWITCH_RIGHT);
	}
	else if ( !( event->jhat.value & SDL_HAT_RIGHT ) && ( prev_hat_position & SDL_HAT_RIGHT ) )
	{ 
		// right hat released
		input_disable(SWITCH_RIGHT);
	}

	if ( ( event->jhat.value & SDL_HAT_DOWN ) && !( prev_hat_position & SDL_HAT_DOWN ) )
	{
		// hat moved to the down position
		input_enable(SWITCH_DOWN);
	}
	else if ( !( event->jhat.value & SDL_HAT_DOWN ) && ( prev_hat_position & SDL_HAT_DOWN ) )
	{ 
		// down hat released
		input_disable(SWITCH_DOWN);
	}

	if ( ( event->jhat.value & SDL_HAT_LEFT ) && !( prev_hat_position & SDL_HAT_LEFT ) )
	{
		// hat moved to the left position
		input_enable(SWITCH_LEFT);
	}
	else if ( !( event->jhat.value & SDL_HAT_LEFT ) && ( prev_hat_position & SDL_HAT_LEFT ) )
	{ 
		// left hat released
		input_disable(SWITCH_LEFT);
	}

	prev_hat_position = event->jhat.value;
}

// functions to help us avoid 'extern' statements
bool get_consoledown()
{
	return (g_consoledown);
}

void set_consoledown (bool value)
{
	g_consoledown = value;
}

// draws console if it's down and if there's been enough of a delay
void check_console_refresh()
{

	static unsigned int console_refresh = 0;
	const unsigned int refresh_every = 125;	// refreshes console every (this many) ms

	if (g_consoledown)
	{
		if (elapsed_ms_time(console_refresh) > refresh_every)
		{
			DrawConsole();
			vid_blit(get_screen_blitter(), 0, 0);
			vid_flip();
			console_refresh = refresh_ms_time();
		}
	}
}

bool input_pause(bool fPause)
{
	bool fCurrentState = g_game->get_game_paused();

	if (fPause != fCurrentState)
	{
		// Pause is only supported when LDP is PLAYING.  Sometimes it can take a second to
		//  go from one state back to PLAYING state.
		input_enable(SWITCH_PAUSE);
		if (g_game->get_game_paused() == fCurrentState) return false;
	}

	return true;
}

// RJS ADD - coin start switch, can't think of a better way off hand, game credits are
// decremented after this routine but before the input_disabled routine.  Also,
// COINSTART won't work with buttons that quickly send there UP and DOWN like NVIDIA's
// search button on their remote.
static int fCoinStart_Started_References = 0;

// if user has pressed a key/moved the joystick/pressed a button
void input_enable(Uint8 move)
{
	// RJS START ADD - coin start switch
	if (move == SWITCH_COINSTART)
	{
		if (fCoinStart_Started_References != 0) return;

		Uint8 nCredits = g_game->get_game_credits();
		if (nCredits > 0)
		{
			move = SWITCH_START1;
			fCoinStart_Started_References++;
		} else {
			move = SWITCH_COIN1;
		}
	}
	// RJS END

	// first test universal input, then pass unknown input on to the game driver

	switch (move)
	{
	default:
		g_game->input_enable(move);
		break;
	case SWITCH_RESET:
		g_game->reset();
		break;
	case SWITCH_SCREENSHOT:
		printline("Screenshot requested!");
		g_ldp->request_screenshot();
		break;
	case SWITCH_PAUSE:
		g_game->toggle_game_pause();
		break;
	case SWITCH_QUIT:
		set_quitflag();
		break;
	case SWITCH_COIN1:
	case SWITCH_COIN2:
		// coin inputs are buffered to ensure that they are not dropped while the cpu is busy (such as during a seek)
		// therefore if the input is coin1 or coin2 AND we are using a real cpu (and not a program such as seektest)
		if (get_cpu_hz(0) > 0)
		{
			add_coin_to_queue(true, move);
		}
		break;
	case SWITCH_CONSOLE:
		// we must not bring down the console if blitting is not allowed
		if (g_ldp->is_blitting_allowed())
		{
#ifdef CPU_DEBUG
			toggle_console();
#endif
		}
		break;
	}
}

// if user has released a key/released a button/moved joystick back to center position
void input_disable(Uint8 move)
{
	// RJS START ADD - coin start switch
	if (move == SWITCH_COINSTART)
	{
		if (fCoinStart_Started_References > 0)
		{
			move = SWITCH_START1;
			fCoinStart_Started_References--;
		} else {
			move = SWITCH_COIN1;
		}
	}
	// RJS END

	// don't send reset or screenshots key-ups to the individual games because they will return warnings that will alarm users
	if ((move != SWITCH_RESET) && (move != SWITCH_SCREENSHOT) && (move != SWITCH_QUIT)
		&& (move != SWITCH_PAUSE))
	{
		// coin inputs are buffered to ensure that they are not dropped while the cpu is busy (such as during a seek)
		// therefore if the input is coin1 or coin2 AND we are using a real cpu (and not a program such as seektest)
		if (((move == SWITCH_COIN1) || (move == SWITCH_COIN2)) && (get_cpu_hz(0) > 0))
		{
			add_coin_to_queue(false, move);
		}
		else
		{
			g_game->input_disable(move);
		}
	}
	// else do nothing
}

inline void add_coin_to_queue(bool enabled, Uint8 val)
{
	Uint64 total_cycles = get_total_cycles_executed(0);
	struct coin_input coin;
	coin.coin_enabled = enabled;
	coin.coin_val = val;

	// make sure that we are >= to the total cycles executed otherwise coin insertions will be really quick
	if (g_last_coin_cycle_used < total_cycles)
	{
		g_last_coin_cycle_used = total_cycles;
	}
	g_last_coin_cycle_used += g_sticky_coin_cycles;	// advance to the next safe slot
	coin.cycles_when_to_enable = g_last_coin_cycle_used;	// and assign this safe slot to this current coin
	g_coin_queue.push(coin);	// add the coin to the queue ...
}

// added by JFA for -idleexit
void reset_idle(void)
{
	static bool bSoundOn = false;

	// At this time, the only way the sound will be muted is if -startsilent was passed via command line.
	// So the first key press should always unmute the sound.
	if (!bSoundOn)
	{
		bSoundOn = true;
		set_sound_mute(false);
	}

	idle_timer = refresh_ms_time();
}
// end edit

// primarily to disable joystick use if user wishes not to use one
void set_use_joystick(bool val)
{
	g_use_joystick = val;
}
