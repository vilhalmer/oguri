// This is cargo-culted from mako, which in turn took it from from sway. It's
// modified to draw into an existing surface instead of creating one, and I
// also de-macro'd the premultiplied alpha routine.

#include "cairo-pixbuf.h"


int oguri_cairo_surface_paint_pixbuf(
		cairo_surface_t * surface, const GdkPixbuf * pixbuf) {
	// This function assumes that the source pixbuf has the same number of
	// channels as the target surface.
	int chan = gdk_pixbuf_get_n_channels(pixbuf);
	if (chan < 3) {
		return 1;
	}

	const guint8 * source_pixels = gdk_pixbuf_read_pixels(pixbuf);
	if (!source_pixels) {
		return 2;
	}
	gint w = gdk_pixbuf_get_width(pixbuf);
	gint h = gdk_pixbuf_get_height(pixbuf);
	int source_stride = gdk_pixbuf_get_rowstride(pixbuf);

	cairo_surface_flush(surface);
	if (!surface || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
		return 3;
	}

	int target_stride = cairo_image_surface_get_stride(surface);
	unsigned char * target_pixels = cairo_image_surface_get_data(surface);

	if (chan == 3) {
		for (int i = h; i; --i) {
			const guint8 * gp = source_pixels;
			unsigned char * cp = target_pixels;
			const guint8 * end = gp + (3 * w);
			while (gp < end) {
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
				cp[0] = gp[2];
				cp[1] = gp[1];
				cp[2] = gp[0];
#else
				cp[1] = gp[0];
				cp[2] = gp[1];
				cp[3] = gp[2];
#endif
				gp += 3;
				cp += 4;
			}
			source_pixels += source_stride;
			target_pixels += target_stride;
		}
	} else {
		/* premul-color = alpha/255 * color/255 * 255 = (alpha*color)/255
		 * (z/255) = z/256 * 256/255     = z/256 (1 + 1/255)
		 *         = z/256 + (z/256)/255 = (z + z/255)/256
		 *         # recurse once
		 *         = (z + (z + z/255)/256)/256
		 *         = (z + z/256 + z/256/255) / 256
		 *         # only use 16bit uint operations, loose some precision,
		 *         # result is floored.
		 *       ->  (z + z>>8)>>8
		 *         # add 0x80/255 = 0.5 to convert floor to round
		 *       =>  (z+0x80 + (z+0x80)>>8 ) >> 8
		 * ------
		 * tested as equal to lround(z/255.0) for uint z in [0..0xfe02]
		 */
		for (int i = h; i; --i) {
			const guint8 * gp = source_pixels;
			unsigned char * cp = target_pixels;
			const guint8 * end = gp + (4 * w);
			guint z1, z2, z3;
			while (gp < end) {
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
				z1 = gp[2] * gp[3] + 0x80;
				z2 = gp[1] * gp[3] + 0x80;
				z3 = gp[0] * gp[3] + 0x80;

				cp[0] = (z1 + (z1 >> 8)) >> 8;
				cp[1] = (z2 + (z2 >> 8)) >> 8;
				cp[2] = (z3 + (z3 >> 8)) >> 8;
				cp[3] = gp[3];
#else
				z1 = gp[0] * gp[3] + 0x80;
				z2 = gp[1] * gp[3] + 0x80;
				z3 = gp[2] * gp[3] + 0x80;

				cp[0] = gp[3];
				cp[1] = (z1 + (z1 >> 8)) >> 8;
				cp[2] = (z2 + (z2 >> 8)) >> 8;
				cp[3] = (z3 + (z3 >> 8)) >> 8;
#endif
				gp += 4;
				cp += 4;
			}
			source_pixels += source_stride;
			target_pixels += target_stride;
		}
	}
	cairo_surface_mark_dirty(surface);
	return 0;
}
