/*
 * Copyright (C) 2009-2010 Andreas Robinson <andr345 at gmail dot com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 */

#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <sane/sane.h>
#include <sane/saneopts.h>

#include "low.h"
#include "sanei.h"
#include "util.h"
#include "util.h"
#include "low.h"
#include "cs4400f.h"
#include "main.h"
#include "scan.h"


/* List of scanners recognized by the driver */
const Scanner_Model g_known_models[] =
{
	{
		.vendor = "Canon",
		.model = "CanonScan 4400F",
		.type = SANE_I18N("flatbed scanner"),
		.vid = 0x04a9, .pid = 0x2228,
		.name = "cs4400f",
	},
	{
		/* End-of-list marker */
		"", "", "", 0, 0, "",
	}
};

/* CanoScan 4400F properties */

#define SANE_VALUE_SCAN_SOURCE_PLATEN	SANE_I18N("Flatbed")
#define SANE_VALUE_SCAN_SOURCE_TA	SANE_I18N("Film")

const SANE_Int cs4400f_sources[] = { 2, LAMP_PLATEN, LAMP_TA };
const SANE_String_Const cs4400f_source_names[] = {
	SANE_VALUE_SCAN_SOURCE_PLATEN, SANE_VALUE_SCAN_SOURCE_TA, NULL };
const SANE_Int cs4400f_modes[] = { 2, SANE_FRAME_GRAY, SANE_FRAME_RGB };
const SANE_String_Const cs4400f_mode_names[] = {
	SANE_VALUE_SCAN_MODE_GRAY, SANE_VALUE_SCAN_MODE_COLOR, NULL };
const SANE_Int cs4400f_bit_depths[]  = { 2, 8, 16 };
const SANE_Int cs4400f_resolutions[] = { 8, 80, 100, 150, 200, 300, 400, 600, 1200 };
const SANE_Range cs4400f_x_limit     = { SANE_FIX(0.0), SANE_FIX(216.0), 0 };
const SANE_Range cs4400f_y_limit     = { SANE_FIX(0.0), SANE_FIX(297.5), 0 };
const SANE_Fixed cs4400f_x_start     = SANE_FIX(2.7);  /* Platen left edge */
const SANE_Fixed cs4400f_y_start     = SANE_FIX(11.7); /* Platen top edge */
const SANE_Fixed cs4400f_y_calpos    = SANE_FIX(5.0);
const SANE_Range cs4400f_x_limit_ta  = { SANE_FIX(0.0), SANE_FIX(24.0), 0 };
const SANE_Range cs4400f_y_limit_ta  = { SANE_FIX(0.0), SANE_FIX(226.0), 0 };
const SANE_Fixed cs4400f_x_start_ta  = SANE_FIX(97.0);  /* TA left edge */
const SANE_Fixed cs4400f_y_start_ta  = SANE_FIX(31.0); /* TA top edge */
const SANE_Fixed cs4400f_y_calpos_ta = SANE_FIX(13.0);

/* Backend globals */

static libusb_context *g_libusb_ctx;
static SANE_USB_Device **g_scanners;
extern int g_dbg_level; /* util.c */

/* Functions */

static size_t max_string_size(const SANE_String_Const *strings)
{
	size_t size, max_size = 0;

	for (; *strings != NULL; strings++) {
		size = strlen(*strings) + 1;
		if (size > max_size)
			max_size = size;
	}
	return max_size;
}

/* Get index of constraint s in string list */
static int find_constraint_string(SANE_String s, const SANE_String_Const *strings)
{
	int i;
	for (i = 0; *strings != NULL; strings++) {
		if (strcmp(s, *strings) == 0)
			return i;
	}
	DBG(DBG_error0, "BUG: unknown constraint string %s\n", s);
	return 0; /* Default to first string */
}

/* Get index of constraint v in word array */
static int find_constraint_value(SANE_Word v, const SANE_Word *values)
{
	int i, N;
	N = *values++;
	for (i = 1; i <= N; i++) {
		if (v == *values++)
			return i;
	}
	DBG(DBG_error0, "BUG: unknown constraint value %d\n", v);
	return 1; /* Default to first value */
}

static SANE_Word *create_gamma(int N, float gamma)
{
	int k;
	SANE_Word *g;

	if (gamma < 0.01)
		gamma = 0.01;
	g = malloc(N * sizeof(SANE_Word));
	if (!g)
		return NULL;

	for (k = 0; k < N; k++) {
		g[k] = (uint16_t) (65535 * powf((float)k / N, 1/gamma) + 0.5);
	}
	return g;
}

static void destroy_CS4400F(CS4400F_Scanner *s)
{
	if (!s)
		return;

	free(s->gray_gamma);
	free(s->red_gamma);
	free(s->green_gamma);
	free(s->blue_gamma);

	/* TODO: Destroy gl843_device */

	memset(s, 0, sizeof(*s));
	free(s);
}

static CS4400F_Scanner *create_CS4400F()
{
	int i;
	const int gamma_len = 256;
	float default_gamma = 1.0;
	SANE_Option_Descriptor *opt;
	CS4400F_Scanner *s;

	CHK_MEM(s = calloc(sizeof(CS4400F_Scanner), 1));

	/** Scanner properties **/

	s->sources      = cs4400f_sources;
	s->source_names = cs4400f_source_names;
	s->modes        = cs4400f_modes;
	s->mode_names   = cs4400f_mode_names;
	s->bit_depths   = cs4400f_bit_depths;
	s->resolutions  = cs4400f_resolutions;

	s->x_limit      = cs4400f_x_limit;
	s->y_limit      = cs4400f_y_limit;
	s->x_start      = cs4400f_x_start;
	s->y_start      = cs4400f_y_start;
	s->y_calpos     = cs4400f_y_calpos;

	s->x_limit_ta   = cs4400f_x_limit_ta;
	s->y_limit_ta   = cs4400f_y_limit_ta;
	s->x_start_ta   = cs4400f_x_start_ta;
	s->y_start_ta   = cs4400f_y_start_ta;
	s->y_calpos_ta  = cs4400f_y_calpos_ta;

	/** Scanning settings **/

	s->source = LAMP_PLATEN;
	s->lamp_to_lim = (SANE_Range){ 0, 15, 0 };
	s->lamp_timeout = 15;

	s->x_scan_lim.min = SANE_FIX(0.0);
	s->x_scan_lim.max = s->x_limit.max - s->x_limit.min,
	s->x_scan_lim.quant = 0;

	s->y_scan_lim.min = SANE_FIX(0.0);
	s->y_scan_lim.max = s->y_limit.max - s->y_limit.min,
	s->y_scan_lim.quant = 0;

	s->tl_x = SANE_FIX(0.0);
	s->tl_y = SANE_FIX(0.0);
	s->br_x = s->x_scan_lim.max;
	s->br_y = s->y_scan_lim.max;

	s->mode = SANE_FRAME_RGB;
	s->depth = 16;
	s->dpi = 300;

	s->use_gamma = SANE_FALSE;
	s->gamma_range  = (SANE_Range){ 0, 65535, 0 };
	s->gamma_len = gamma_len;
	CHK_MEM(s->gray_gamma = create_gamma(gamma_len, default_gamma));
	CHK_MEM(s->red_gamma = create_gamma(gamma_len, default_gamma));
	CHK_MEM(s->green_gamma = create_gamma(gamma_len, default_gamma));
	CHK_MEM(s->blue_gamma = create_gamma(gamma_len, default_gamma));

	s->bw_range = (SANE_Range){ SANE_FIX(0.0), SANE_FIX(100.0), 0 };
	s->bw_threshold = SANE_FIX(50.0);
	s->bw_hysteresis = SANE_FIX(0.0);

	/** SANE options **/

	for (i = 0; i < OPT_NUM_OPTIONS; i++)
		s->opt[i].cap = SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT;

	/* Number of options */

	opt = s->opt + OPT_NUM_OPTS;

	opt->title = SANE_TITLE_NUM_OPTIONS;
	opt->desc = SANE_DESC_NUM_OPTIONS;
	opt->type = SANE_TYPE_INT;
	opt->size = sizeof(SANE_Word);
	opt->cap = SANE_CAP_SOFT_DETECT;

	/* Standard options */

	opt = s->opt + OPT_MODE_GROUP;

	opt->title = SANE_TITLE_STANDARD;
	opt->desc = SANE_DESC_STANDARD;
	opt->type = SANE_TYPE_GROUP;
	opt->size = sizeof(SANE_Word);
	opt->cap = 0;
	opt->constraint_type = SANE_CONSTRAINT_NONE;

	/* color modes */

	opt = s->opt + OPT_MODE;

	opt->name = SANE_NAME_SCAN_MODE;
	opt->title = SANE_TITLE_SCAN_MODE;
	opt->desc = SANE_DESC_SCAN_MODE;
	opt->type = SANE_TYPE_STRING;
	opt->size = max_string_size(s->mode_names);
	opt->cap |= 0;
	opt->constraint_type = SANE_CONSTRAINT_STRING_LIST;
	opt->constraint.string_list = s->mode_names;

	/* sources */

	opt = s->opt + OPT_SOURCE;

	opt->name  = SANE_NAME_SCAN_SOURCE;
	opt->title = SANE_TITLE_SCAN_SOURCE;
	opt->desc = SANE_DESC_SCAN_SOURCE;
	opt->type = SANE_TYPE_STRING;
	opt->size = max_string_size(s->source_names);
	opt->cap |= 0;
	opt->constraint_type = SANE_CONSTRAINT_STRING_LIST;
	opt->constraint.string_list = s->source_names;

	/* bit depths */

	opt = s->opt + OPT_BIT_DEPTH;

	opt->name  = SANE_NAME_BIT_DEPTH;
	opt->title = SANE_TITLE_BIT_DEPTH;
	opt->desc = SANE_DESC_BIT_DEPTH;
	opt->type = SANE_TYPE_INT;
	opt->unit = SANE_UNIT_BIT;
	opt->size = sizeof(SANE_Word);
	opt->cap |= 0;
	opt->constraint_type = SANE_CONSTRAINT_WORD_LIST;
	opt->constraint.word_list = s->bit_depths;

	/* resolutions */

	opt = s->opt + OPT_RESOLUTION;

	opt->name = SANE_NAME_SCAN_RESOLUTION;
	opt->title = SANE_TITLE_SCAN_RESOLUTION;
	opt->desc = SANE_DESC_SCAN_RESOLUTION;
	opt->type = SANE_TYPE_INT;
	opt->unit = SANE_UNIT_DPI;
	opt->size = sizeof(SANE_Word);
	opt->cap |= 0;
	opt->constraint_type = SANE_CONSTRAINT_WORD_LIST;
	opt->constraint.word_list = s->resolutions;

  	/* Geometry options */

	opt = s->opt + OPT_GEOMETRY_GROUP;

	opt->title = SANE_TITLE_GEOMETRY;
	opt->desc = SANE_DESC_GEOMETRY;
	opt->type = SANE_TYPE_GROUP;
	opt->size = sizeof(SANE_Word);
	opt->cap = 0;
	opt->constraint_type = SANE_CONSTRAINT_NONE;

	/* left */

	opt = s->opt + OPT_TL_X;

	opt->name = SANE_NAME_SCAN_TL_X;
	opt->title = SANE_TITLE_SCAN_TL_X;
	opt->desc = SANE_DESC_SCAN_TL_X;
	opt->type = SANE_TYPE_FIXED;
	opt->size = sizeof(SANE_Fixed);
	opt->cap |= 0;
	opt->unit = SANE_UNIT_MM;
	opt->constraint_type = SANE_CONSTRAINT_RANGE;
	opt->constraint.range = &s->x_limit;

	/* top */

	opt = s->opt + OPT_TL_Y;

	opt->name = SANE_NAME_SCAN_TL_Y;
	opt->title = SANE_TITLE_SCAN_TL_Y;
	opt->desc = SANE_DESC_SCAN_TL_Y;
	opt->type = SANE_TYPE_FIXED;
	opt->unit = SANE_UNIT_MM;
	opt->size = sizeof(SANE_Fixed);
	opt->cap |= 0;
	opt->constraint_type = SANE_CONSTRAINT_RANGE;
	opt->constraint.range = &s->y_limit;

	/* right */

	opt = s->opt + OPT_BR_X;

	opt->name = SANE_NAME_SCAN_BR_X;
	opt->title = SANE_TITLE_SCAN_BR_X;
	opt->desc = SANE_DESC_SCAN_BR_X;
	opt->type = SANE_TYPE_FIXED;
	opt->unit = SANE_UNIT_MM;
	opt->size = sizeof(SANE_Fixed);
	opt->cap |= 0;
	opt->constraint_type = SANE_CONSTRAINT_RANGE;
	opt->constraint.range = &s->x_limit;

	/* bottom */

	opt = s->opt + OPT_BR_Y;

	opt->name = SANE_NAME_SCAN_BR_Y;
	opt->title = SANE_TITLE_SCAN_BR_Y;
	opt->desc = SANE_DESC_SCAN_BR_Y;
	opt->type = SANE_TYPE_FIXED;
	opt->unit = SANE_UNIT_MM;
	opt->size = sizeof(SANE_Fixed);
	opt->cap |= 0;
	opt->constraint_type = SANE_CONSTRAINT_RANGE;
	opt->constraint.range = &s->y_limit;

	/* Enhancement options */

	opt = s->opt + OPT_ENHANCEMENT_GROUP;

	opt->title = SANE_TITLE_ENHANCEMENT;
	opt->desc = SANE_DESC_ENHANCEMENT;
	opt->type = SANE_TYPE_GROUP;
	opt->size = 0;
	opt->cap = SANE_CAP_ADVANCED;
	opt->constraint_type = SANE_CONSTRAINT_NONE;

	/* enable/disable gamma correction */

	opt = s->opt + OPT_CUSTOM_GAMMA;

	opt->name = SANE_NAME_CUSTOM_GAMMA;
	opt->title = SANE_TITLE_CUSTOM_GAMMA;
	opt->desc = SANE_DESC_CUSTOM_GAMMA;
	opt->type = SANE_TYPE_BOOL;
	opt->cap |= SANE_CAP_ADVANCED;

	/* grayscale gamma vector */

	opt = s->opt + OPT_GAMMA_VECTOR;

	opt->name = SANE_NAME_GAMMA_VECTOR;
	opt->title = SANE_TITLE_GAMMA_VECTOR;
	opt->desc = SANE_DESC_GAMMA_VECTOR;
	opt->type = SANE_TYPE_INT;
	opt->unit = SANE_UNIT_NONE;
	opt->size = s->gamma_len * sizeof(SANE_Word);
	opt->cap |= SANE_CAP_INACTIVE | SANE_CAP_ADVANCED;
	opt->constraint_type = SANE_CONSTRAINT_RANGE;
	opt->constraint.range = &s->gamma_range;

	/* red gamma vector */

	opt = s->opt + OPT_GAMMA_VECTOR_R;

	opt->name = SANE_NAME_GAMMA_VECTOR_R;
	opt->title = SANE_TITLE_GAMMA_VECTOR_R;
	opt->desc = SANE_DESC_GAMMA_VECTOR_R;
	opt->type = SANE_TYPE_INT;
	opt->unit = SANE_UNIT_NONE;
	opt->size = s->gamma_len * sizeof(SANE_Word);
	opt->cap |= SANE_CAP_INACTIVE | SANE_CAP_ADVANCED;
	opt->constraint_type = SANE_CONSTRAINT_RANGE;
	opt->constraint.range = &s->gamma_range;

	/* green gamma vector */

	opt = s->opt + OPT_GAMMA_VECTOR_G;

	opt->name = SANE_NAME_GAMMA_VECTOR_G;
	opt->title = SANE_TITLE_GAMMA_VECTOR_G;
	opt->desc = SANE_DESC_GAMMA_VECTOR_G;
	opt->type = SANE_TYPE_INT;
	opt->unit = SANE_UNIT_NONE;
	opt->size = s->gamma_len * sizeof(SANE_Word);
	opt->cap |= SANE_CAP_INACTIVE | SANE_CAP_ADVANCED;
	opt->constraint_type = SANE_CONSTRAINT_RANGE;
	opt->constraint.range = &s->gamma_range;

	/* blue gamma vector */

	opt = s->opt + OPT_GAMMA_VECTOR_B;

	opt->name = SANE_NAME_GAMMA_VECTOR_B;
	opt->title = SANE_TITLE_GAMMA_VECTOR_B;
	opt->desc = SANE_DESC_GAMMA_VECTOR_B;
	opt->type = SANE_TYPE_INT;
	opt->size = s->gamma_len * sizeof(SANE_Word);
	opt->cap |= SANE_CAP_INACTIVE | SANE_CAP_ADVANCED;
	opt->unit = SANE_UNIT_NONE;
	opt->constraint_type = SANE_CONSTRAINT_RANGE;
	opt->constraint.range = &s->gamma_range;

	s->need_warmup = SANE_TRUE;
	s->need_shading = SANE_TRUE;
	s->is_scanning = SANE_FALSE;

	return s;

chk_mem_failed:
	DBG(DBG_error0, "Out of memory\n");
	destroy_CS4400F(s);
	return NULL;
}

static void dump_scan_settings(CS4400F_Scanner *s)
{
	int i, j;
	SANE_Parameters p;

	i = find_constraint_value(s->mode, s->modes) - 1;
	j = find_constraint_value(s->source, s->sources) - 1;
	sane_get_parameters(s, &p);

	DBG(DBG_info, "Current scanning settings\n");
	DBG(DBG_info, "-------------------------\n");
	DBG(DBG_info, "       mode: %s\n", s->mode_names[i]);
	DBG(DBG_info, "     source: %s\n", s->source_names[j]);
	DBG(DBG_info, "      depth: %d [bits]\n", s->depth);
	DBG(DBG_info, " resolution: %d [dpi]\n", s->dpi);
	DBG(DBG_info, "        top: %.1f [mm]\n", SANE_UNFIX(s->tl_y));
	DBG(DBG_info, "       left: %.1f [mm]\n", SANE_UNFIX(s->tl_x));
	DBG(DBG_info, "     bottom: %.1f [mm]\n", SANE_UNFIX(s->br_y));
	DBG(DBG_info, "      right: %.1f [mm]\n", SANE_UNFIX(s->br_x));
	DBG(DBG_info, "gamma corr.: %s\n", s->use_gamma ? "yes" : "no");
	DBG(DBG_info, "-------------------------\n");
	DBG(DBG_info, "pixels/line: %d\n", p.pixels_per_line);
	DBG(DBG_info, " bytes/line: %d\n", p.bytes_per_line);
	DBG(DBG_info, "      lines: %d\n", p.lines);
	DBG(DBG_info, "-------------------------\n");
}

static void free_sane_usb_dev(SANE_USB_Device *s)
{
	if (!s)
		return;

	libusb_unref_device(s->usbdev);
	free((void*)s->sane_dev.name);
	memset(s, 0, sizeof(*s));
	free(s);
}

static SANE_USB_Device *mk_sane_usb_dev(const Scanner_Model *model,
					libusb_device *usbdev)
{
	int ret;
	SANE_USB_Device *d;

	CHK_MEM(d = calloc(sizeof(*d), 1));

	CHK(asprintf((char **) &d->sane_dev.name, "%s:%03d:%03d", model->name,
		libusb_get_bus_number(usbdev), libusb_get_device_address(usbdev)));

	d->sane_dev.vendor = model->vendor;
	d->sane_dev.model = model->model;
	d->sane_dev.type = model->type;
	d->usbdev = libusb_ref_device(usbdev);
	return d;

chk_failed:
chk_mem_failed:
	free_sane_usb_dev(d);
	return NULL;
}

static void free_sane_usb_devs(SANE_USB_Device **devs)
{
	SANE_USB_Device **d;

	if (!devs)
		return;

	for (d = devs; *d != NULL; d++)
		free_sane_usb_dev(*d);

	free(devs);
}

#ifndef DRIVER_BUILD
#error DRIVER_BUILD number is undefined
#endif

SANE_Status sane_init(SANE_Int* version_code,
		      SANE_Auth_Callback authorize)
{
	int ret;

	if (version_code)
		*version_code = SANE_VERSION_CODE(1,0,DRIVER_BUILD);

	g_scanners = NULL;

	init_debug("GL843", -1);
	ret = libusb_init(&g_libusb_ctx);
	if (ret != LIBUSB_SUCCESS) {
		DBG(DBG_error0, "Cannot initialize libusb: %s",
			sanei_libusb_strerror(ret));
		return SANE_STATUS_IO_ERROR;
	}

	if (g_dbg_level > 0)
		libusb_set_debug(g_libusb_ctx, 2);

	return SANE_STATUS_GOOD;
}

void sane_exit()
{
	if (g_libusb_ctx) {
		libusb_exit(g_libusb_ctx);
		g_libusb_ctx = NULL;
	}
	free_sane_usb_devs(g_scanners);
}

SANE_Status sane_get_devices(const SANE_Device ***device_list,
			     SANE_Bool local_only)
{
	int ret;
	int i, n;
	struct libusb_device **usbdevs = NULL;

	/* Clear current device list */

	free_sane_usb_devs(g_scanners);
	CHK_MEM(g_scanners = calloc(sizeof(SANE_USB_Device*), 1));

	/* Scan all USB devices */

	CHK(libusb_get_device_list(g_libusb_ctx, &usbdevs));

	n = 0;
	for (i = 0; usbdevs[i] != NULL; i++) {

		struct libusb_device_descriptor dd;
		SANE_USB_Device **tmp;
		const Scanner_Model *m;

		/* Find matching scanner model */

		CHK(libusb_get_device_descriptor(usbdevs[i], &dd));

		for (m = g_known_models; m->vid != 0; m++) {
			if (m->vid == dd.idVendor && m->pid == dd.idProduct)
				break;
		}
		if (m->vid == 0)
			continue;

		/* Enumerate found scanner */

		DBG(DBG_info, "found USB device 0x%04x:0x%04x\n",
			dd.idVendor, dd.idProduct);

		CHK_MEM(tmp = realloc(g_scanners, (n+2)*sizeof(*tmp)));
		g_scanners = tmp;
		CHK_MEM(g_scanners[n] = mk_sane_usb_dev(m, usbdevs[i]));
		n++;
	}
	g_scanners[n] = NULL; /* Add list terminator */

	if (device_list)
		*device_list = (const SANE_Device **)g_scanners;

	libusb_free_device_list(usbdevs, 1);
	return SANE_STATUS_GOOD;

chk_mem_failed:
	ret = LIBUSB_ERROR_NO_MEM;
chk_failed:
	if (usbdevs)
		libusb_free_device_list(usbdevs, 1);
	free_sane_usb_devs(g_scanners);
	DBG(DBG_error, "Device enumeration failed: %s\n",
		sanei_libusb_strerror(ret));
	return SANE_STATUS_IO_ERROR;
}

static void destroy_scanner(CS4400F_Scanner *s)
{
	if (!s)
		return;
	if (s->hw) {
		libusb_close(s->hw->usbdev);
		destroy_gl843dev(s->hw);
	}
	memset(s, 0, sizeof(*s));
	free(s);
}

static SANE_Status create_scanner(libusb_device *usbdev, CS4400F_Scanner **scanner)
{
	int ret;
	libusb_device_handle *h;
	CS4400F_Scanner *s = NULL;

	CHK(libusb_open(usbdev, &h));
	CHK(libusb_set_configuration(h, 1));
	CHK(libusb_claim_interface(h, 0));
	CHK_MEM(s = create_CS4400F());
	CHK_MEM(s->hw = create_gl843dev(h));
	CHK(setup_static(s->hw));
	/* Begin warming up the lamp before the user configures the scan.
	 * This can save time later. */
	CHK(set_lamp(s->hw, s->source, s->lamp_timeout));

	*scanner = s;
	return SANE_STATUS_GOOD;

chk_mem_failed:
	destroy_scanner(s);
	return SANE_STATUS_NO_MEM;
chk_failed:
	destroy_scanner(s);
	return SANE_STATUS_IO_ERROR;
}

SANE_Status sane_open(SANE_String_Const devicename,
		      SANE_Handle *handle)
{
	int i;
	int ret;
	SANE_USB_Device *dev = NULL;
	CS4400F_Scanner *s;

	DBG(DBG_msg, "opening %s.\n", devicename);

	if (g_scanners == NULL)
		CHK_SANE(sane_get_devices(NULL, SANE_TRUE));

	/* Find scanner in device list */

	if (strlen(devicename) == 0 || strcmp(devicename, "auto") == 0) {
		dev = g_scanners[0];
	} else {
		for (i = 0; g_scanners[i] != NULL; i++) {
			if (strcmp(devicename, g_scanners[i]->sane_dev.name) == 0) {
				dev = g_scanners[i];
				break;
			}
		}
	}

	if (dev == NULL) {
		DBG(DBG_warn, "device not found\n");
		return SANE_STATUS_INVAL; /* No device found */
	}

	CHK_SANE(create_scanner(dev->usbdev, &s));

	*handle = s;
	return SANE_STATUS_GOOD;

chk_sane_failed:
	return ret;
}

void sane_close(SANE_Handle handle)
{
	CS4400F_Scanner *s = (CS4400F_Scanner *) handle;
	if (s) {
//		reset_scanner(s->hw);
		destroy_scanner(s);
	}
}

const SANE_Option_Descriptor *sane_get_option_descriptor(SANE_Handle handle,
							 SANE_Int option)
{
	if (option < 0 || option >= OPT_NUM_OPTIONS)
		return NULL;
	return ((CS4400F_Scanner *) handle)->opt + option;
}

static void enable_option(CS4400F_Scanner *s, SANE_Int option)
{
	s->opt[option].cap &= ~SANE_CAP_INACTIVE;
}

static void disable_option(CS4400F_Scanner *s, SANE_Int option)
{
	s->opt[option].cap |= SANE_CAP_INACTIVE;
}

SANE_Status sane_control_option(SANE_Handle handle,
				SANE_Int option,
				SANE_Action action,
				void *value,
				SANE_Int *info)
{
	int i;
	CS4400F_Scanner *s = (CS4400F_Scanner *) handle;
	Option_Value *val = (Option_Value *)value;
	SANE_Int flags = 0;
	SANE_Option_Descriptor *opt;

	if (option < 0 || option >= OPT_NUM_OPTIONS)
		return SANE_STATUS_INVAL;

	opt = &(s->opt[option]);

	/* Getters */

	switch (action) {
	case SANE_ACTION_GET_VALUE:
	{
		if (value == NULL)
			return SANE_STATUS_INVAL;

		switch (option) {
		case OPT_NUM_OPTS:
			val->w = OPT_NUM_OPTIONS;
			break;
		case OPT_MODE:
			i = find_constraint_value(s->mode, s->modes) - 1;
			strcpy(value, s->mode_names[i]);
			break;
		case OPT_SOURCE:
			i = find_constraint_value(s->source, s->sources) - 1;
			strcpy(value, s->source_names[i]);
			break;
		case OPT_BIT_DEPTH:
			val->w = s->depth;
			break;
		case OPT_RESOLUTION:
			val->w = s->dpi;
			break;
		case OPT_TL_X:
			val->w = s->tl_x;
			break;
		case OPT_TL_Y:
			val->w = s->tl_y;
			break;
		case OPT_BR_X:
			val->w = s->br_x;
			break;
		case OPT_BR_Y:
			val->w = s->br_y;
			break;
		case OPT_CUSTOM_GAMMA:
			val->w = s->use_gamma;
			break;
		case OPT_GAMMA_VECTOR:
			memcpy(value, s->gray_gamma,
				s->gamma_len * sizeof(SANE_Word));
			break;
		case OPT_GAMMA_VECTOR_R:
			memcpy(value, s->red_gamma,
				s->gamma_len * sizeof(SANE_Word));
			break;
		case OPT_GAMMA_VECTOR_G:
			memcpy(value, s->green_gamma,
				s->gamma_len * sizeof(SANE_Word));
			break;
		case OPT_GAMMA_VECTOR_B:
			memcpy(value, s->blue_gamma,
				s->gamma_len * sizeof(SANE_Word));
			break;
		default:
			return SANE_STATUS_INVAL;
		}

		return SANE_STATUS_GOOD;
	}

	/* Setters */

	case SANE_ACTION_SET_VALUE:
	{
		SANE_Status ret;

		if (value == NULL)
			return SANE_STATUS_INVAL;

		if (s->is_scanning)
			return SANE_STATUS_DEVICE_BUSY;

		if (!SANE_OPTION_IS_ACTIVE(opt->cap)
				|| !SANE_OPTION_IS_SETTABLE(opt->cap))
			return SANE_STATUS_INVAL;

		ret = sanei_constrain_value(opt, value, info);
		if (ret != SANE_STATUS_GOOD)
			return SANE_STATUS_INVAL;

		switch (option) {
		case OPT_MODE:
			i = find_constraint_string(value, s->mode_names);
			s->mode = s->modes[i+1];
			flags |= SANE_INFO_RELOAD_PARAMS;
			break;
		case OPT_SOURCE:
			i = find_constraint_string(value, s->source_names);
			s->need_warmup |= (s->source != s->sources[i+1]);
			s->need_shading |= s->need_warmup;
			s->source = s->sources[i+1];
			flags |= SANE_INFO_RELOAD_PARAMS;
			break;
		case OPT_BIT_DEPTH:
			s->depth = val->w;
			flags |= SANE_INFO_RELOAD_PARAMS;
			break;
		case OPT_RESOLUTION:
			s->need_shading |= (s->dpi != val->w);
			s->dpi = val->w;
			flags |= SANE_INFO_RELOAD_PARAMS;
			break;
		case OPT_TL_X:
			s->tl_x = val->w;
			flags |= SANE_INFO_RELOAD_PARAMS;
			break;
		case OPT_TL_Y:
			s->tl_y = val->w;
			flags |= SANE_INFO_RELOAD_PARAMS;
			break;
		case OPT_BR_X:
			s->br_x = val->w;
			flags |= SANE_INFO_RELOAD_PARAMS;
			break;
		case OPT_BR_Y:
			s->br_y = val->w;
			flags |= SANE_INFO_RELOAD_PARAMS;
			break;
		case OPT_CUSTOM_GAMMA:
			if (val->w != s->use_gamma)
				flags |= SANE_INFO_RELOAD_OPTIONS;

			s->use_gamma = val->w;

			if (s->use_gamma && s->depth == 16) {
				s->depth = 8;
				flags |= SANE_INFO_RELOAD_PARAMS;
			}

			if (s->use_gamma && s->mode == SANE_FRAME_RGB) {
				/* Enable color gamma, disable gray gamma */
				disable_option(s, OPT_GAMMA_VECTOR);
				enable_option(s, OPT_GAMMA_VECTOR_R);
				enable_option(s, OPT_GAMMA_VECTOR_G);
				enable_option(s, OPT_GAMMA_VECTOR_B);

			} else if (s->use_gamma && s->mode == SANE_FRAME_GRAY) {
				/* Enable gray gamma, disable color gamma */
				enable_option(s, OPT_GAMMA_VECTOR);
				disable_option(s, OPT_GAMMA_VECTOR_R);
				disable_option(s, OPT_GAMMA_VECTOR_G);
				disable_option(s, OPT_GAMMA_VECTOR_B);

			} else {
				/* Disable all gamma */
				disable_option(s, OPT_GAMMA_VECTOR);
				disable_option(s, OPT_GAMMA_VECTOR_R);
				disable_option(s, OPT_GAMMA_VECTOR_G);
				disable_option(s, OPT_GAMMA_VECTOR_B);
			}
			break;
		case OPT_GAMMA_VECTOR:
			memcpy(s->gray_gamma, value,
				s->gamma_len * sizeof(SANE_Word));
			break;
		case OPT_GAMMA_VECTOR_R:
			memcpy(s->red_gamma, value,
				s->gamma_len * sizeof(SANE_Word));
			break;
		case OPT_GAMMA_VECTOR_G:
			memcpy(s->green_gamma, value,
				s->gamma_len * sizeof(SANE_Word));
			break;
		case OPT_GAMMA_VECTOR_B:
			memcpy(s->blue_gamma, value,
				s->gamma_len * sizeof(SANE_Word));
			break;
		default:
			return SANE_STATUS_INVAL;
		}

		if (info)
			*info |= flags;
		return SANE_STATUS_GOOD;
	}

	case SANE_ACTION_SET_AUTO:
		return SANE_STATUS_UNSUPPORTED;	/* Not used */
	default:
		return SANE_STATUS_INVAL;
	}

	/* NOTREACHED */
}

SANE_Status sane_get_parameters(SANE_Handle handle, SANE_Parameters *params)
{
	CS4400F_Scanner *s = (CS4400F_Scanner *) handle;

	params->format = s->mode;
	params->last_frame = SANE_TRUE;
	params->pixels_per_line = mm_to_px(s->tl_x, s->br_x, s->dpi, NULL)
		& ~1; /* Must be even. (CS4400F hardware requirement) */
	params->bytes_per_line = (params->pixels_per_line * s->depth + 7) / 8;
	if (s->mode == SANE_FRAME_RGB)
		params->bytes_per_line *=  3;
	params->lines = mm_to_px(s->tl_y, s->br_y, s->dpi, NULL);
	params->depth = s->depth;

	return SANE_STATUS_GOOD;
}

static SANE_Bool params_ok(CS4400F_Scanner *s)
{
	int ret = SANE_TRUE;
	ret = ret && (s->tl_x < s->br_x);
	ret = ret && (s->tl_y < s->br_y);
	ret = ret && (s->source == LAMP_PLATEN || s->source == LAMP_TA);

	return ret;
}

SANE_Status sane_start(SANE_Handle handle)
{
	int ret;
	CS4400F_Scanner *s = (CS4400F_Scanner *) handle;
	SANE_Parameters p;
	struct scan_setup *ss;
	float cal_y_pos;

	sane_get_parameters(s, &p);

	if (!params_ok(s)) {
		DBG(DBG_error, "Invalid parameter values set. Won't start scan.\n");
		return SANE_STATUS_INVAL;
	}

	ss = &s->setup;
	memset(ss, 0, sizeof(*ss));

	ss->source = s->source;
	ss->fmt = s->depth * (s->mode == SANE_FRAME_RGB ? 3 : 1);
	ss->dpi = s->dpi;

	ss->start_x = mm_to_px(SANE_FIX(0.0), s->x_start + s->tl_x, s->dpi, NULL);
	ss->width = p.pixels_per_line;
	ss->start_y = mm_to_px(SANE_FIX(0.0), s->y_start + s->tl_y, s->dpi, NULL);
	ss->height = p.lines;

	ss->bwthr = SANE_UNFIX(s->bw_threshold) * 255 / 100;
	ss->bwhys = SANE_UNFIX(s->bw_hysteresis) * 255 / 100;

	ss->use_backtracking = 1; /* TODO: Make user controllable */

	if (s->source == LAMP_PLATEN) {
		cal_y_pos = SANE_UNFIX(s->y_calpos);
	} else {
		cal_y_pos = SANE_UNFIX(s->y_calpos_ta);
	}

	/* See if the lamp was turned off */

	ret = read_reg(s->hw, GL843_LAMPSTS);
	CHK(ret);
	if (ret == 0) {  /* Lamp is off */
		DBG(DBG_msg, "The lamp was turned off, will perform warm-up.\n");
		s->need_warmup = SANE_TRUE;
	}

	/* Ensure the head is home. */

	CHK(reset_scanner(s->hw));
	CHK(set_lamp(s->hw, s->source, s->lamp_timeout));
	while(!read_reg(s->hw, GL843_HOMESNR))
		usleep(10000);

	/* Warm up */

	if (s->need_warmup) {
		CHK(warm_up_scanner(s->hw, s->source, s->lamp_timeout, cal_y_pos));
		s->need_warmup = SANE_FALSE;
	}

	/* TODO: Set up shading correction */

	if (s->need_shading) {
		s->need_shading = SANE_FALSE;
	}

	/* TODO: Set up shading correction for current resolution and width */
	/* TODO: Gamma correction */

	s->bytes_left = p.bytes_per_line * p.lines;

	DBG(DBG_msg, "p.bytes_per_line = %d, lines = %d\n",
		p.bytes_per_line, p.lines);
	DBG(DBG_msg, "s->bytes_left = %d\n", s->bytes_left);


	int foo = p.bytes_per_line; // FIXME: Remove foo.
	CHK_MEM(init_line_buffer(s->hw, foo));

	CHK(setup_common(s->hw, ss));
	s->hw->pconv = setup_pixel_converter(ss);
	CHK(setup_horizontal(s->hw, ss));
	CHK(setup_vertical(s->hw, ss, 0));
	CHK(start_scan(s->hw));

	return SANE_STATUS_GOOD;
chk_failed:
	return SANE_STATUS_IO_ERROR;
chk_mem_failed:
	return SANE_STATUS_NO_MEM;
}

SANE_Status sane_read(SANE_Handle handle,
		      SANE_Byte *data,
		      SANE_Int max_length,
		      SANE_Int *length)
{
	int ret;
	CS4400F_Scanner *s = (CS4400F_Scanner *) handle;
	int len;

	if (s->bytes_left <= 0) {
		*length = 0;
		return SANE_STATUS_EOF;
	}

	DBG(DBG_info, "%d bytes left %d bytes requested.\n",
		s->bytes_left, max_length);

	len = s->bytes_left > max_length ? max_length : s->bytes_left;
	CHK(read_pixels(s->hw, data, len, s->setup.fmt, 10000));

	s->bytes_left -= len;
	*length = len;

	return SANE_STATUS_GOOD;
chk_failed:
	return SANE_STATUS_IO_ERROR;
}

void sane_cancel(SANE_Handle handle)
{
	int ret;
	CS4400F_Scanner *s = (CS4400F_Scanner *) handle;
	destroy_pixel_converter(s->hw->pconv);
	s->hw->pconv = NULL;
	CHK(reset_scanner(s->hw));
	CHK(set_lamp(s->hw, s->source, s->lamp_timeout));
chk_failed:
	return;
}

SANE_Status sane_set_io_mode(SANE_Handle handle, SANE_Bool non_blocking)
{
	if (non_blocking == SANE_FALSE)
		return SANE_STATUS_GOOD;
	return SANE_STATUS_UNSUPPORTED;
}

SANE_Status sane_get_select_fd(SANE_Handle handle, SANE_Int *fd)
{
	return SANE_STATUS_UNSUPPORTED;
}


/* DLL exports */

/* GCC 4.x attribute */
#define DLL_EXPORT __attribute__ ((visibility("default")))
#define ENTRY(name) sane_gl843_##name

DLL_EXPORT SANE_Status
ENTRY(init)(SANE_Int* version_code, SANE_Auth_Callback authorize)
{
	return sane_init(version_code, authorize);
}

DLL_EXPORT void
ENTRY(exit)()
{
	sane_exit();
}

DLL_EXPORT SANE_Status
ENTRY(get_devices)(const SANE_Device ***device_list, SANE_Bool local_only)
{
	return sane_get_devices(device_list, local_only);
}

DLL_EXPORT SANE_Status
ENTRY(open)(SANE_String_Const devicename, SANE_Handle *handle)
{
	return sane_open(devicename, handle);
}

DLL_EXPORT
void ENTRY(close)(SANE_Handle handle)
{
	sane_close(handle);
}

DLL_EXPORT const SANE_Option_Descriptor *
ENTRY(get_option_descriptor)(SANE_Handle handle, SANE_Int option)
{
	return sane_get_option_descriptor(handle, option);
}

DLL_EXPORT SANE_Status
ENTRY(control_option)(SANE_Handle handle,
		      SANE_Int option,
		      SANE_Action action,
		      void *value,
		      SANE_Int *info)
{
	return sane_control_option(handle, option, action, value, info);
}

DLL_EXPORT SANE_Status
ENTRY(get_parameters)(SANE_Handle handle, SANE_Parameters *params)
{
	return sane_get_parameters(handle, params);
}

DLL_EXPORT SANE_Status
ENTRY(start)(SANE_Handle handle)
{
	return sane_start(handle);
}

DLL_EXPORT SANE_Status
ENTRY(read)(SANE_Handle handle,
	    SANE_Byte *data,
	    SANE_Int max_length,
	    SANE_Int *length)
{
	return sane_read(handle, data, max_length, length);
}

DLL_EXPORT void
ENTRY(cancel)(SANE_Handle handle)
{
	sane_cancel(handle);
}

DLL_EXPORT SANE_Status
ENTRY(set_io_mode)(SANE_Handle handle, SANE_Bool non_blocking)
{
	return sane_set_io_mode(handle, non_blocking);
}

DLL_EXPORT SANE_Status
ENTRY(get_select_fd)(SANE_Handle handle, SANE_Int *fd)
{
	return sane_get_select_fd(handle, fd);
}

