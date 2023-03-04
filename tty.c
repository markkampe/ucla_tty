/* name:
	tty.c

function:
	generic driver for all teletypes

globals:
	maptab		for parity determination and LCASE mapping
	spcltable	for identifying special editing characters
	wordtable	for identifying "word" characters
	indtable	for identifying wierd control characters
	cfree		array of clist elements
	cfreelist	header of free clist element queue
	panicstr	used for system status message
	statstr		used for system statistics message

contains:
	ttyopen
	ttread
	ttywrite
	ttyinput
	ttyoutput
	gtty	(system call)
	stty	(system call)
	spclfcn	(system call)
	ttystty
	flushtty
	wflushtty
	cinit	(character device - system initialization routine)
	ttrstrt
	ttstart
	spclchar

referenced by:
	gtty, stty and spclfcn are referenced by sysent.c
	cinit is referenced by main.c
	ttystty, ttyinput, ttread, ttwrite, ttyopen
		flushtty, and wflushtty are referenced by drivers
		for various terminal interfaces.

history:
	Designed and coded by Mark Kampe, UCLA-ATS, March 1976
	in response to the need for variable break characters, improved
	editing abilities, more general special functions and general
	sexier behavior.
 */
#define true 0177777
#define false 000000
#define then  /**/
#include "../param.h"
#include "../systm.h"
#include "../user.h"
#include "../tty.h"
#include "../proc.h"
#include "../inode.h"
#include "../file.h"
#include "../reg.h"
#include "../conf.h"

char maptab[] {	/*	maptab consolidates the functions of the old
	maptab and partab.  The high bit of each entry  is the even
	parity bit for that character.  If terminal is in LCASE mode,
	the low 7 bits are what the character maps to when prefixed by
	a literal escape.	*/
   0000+000,0200+000,0200+000,0000+000,0200+000,0000+000,0000+000,0200+000,
   0200+000,0000+000,0000+000,0200+000,0000+000,0200+000,0200+000,0000+000,
   0200+000,0000+000,0000+000,0200+000,0000+000,0200+000,0200+000,0000+000,
   0000+000,0200+000,0200+000,0000+000,0200+000,0000+000,0000+000,0200+000,
   0200+000,0000+'|',0000+000,0200+000,0000+000,0200+000,0200+000,0000+'`',
   0000+'{',0200+'}',0200+000,0000+000,0200+000,0000+000,0000+000,0200+000,
   0000+000,0200+000,0200+000,0000+000,0200+000,0000+000,0000+000,0200+000,
   0200+000,0000+000,0000+000,0200+000,0000+000,0200+000,0200+000,0000+000,
   0200+000,0000+000,0000+000,0200+000,0000+000,0200+000,0200+000,0000+000,
   0000+000,0200+000,0200+000,0000+000,0200+000,0000+000,0000+000,0200+000,
   0000+000,0200+000,0200+000,0000+000,0200+000,0000+000,0000+000,0200+000,
   0200+000,0000+000,0000+000,0200+000,0000+000,0200+000,0200+'~',0000+000,
   0000+000,0200+'A',0200+'B',0000+'C',0200+'D',0000+'E',0000+'F',0200+'G',
   0200+'H',0000+'I',0000+'J',0200+'K',0000+'L',0200+'M',0200+'N',0000+'O',
   0200+'P',0000+'Q',0000+'R',0200+'S',0000+'T',0200+'U',0200+'V',0000+'W',
   0000+'X',0200+'Y',0200+'Z',0000+000,0200+000,0000+000,0000+000,0200+000};

struct cblock			/* structure for an element of the 	*/
 {	struct cblock *c_next;	/* character queues.  All queued bytes	*/
	char	info[6]; };	/* for ttys are kept in queues of these */
struct cblock	*cfreelist;	/* header for the list of free cblocks	*/
struct cblock	cfree[NCLIST];	/* the array containing all the cblocks */

char	spcltable[]		/* bitmap used by ttyinput to decide if	*/
 {	0000,0000,0000,0000,	/* an editing action is required by any	*/
	0000,0000,0000,0000,	/* particular character.  This map is	*/
	0000,0000,0000,0000,	/* initialized in cinit when the system	*/
	0000,0000,0000,0000 };	/* first comes up.			*/

char	wordtable[]		/* word table is used by the word	*/
{	0000,0000,0000,0000,	/* deletion special editing function to	*/
	0200,0000,0377,0003,	/* determine whether or not a character	*/
	0376,0377,0377,0007,	/* is an alphameric.  If the c'th bit	*/
	0376,0377,0377,0007 };	/* is on then that character is.	*/

char	indtable[]		/* indicate table is used by ttyoutput	*/
{	0177,0333,0377,0377,	/* to determine whether or not a ctl	*/
	0000,0000,0000,0000,	/* character is "funny".  If the INDCTL	*/
	0000,0000,0000,0000,	/* bit has been turned on, any char	*/
	0000,0000,0000,0200};	/* who's bit is on gets "indicated"	*/

struct {int ttrcsr;	/* structure for KL/DL registers.  Used by	*/
	int ttrbuf;	/* the KL driving code which is included in	*/
	int tttcsr;	/* ttstart.					*/
	int tttbuf; };

char *panicstr;		/* used for printing out system status */
char statstr[];		/* string containing system statistics */

/* name:
	ttyopen

function:
	Called in response to an open on a tty.  If this is
	the first open, initialize the process group.

algorithm:
	if opening process is not yet in any process group
		set process group to be process id;
		set process group of tty to be process id;
		set device number and tty pointer in u.u structure;
	mark the device state open (no longer waiting for open)

parameters:
	int	major and minor device numbers
	*tty	pointer to associated tty structure

returns:
	nothing

globals:
	u.u_ttyp =
	u.u_ttyd =
	p_pgrp =

calls:

called by:
	device open routines

history:
	Taken in its entirety from a Bell Unix 6.3 tty.c

 */
ttyopen( dev , atp )
 int dev;
 struct tty *atp;
{	register struct proc *pp;
	register struct tty *tp;

	pp = u.u_procp;
	tp = atp;
	if (pp->p_pgrp == 0) then
	{	pp->p_pgrp = pp->p_pid;
		tp->t_pgrp = pp->p_pid;
		u.u_ttyp = tp;
		u.u_ttyd = dev;

		tp->t_width = 0;
		tp->t_length = 0;
	}

	tp->t_state =& ~WOPEN;
	tp->t_state =| ISOPEN;

}
/* name:
	ttread

function:
	the read routine for all tty devices

algorithm:
	if a screen length is specified
		turn on paging bit;
		set current line to zero;

		( every process read resets all paging indicators )
	else	turn off paging bit (incase it was left on)
	while not enough characters are queued up
		if the line is dead, return an error
		else sleep on the input queue
	while there are still characters in the queue
		if we get an escape, get the next character
		else if we are using delimiters and get one, break out
		if user wants more characters, continue;
		if we arent using delimiters, return;
		if the next character in the queue is not a delimiter, return;
		(test added because user might read up to, but not including
		 a delimiter, and thus read an eof on his next read)
parameters:
	pointer to a tty structure

returns:

globals:
	u.u_error =

calls:
	sleep
	passc

called by:
	device read routine (after mapping device into tty structure pointer )

history:
	Designed and coded by Mark Kampe, UCLA-ATS, march '76.  Major changes.
	The elimination of the cannon queue and cannonization as a seperate
	processing step.  The changing of delimiters from high bit on to an
	escape sequence to allow 8-bit bytes to be read.  The removal of
	delimiters from raw mode to allow as many characters as possible to
	be read.

 */
ttread( atp )
 struct tty *atp;
{	register struct tty *tp;
	register int c;
	register int delimited;
	tp = atp;


	if ((tp->t_flags&(RAW+USRBRK)) == RAW)
	then	delimited = false;
	else	delimited = true;
	spl5();
	while( (tp->t_inq.c_cc == 0) || (delimited && (tp->t_delct == 0)) )
		if (tp->t_state & CARR_ON) then sleep( &tp->t_inq , TTIPRI  );
		else {
			spl0();
			return( u.u_error = ENXIO );
			} ;
	spl0();

	while( (c = getc( &tp->t_inq )) >= 0 )
	{	if (c == CESCAPE) then
			c = getc( &tp->t_inq );
		else	if ( delimited && (c == CDELIM) ) then
		{	tp->t_delct--;
			break;
		}
		if ((c = passc(c)) >= 0) then continue;
		if (!delimited) then break;
		if ( (*(tp->t_inq.c_cf)&0377) != CDELIM ) then break;
	}
	if (tp->t_length) then
	{	tp->t_state =| PAGING;
		tp->t_line = 0;
	}
	else	tp->t_state =& ~PAGING;
}
/* name:
	ttwrite

function:
	the write routine for all tty devices

algorithm:
	while ( line is alive and user buffer is nonempty )
		spl5
		while ( too many characters are in the output queue )
			kick the driver;
			note that someone is waiting for completion;
			goto sleep on the output queue;
		spl0
		pass one more character from the user buffer to the output q;

	if line is dead then set u.u_error

parameters:
	pointer to the tty structure for the output device

returns:

globals:
	u.u_error =

calls:
	sleep
	cpass
	ttstart
	ttyoutput

called by:
	device write routines

history:
	taken in its entirety from a standard Bell Unix 6.3 tty.c

 */
ttwrite( atp )
 struct tty *atp;
{	register struct tty *tp;
	register int c;

	tp = atp;
	while ( tp->t_state&CARR_ON && ( (c = cpass()) >= 0 ) )
	{	spl5();
		while ( tp->t_outq.c_cc > TTHIWAT )
		{	ttstart(tp);
			tp->t_state =| ASLEEP;
			sleep( &tp->t_outq , TTOPRI );
		}
		spl0();
		ttyoutput( c , tp );
	}

	if ( (tp->t_state&CARR_ON) == 0 ) then u.u_error = ENXIO;
	else	ttstart( tp );
}
/* name:
	ttyinput

function:
	process a character which has just been received by an interrupt
	routine for what could be considered as a teletype

algorithm:
	if we aren't using all 8 bits, and off the high bit;
	if we are in crmode and char is a cr, say it is an nl;
	if we are in LCASE mode and char is Upper case, map it to lower;
	if output is stopped, eat the character, restart output and return;
	if we are in literal state, do the escaping;
	if we are delimiting and character is a user break goto stashchar;
	say we arent delimiting;
	if we are editing and character is special, call spclchar
	else
	stashchar:
		if no room for this character, discard it and say its a bell
		else put it into the queue and if we are delimiting,
		     put a delimiter into the queue after it.
	If the character is non null, (and we are echoing)  echo it
	Call ttstart
	return

parameters:
	character to be input
	pointer to tty structure for appropriate device

returns:

globals:
	maptab		for upper case terminal mapping
	spcltable	for editing character recognition

calls:
	ttyoutput
	putc
	wakeup
	signal
	zapc
	spclchar

called by:
	the interrupt routines for the various teletype devices

history:
	designed and coded by mark kampe, ucla-ats, March '76.
	very little similarity to previous bell code.
 */
ttyinput( ac , atp )
 int ac;
 struct tty *atp;
{	register int c;
	register struct tty *tp;
	register int t_flags;
	int delimit;

	tp = atp;
	delimit = false;
	t_flags = tp->t_flags;
	c = ac;

	if (t_flags&ALL8)
	then	c =& 0377;
	else	c =& 0177;

	if (t_flags&ALTINT && c==CALTINT) then c = CINTR; /* local mapping */

	if ( tp->t_state & STOPOUT ) then
	{	tp->t_state =& ~STOPOUT;
		if ((c == '\r') || (c == '\n')) then
			tp->t_state =& ~PAGING;
		if ((c == CINTR) || (c == CQUIT)) then
			spclchar( c , tp );
		ttrstrt(tp);
		return;
	}

	if ( (t_flags&CRMOD) && (c == '\r') )
	then	c = '\n';

	if ( (t_flags&LCASE) && (c >= 'A') && (c <= 'Z') )
	then	c =+ 'a' - 'A';

	if (tp->t_state&LITERAL) then
	{	tp->t_state =& ~LITERAL;
		if ( ((t_flags&LCASE) == 0) || (c >= '\200') )
		then	goto stashchar;
		if (maptab[c]&0177) then c = maptab[c]&0177;
		goto stashchar;
	}

	if ((c < ' ') && (t_flags&USRBRK) && bit_on( &tp->t_brktab , c )) then
	{	delimit = true;
		goto stashchar;
	}

	if (((t_flags&RAW) == 0) && (c < '\200')) then
	{	if (bit_on( &spcltable , c )) then
		{	c = spclchar( c , tp );
			goto echochar;
		}
		else if (c == tp->t_kill) then
		{	c = spclchar( CKILL , tp );
			goto echochar;
		}
		else if (c == tp->t_erase) then
		{	c = spclchar( CERASE , tp );
			goto echochar;
		}
	}
 stashchar:
	if (tp->t_inq.c_cc >= TTYHOG) then
	{       c = CBELL;
		goto  echochar;
	}
	if ((c == CDELIM) || (c == CESCAPE))
	then	putc( CESCAPE , &tp->t_inq );
	if (putc( c , &tp->t_inq) != 0) then
	{       c = CBELL;
		goto echochar;
	}
	if (delimit && (putc( CDELIM, &tp->t_inq ) == 0)) then
		tp->t_delct++;
	if (delimit || (t_flags&RAW)) then
		wakeup( &tp->t_inq );
 echochar:
	if ( c && (t_flags&ECHO) ) then
	{	ttyoutput( c , tp );
		ttstart( tp );
	}
}
/* name:
	spclchar

function:
	to handle special characters (editing and otherwise)

algorithm:
	do a case on the character

parameters:
	char	the character received
	*tty	pointer to the associated tty table

returns:
	char	character if it should be echoed
		(but it may not be echoed depending on ECHO)
		zero if no echoing should be done.

globals:
	constants for the characters
	wordtable	for recognition of alphamerics

calls:
	ttyoutput
	zapc
	putc
	signal
	wakeup
	ttstart
	flushtty

called by:
	ttyinput

history:
	Designed and coded by Mark Kampe, UCLA-ATS, 4/76.  This code was
	put into a seperate procedure to simplify reading the listing and
	because it is very installation dependent.  If you want to change
	any of the editing characters (or add or remove any) you must do:
		change the case statement in this routine accordingly.
		change the character constants in tty.h accordingly.
		change the bit map (spcltable) initialization in cinit.
*/
spclchar( ac , atp )
 char ac;
 struct tty *atp;
{	register int c;
	register struct tty *tp;
	register char *p;
	int delims;

	c = ac&0177;
	tp = atp;

	switch (c)
	{case CINTR:	c = SIGINT;
			p = "(INT)\n";
			goto dosig;
	 case CQUIT:	c = SIGQIT;
			p = "(QUIT)\n";
		dosig:	signal( tp->t_pgrp , c );
			flushtty( tp );
			while( *p ) ttyoutput( *p++ , tp );
			ttstart( tp );
			return( 0 );

	 case '\n':	putc( c , &tp->t_inq );
	 case CEOT:	if (putc( CDELIM , &tp->t_inq ) == 0)
			then	tp->t_delct++;
			else	return( CBELL );
			wakeup( &tp->t_inq );
			if (c == CEOT) then
			{	for(p = "(EOF)\n"; *p; ttyoutput( *p++ , tp ));
				ttstart( tp );
				return( 0 );
			}
			else	return( '\n' );

	 case CLITERAL:	tp->t_state =| LITERAL;
			return( 0 );

	 case '\b':
	 case CERASE:	if ( (c = zapc(&tp->t_inq)) == -1 )
			then	return( CBELL );
			if ( (tp->t_flags&ECHO) == 0 ) then return( 0 );
			if (tp->t_flags&SCOPE) then
			{	ttyoutput( '\b' , tp );
				ttyoutput( ' ', tp );
				return( '\b' );
			}
			else
			{	ttyoutput( '\\' , tp );
				return( c );
			}

	 case CKILL:	while( zapc(&tp->t_inq) >= 0 );
			if ( (tp->t_flags&ECHO) == 0 ) then return( 0 );
			ttyoutput( c='X' , tp );
			ttyoutput( c , tp );
			ttyoutput( c , tp );
			return('\n');

	 case CDWORD:	c = zapc( &tp->t_inq );
			while ( (c >= 0) && (bit_on( wordtable , c ) == false))
				c = zapc( &tp->t_inq );
			while ( (c >= 0) && bit_on( wordtable , c ) )
				c = zapc( &tp->t_inq );
			if (c >= 0) then putc( c , &tp->t_inq );
			if ((tp->t_flags&ECHO) == 0 ) then return(0);
			ttyoutput( '<' , tp );
			return( '-' );

	 case CRETYPE:	if ((tp->t_flags&ECHO) == 0 ) then return( 0 );
			delims = tp->t_delct;
			c = tp->t_inq.c_cc;
			p = tp->t_inq.c_cf;
			if( tp->t_flags&SCOPE )
			then	ttyoutput( '\r' , tp );
			else	ttyoutput( '\n' , tp );
			while ( c-- )
			{	if (delims) then
				{	if (((*p++)&0377) == CDELIM) then 
						delims--;	}
				else	ttyoutput( *p++ , tp );
				if ((p&07) == 0) then
					p = ((p-8)->integ) + 2;
			}
			return( CBELL );

	 case CSTOP:	putc( CBELL , &tp->t_outq );
			putc( 0200 , &tp->t_outq );
			ttstart( tp );
			return( 0 );

	 case CSTATUS:	for(p = "System status: "; *p; ttyoutput( *p++ , tp ));
			for(p = panicstr; *p; ttyoutput( *p++ , tp ));
			for(p = statstr; *p; ttyoutput( *p++ , tp ));
			ttyoutput( '\n', tp );
			ttstart( tp );
			return( 0 );
	}
}
/* name:
	ttyoutput

function:
	to cause a character to be written out to a terminal

algorithm:
	if not using 8 bit bytes, and off the high bit;
	if upper case mapping
		go through escape sequence and translations
	if it is a wierd control character
		output an up arrow
		figure out what control char it is and say char is that
	if character is a tab and we are tab expanding
		turn it into spaces
		return
	if char is nl and we are in crlf mode
		ttyoutput a cr
	if character is an eot and we aren't in raw mode, discard it
	if we are in paging mode and this is a character we should stop on
		put a bell in the output queue
		put a "halt here" marker in the output queue
	if character has high bit on, put out an escape
	(necessary because usually high bit on means output delay control)
	put the character onto the output queue
	figure out the new collumn and do any necessary delays

parameters:
	char	character to be printed
	*tty	pointer to associated tty structure

returns:

globals:
	maptab	for character types

calls:
	putc
	ttyoutput

called by:
	ttwrite
	ttyinput

history:
	Taken, mostly from a standard Bell releas 6 system.  Padding has
	been changed so it is easier for the user to specify it and literal
	escapes have been added so that 8 bit characters can be sent to
	a terminal.
*/

ttyoutput( ac , atp )
 char ac;
 struct tty *atp;
{	register char c;
	register struct tty *tp;
	register char *colp;
	int flags;

	tp = atp;
	flags = tp->t_flags;
	if (flags & ALL8)
	then	c = ac&0377;
	else	c = ac&0177;

	if (flags & LCASE) then
	{	colp = "({)}!|^~'`";
		while(*colp++)
			if (c == *colp++) then
			{	ttyoutput('\\' , tp);
				c = colp[-2];
				break;
			};
		if ('a' <= c && c <= 'z') then
			c =+ 'A' - 'a';
	}

	if ((flags & INDCTL) && bit_on( indtable , c ))
	{	ttyoutput( '^' , tp );
		if (c == '\177')
		then	c = '?';
		else	c =| 0100;
	}

	colp = &tp->t_col;
	if ((c == '\t') && (flags&XTABS)) then
	{	do	ttyoutput(' ', tp);
		while( *colp & 07 );
		return;
	}


	if ((c == '\n') && (flags&CRMOD)) then
		ttyoutput( '\r' , tp );

	if ((flags&RAW == 0) && (c == '\004')) then return;

	flags = PS->integ;
	spl5();
	if ( (tp->t_state&PAGING) && ((c == CFORM) || (c == CVTAB) ||
		((c == '\n') && (++(tp->t_line) == tp->t_length)) ) ) then
	{	putc( CBELL , &tp->t_outq );
		putc( 0200 , &tp->t_outq );
		tp->t_line = 0;
	}
	if (c&0200) then
		putc( CESCAPE , &tp->t_outq );
	putc( c , &tp->t_outq );
	PS->integ = flags;

	/* now do collumn computation and delay handling */
	if (c == '\n') then
	{	*colp = 0;
		flags = tp->t_nldelay;
		goto dodelay;
	}
	if (c == '\r') then
	{	*colp = 0;
		flags = tp->t_crdelay;
		goto dodelay;
	}
	if (c == '\b') then
	{	(*colp)--;
		return;
	}
	if (c == '\t') then
	{	*colp =| 07;
		(*colp)++;
		flags = tp->t_tbdelay;
		goto dodelay;
	}
	if ((c == CFORM) || (c == CVTAB)) then
	{	flags = tp->t_vtdelay;
		goto dodelay;
	}
	/* it is not a special character */
	if ((c >= ' ') && (c < '\177')) then
	{	(*colp)++;
		if ( tp->t_width && (*colp >= tp->t_width)) then
			for( colp = "\n ***"; *colp; ttyoutput(*colp++ , tp));
	}
	return;

dodelay:/*	flags = zero means no padding			*/
	/*	flags > 0 means pad with that many nulls	*/
	/*	flags & 200 means wait flags &177 60ths second	*/
	if ((flags =& 0377) <= 0200) then
		for(flags =& 0177; flags; flags--)
			putc( '\000' , &tp->t_outq );
	else
	{	if (flags == CESCAPE) then flags--;
		putc( flags , &tp->t_outq );
	}
}
/* name:
	ttstart and ttrstrt

function:
	start (or restart) output on a particular line

algorithm:
	ttrstrt:
		if output was explicitly stopped, return;
		turn off the timeout flag
	ttstart:
		disable interrupts;
		if tty has a special start routine
			call it
			goto done;
		else (asume it is a KL type device)
			if device not ready or in timeout state
				goto done;
			if char is a literal escape, get next char
			else if char is a timeout indication
				schedule a restart for later
				goto done;
			stash the character in the output register
	done:	restore interrupts;
		return;

parameters:
	*tty	pointer to relevent tty structure

returns:

globals:
	maptab	for parity computation

calls:
	timeout
	special startup routine (through tty structure)

called by:
	lots of people

history:
	Basically from Bell release 6, with change to ttrstrt for output
	stopping and change to ttstart to use literal escapes to send
	8 bit data to a terminal.
*/
ttrstrt( atp )
 struct tty *atp;
{	register struct tty *tp;

	tp = atp;
	if (tp->t_state&STOPOUT) then return;
	tp->t_state =& ~TIMEOUT;
	ttstart( tp );
}
ttstart( atp )
 struct tty *atp;
{	register struct tty *tp;
	register int *addr;
	register int c;
	int sps;
	struct { int (*func)(); };

	sps = PS->integ;
	spl5();
	tp = atp;
	addr = tp->t_addr;

	if (tp->t_state & SSTART) then (*addr.func)(tp);
	else
	{	if ((addr->tttcsr&DONE) == 0  || (tp->t_state&TIMEOUT))
		then 	goto done;
		if ((c = getc( &tp->t_outq )) >= 0) then
		{	if (c == CESCAPE) then
				c = getc( &tp->t_outq );
			else	if (c&0200) then
			{	if (c =& 0177) then
					timeout(ttrstrt, tp, c);
				else	tp->t_state =| STOPOUT;
				tp->t_state =| TIMEOUT;
				goto done;
			}
			if (tp->t_flags&RAW) then
				addr->tttbuf = c;
			else	addr->tttbuf = c | (maptab[c] & 0200);
		}
	}
	done:
		PS->integ = sps;
}
/* name:
	flushtty and wflushtty

function:
	to flush all input and output queues associated with a given tty

algorithm:
	wflushtty:
		while line is up and characters are waiting for output
			mark line asleep
			sleep on it
	flushtty:
		flush all characters from output queue
		flush all characters from input queue
		reset the delimiter count;
		send wakeups to processes sleeping on either queue

parameters:
	*tty	pointer to the relevent tty structure

returns:

globals:

calls:
	sleep
	spl
	wakeup
	getc

called by:
	lots of people

history:
	very much like the bell v6 routines.  Minor changes for removing
	the cannon queue and checking for a dead line in wflushtty
*/

wflushtty( atp )
 struct tty *atp;
{	register struct tty *tp;

	tp = atp;
	spl5();
	while( (tp->t_state&CARR_ON) && (tp->t_outq.c_cc) )
	{	tp->t_state =| ASLEEP;
		sleep( &tp->t_outq , TTOPRI );
	}
	flushtty( tp );
	spl0();
}

flushtty( atp )
 struct tty *atp;
{	register struct tty *tp;
	register int sps;

	tp = atp;
	while ( getc( &tp->t_outq ) >= 0 );
	sps = PS->integ;
	spl5();
	while ( getc( &tp->t_inq ) >= 0 );
	tp->t_delct = 0;
	PS->integ = sps;
	wakeup( &tp->t_inq );
	wakeup( &tp->t_outq );
}
/* name:
	cinit

function:
	initialization routine for character devices

algorithm:
	Initialize the free list to contain all cblocks
	Count the character devices
	initialize the special character bitmap
	initialize panicstr to say system is up

parameters:

returns:

globals:
	cfree
	nchrdev
	spcltable
	panicstr
	cdevsw

calls:
	set_bit

called by:
	main

history:
	Code to initialize the spcltable added by mark kampe.
	The spcl table is initialized here (dynamically) because that
	is a more comprehensible initialization of a bit map and so
	that the initialization will automatically change when you
	change the character definitions in tty.h
*/
cinit()
{	register int ccp;
	register struct cblock *cp;
	register struct cdevsw *cdp;

	ccp = cfree;
	for (cp =(ccp+07)&~07; cp<&cfree[NCLIST-1]; cp++)
	{	cp->c_next = cfreelist;
		cfreelist = cp;
	}

	ccp = 0;
	for(cdp = cdevsw; cdp->d_open; cdp++)
		ccp++;
	nchrdev = ccp;

	/* CERASE and CKILL are explicitly not in the table because
	   they are user setable.  Puting them in the table would force
	   the default characters on all users */
	set_bit( &spcltable , CINTR );
	set_bit( &spcltable , CQUIT );
	set_bit( &spcltable , CEOT );
	set_bit( &spcltable , CLITERAL );
	set_bit( &spcltable , CDWORD );
	set_bit( &spcltable , CRETYPE );
	set_bit( &spcltable , CSTOP );
	set_bit( &spcltable , CSTATUS );
	set_bit( &spcltable , '\b' );
	set_bit( &spcltable , '\n' );

	panicstr = "up";
}
/* name:
	stty, gtty and spclfcn

function:
	routines to implement the stty, gtty and (more general) spclfcn
	system calls

algorithm:
	gtty:
		reformat the arguments and call spclfcn
	stty:
		reformat the arguments and call spclfcn

	spclfcn:
		get pointer to associated file block;
		if none, then error;
		if not a character device, error;
		call the (device specific) special function routine
		(passing it the major and minor device numbers)

parameters:
	in u.u_arg and u.u_ar0[R0]

returns:

globals:
	u.u_error
	u.u_arg
	u.u_ar0
	cdevsw

calls:
	special function routines through cdevsw

called by:
	called through sysent by system calls

history:
	Designed and coded by Mark Kampe, UCLA-ATS, 4/76
	the stty and gtty system calls (special function calls for
	character devices) are really special function calls for
	terminals.  I have added a more general system call (spclfcn)
	and changed stty and gtty to be alternate entry points into that
	more general call.
*/
gtty()
{	u.u_arg[1] = u.u_arg[0];	/* move vector pointer to 2nd arg */
	u.u_arg[0].hibyte = 't';	/* it is a teletype device */
	u.u_arg[0].lobyte = 0;		/* and a general status request */
	spclfcn();
}
stty()
{	u.u_arg[1] = u.u_arg[0];	/* move vector ptr to 2nd arg */
	u.u_arg[0].hibyte = 't';	/* it is a teletype device */
	u.u_arg[0].lobyte = 1;		/* and a type 1 operation */
	spclfcn();
}

/*	the format of arguments for the spclfcn call is:
	r0	file descriptor
	arg1.hi	letter, identifying the device type
	arg1.lo	op code
	arg2	undefined argument (maybe a pointer to a vector) */
spclfcn()
{	register struct file *fp;
	register struct inode *ip;

	if ((fp = getf(u.u_ar0[R0])) == NULL ) then return;
	ip = fp->f_inode;
	if ((ip->i_mode&IFMT) != IFCHR)
	then	u.u_error = ENOTTY;
	else	(*cdevsw[ip->i_addr[0].d_major].d_sgtty) (ip->i_addr[0]);
}

/* name:
	ttystty

function:
	special function routine for tty type devices

algorithm:
	verify that the user thinks device is a terminal
	case on the op code
		0	old style gtty maps to this call
		1	old stype stty maps to this call
		2	return (speeds, delays, flags, editing and brk chars)
		3	set(speeds, delays, flags, editing and brk chars)
		default	error

parameters:
	ptr to tty structure
	u.u_arg[0].hibyte	device identifying letter
	u.u_arg[0].lobyte	op code
	u.u_arg[1]		a vector pointer

returns:
	false	if modes were changed
	true	if modes were not changed

globals:

calls:

called by:
	other tty type device spclfcn routines

history:
	Recoded by Mark Kampe, UCLA-ATS, 4/76 for the more general
	spclfcn arguments.
	The only change necessary to add a new op code is to add it to the
	case statement
*/
ttystty( atp )
{	register struct tty *tp;
	register int i;
	register int *v;
	int *p;

	tp = atp;
	if (u.u_arg[0].hibyte != 't') then
	{	u.u_error = EBADSPCL;
		return( true );
	};
	v = u.u_arg[1];
	switch (u.u_arg[0].lobyte)
	{ case 0:	/* status of speeds, erase, kill and flags */
			suword( v++ , tp->t_speeds );
			suword( v++ , (tp->t_kill << 8) | tp->t_erase );
			i = tp->t_flags&0377;
			i =| (tp->t_delays<<8);
			suword( v , i );
			return( true );
	  case 1:	/* set speeds, erase, kill and flags */
			wflushtty( tp );
			tp->t_speeds = fuword( v++ );
			i = fuword( v++ );
			tp->t_kill = i>>8;
			tp->t_erase = i&0377;
			i = fuword( v );	/* get the old style flags */
			tp->t_flags =& 0177400;
			tp->t_flags =| (i&0377);
			i =>> 8;
			tp->t_delays = i;
			/* now calculate what the delays should be */
			tp->t_nldelay = "\000\010\214\226"[i&03];
			i =>> 2;
			tp->t_tbdelay = "\000\007\213\226"[i&03];
			i =>> 2;
			tp->t_crdelay = "\000\011\220\236"[i&03];
			i =>> 2;
			if (i&01) then tp->t_vtdelay = '\370';
			else	tp->t_vtdelay = 0;
			return(false);
	  case 2:	/* examine all settings */
			p = &tp->t_speeds;
			for( i = 8; i; i--)	/* copy 8 words */
				suword( v++ , *p++ );
			return( true );
	  case 3:	/* set all settings */
			wflushtty( tp );
			p = &tp->t_speeds;
			for( i = 8; i; i--)	/* copy 8 words */
				*p++ = fuword( v++ );
			if (tp->t_length < 10) then tp->t_length = 0;
			if (tp->t_width < 20) then tp->t_width = 0;
			return(false);

	  default:	u.u_error = EBADSPCL;
			return( true );
	}
}

