/* name:
	DM11 modem controller driver

function:
	To provide open, close and interrupt routines for the DM11 modem
	controller for the DH11 multiplexor.

notes:
	This driver allows modem control to be enabled on a per line basis.
	the same driver is used whether all lines have modem control or
	none do.  This does away with the dm dh dhdm ordering problem
	which (of necessity) exists with the standard bell dm driver

contains:
	dmopen
	dmclose
	dmint

globals:
	dh11[]		the tty structures for the dh11 ports.
	dmaddrs		array of pointers to the sets of dm11 registers.
	dmmodem         array indicating which lines have modem control.

calls:
	signal
	sleep
	wakeup
	flushtty

called by:
	The DH11 driver

history:
	Started out as a standard release 6 DM11 driver.
	Modified for Multi DM support by Mark Kampe 1/21/76
	Modified for partial modem control by Mark Kampe 8/2/77
 */
#include "../param.h"
#include "../tty.h"
#include "../conf.h"

#define NDM11   16
char    dmmodem[ ]              /* setting the i'th entry to a nonzero  */
{       1,1,1,1,1,1,1,1,        /* value enables modem control on the   */
	1,1,1,1,1,1,1,1         /* i'th dh line.  Setting it to zero,   */
};                              /* disables modem control on that line. */
				/* This array is statically initialized */
				/* but can be dynamically altered.      */


int dmaddrs[2] {0170500};

struct	tty dh11[];
int	ndh11;		/* Set by dh.c to number of lines */

#define	DONE	0200
#define	SCENABL	040
#define	CLSCAN	01000
#define	TURNON	07	/* RQ send, CD lead, line enable */
#define	TURNOFF	1	/* line enable only */
#define	CARRIER	0100

struct dmregs {
	int	dmcsr;
	int	dmlstat;
};

/* name:
	dmopen

function:
	To initialize a dm11 line

algorithm:
	if modem control is not enabled on that line
		return, saying that it is open
	bring up the signals
	while (carrier is down for that line)
		sleep

parameters:
	device esignation for the line to be opened

returns:
	when the line is open

globals:
	dmmodem
	dh11

calls:
	sleep

called by:
	dhopen
 */
dmopen(dev)
{
	register struct tty *tp;
	register struct dmregs *dmaddr;
	register int devno;

	devno = dev.d_minor;
	tp = &dh11[ devno ];
	if (!dmmodem[ devno ])
		return( tp->t_state =| CARR_ON );

	dmaddr = dmaddrs[ devno >> 4];
	dmaddr->dmcsr = devno&017;
	dmaddr->dmlstat = TURNON;
	if (dmaddr->dmlstat&CARRIER)
		tp->t_state =| CARR_ON;
	dmaddr->dmcsr = IENABLE|SCENABL;
	spl5();
	while ((tp->t_state&CARR_ON)==0)
		sleep(&tp->t_inq, TTIPRI);
	spl0();
}

/* name:
	dmclose

function:
	To disable/turn off a DM11 port

algorithm:
	if modem control is disabled return
	if the line has hangup on close specified,
		drop the signals

parameters:
	device designation for the line to be closed

globals:
	dmmodem

called by:
	dhclose
 */
dmclose(dev)
{
	register struct tty *tp;
	register struct dmregs *dmaddr;
	register int devno;

	devno = dev.d_minor;
	dmaddr = dmaddrs[devno>>4];
	tp = &dh11[devno];
	if (!dmmodem[ devno ])
		return;
	if (tp->t_flags&HUPCL) {
		dmaddr->dmcsr = dev.d_minor&017;
		dmaddr->dmlstat = TURNOFF;
		dmaddr->dmcsr = IENABLE|SCENABL;
	}
}

/* name:
	dmint

function:
	DM11 transition interrupt handler

algorithm:
	get a pointer to the registers for the dm which interrupted
	if the controller is done
		if modem control is enabled for the interrupting line
			if transition is carrier down
				if line is open
					signal hangup to its group
				mark line down
			else    mark line up

parameters:
	unit number of the interrupting dm11 controller

globals:
	dh11
	dmmodem

calls:
	wakeup
	signal
 */
dmint(unit)
int unit;
{
	register struct tty *tp;
	register struct dmregs *dmaddr;
	register int devno;

	dmaddr = dmaddrs[unit];
	devno = (unit << 4) | (dmaddr->dmcsr&017);

	if (dmaddr->dmcsr&DONE) {
		tp = &dh11[ devno ];
		if ((tp < &dh11[ndh11]) && dmmodem[ devno ])
		{       wakeup(tp);
			if ((dmaddr->dmlstat&CARRIER)==0) {
				if ((tp->t_state&WOPEN)==0) {
					signal(tp->t_pgrp, SIGHUP);
					dmaddr->dmlstat = 0;
					flushtty(tp);
				}
				tp->t_state =& ~CARR_ON;
			} else
				tp->t_state =| CARR_ON;
		}
		dmaddr->dmcsr = IENABLE|SCENABL;
	}
}
