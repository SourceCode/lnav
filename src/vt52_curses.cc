/**
 * @file vt52_curses.cc
 */

#include "config.h"

#include <assert.h>
#include <curses.h>
#include <unistd.h>

#include <map>

#include "vt52_curses.hh"

using namespace std;

/**
 * Singleton used to hold the mapping of ncurses keycodes to VT52 escape
 * sequences.
 */
class vt52_escape_map {
public:

    /** @return The singleton. */
    static vt52_escape_map &singleton()
    {
	static vt52_escape_map s_vem;

	return s_vem;
    };

    /**
     * @param ch The ncurses keycode.
     * @return The null terminated VT52 escape sequence.
     */
    const char *operator[](int ch) const
    {
	map < int, const char * >::const_iterator iter;
	const char *retval = NULL;

	if ((iter = this->vem_map.find(ch)) != this->vem_map.end()) {
	    retval = iter->second;
	}

	return retval;
    };

    const char *operator[](const char *seq) const
    {
	map < string, const char * >::const_iterator iter;
	const char *retval = NULL;

	assert(seq != NULL);

	if ((iter = this->vem_input_map.find(seq)) !=
	    this->vem_input_map.end()) {
	    retval = iter->second;
	}

	return retval;
    };

private:

    /** Construct the map with a few escape sequences. */
    vt52_escape_map()
    {
	static char area_buffer[1024];
	char *area = area_buffer;
	
	if (tgetent(NULL, "vt52") == ERR) {
	    perror("tgetent");
	}
	this->vem_map[KEY_UP]        = tgetstr("ku", &area);
	this->vem_map[KEY_DOWN]      = tgetstr("kd", &area);
	this->vem_map[KEY_RIGHT]     = tgetstr("kr", &area);
	this->vem_map[KEY_LEFT]      = tgetstr("kl", &area);
	this->vem_map[KEY_HOME]      = tgetstr("kh", &area);
	this->vem_map[KEY_BACKSPACE] = "\010";
	this->vem_map[KEY_DC]        = "\x4";

	this->vem_map[KEY_BEG] = "\x1";
	this->vem_map[KEY_END] = "\x5";

	this->vem_map[KEY_SLEFT] = tgetstr("#4", NULL);
	if (this->vem_map[KEY_SLEFT] == NULL) {
	    this->vem_map[KEY_SLEFT] = "\033b";
	}
	this->vem_map[KEY_SRIGHT] = tgetstr("%i", NULL);
	if (this->vem_map[KEY_SRIGHT] == NULL) {
	    this->vem_map[KEY_SRIGHT] = "\033f";
	}

	this->vem_input_map[tgetstr("ce", &area)] = "ce";
	this->vem_input_map[tgetstr("kl", &area)] = "kl";
	tgetent(NULL, getenv("TERM"));
    };

    /** Map of ncurses keycodes to VT52 escape sequences. */
    map < int, const char * > vem_map;
    map < string, const char * > vem_input_map;
};

vt52_curses::vt52_curses()
    : vc_window(NULL),
      vc_x(0),
      vc_y(0),
      vc_escape_len(0)
{ }

vt52_curses::~vt52_curses()
{ }

const char *vt52_curses::map_input(int ch, int &len_out)
{
    const char *esc, *retval;

    /* Check for an escape sequence, otherwise just return the char. */
    if ((esc = vt52_escape_map::singleton()[ch]) != NULL) {
	retval  = esc;
	len_out = strlen(retval);
    }
    else {
	this->vc_map_buffer = (char)ch;
	retval  = &this->vc_map_buffer;/* XXX probably shouldn't do this. */
	len_out = 1;
    }

    assert(retval != NULL);
    assert(len_out > 0);

    return retval;
}

void vt52_curses::map_output(const char *output, int len)
{
    int y, lpc;

    assert(this->vc_window != NULL);

    y = this->get_actual_y();
    wmove(this->vc_window, y, this->vc_x);
    for (lpc = 0; lpc < len; lpc++) {
	if (this->vc_escape_len > 0) {
	    const char *cap;

	    this->vc_escape[this->vc_escape_len] = output[lpc];
	    this->vc_escape_len += 1;
	    this->vc_escape[this->vc_escape_len] = '\0';

	    if ((cap = vt52_escape_map::singleton()[this->vc_escape]) != NULL) {
		if (strcmp(cap, "ce") == 0) {
		    wclrtoeol(this->vc_window);
		    this->vc_escape_len = 0;
		}
		else if (strcmp(cap, "kl") == 0) {
		    this->vc_x -= 1;
		    wmove(this->vc_window, y, this->vc_x);
		    this->vc_escape_len = 0;
		}
	    }
	}
	else {
	    switch (output[lpc]) {
	    case STX:
		this->vc_past_lines.clear();
		this->vc_x = 0;
		wmove(this->vc_window, y, this->vc_x);
		wclrtoeol(this->vc_window);
		break;

	    case BELL:
		flash();
		break;

	    case BACKSPACE:
		this->vc_x -= 1;
		wmove(this->vc_window, y, this->vc_x);
		break;

	    case ESCAPE:
		this->vc_escape[0]  = ESCAPE;
		this->vc_escape_len = 1;
		break;

	    case '\n':
		{
		    unsigned long width, height;
		    char          *buffer;

		    getmaxyx(this->vc_window, height, width);

		    buffer     = (char *)alloca(width);
		    this->vc_x = 0;
		    wmove(this->vc_window, y, this->vc_x);

		    /*
		     * XXX Not sure why we need to subtract one from the width
		     * here, but otherwise it seems to spill over and screw up
		     * the next line when we're writing it back out in
		     * do_update().
		     */
		    winnstr(this->vc_window, buffer, width - 1);
		    this->vc_past_lines.push_back(buffer);
		    wclrtoeol(this->vc_window);
		}
		break;

	    case '\r':
		this->vc_x = 0;
		wmove(this->vc_window, y, this->vc_x);
		break;

	    default:
		mvwaddch(this->vc_window, y, this->vc_x, output[lpc]);
		this->vc_x += 1;
		break;
	    }
	}
    }
}

void vt52_curses::do_update(void)
{
    list<string>::iterator iter;
    int y;

    y = this->get_actual_y() - (int)this->vc_past_lines.size();
    for (iter = this->vc_past_lines.begin();
	 iter != this->vc_past_lines.end();
	 iter++, y++) {
	if (y >= 0) {
	    mvwprintw(this->vc_window, y, 0, "%s", iter->c_str());
	    wclrtoeol(this->vc_window);
	}
    }
    wmove(this->vc_window, y, this->vc_x);
}
