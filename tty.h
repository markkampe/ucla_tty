/*
 * A clist structure is the head
 * of a linked list queue of characters.
 * The characters are stored in 4-word
 * blocks containing a link and 6 characters.
 * The routines getc, putc and zapc (m45.s or m40.s)
 * manipulate these structures.
 */
struct clist
{
	int	c_cc;		/* character count */
	char	*c_cf;		/* pointer to first block */
	char	*c_cl;		/* pointer to last block */
};

/*
 * A tty structure is needed for
 * each UNIX character device that
 * is used for normal terminal IO.
 * The routines in tty.c handle the
 * common code associated with
 * these structures.
 * The definition and device dependent
 * code is in each driver. (kl.c dc.c dh.c)
 */
struct tty
{
	struct	clist t_inq;	/* input list from device */
	struct	clist t_outq;	/* output list to device */
	int	t_speeds;	/* output and input line speeds */
	char	t_erase;	/* the character delete */
	char	t_kill;		/* the line delete */
	int	t_flags;	/* modes, settable via spcl fcn call */
	char	t_nldelay;	/* delay for newline */
	char	t_crdelay;	/* delay for cr */
	char	t_tbdelay;	/* delay for horizontal tabs */
	char	t_vtdelay;	/* delay for vertical motion */
	char	t_width;	/* maximum line length for folding */
	char	t_length;	/* maximum screen length for paging */
	int	t_brktab[2];	/* break character map */
	int	*t_addr;	/* device addr (register or startup rtn */
	int	t_state;	/* internal state, not visible */
	int	t_pgrp;		/* process group number */
	char	t_line;		/* line number on screen */
	char	t_col;		/* collumn number on line */
	char	t_delays;	/* old format delays for compatibility */
	char	t_delct;	/* number of delimiters in queue */
	char	t_char;		/* scratch byte for the driver */
	char	t_garbage;	/* pad the structure out to a word boundary */
};


#define	TTIPRI	10
#define	TTOPRI	20

#define	CERASE	001		/* ^a, the default character delete */
#define	CINTR	003		/* ^c, the default interrupt character */
#define	CEOT	004		/* ^d, the default end of file indicator */
#define CBELL	007
#define CBACK	010
#define CVTAB	013
#define CFORM	014
#define	CSTOP	017		/* ^o, the default stop output character */
#define	CRETYPE	022		/* ^r, the default retype current line char */
#define CSTATUS	024		/* ^t, the default system status character */
#define CLITERAL 026		/* ^v, the default literal escape char */
#define CDWORD	027		/* ^w, the default word delete char */
#define	CKILL	030		/* ^x, the default line delete char */
#define CQUIT	034		/* ^\, "Eat flaming death, fascist pigs!" */
#define	CDELIM	0377		/* there is no reason to alter these 2 defs */
#define	CESCAPE	0376		/* as they are purely internal to the driver */
#define CALTINT	005		/* local alternate interrupt character */

/* limits */
#define	TTHIWAT	50
#define	TTLOWAT	30
#define	TTYHOG	256

/* modes */
#define	HUPCL	01
#define	XTABS	02
#define	LCASE	04
#define	ECHO	010
#define	CRMOD	020
#define	RAW	040
#define	ODDP	0100
#define	EVENP	0200
#define	SCOPE	0400
#define INDCTL	01000
#define	USRBRK	02000
#define	ALL8	04000
#define ALTINT	010000		/* map ^e's into ^c's (local mapping) */
#define NETTY	0100000
#define SEEMAP	0000000

/* delay fields */
#define	NLDELAY	0003
#define TBDELAY	0014
#define CRDELAY	0060
#define VTDELAY	0100

/* Hardware bits */
#define	DONE	0200
#define	IENABLE	0100

/* Internal state bits */
#define	TIMEOUT	01		/* Delay timeout in progress */
#define	WOPEN	02		/* Waiting for open to complete */
#define	ISOPEN	04		/* Device is open */
#define	SSTART	010		/* Has special start routine at addr */
#define	CARR_ON	020		/* Software copy of carrier-present */
#define	BUSY	040		/* Output in progress */
#define	ASLEEP	0100		/* Wakeup when output done */
#define LITERAL	0200		/* last char was a literal escape */
#define	STOPOUT	0400		/* momentarily halt output */
#define PAGING	01000		/* length specified, not temp. disabled */
