/*
 * ux_screen.c - Unix interface, screen manipulation
 *
 * This file is part of Frotz.
 *
 * Frotz is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Frotz is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 * Or visit http://www.fsf.org/
 */

#define __UNIX_PORT_FILE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef USE_NCURSES_H
#include <ncurses.h>
#else
#include <curses.h>
#endif

#include "ux_frotz.h"

extern void resize_screen(void);
extern void restart_header(void);
extern void restart_screen(void);

static WINDOW *saved_screen = NULL;

/*
 * os_erase_area
 *
 * Fill a rectangular area of the screen with the current background
 * colour. Top left coordinates are (1,1). The cursor does not move.
 *
 * The final argument gives the window being changed, -1 if only a
 * portion of a window is being erased, or -2 if the whole screen is
 * being erased.  This is not relevant for the curses interface.
 *
 */
void os_erase_area (int top, int left, int bottom, int right, int UNUSED(win))
{
    int y, x, i, j;

    /* Catch the most common situation and do things the easy way */
    if ((top == 1) && (bottom == h_screen_rows) &&
	(left == 1) && (right == h_screen_cols)) {
#ifdef COLOR_SUPPORT
      /* Only set the curses background when doing an erase, so it won't
       * interfere with the copying we do in os_scroll_area.
       */
      bkgdset(u_setup.current_color | ' ');
      erase();
      bkgdset(0);
#else
      erase();
#endif
    } else {
        /* Sigh... */
	int saved_style = u_setup.current_text_style;
	os_set_text_style(u_setup.current_color);
	getyx(stdscr, y, x);
	top--; left--; bottom--; right--;
	for (i = top; i <= bottom; i++) {
	  move(i, left);
	  for (j = left; j <= right; j++)
	    addch(' ');
	}
	move(y, x);
	os_set_text_style(saved_style);
    }
}/* os_erase_area */


/*
 * os_scroll_area
 *
 * Scroll a rectangular area of the screen up (units > 0) or down
 * (units < 0) and fill the empty space with the current background
 * colour. Top left coordinates are (1,1). The cursor stays put.
 *
 */
void os_scroll_area (int top, int left, int bottom, int right, int units)
{
  top--; left--; bottom--; right--;

  if ((left == 0) && (right == h_screen_cols - 1)) {
    static int old_scroll_top = 0;
    static int old_scroll_bottom = 0;

    if (!((old_scroll_top == top) && (old_scroll_bottom == bottom))) {
        old_scroll_top = top; old_scroll_bottom = bottom;
        setscrreg(top, bottom);
    }
    scrollok(stdscr, TRUE);
    scrl(units);
    scrollok(stdscr, FALSE);
  } else {
    int row, col, x, y;
    chtype ch;

    getyx(stdscr, y, x);
    /* Must turn off attributes during copying.  */
    attrset(0);
    if (units > 0) {
      for (row = top; row <= bottom - units; row++)
	for (col = left; col <= right; col++) {
	  ch = mvinch(row + units, col);
	  mvaddch(row, col, ch);
	}
    } else if (units < 0) {
      for (row = bottom; row >= top - units; row--)
	for (col = left; col <= right; col++) {
	  ch = mvinch(row + units, col);
	  mvaddch(row, col, ch);
	}
    }
    /* Restore attributes.  */
    os_set_text_style(u_setup.current_text_style);
    move(y, x);
  }
  if (units > 0)
    os_erase_area(bottom - units + 2, left + 1, bottom + 1, right + 1, 0);
  else if (units < 0)
    os_erase_area(top + 1, left + 1, top - units, right + 1, 0);
}/* os_scroll_area */


/**
 * Resize the display and redraw.  Retain the old screen starting from the
 * top left.  Call resize_screen, which may repaint more accurately.
 */
void unix_resize_display(void)
{
    if ((saved_screen = newpad(h_screen_rows, h_screen_cols))
        && overwrite(stdscr, saved_screen) == ERR) {
        delwin(saved_screen);
        saved_screen = NULL;
    }
    if (saved_screen) {
        int y, x;
        getyx(stdscr, y, x);
        wmove(saved_screen, y, x);
    }

    endwin();
    refresh();
    unix_get_terminal_size();

    resize_screen();
    if (zmp != NULL) {
	restart_header();
    }
    if (saved_screen) {
        delwin(saved_screen);
        saved_screen = NULL;
    }
}/* unix_redraw_display */


/**
 * Repaint a window.
 *
 * This can only be called from resize_screen.  It copies part of the screen
 * as it was before the resize onto the current screen.  The source and
 * destination rectangles may start at different rows but the columns
 * are the same.  Positions are 1-based.  win should be the index
 * of the window that is being repainted.  If it equals the current window,
 * the saved cursor position adjusted by ypos_new - ypos_old is also restored.
 *
 * The copied rectangle is clipped to the saved window size.  Returns true
 * on success, false if anything went wrong.
 */
bool os_repaint_window(int win, int ypos_old, int ypos_new, int xpos,
                       int ysize, int xsize)
{
    int lines, cols;
    if (!saved_screen)
        return false;
    getmaxyx(saved_screen, lines, cols);
    ypos_old--, ypos_new--, xpos--;
    if (xpos + xsize > cols)
        xsize = cols - xpos;
    if (ypos_old + ysize > lines)
        ysize = lines - ypos_old;
    /* Most of the time we are in os_read_line, where the cursor position
       is different from that in the window properties.  So use the real cursor
       position. */
    if (win == cwin) {
        int y, x;
        getyx(saved_screen, y, x);
        y += ypos_new - ypos_old;
        if (y >= ypos_new && y< ypos_new + ysize
            && x >= xpos && x < xpos + xsize)
            move(y, x);
    }
    if (xsize <= 0 || ysize <= 0)
        return false;
    return copywin(saved_screen, stdscr, ypos_old, xpos, ypos_new, xpos,
                   ypos_new + ysize - 1, xpos + xsize - 1, FALSE) != ERR;
}
