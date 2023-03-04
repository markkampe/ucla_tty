In 1976 I had been playing with UNIX for a while at UCLA (where I brought 
up the 2nd UNIX system in California, under the licence that AT&T had
granted to UC).  After having worked on the DH-11 serial line driver and
written the DZ-11 driver I felt that the tty driver could be restructured
to provide much better services, improver user interaction, and simplify
the code in the line drivers.  I published that code to the UNIX User's
Group and Dennis Richie asked if he could use it as a basis for work that
AT&T was doing (creating vastly superior interconnected streams of drivers).

I was proud that some of my code had made it into the base system, and was
occasionally asked questions about features (or merely fun choices I had
made).

In 2022 I built a PiDP 11/70, on which I was again able to run UNIX v6.
This caused me to look back through old files, and I found a couple of
1976 DEC-Tapes which I was sure could never be read again.  When I
mentioned this, I was pointed to a delightful computer museum crazy:
Josh Derschy, who said he would be happy to read those tapes for me.

On it, I found that ancient rewrite of the UNIX tty driver (and updates
to use it from the various line drivers).  This is code that nobody will
every want to use ... but, as the first widely disseminated OS work I
ever did, I thought it worth saving:
   
   - [tty.h](tty.h) ... per device information structure, mode-bits, and default editing characters.
   - [tty.c](tty.c) ... common code, used by all line drivers
   - [kl.c](kl.c) ... line driver for the (old) single-port KL/DL11 console interface
   - [dj.c](dj.c) ... line driver for the (old) 8-port DJ11 multiplexer
   - [dh.c](dh.c) ... line driver for the DH11 16-port multiplexer
   - [dm.c](dm.c) ... diver for the 16-port DM11 modem-signal controller (works with DH11)
   - [dz.c](dz.c) ... line driver for the (newer) DZ11 8-port multiplexer
     
The line drivers only did the work to talk to the hardware.  All higher level
input-editing and output preening was done by the common tty driver.  Each line
driver had:
   - an xx``open`` routie that would bring the line alive, and then call
     ``ttyopen`` to initialize the session.
   - an xx``rint`` received interrupt handler that called ``ttyinput`` with the received
     character and a pointer to a unit information structure.
   - an xx``xint`` transmission interrupt handler that called ``ttstart`` to start new
     output and (if appropriate) woke up any output that might have been blocked.
   - an xx``read`` routine that called ``ttread``, passing it a unit information structure.
   - an xx``write`` routine that called ``ttwrite``, passing it a unit information structure.
   - an xx``sgtty`` routine that called ``ttystty`` to handle any mode changes.

The general purpose tty driver:
   - ``ttyinput`` handled each received character, handling all input editing
     and all special (e.g. interrupt, kill, retype) characters.
   - ``ttread`` handled the delivery of received data to the requesting user
   - ``ttwrite`` handled writes from the user to the device
   - ``ttyoutput`` passed each written character to the line driver, handling
     tabs, newlines, bells, and other special characters.
   - ``ttstart`` and ``ttrstart`` initiated the flow of output that had not yet
     started or had been blocked (e.g. by ^O).
   - ``ttystty`` handled all length/speed/mode changes

