/*--------------------------------------------------
   TGB Dual - Gameboy Emulator -
   Copyright (C) 2001  Hii

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version 2
   of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

// libretro implementation of the renderer, should probably be renamed from dmy.

#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>

#include "dmy_renderer.h"
#include "../gb_core/gb.h"
#include "libretro.h"

extern gb *g_gb[2];

extern retro_video_refresh_t video_cb;
extern retro_audio_sample_batch_t audio_batch_cb;
extern retro_input_poll_t input_poll_cb;
extern retro_input_state_t input_state_cb;
extern retro_environment_t environ_cb;

#define MSG_FRAMES 60
#define SAMPLES_PER_FRAME (44100/60)

static inline bool button_pressed(int pad, int btn) {
	static bool held[16] = {
		false, false, false, false, false, false, false, false,
		false, false, false, false, false, false, false, false,
	};
	if ( input_state_cb(pad, 1,0, btn) ) {
		if ( ! held[btn] ) {
			held[btn] = true;
			return true;
		}
	} else {
		held[btn] = false;
	}
	return false;
}

dmy_renderer::dmy_renderer(int which)
{
	which_gb = which;
}

dmy_renderer::~dmy_renderer()
{
}

word dmy_renderer::map_color(word gb_col)
{
	return ((gb_col&0x1F)<<10)|(gb_col&0x3e0)|((gb_col&0x7c00)>>10);
}

word dmy_renderer::unmap_color(word gb_col)
{
	return ((gb_col&0x1F)<<10)|(gb_col&0x3e0)|((gb_col&0x7c00)>>10);
}

void dmy_renderer::refresh() {
	static int16_t stream[SAMPLES_PER_FRAME*2];

	static int audio_2p_mode = 0;
	struct retro_message audio_2p_mode_descriptions[] = {
		{ "Audio: only playing P1",   MSG_FRAMES },
		{ "Audio: only playing P2",   MSG_FRAMES },
		{ "Audio: P1 left, P2 right", MSG_FRAMES },
		{ "Audio: silence",           MSG_FRAMES },
	};

	if (which_gb == 0) {
		// only once per frame, even in dual gb mode.
		input_poll_cb();
	}

	if (g_gb[1]) { // if dual gb mode
		if (audio_2p_mode == 2) {
			// mix down to one per channel (dual mono)
			int16_t tmp_stream[SAMPLES_PER_FRAME*2];
			this->snd_render->render(tmp_stream, SAMPLES_PER_FRAME);
			for(int i = 0; i < SAMPLES_PER_FRAME; ++i) {
				int l = tmp_stream[(i*2)+0], r = tmp_stream[(i*2)+1];
				stream[(i*2)+which_gb] = int16_t( (l+r) / 2 );
			}
		} else if (audio_2p_mode == which_gb) {
			// only play gb 0 or 1
			this->snd_render->render(stream, SAMPLES_PER_FRAME);
		}
		if (which_gb == 1) {
			// only do audio callback after both gb's are rendered.
			audio_batch_cb(stream, SAMPLES_PER_FRAME);

			// switch the playback mode with L/R
			if ( button_pressed(0, RETRO_DEVICE_ID_JOYPAD_L) ) {
				--audio_2p_mode;
			} else if ( button_pressed(0, RETRO_DEVICE_ID_JOYPAD_R) ) {
				++audio_2p_mode;
			} else {
				goto no_change;
			}
			audio_2p_mode &= 3;
			memset(stream, 0, sizeof(stream));
			environ_cb( RETRO_ENVIRONMENT_SET_MESSAGE,
			            &audio_2p_mode_descriptions[audio_2p_mode] );
		no_change:
			;
		}
	} else {
		this->snd_render->render(stream, SAMPLES_PER_FRAME);
		audio_batch_cb(stream, SAMPLES_PER_FRAME);
	}
	fixed_time = time(NULL);
}

int dmy_renderer::check_pad()
{
	// a,b,select,start,down,up,left,right
	return
	(!!input_state_cb(which_gb,1,0, RETRO_DEVICE_ID_JOYPAD_A)     ) << 0 |
	(!!input_state_cb(which_gb,1,0, RETRO_DEVICE_ID_JOYPAD_B)     ) << 1 |
	(!!input_state_cb(which_gb,1,0, RETRO_DEVICE_ID_JOYPAD_SELECT)) << 2 |
	(!!input_state_cb(which_gb,1,0, RETRO_DEVICE_ID_JOYPAD_START) ) << 3 |
	(!!input_state_cb(which_gb,1,0, RETRO_DEVICE_ID_JOYPAD_DOWN)  ) << 4 |
	(!!input_state_cb(which_gb,1,0, RETRO_DEVICE_ID_JOYPAD_UP)    ) << 5 |
	(!!input_state_cb(which_gb,1,0, RETRO_DEVICE_ID_JOYPAD_LEFT)  ) << 6 |
	(!!input_state_cb(which_gb,1,0, RETRO_DEVICE_ID_JOYPAD_RIGHT) ) << 7;
}

void dmy_renderer::render_screen(byte *buf,int width,int height,int depth) {
	static byte joined_buf[160*144*2*2]; // two screens' worth of 16-bit data
	const int half = sizeof(joined_buf)/2;
	int pitch = width*((depth+7)/8);
	if(g_gb[1]) { // are we running two gb's?
		#ifdef VERTICAL
		memcpy(joined_buf + which_gb*half, buf, half);
		if(which_gb == 1) {
			video_cb(joined_buf, width, height*2, pitch);
		}
		#else
		for (int row = 0; row < height; ++row) {
			memcpy(joined_buf + pitch*(2*row + which_gb), buf+pitch*row, pitch);
		}
		if(which_gb == 1) {
			video_cb(joined_buf, width*2, height, pitch*2);
		}
		#endif
	} else {
		video_cb(buf, width, height, pitch);
	}
}

byte dmy_renderer::get_time(int type)
{
	dword now=fixed_time-cur_time;

	switch(type){
	case 8: // second
		return (byte)(now%60);
	case 9: // minute
		return (byte)((now/60)%60);
	case 10: // hour
		return (byte)((now/(60*60))%24);
	case 11: // day (L)
		return (byte)((now/(24*60*60))&0xff);
	case 12: // day (H)
		return (byte)((now/(256*24*60*60))&1);
	}
	return 0;
}

void dmy_renderer::set_time(int type,byte dat)
{
	dword now=fixed_time;
	dword adj=now-cur_time;

	switch(type){
	case 8: // second
		adj=(adj/60)*60+(dat%60);
		break;
	case 9: // minute
		adj=(adj/(60*60))*60*60+(dat%60)*60+(adj%60);
		break;
	case 10: // hour
		adj=(adj/(24*60*60))*24*60*60+(dat%24)*60*60+(adj%(60*60));
		break;
	case 11: // day (L)
		adj=(adj/(256*24*60*60))*256*24*60*60+(dat*24*60*60)+(adj%(24*60*60));
		break;
	case 12: // day (H)
		adj=(dat&1)*256*24*60*60+(adj%(256*24*60*60));
		break;
	}
	cur_time=now-adj;
}

