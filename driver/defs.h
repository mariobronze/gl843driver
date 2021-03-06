#ifndef _GL843_DEFS_H_
#define _GL843_DEFS_H_

enum gl843_pixformat {	/* Note; Format enumerations == bits per pixel */
	PXFMT_UNDEFINED = 0,
	PXFMT_LINEART = 1,	/* 1 bit per pixel, black and white */
	PXFMT_GRAY8 = 8,	/* 8 bits per pixel, grayscale */
	PXFMT_GRAY16 = 16,	/* 16 bits per pixel, grayscale */
	PXFMT_RGB8 = 24,	/* 24 bits per pixel, RGB color */
	PXFMT_RGB16 = 48,	/* 48 bits per pixel, RGB color */
};

struct gl843_image {
	int bpp;		/* Bits per pixel 1, 8, 16, 24 or 48 */
	int width;		/* Pixels per line */
	int stride;		/* Bytes per line */
	int height;		/* Number of lines */
	size_t len;		/* Data buffer length, in bytes */
	uint8_t data[0];	/* Data buffer follows */
};

enum gl843_lamp {
	LAMP_OFF,
	LAMP_PLATEN,	/* Flatbed lamp */
	LAMP_TA		/* Transparency adapter lamp */
};

enum gl843_shading {
	SHADING_CORR_OFF,	/* Disable shading correction */
	SHADING_CORR_AREA,	/* Enable area shading correction */
	SHADING_CORR_LINE	/* Enable full-line shading correction */
};

enum gl843_sysclk {
	SYSCLK_24_MHZ = 0,
	SYSCLK_30_MHZ = 1,
	SYSCLK_40_MHZ = 2,
	SYSCLK_48_MHZ = 3,
	SYSCLK_60_MHZ = 4
};

enum motor_steptype {
	FULL_STEP = 0,
	HALF_STEP = 1,
	QUARTER_STEP = 2,
	EIGHTH_STEP = 3
};

/* This must be 2 to get the biggest possible accel tables (1020 entries). */
#define STEPTIM 2
/* Must be 1020 (hardware limit). */
#define MTRTBL_SIZE 1020

struct motor_accel {
	uint16_t c_start;		/* Start speed [counter ticks/step]. */
	uint16_t c_end;			/* End speed [counter ticks/step]. */
	unsigned int alen;		/* Number of steps to reach max_speed.
					 * The constructor must ensure that the
					 * value is  divisible by 2^STEPTIM. */
	unsigned int t_max;		/* Sum of a[0] to a[alen - 1] */
	uint16_t a[MTRTBL_SIZE];	/* The acceleration table */
};

struct scan_setup {

	/* Generic parameters */

	enum gl843_lamp source;
	enum gl843_pixformat fmt;
	int dpi;
	int start_x;		/* Pixels from left edge */
	int width;		/* Pixels per line */
	int start_y;		/* Steps from top edge */
	int height;		/* Lines */
	int overscan;		/* Extra lines for correcting CCD RGB line distances */
	float bwthr;		/* Black/white threshold (0.0 - 1.0) */
	float bwhys;		/* Black/white hysteresis (0.0 - 1.0) */
	int use_backtracking;

	/* Hardware-specific parameters */

	enum motor_steptype steptype;
	int step_dpi;
	int lperiod;
	int linesel;
};

extern int usleep();

#endif /* _GL843_DEFS_H_ */
