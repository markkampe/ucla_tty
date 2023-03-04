/* name:
	DH11 Driver

function:
	To provide the read, write, open, close and special function
	routines for the DH11 programable 16 line multiplexor.

globals:
	dhaddrs		contains base addresses for the various dh11s in the
			system.
	dhsar		contains (for each dh11) a copy of the bar as it
			was last known to be.
	dh_clist	array of buffers for dma output.
	dh11		array of tty structures for the dh11 ports
	ndh11		The number of known dh11 ports.

contains:
	dhopen
	dhclose
	dhread
	dhwrite
	dhstty
	dhparams

compile time parameters:
	UCLATTY         define this symbol if you are using the UCLA tty.c
			else leave it undefined

entry mechanism:
	through the character device table

history:
	Initially a Release six driver.
	Modified by Mark Kampe for multiple DH/DM support 1/20/76.
	Modified by Mark Kampe for the UCLA tty driver 6/1/76.
 */
#include "../param.h"
#include "../conf.h"
#include "../user.h"
#include "../tty.h"
#include "../options.h"

#define NDH11   16      /* maximum number of DH11 lines available */
#define	DHNCH	8	/* max number of DMA chars */

struct	tty dh11[NDH11];

char    dh_clist[NDH11][DHNCH]; /* buffers for DMA output */

int     ndh11   NDH11;  /* used by DM11 driver */

/* Definitions for bits in line parameter register  */
#define BITS6   01      /* six bit characters   */
#define BITS7   02      /* seven bit characters */
#define BITS8   03      /* eight bit characters */
#define TWOSB   04      /* two stop bits        */
#define PENABLE 020     /* enable parity        */
#define OPAR    040     /* odd parity           */
#define HDUPLX  040000  /* half duplex line     */
#define SSPEED  7       /* default speed        */

/* Definitions for bits in other control/status registers */
#define IENABLE 030100  /* transmit and receive int enable (csr)*/
#define PERROR  010000  /* parity error in received char (nch)  */
#define FRERROR 020000  /* framing error in received char (nch) */
#define XINT    0100000 /* transmit interrupt (in csr)

/* last known transmit status of all lines */
int     dhsar[(NDH11 + 15) >> 4];

struct dhregs {         /* structure for referencing dh11 registers     */
	int dhcsr;      /* control status register                      */
	int dhnxch;     /* next character register                      */
	int dhlpr;      /* line parameter register                      */
	int dhcar;      /* current address register                     */
	int dhbcr;      /* byte count register                          */
	int dhbar;      /* buffer activer register                      */
	int dhbreak;    /* break control register                       */
	int dhsilo;     /* silo status register                         */
};

int dhaddrs[]
{       0760020 };

/* name:
	dhopen

function:
	To open a dh11 port

algorithm:
	If the device is outside of the range of allowed dh ports, error;
	Locate the base of the register set for the associated dh11;
	Locate the tty table for the associated teletype;
	Enable DH11 interrupts;
	If line is not already open, initialize line parameters;
	Call the DM11 open routine;
	Do a general teletype open;

parameters:
	a device number (major and minor)
	a flag (ignored)

returns:

globals:
	dh11
	dhaddr

calls:
	dhparams
	dmopen
	ttyopen

called by:
	openi   through cdevsw

 */
dhopen(dev, flag)
{
	register struct tty *tp;
	register struct dhregs *dhaddr;
	extern dhstart();

	if (dev.d_minor >= NDH11) {
		u.u_error = ENXIO;
		return;
	}

	dhaddr = dhaddrs[ dev.d_minor >> 4 ];
	tp = &dh11[dev.d_minor];
	tp->t_addr = dhstart;
	tp->t_state =| WOPEN|SSTART;
	dhaddr->dhcsr =| IENABLE;

	if ((tp->t_state&ISOPEN) == 0) {
		tp->t_erase = CERASE;
		tp->t_kill = CKILL;
		tp->t_speeds = SSPEED | (SSPEED<<8);
		tp->t_flags = ODDP|EVENP|ECHO;
		dhparam(dev);
	}

	dmopen(dev);
	ttyopen(dev, tp);
}

/* name:
	dhclose

function:
	To close a dh11 port
 */
dhclose(dev)
{
	register struct tty *tp;

	tp = &dh11[dev.d_minor];
	dmclose(dev);
	tp->t_state =& (CARR_ON|SSTART);
	wflushtty(tp);
}

/*
 name:
	dhread

function:
	To initiate a read on a dh11 port.

*/
dhread(dev)
{
	ttread(&dh11[dev.d_minor]);
}

/*
 name:
	dhwrite

 function:
	to initiate a write on a dh11 port.
*/
dhwrite(dev)
{
	ttwrite(&dh11[dev.d_minor]);
}

/* name:
	dhrint

function:
	process a dh11 input interrupt

algorithm:
	Get a pointer to the register set for the DH11 which caused interrupt;
	While there are any characters in the silo;
		Get the next one;
		If it came from an illegal line, ignore it;
		Get ptr to tty structure associated with that line.
		If line is closed or the character had a parity error
			wakeup anyone waiting on that device;
			ignore the character;
		Call ttyinput with the character and tty table;
		end;

parameters:
	int dev		dh11 number (index into dhaddrs) passed from
			the condition codes of the interrupt PS.

globals:
	dh11

calls:
	wakeup
	ttyinput

called by:
	DH11 recieve interrupt

history:

 */
dhrint(unit)
int unit;
{
	register struct tty *tp;
	register int c;
	register struct dhregs *dhaddr;

	dhaddr = dhaddrs[unit];
	while ((c = dhaddr->dhnxch) < 0) {	/* char. present */
		tp = &dh11[ (unit<<4) | ((c>>8) & 017) ];
		if (tp >= &dh11[NDH11])
			continue;
		if((tp->t_state&ISOPEN)==0 || (c&PERROR)) {
			wakeup(tp);
			continue;
		}
		if (c&FRERROR)		/* break */
			c = 0;
		ttyinput(c, tp);
	}
}

/* name:
	stty/gtty routine for dh11

function:
	To impliment the stty and getty system calls for dh11 ports

algorithm:
	Get a pointer to the tty table associated with the particular line;
	Call ttystty with the pointer and the argument vector;
	Call dhparam with the device number;

parameters:
	a device number
	a pointer to an argument vector

returns:

globals:
	dh11

calls:
	ttystty
	dhparam

called by:
	As the special function, through the character device table

history:

 */
#ifdef  UCLATTY
dhsgtty( dev )
 int dev;
#endif
#ifndef UCLATTY
dhsgtty( dev, v )
 int dev, v;
#endif
{       register struct tty *tp;
	register r;

	tp = &dh11[dev.d_minor];
#ifdef  UCLATTY
	if (ttystty( tp ))
#endif
#ifndef UCLATTY
	if (ttystty( tp, v ))
#endif
		return;
	dhparam(dev);
}

/* name:
	dhparam

function:
	To set the line parameters for a dh11 line

algorithm:
	Figure out the base address of the DH11 associated with that line;
	Select the particular line in question;
	If speed has been set to zero
		close down the line;
		return;
	Install all of the parameters;
	return;

parameters:
	a device number	(major and minor)

returns:

globals:

calls:
	dmclose

called by:
	dhsgtty

history:

 */
dhparam(dev)
{
	register struct tty *tp;
	register int lpr;
	register struct dhregs *dhaddr;

	tp = &dh11[dev.d_minor];
	dhaddr = dhaddrs[ dev.d_minor >> 4 ];
	spl5();
	dhaddr->dhcsr.lobyte = (dev.d_minor&017) | IENABLE;
	/*
	 * Hang up line?
	 */
	if (tp->t_speeds.lobyte==0) {
		tp->t_flags =| HUPCL;
		dmclose(dev);
		return;
	}
	lpr = (tp->t_speeds.hibyte<<10) | (tp->t_speeds.lobyte<<6);
	if (tp->t_speeds.lobyte == 4)		/* 134.5 baud */
		lpr =| BITS6|PENABLE|HDUPLX|OPAR; else
		if (tp->t_flags&EVENP)
			if (tp->t_flags&ODDP)
				lpr =| BITS8; else
				lpr =| BITS7|PENABLE; else
			lpr =| BITS7|OPAR|PENABLE;
	if (tp->t_speeds.lobyte == 3)	/* 110 baud */
		lpr =| TWOSB;
	dhaddr->dhlpr = lpr;
	spl0();
}

/* name:
	dhxint

function:
	dh11 transmission complete interrupt routine

algorithm:
	for each line on the interrupting controller
		if an I/O operation has completed on that line
			mark that line unbusy
			call dhstart on it

parameters:
	unit number of the interrupting dh controller

globals:
	dh11
	dhsar

calls:
	dhstart
 */
dhxint(unit)
 int unit;
{
	register struct tty *tp;
	register ttybit, bar;
	struct dhregs *dhaddr;

	dhaddr = dhaddrs[unit];
	bar = dhsar[unit] & ~dhaddr->dhbar;
	dhaddr->dhcsr =& ~XINT;
	ttybit = 1;
	for (tp = &dh11[unit<<4]; bar; tp++) {
		if(bar&ttybit) {
			dhsar[unit] =& ~ttybit;
			bar =& ~ttybit;
			tp->t_state =& ~BUSY;
			dhstart(tp);
		}
		ttybit =<< 1;
	}
}
/* name:
	dhstart

function:
	to start or restart transmission on a particular line

algorithm:
	if that line is timed out or busy, return
	if there was a left over delay indication
		timeout or stopoutput as is specified
		return
	get a pointer to the buffer for that line
	while buffer is not full
		get the next character
		if it is a delay indication, note it and break
		copy it into the buffer
	if a writer was asleep and we can unblock him,
		wake him up
	if any characters in the buffer,
		initiate the dma transfer
		mark the line busy
	else if a delay indication has been noted
		timeout or stopout as is apprpriate
parameters:
	tty structure for the affected line

globals:
	dh11

calls:
	timeout
	wakeup
	getc

called by:
	ttstart
	dhxint
*/
dhstart( atp )
struct tty *atp;
{       extern  ttrstrt();
	register c, nch;
	register struct tty *tp;
	int sps, dev;
	struct dhregs *dhaddr;
	char *cp;

	sps = PS->integ;
	spl5();
	tp = atp;
	dev = tp-dh11;
	dhaddr = dhaddrs[ dev.d_minor >> 4 ];
	/*
	 * If it's currently active, or delaying,
	 * no need to do anything.
	 */
	if (tp->t_state&(TIMEOUT|BUSY))
		goto out;
	/*
	 * t_char is a delay indicator which may have been
	 * left over from the last start.
	 * Arrange for the delay.
	 */
	if (c = tp->t_char) {
		tp->t_char = 0;
		tp->t_state =| TIMEOUT;
		if (c =& 0177)	timeout(ttrstrt, tp, c);
#ifdef  UCLATTY
		else	tp->t_state =| STOPOUT;
#endif
		goto out;
	}
	cp = dh_clist[dev.d_minor];
	nch = 0;
	/*
	 * Copy DHNCH characters, or up to a delay indicator,
	 * to the DMA area.
	 */
	while (nch > -DHNCH && (c = getc(&tp->t_outq))>=0) {
#ifdef  UCLATTY
		if (c == CESCAPE) 
			c = getc( &tp->t_outq );
		else
#endif
		if (c&0200)
		{	tp->t_char = c;
			break;
		}
		*cp++ = c;
		nch--;
	}
	/*
	 * If the writer was sleeping on output overflow,
	 * wake him when low tide is reached.
	 */
	if (tp->t_outq.c_cc<=TTLOWAT && tp->t_state&ASLEEP) {
		tp->t_state =& ~ASLEEP;
		wakeup(&tp->t_outq);
	}
	/*
	 * If any characters were set up, start transmission;
	 * otherwise, check for possible delay.
	 */
	if (nch) {
		dhaddr->dhcsr.lobyte = (dev.d_minor & 017) | IENABLE;
		dhaddr->dhcar = cp+nch;
		dhaddr->dhbcr = nch;
		c = 1 << (dev.d_minor & 017);
		dhaddr->dhbar =| c;
		dhsar[dev.d_minor>>4] =|c;
		tp->t_state =| BUSY;
	} else if (c = tp->t_char) {
		tp->t_char = 0;
		if (c =& 0177)	timeout(ttrstrt, tp, c);
#ifdef  UCLATTY
		else	tp->t_state =| STOPOUT;
#endif
		tp->t_state =| TIMEOUT;
	}
    out:
	PS->integ = sps;
}
