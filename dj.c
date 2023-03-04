/*
 *	DJ-11 driver
 */
 
#include "../param.h"
#include "../conf.h"
#include "../user.h"
#include "../tty.h"
#include "../proc.h"
#include "../options.h"
 
/* DJ device address */
#define	DJADDR	0160010
 
#define	NDJ11	16	/* number of lines */
 
/* teletype buffer allocation see tty.h for description */
struct	tty dj11[NDJ11];
 
 
/*
 * Hardware control bits in the csr register
*/
#define TRANSRD	0100000	/* transmitter ready bit */
#define TRANSINT 040000	/* transmitter interrupt enable bit */
#define FIFOFULL 020000	/* FIFO silo full ibit */
#define FIFOEN	010000	/* FIFO silo full interrupt enable bit */
#define BRKSEL	02000	/* break select bit */
#define SCANEN	0400	/* transmitter scan enable bit */
#define RCVRDN	0200	/* receive done bit */
#define RCVRINT	0100	/* receiver interrupt enable */
#define BUSYCLR	020	/* busy clear */
#define MOSCLR	010	/* mos clear */
#define MAINT	04	/* maintenance mode transmit and receive connected */
#define HDUPLX	02	/* half duplex */
#define RCVREN	01	/* receiver enabled */
 
/*
 * Hardware bits in the rbuf and tbuf registers
*/
#define	PERROR	010000	/* parity error bit */
#define	FRERROR	020000	/* framing error bit *
#define	XINT	0100000	/* data present */
#define LINENO	07400	/* line number bits in both rbuf and tbuf */
 
 
/* DJ device registers */
struct djregs {
	int djcsr;		/* command and status */
	int djrbuf;		/* receiver buffer */
	int djtcr;		/* transmit control */
	int djtbuf;		/* transmit buffer */
};
 
/*
 * Open a DJ11 line.
	test if line number is greater than the largest possible line number
	if so return error
 
	get teletype buffer address
	set up special djstart routine in loacation for special start routine
	set csr register bits for the DJ
	set teletype state to waiting for open and special start routine
	if tty not previously opened then set teletype state flags
	set ttystate to open 
 */
djopen(dev, flag)
{	register struct tty *tp;
	extern djstart();
 
	if (dev.d_minor >= NDJ11) {
		u.u_error = ENXIO;
		return;
	}
	tp = &dj11[dev.d_minor];
	DJADDR->djcsr =| TRANSINT | FIFOEN | SCANEN | RCVRINT | RCVREN;
	tp->t_addr = djstart;
	tp->t_state =| WOPEN|SSTART|CARR_ON;
	if ((tp->t_state&ISOPEN) == 0) {
		tp->t_erase = CERASE;
		tp->t_kill = CKILL;
		tp->t_flags = XTABS|LCASE|CRMOD|ECHO;
	}
	ttyopen(dev,tp);
}
 
/* %%%%%%%%%%	 djclose	 %%%%%%%%%% */
/*
 * Close a DJ11 line.
	clear the teletype state bits and flush the remaining chars if the buffer
 */
djclose(dev)
{	register struct tty *tp;
 
	tp = &dj11[dev.d_minor];
	tp->t_state =& (CARR_ON|SSTART);
	wflushtty(tp);
}
 
/* %%%%%%%%%%	 djread	 %%%%%%%%%% */
/*
 * Read from a DJ11 line.
	call the teletype read routine located in /usr/sys/dmr/tty.c
 */
djread(dev)
{
	ttread(&dj11[dev.d_minor]);
}
 
/* %%%%%%%%%%	 djwrite	 %%%%%%%%%% */
/*
 * Write on a DJ11 line
	call the teletype write routine located in /usr/sys/dmr/tty.c
 */
djwrite(dev)
{
	ttwrite(&dj11[dev.d_minor]);
}
 
/* %%%%%%%%%%	 djrint	 %%%%%%%%%% */
/*
 * DJ11 receiver interrupt.
	get character form dj register
	get line number from dj register
	get buffer address
	if framing error
		if in raw mode, convert it to a null
		else ignore it
	if parity error discard the character
	if line is not open, wake any hanging open
	else	put the character into the buffer
 */
djrint()
{	register struct tty *tp;
	register int c;
 
	while ((c = DJADDR->djrbuf) < 0) {	/* char. present */
		tp = &dj11[(c>>8)&017];
		if (tp >= &dj11[NDJ11])
			continue;
		if (c&FRERROR)		/* break */
			if (tp->t_flags&RAW)
				c = 0;		/* null (for getty) */
			else
				continue;
		if (c&PERROR)		/* parity error */
			continue;
		if (tp->t_state & ISOPEN)
			ttyinput( c, tp );
		else	wakeup( &tp->t_inq );
	}
}
 
 
/* %%%%%%%%%%	 djsgtty	 %%%%%%%%%% */
/*
 * stty/gtty for DJ11
	call teletype mode routine found in /usr/sys/dmr/tty.c
 */
#ifdef  UCLATTY
djsgtty(dev)
#endif
#ifndef UCLATTY
djsgtty( dev, v )
#endif
{	register struct tty *tp;
 
	tp = &dj11[dev.d_minor];
#ifdef  UCLATTY
	ttystty( tp );
#endif
#ifndef UCLATTY
	ttystty( tp, v );
#endif
}
 
 
/* %%%%%%%%%%	 djxint	 %%%%%%%%%% */
/*
 * DJ11 transmitter interrupt.
	while a line is ready to be served (TRANSRD bit high)
		reset the transmitter read and interrupt enable bits
		get the line number and use it to get the tty buffer
		get the next character from the tty buffer
		if it is an escape, get the next character.
		else if it has the high bit on
			if it is a delay character schedule the restart
			else stop the output
		else send it out to the terminal.
	wake up a sleeping process if it was waiting on a full buffer
	reset the transmit interrupt enable bit
 */
djxint()
{	register struct tty *tp;
	register int c;
	register int line;
	extern ttrstrt();
 
	while (DJADDR->djcsr&TRANSRD)
	{	DJADDR->djcsr =& ~(TRANSRD | TRANSINT);
		line = (DJADDR->djtbuf&LINENO) >> 8;
		tp = &dj11[line];

		if (tp->t_state&TIMEOUT)
			goto disable;
		if ((c = getc(&tp->t_outq)) < 0)
			goto disable;
#ifdef  UCLATTY
		if (c == CESCAPE)
			c = getc( &tp->t_outq );
		else
#endif
		if (c & 0200)
		{	if (c =& 0177)
				timeout( ttrstrt, tp, c );
#ifdef  UCLATTY
			else	tp->t_state =| STOPOUT;
#endif
			tp->t_state =| TIMEOUT;
			goto disable;
		}
		DJADDR->djtbuf = c;
	/*
	 * If the writer was sleeping on output overflow,
	 * wake him when low tide is reached.
	 */
		if (tp->t_outq.c_cc<=TTLOWAT && tp->t_state&ASLEEP) 
		{	tp->t_state =& ~ASLEEP;
			wakeup(&tp->t_outq);
		} 
		continue;
 disable:
		DJADDR->djtcr =& ~(1 << line);
	}
	DJADDR->djcsr =| TRANSINT;
}
 
 
/* %%%%%%%%%%	 djstart	 %%%%%%%%%% */
/*
* DJ start routine
	start (restart) transmission on the DJ line by setting the transmit control bit
*/
djstart(atp)
	struct tty *atp;
{	register struct tty *tp;
	register int line;

	tp = atp;
	line = tp - dj11;

	if (tp->t_state&TIMEOUT)
		return;

	DJADDR->djtcr =| (1 << line);
  }
_cc<=TTLOWAT && tp->t_state&ASLEEP) 
		{	tp->t_state =& ~ASLEEP;
			wakeup(&tp->t_outq);
		} 
		continue;
 disable:
		DJADDR->djtcr =& ~(1 << line);
	}
	DJADDR->djcsr =| TRANSINT;
}
