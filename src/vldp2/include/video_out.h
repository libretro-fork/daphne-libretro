/*
 * video_out.h
 * Copyright (C) 2000-2002 Michel Lespinasse <walken@zoy.org>
 * Copyright (C) 1999-2000 Aaron Holtzman <aholtzma@ess.engr.uvic.ca>
 *
 * This file is part of mpeg2dec, a free MPEG-2 video stream decoder.
 * See http://libmpeg2.sourceforge.net/ for updates.
 *
 * mpeg2dec is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * mpeg2dec is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef VIDEO_OUT_H
#define VIDEO_OUT_H

struct convert_init_s;
typedef struct {
    void (* convert) (int, int, uint32_t, void *, struct convert_init_s *);
} vo_setup_result_t;

typedef struct vo_instance_s vo_instance_t;
struct vo_instance_s {
    int (* setup) (vo_instance_t * instance, int width, int height,
		   vo_setup_result_t * result);
    void (* setup_fbuf) (vo_instance_t * instance, uint8_t ** buf, void ** id);
    void (* set_fbuf) (vo_instance_t * instance, uint8_t ** buf, void ** id);
    void (* start_fbuf) (vo_instance_t * instance,
			 uint8_t * const * buf, void * id);
    void (* draw) (vo_instance_t * instance, uint8_t * const * buf, void * id);
    void (* discard) (vo_instance_t * instance,
		      uint8_t * const * buf, void * id);
    void (* close) (vo_instance_t * instance);
};

typedef vo_instance_t * vo_open_t (void);

// RJS ADD - not sure how it vo940 was supposed to see this routine
vo_instance_t * vo940_open(void);

typedef struct {
    char * name;
    vo_open_t * open;
} vo_driver_t;

void vo_accel (uint32_t accel);

/* return NULL terminated array of all drivers */
vo_driver_t * vo_drivers (void);

vo_instance_t * vo_null_open (void);	// MPO added because this isn't declared elsewhere ...

#endif /* VIDEO_OUT_H */
