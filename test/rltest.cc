
#include <stdio.h>
#include <curses.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <util.h>
#include <sys/time.h>

#include <readline/readline.h>

#include "vt52_curses.hh"

static const int KEY_TIMEOUT = 500 * 1000;

static int got_line = 0;
static int got_timeout = 0;

static void sigalrm(int sig)
{
    got_timeout = 1;
}

static void line_ready(char *line)
{
    fprintf(stderr, "got line: %s\n", line);
    add_history(line);
    got_line = 1;
}

static void child_readline(void)
{
    fd_set rfds;

    FD_ZERO(&rfds);
    FD_SET(STDIN_FILENO, &rfds);
    
    rl_callback_handler_install("/", (void (*)())line_ready);
    while (1) {
	fd_set ready_rfds = rfds;
	int rc;

	rc = select(STDIN_FILENO + 1, &ready_rfds, NULL, NULL, NULL);
	if (rc < 0) {
	    switch (errno) {
	    case EINTR:
		break;
	    }
	}
	else {
	    if (FD_ISSET(STDIN_FILENO, &ready_rfds)) {
		struct itimerval itv;

		itv.it_value.tv_sec = 0;
		itv.it_value.tv_usec = KEY_TIMEOUT;
		itv.it_interval.tv_sec = 0;
		itv.it_interval.tv_usec = 0;
		setitimer(ITIMER_REAL, &itv, NULL);
		
		rl_callback_read_char();
	    }
	}

	if (got_timeout) {
	    fprintf(stderr, "got timeout\n");
	    got_timeout = 0;
	}
	if (got_line) {
	    rl_callback_handler_remove();
	    got_line = 0;
	    rl_callback_handler_install("/", (void (*)())line_ready);
	}
    }
}

static void finish(int sig)
{
    endwin();
    exit(0);
}

int main(int argc, char *argv[])
{
    int fd, retval = EXIT_SUCCESS;
    signal(SIGALRM, sigalrm);
    
    fd = open("/tmp/rltest.err", O_WRONLY|O_CREAT|O_APPEND, 0666);
    dup2(fd, STDERR_FILENO);
    fprintf(stderr, "startup\n");

    if (0) {
	while(1) {
	    char *ret = readline("/");

	    add_history(ret);
	}
    }
    
    (void) signal(SIGINT, finish);      /* arrange interrupts to terminate */

    WINDOW *mainwin = initscr();      /* initialize the curses library */
    keypad(stdscr, TRUE);  /* enable keyboard mapping */
    (void) nonl();         /* tell curses not to do NL->CR/NL on output */
    (void) cbreak();       /* take input chars one at a time, no wait for \n */
    (void) noecho();       /* don't echo input */

    if (fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK) < 0)
	perror("fcntl");

    {
	int master, slave;
	pid_t rl;
	
	if (openpty(&master, &slave, NULL, NULL, NULL) < 0) {
	    perror("openpty");
	}
	else if ((rl = fork()) < 0) {
	    perror("fork");
	}
	else if (rl == 0) {
	    close(master);
	    master = -1;

	    dup2(slave, STDIN_FILENO);
	    dup2(slave, STDOUT_FILENO);

	    setenv("TERM", "vt52", 1);
	    
	    child_readline();
	}
	else {
	    vt52_curses vc(mainwin);
	    fd_set rfds;
	    
	    FD_ZERO(&rfds);
	    FD_SET(STDIN_FILENO, &rfds);
	    FD_SET(master, &rfds);

	    while (1) {
		fd_set ready_rfds = rfds;
		int rc;

		rc = select(master + 1, &ready_rfds, NULL, NULL, NULL);
		if (rc < 0) {
		    break;
		}
		else {
		    char buffer[1024];
		    
		    if (FD_ISSET(STDIN_FILENO, &ready_rfds)) {
			int ch;

			if ((ch = getch()) != ERR) {
			    const char *bch;
			    int len;

			    bch = vc.map_input(ch, len);

			    if (len > 0) {
				fprintf(stderr, "stdin: %x\n", ch);
				if (write(master, bch, len) < 0)
				    perror("write");
			    }
			}
		    }
		    if (FD_ISSET(master, &ready_rfds)) {
			int lpc;
			
			rc = read(master, buffer, sizeof(buffer));

			fprintf(stderr, "child: ");
			for (lpc = 0; lpc < rc; lpc++) {
			    fprintf(stderr, "%x ", buffer[lpc]);
			}
			fprintf(stderr, "\n");
			
			vc.map_output(buffer, rc);
		    }
		}
		refresh();
	    }
	}
    }

    finish(0);
    
    return retval;
}
