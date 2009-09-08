#include <stdlib.h>
#include <stdint.h>
#include "gl843_low.h"
#include "gl843_util.h"
#include "gl843_cs4400f.h"

/* Device-specific settings and functions for Canon Canoscan 4400F */

void set_sysclk(struct gl843_device *dev, enum gl843_sysclk clkset)
{
	set_reg(dev, GL843_CLKSET, clkset);
}

/* Enable/disable the lamps, with or without timeouts.
 * timeout: timeout, 0 - 15 minutes. 0 => no timeout
 */
void set_lamp(struct gl843_device *dev, enum gl843_lamp state, int timeout)
{
	struct regset_ent lamp[] = {
		/* 0x03 */
		{ GL843_LAMPDOG, (timeout != 0) ? 1 : 0 },
		{ GL843_XPASEL, (state == LAMP_TA) ? 1 : 0 },
		{ GL843_LAMPPWR,(state != LAMP_OFF) ? 1 : 0 },
		{ GL843_LAMPTIM, timeout },

		{ GL843_MTLLAMP, 0 },	/* 0x05: timeout = LAMPTIM * 2^MTLLAMP */
		{ GL843_LPWMEN, 0 },	/* 0x0A: 0 = Disable lamp PWM */
		{ GL843_ONDUR, 159 },	/* 0x98,0x99 */
		{ GL843_OFFDUR, 175  },	/* 0x9A,0x9B */
	};
	set_regs(dev, lamp, ARRAY_SIZE(lamp));
}

void set_motor(struct gl843_device *dev)
{
	int acdcdis = 0;
	int agohome = 0;
	int mtrpwr = 0;
	int mtrrev = 0;
	int fastfed = 0;

	int scanfed = 255, fwdstep = 0, bwdstep = 0;
	int lincnt = 1;
	int feedl = 1;
	int stoptim = 15;
	int decsel = 0;
	int stepno = 0, fastno = 0, fshdec = 0, fmovno = 0, fmovdec = 0;
	int steptim = 0;
	int z1mod = 0, z2mod = 0;
	int stepsel = 1, fstpsel = 1;
	int vrhome = 0, vrmove = 0, vrback = 0, vrscan = 0;

	struct regset_ent motor[] = {

		/* Operating modes */

		/* 0x02 */
		{ GL843_NOTHOME, 0 },
		{ GL843_ACDCDIS, acdcdis },
		{ GL843_AGOHOME, agohome },
		{ GL843_MTRPWR, mtrpwr },
		{ GL843_MTRREV, mtrrev },
		{ GL843_FASTFED, fastfed },	/* 0x02 */
		{ GL843_LONGCURV, 0 },		/* 0x02 */

		/* Carriage movement */

		{ GL843_SCANFED, scanfed },	/* 0x1F: steps to scanning pos */
		{ GL843_FWDSTEP, fwdstep },	/* 0x22 */
		{ GL843_BWDSTEP, bwdstep },	/* 0x23 */
		{ GL843_LINCNT, lincnt },	/* 0x25,0x26,0x27 */
		{ GL843_FEEDL, feedl },		/* 0x3D,0x3E,0x3F */

		{ GL843_STOPTIM, stoptim },	/* 0x5E: 15 or 31 */
		{ GL843_DECSEL, decsel },	/* 0x5E: 0 or 1 */
		{ GL843_MULSTOP, 0 },		/* 0xAB: STOPTIM * 2^MULSTOP */

		{ GL843_MCNTSET, 0 },		/* 0x09: 0 = Motor table counts
						 * pixel clk */
		{ GL843_STEPNO, stepno },	/* 0x21: Motor table 1 size */
		{ GL843_FASTNO, fastno },	/* 0x24: Motor table 2 size */
		{ GL843_FSHDEC, fshdec },	/* 0x69: Motor table 3 size */
		{ GL843_FMOVNO, fmovno },	/* 0x6A: Motor table 4 size */
		{ GL843_FMOVDEC, fmovdec },	/* 0x5F: Motor table 5 size */
		{ GL843_STEPTIM, steptim },	/* 0x9D: table size multiplier */

		{ GL843_Z1MOD, z1mod },		/* 0x60,0x61,0x62 */
		{ GL843_Z2MOD, z2mod },		/* 0x63,0x64,0x65 */
		/* 0xAB */
		{ GL843_NODECEL, 0 },
		{ GL843_TB3TB1, 0 },
		{ GL843_TB5TB2, 0 },

		/* Stepping motor */

		/* 0x67 */
		{ GL843_STEPSEL, stepsel },
		/* 0x68 */
		{ GL843_FSTPSEL, fstpsel },
		/* 0x80, 0xAC */
		{ GL843_VRHOME, vrhome },	/*  0, 1, 4 or 5 */
		{ GL843_VRMOVE, vrmove },	/* 0 */
		{ GL843_VRBACK, vrback },	/* 0, 1, 3, 4 or 7 */
		{ GL843_VRSCAN, vrscan },	/* 0, 1, 4 or 5 */

	};
	set_regs(dev, motor, ARRAY_SIZE(motor));
}

void set_frontend(struct gl843_device *dev,
		  int afe_dpi, int deep_color,
		  int linesel, int maxwd,
		  int tgtime, int lperiod,
		  int expr, int expg, int expb)
{
	int scanmod = (afe_dpi == 0) ? 0 : 7; /* 7 = 16 sys clks / pixel */
	int filter = 0;

	int tgw = 10, tgshld = 11;
	int ck1map = 0, ck3map = 0, ck4map = 0;
	int cph = 0, cpl = 0, rsh = 0, rsl = 0;
	int ck1mtgl = 0, ck3mtgl = 0;

	int vsmp = 11;
	int rhi = 11, rlow = 13, ghi = 0, glow = 3, bhi = 6, blow = 9;

	/* afe_dpi = resolution seen by the A/D converter,
	   1:1, 1:2, 1:4 of the CCD resolution */

	if (afe_dpi == 1200) {

		tgw = 10;
		tgshld = 11;
		//tgtime = 0;		// CCD line period = LPERIOD

		ck1map = 0xf838;	// 0b1111100000111000 (63544)
		ck3map = 0xfc00;	// 0b1111110000000000 (64512)
		ck4map = 0x92a4;	// 0b1001001010100100 (37540)

		cph = 1; cpl = 3; rsh = 0; rsl = 2;

		vsmp = 11;
		rhi = 10; rlow = 13; ghi = 0; glow = 3; bhi = 6; blow = 8;

	} else if (afe_dpi == 2400) {

		tgw = 21;
		tgshld = 21;
		//tgtime = 1;		// CCD line period = LPERIOD * 2

		ck1map = 0xff00;	// 0b1111111100000000 (65280)
		ck3map = 0xff00;	// 0b1111111100000000 (65280)
		ck4map = 0x5492;	// 0b0101010010010010 (21650)

		cph = 2; cpl = 4; rsh = 0; rsl = 2;

		vsmp = 10; /*12?*/
		rhi = 11; rlow = 13 /*14?*/; ghi = 0; glow = 3; bhi = 6; blow = 9;

	} else if (afe_dpi == 4800) {

		tgw = 21;
		tgshld = 21;
		//tgtime = 1;		// CCD line period = LPERIOD * 2

		ck1map = 0xffff;	// 0b1111111111111111 (65535)
		ck3map = 0xffff;	// 0b1111111111111111 (65535)
		ck4map = 0x5492;	// 0b0101010010010010 (21650)

		cph = 10; cpl = 12; rsh = 8; rsl = 10;

		vsmp = 3;
		rhi = 2; rlow = 5; ghi = 8; glow = 11; bhi = 13; blow = 15;
	}

	/* CCD/CIS and AFE settings */
	struct regset_ent frontend[] = {
		/* 0x04 */
		{ GL843_BITSET, deep_color ? 1 : 0 },	/* 8-bit/16-bit data */
		{ GL843_FILTER, filter },	/* 0 = color (1,2,3 = R,G,B) */
		/* 0x06 */
		{ GL843_SCANMOD, scanmod },	/* 0 = 12 clocks/pixel (24bit mode) */
						/* 7 = 16 clocks/pixel (48bit mode) */
		/* RGB exposure times */

		{ GL843_EXPR, expr },	/* 0x10,0x11 */
		{ GL843_EXPG, expg },	/* 0x12,0x13 */
		{ GL843_EXPB, expb },	/* 0x14,0x15 */
		/* 0x17 */
		{ GL843_TGMODE, 0 },
		{ GL843_TGW, tgw },	/* CCD TG plus width = 10 or 21 */
		/* 0x19 */
		{ GL843_EXPDMY, 42 },
		/* 0x1C */
		{ GL843_CK4MTGL, 0 },
		{ GL843_CK3MTGL, ck3mtgl },
		{ GL843_CK1MTGL, ck1mtgl },
		{ GL843_TGTIME, tgtime },
		/* 0x1D */
		{ GL843_TGSHLD, tgshld },	/* 11 or 21 */
		/* 0x9E */
		{ GL843_TGSTIME, 5 },	/*  TGSHLD * 2^TGSTIME */
		{ GL843_TGWTIME, 5 },	/*  TGW * 2^TGWTIME */
		/* 0x1E */
		{ GL843_LINESEL, linesel }, /* 0 or 1 */
		/* 0x34,0x35,0x36,0x37,0x38,0x39 */
		{ GL843_DUMMY, 20 },
		{ GL843_MAXWD, maxwd },
		{ GL843_LPERIOD, lperiod },
		/* 0x52,0x53,0x54,0x55,0x56,0x57  Depends on AFE clocks/pixel */
		{ GL843_RHI, rhi },
		{ GL843_RLOW, rlow },
		{ GL843_GHI, ghi },
		{ GL843_GLOW, glow },
		{ GL843_BHI, bhi },
		{ GL843_BLOW, blow },
		/* 0x58 */
		{ GL843_VSMP, vsmp },	/* Depends on AFE clocks/pixel */
		{ GL843_VSMPW, 3 },	/* Sampling pulse width */
		/* 0x70,0x71,0x72,0x73 */
		{ GL843_RSH, rsh },
		{ GL843_RSL, rsl },
		{ GL843_CPH, cph },
		{ GL843_CPL, cpl },
		/* 0x74,0x75,0x76,0x77,0x78,0x79,0x7A,0x7B,0x7C */
		{ GL843_CK1MAP, ck1map },
		{ GL843_CK3MAP, ck3map },
		{ GL843_CK4MAP, ck4map },
	};
	set_regs(dev, frontend, ARRAY_SIZE(frontend));
}

#define CHK(x)					\
	do {					\
		if ((x) < 0)			\
			goto chk_failed;	\
	} while (0)



int init_fe(struct gl843_device *dev)
{
	/* MODE4 = 0
	 * PGAFS = 2: full scale output (negative),
	 * MONO = 0: colour mode,
	 * CDS = 1: Enable correlated double sampling (CDS) mode, EN = 1
	 */
	CHK(write_afe(dev, 1, 0x23));

	/* DEL = 0: minimum output latency,
	 * RLCDACRNG = 1: RLCDAC ranges 0 to Vrt
	 * VRLCEXT = 0: internal drive of VRLC and VBIAS,
	 * INVOP = 1: negative going video => positive going output data
	 * MUXOP = 0: 16-bit parallel output
	 */
	CHK(write_afe(dev, 2, 0x24));
	CHK(write_afe(dev, 3, 0x1f)); /* Default. Will change */

	CHK(write_afe(dev, 6, 0));
	CHK(write_afe(dev, 8, 0));
	CHK(write_afe(dev, 9, 0));
	return 0;
chk_failed:
	DBG2(DBG_error, "Cannot configure the analog frontend (AFE).\n");
	return -1;
}

static void setup_gpio(struct gl843_device *dev)
{
}

void setup_scanner(struct gl843_device *dev)
{
	struct regset_ent gpio1[] = {

		/* CCD/CIS/ADF */

		/* 0x6B */
		{ GL843_GPOCK4, 0 },	/* 0 = pin 62 is CCD_CK4X signal */
		{ GL843_GPOCP, 0 },	/* 0 = pin 68 is CCD_CPX signal */
		{ GL843_GPOLEDB, 1 },	/* unused (no CIS) */
		{ GL843_GPOADF, 0 },	/* unused (no ADF) */

		/* Motor Vref control */

		{ GL843_GPOM13, 1 },	/* 0x6B: 1 = GPIO13 is Vref ctrl */
		{ GL843_GPOM12, 1 },	/* 0x6B: 1 = GPIO12 is Vref ctrl */
		{ GL843_GPOM11, 1 },	/* 0x6B: 1 = GPIO11 is Vref ctrl */
		{ GL843_GPOM9, 0 },	/* 0xAB: 0 = GPIO9 is GPIO */

		/* LED outputs */

		{ IOREG(0x7e), 0 },	/* GPOLED25-21,10-8 are GPIO */
	};
	set_regs(dev, gpio1, ARRAY_SIZE(gpio1));
	flush_regs(dev);

	write_ioreg(dev, 0xa8, 0);	/* GPOE27-25 inputs, GPIO27-25 = 0 */
	write_ioreg(dev, 0xa7, 0xff);	/* GPOE24-17 are outputs */
	write_ioreg(dev, 0xa6, 0);	/* GPIO24-17 */
	write_ioreg(dev, 0x6e, 0xff);	/* GPOE16-9 are outputs */
	write_ioreg(dev, 0x6c, 1);	/* GPIO16-9 */
	write_ioreg(dev, 0x6f, 0);	/* GPOE8-1 are inputs */
	write_ioreg(dev, 0x6d, 0);	/* GPIO8-1 */


	struct regset_ent static_setup[] = {

		/* Frontend and CCD/CIS */

		/* 0x01 */
		{ GL843_CISSET, 0 },
		/* 0x04 */
		{ GL843_AFEMOD, 1 },	/* 1 = pixel-by-pixel color */
		{ GL843_FESET, 0 },	/* Frontend is ESIC type 1 */
		/* 0x05 */
		{ GL843_DPIHW, 3 },	/* CCD resolution = 4800 DPI */
		{ GL843_ENB20M, 0 },	/* variable pixel clock for CCD CK1 */
		{ GL843_MTLBASE, 0 },	/* CCD pixel CLK = system pixel CLK */
		/* 0x09 */
		{ GL843_EVEN1ST, 0 },	/* 0 = First line of stagger CCD is odd */
		{ GL843_BLINE1ST, 0 },	/* 0 = First CCD line is red */
		{ GL843_SHORTTG, 0 },	/* 1 = Short SH(TG) period */
		/* 0x16 */
		{ GL843_CTRLHI, 0 },
		{ GL843_TOSHIBA, 0 },
		{ GL843_TGINV, 0 },
		{ GL843_CK1INV, 1 },
		{ GL843_CK2INV, 0 },
		{ GL843_CTRLINV, 0 },
		{ GL843_CKDIS, 1 },
		{ GL843_CTRLDIS, 1 },
		/* 0x18 */
		{ GL843_CNSET, 0 },
		{ GL843_DCKSEL, 0 },
		{ GL843_CKTOGGLE, 1 },
		{ GL843_CKDELAY, 0 },
		{ GL843_CKSEL, 0 },
		/* 0x19 */
		{ GL843_EXPDMY, 42 },
		/* 0x1A */
		{ GL843_TGLSW2, 0 },
		{ GL843_TGLSW1, 0 },
		{ GL843_MANUAL3, 1 },
		{ GL843_MANUAL1, 1 },
		{ GL843_CK4INV, 0 },
		{ GL843_CK3INV, 0 },
		{ GL843_LINECLP,0 },
		/* 0x1B */
		{ GL843_GRAYSET, 0 },
		{ GL843_CHANSEL, 0 },
		{ GL843_BGRENB, 0 },
		{ GL843_ICGENB, 0 },
		{ GL843_ICGDLY, 0 },
		/* 0x1C */
		{ GL843_CK4MTGL, 0 },
		{ GL843_CKAREA, 0 },
		/* 0x1D */
		{ GL843_CK4LOW, 0 },
		{ GL843_CK3LOW, 1 },
		{ GL843_CK1LOW, 1 },

		/* 0x34 */
		{ GL843_DUMMY, 20 },
		/* 0x58 */
		{ GL843_VSMPW, 3 },	/* Sampling pulse width */
		/* 0x59 */
		{ GL843_BSMP, 0 },
		{ GL843_BSMPW, 0 },
		/* 0x5A */
		{ GL843_ADCLKINV, 0 },
		{ GL843_RLCSEL, 1 },	/* pixel-by-pixel reset-level clamp */
		{ GL843_CDSREF, 0 },
		{ GL843_RLC, 0 },
		/* 0x7D */
		{ GL843_CK1NEG, 0 },
		{ GL843_CK3NEG, 0 },
		{ GL843_CK4NEG, 0 },
		{ GL843_RSNEG, 0 },
		{ GL843_CPNEG, 0 },
		{ GL843_BSMPNEG, 0 },
		{ GL843_VSMPNEG, 0 },
		{ GL843_DLYSET, 0 },
		/* 0x7F */
		{ GL843_BSMPDLY, 0 },	/* 0 = don't delay BSMP output */
		{ GL843_VSMPDLY, 0 },	/* 0 = don't delay VSMP output */
		/* 0x87 */
		{ GL843_ACYCNRLC, 0 },	/* */
		{ GL843_ENOFFSET, 0 },	/* */
		{ GL843_LEDADD, 0 },	/* */
		{ GL843_CK4ADC, 1 },	/* 1 = CK4MAP controls AFE MCLK */
		{ GL843_AUTOCONF, 0 },	/* unused (not CIS) */
		/* 0x9D */
		{ GL843_MULDMYLN, 0 },	/* dummy lines = LINESEL * 2^MULDMYLN */
		/* 0x9E */
		{ GL843_SEL3INV, 0 },
		/* 0xAD */
		{ GL843_CCDTYP, 0 },	/*  */

		/* SDRAM */

		/* 0x0B */
		{ GL843_ENBDRAM, 1 },	/* posedge => SDRAM power-on sequence */
		{ GL843_RFHDIS, 0 },	/* 0 = use auto-refresh */
		{ GL843_DRAMSEL, 1 },	/* 1 = 16Mbit */
		/* 0x9D */
		{ GL843_RAMDLY,  0 },	/* SCLK delay */
		/* 0xA2 */
		{ GL843_RFHSET, 31 },  /* refresh time [2µs] */

		/* Misc */

		{ GL843_HOMENEG, 0 },	/* 0x02: home sensor polarity */
		{ GL843_BUFSEL, 16 },	/* 0x20: buffer-full threshold */
		{ GL843_BACKSCAN, 0 },	/* 0x09 */

		/* Motor */

		/* 0x66 */
		{ GL843_PHFREQ, 0 },	/* unused */
		/* 0x67 */
		{ GL843_MTRPWM, 63 },	/* No PWM (not unipolar motor) */
		/* 0x68 */
		{ GL843_FASTPWM, 63 },	/* No PWM (not unipolar motor) */
		/* 0x87 */
		{ GL843_YENB, 0 },	/* unused */
		{ GL843_YBIT, 0 },	/* unused */

		/* Unused functions */

		/* 0x06 */
		{ GL843_OPTEST, 0 },
		/* 0x09 */
		{ GL843_ENHANCE, 0 },
		{ GL843_NWAIT, 0 },
		/* 0x0A */
		{ GL843_LCDSEL, 0 },	/* unused (no LCD) */
		{ GL843_LCMSEL, 0 },	/* unused */
		{ GL843_ADFSEL, 0 },	/* unused (no ADF) */
		{ GL843_EPROMSEL, 0 },	/* unused (no EPROM) */
		{ GL843_RS232SEL, 0 },	/* unused (no RS232 i/f) */
		{ GL843_BAUDRAT, 0 },	/* unused (no RS232 i/f) */
		{ GL843_DOGENB, 0 },	/* 0x01 */
		{ GL843_MTLWD, 0 },	/* 0x1C */
		{ GL843_WDTIME, 2 },	/* 0x1E */
		/* 0x6B */
		{ GL843_MULTFILM, 0 },	/* unused */
		/* 0x7F */
		{ GL843_LEDCNT, 0 },	/* unused. 0 = disable LED blinking */
		/* 0x94 */
		{ GL843_MTRPLS, 255 },	/* unused (no ADF) */
		/* 0x9D */
		{ GL843_MOTLAG, 0 },	/* unused (no ADF) */
		{ GL843_CMODE, 0 },	/* unused (no RS232 i/f or LCD) */
		{ GL843_IFRS, 0 },	/* unused (no LCM) */
		/* 0xAB */
		{ GL843_FIX16CLK, 0 },	/* unknown */
		/* 0xAD */
		{ GL843_ADFTYP, 0 },	/* unused (no ADF) */
		/* 0xAE */
		{ GL843_MOTSET, 0 },	/* unused (unknown) */
		{ GL843_PROCESS, 0 },	/* unused (unknown) */
		/* 0xAF: GL843_SCANTYP, GL843_FEDTYP, GL843_ADFMOVE, */
		{ IOREG(0xaf), 0 },	/* unused (no ADF) */
	};
	set_regs(dev, static_setup, ARRAY_SIZE(static_setup));

	set_sysclk(dev, SYSCLK_60_MHZ);
	set_lamp(dev, LAMP_OFF, 0);
	flush_regs(dev);

	set_reg(dev, GL843_PWRBIT, 1);	/* 0x06 */
	flush_regs(dev);


}

#if 0
void set_postprocessing(struct gl843_device *dev)
{
	/* Postprocessing encompasses all pixel processing between
	 * the analog front end (AFE) and USB interface */
	struct regset_ent postprocessing[] = {
		/* 0x01 */
		{ GL843_DVDSET, dvdset },
		{ GL843_STAGGER, 0 },
		{ GL843_COMPENB, 0 },
		{ GL843_TRUEGRAY, 0 },
		{ GL843_SHDAREA, shdarea },
		/* 0x03 */
		{ GL843_AVEENB, aveenb }, 	/* X scaling: 1=avg, 0=del */
		/* 0x04 */
		{ GL843_LINEART, lineart },	/* 0 = RGB/gray, 1 = lineart mode */
		/* 0x05 */
		{ GL843_GMMENB, gmmenb },
		/* 0x06 */
		{ GL843_GAIN4, 0 },		/* 0/1: shading gain of 4/8. */
		/* 0x08 */
		{ GL843_DECFLAG, decflag },
		{ GL843_GMMFFR, gmmffr },
		{ GL843_GMMFFG, gmmffg },
		{ GL843_GMMFFB, gmmffb },
		{ GL843_GMMZR, gmmzr },
		{ GL843_GMMZG, gmmzg },
		{ GL843_GMMZB, gmmzb },
		/* 0x2E,0x2F */
		{ GL843_BWHI, bwhi },	/* lineart low/high thresholds */
		{ GL843_BWLOW, bwlo },	/* with hysteresis */
		/* 0x2C,0x2D, 0x30,0x31,0x32,0x33 */
		{ GL843_DPISET, dpiset },
		{ GL843_STRPIXEL, strpixel },
		{ GL843_ENDPIXEL, endpixel },
		/* 0xA3,0xA4,0xA5 */
		{ GL843_TRUER, 0 },
		{ GL843_TRUEG, 0 },
		{ GL843_TRUEB, 0 },
	};
	set_regs(dev, postprocessing, ARRAY_SIZE(postprocessing));
}
#endif

#if 0
	/* Command flags */

	/* 0x01 */
	{ GL843_SCAN, scan },
	/* 0x0D */
	{ GL843_JAMPCMD, 0 },		/* unused (no ADF) */
	{ GL843_DOCCMD, 0 },		/* unused (no ADF) */
	{ GL843_CCDCMD, 0 },		/* ? */
	{ GL843_FULLSTP, 0 },		/* 1 = Reset to motor full steps */
	{ GL843_SEND, 0 },		/* unused (no RS232 i/f) */
	{ GL843_CLRMCNT, clrmcnt },	/* 1 = clear FEDCNT */
	{ GL843_CLRDOCJM, 0 },		/* unused (no ADF) */
	{ GL843_CLRLNCNT, clrlncnt },	/* 1 = clear SCANCNT */
	/* 0x0E */
	{ GL843_SCANRESET, scanreset },	/* Scanner reset on write */
	/* 0x0F */
	{ GL843_MOVE, move },		/* Start motor on write */
#endif

#if 0
	struct regset_ent unknown_gpio[] = {
		{ GL843_GPIO13, gpio13 },
		{ GL843_GPIO10, gpio10 },
		{ GL843_GPOE16, gpoe16 },
		{ GL843_GPOE14, gpoe14 },
	};
	set_regs(dev, unknown_gpio, ARRAY_SIZE(unknown_gpio));
#endif

/*

Read-only registers
-------------------

0x40: GL843_DOCSNR, GL843_ADFSNR, GL843_COVERSNR, GL843_CHKVER,
      GL843_DOCJAM, GL843_HISPDFLG, GL843_MOTMFLG, GL843_DATAENB,

0x41: GL843_PWRBIT_RD, GL843_BUFEMPTY, GL843_FEEDFSH, GL843_SCANFSH,
      GL843_HOMESNR, GL843_LAMPSTS, GL843_FEBUSY, GL843_MOTORENB,

0x42,0x43,0x44: GL843_VALIDWORD,

0x48,0x49,0x4A: GL843_FEDCNT,

0x4B,0x4C,0x4D: GL843_SCANCNT,

0x4F: GL843_DOGON, GL843_ROMBSY, GL843_LCMBSY, GL843_TX232BSY,
      GL843_RX232BSY, GL843_RXREADY,

Unused IO registers
-------------------

Please update this list if you make any changes. Every device register
and IO register should be mentioned at least once in this file so that
one can be sure that no registers are ever undefined.

0x07: GL843_LAMPSIM, GL843_CCDCTL, GL843_DRAMCTL, GL843_MOVCTL,
      GL843_RAMSEL, GL843_FASTDMA, GL843_DMASEL, GL843_DMARDWR,

0x0C: GL843_SWSH, GL843_CCDLMT,

0x5C: GL843_HISPD

0x81: GL843_LOADSET,
0x82: GL843_CONTB, GL843_CONTA,
0x83: GL843_IMGSET,
0x84: GL843_PACK, GL843_PACKCNT,

0x85,0x86: undefined

0x88: GL843_RDNUM,
0x89: GL843_RS232WD,
0x8A: GL843_RS232RD,

0x8B: GL843_ROMADDR,
0x8C,0x8D: GL843_ROMWD,
0x8E,0x8F: GL843_ROMRD,

0x90,0x91: GL843_PREFED,
0x92,0x93: GL843_PSTFED,

0x95,0x96,0x97: GL843_SCANLEN,

0x9C: GL843_LCMWD,
0x9F: GL843_LCDCTL, GL843_LCMCTL, GL843_EPROMCTL, GL843_TGCTL,
      GL843_MPUCTL, GL843_MOTMPU, GL843_NEC8884, GL843_DPI9600,
0xA0: GL843_LNOFSET,
0xA1: GL843_STGSET,

0xA6: GPIO24-17
0xAA: undefined

*/

/*

Film (16-bit) @ 1200 dpi:
CPH = 0,  CPL = 2,  RSH = 0,  RSL = 2

Film (16-bit) @ 2400 dpi:
CPH = 2,  CPL = 4,  RSH = 0,  RSL = 2

Film (16-bit) @ 4800 dpi:
CPH = 10, CPL = 12, RSH = 8,  RSL = 10

Platen (8-bit color) @ 50,75,100,150,200,300,400 dpi:
CPH = 0,1,     CPL = 0,3,     RSH = 0,      RSL = 0,2

Platen (8-bit color) @ 1200 dpi:
CPH = 0,1,2,   CPL = 0,3,4,   RSH = 0,      RSL = 0,2

Platen (8-bit color) @ 50 - 1200 dpi:
CPH = 1,  CPL = 3,  RSH = 0,  RSL = 2

Platen (8-bit gray or lineart) @ 300 dpi:
CPH = 2, CPL = 4 or CPH = 1, CPL = 3. TODO: Figure out why.

*/
/*
BHI = 13,17,6,
BLOW = 15,2,8,9,
GHI = 0,11,8,
GLOW = 11,14,3,
RHI = 10,11,2,5,
RLOW = 13,5,8,
*/