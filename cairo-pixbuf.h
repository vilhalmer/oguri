#ifndef OGURI_CAIRO_PIXBUF_H
#define OGURI_CAIRO_PIXBUF_H

#include <cairo/cairo.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

int oguri_cairo_surface_paint_pixbuf(
		cairo_surface_t * surface, const GdkPixbuf * pixbuf);

#endif
