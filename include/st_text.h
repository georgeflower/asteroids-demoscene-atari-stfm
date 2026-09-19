#ifndef ST_TEXT_H
#define ST_TEXT_H

/*
 * Bitmap text for a 4-plane low-resolution ST screen (320x200, 160 bytes per row).
 * Each character is an 8x8 cell (16x16 at scale 2) drawn opaquely: set pixels get colour
 * fg, the rest colour bg (palette indices 0-15). x is rounded down to a multiple of 8
 * (16 at scale 2). Text is upper case only; anything off the screen is skipped.
 */
void st_text_draw(unsigned char *buffer, int x, int y, const char *text, int fg, int bg, int scale);

/* Set one pixel to palette index `color` (all four planes), for the stars. */
void st_plot_point(unsigned char *buffer, int x, int y, int color);

#endif
