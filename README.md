# oguri
## A very nice animated wallpaper tool for Wayland compositors

    Usage:
    oguri <display> <image>

## Features

- Displays a gif on your desktop
- That's all
- (ok, it can actually display static images too)

## Testing

`oguri` is known to work with the following compositors:

- [sway](https://github.com/swaywm/sway) 1.0 alpha

If you find that it works with something else, please submit a PR! :) There is
a good chance that it will work with any wlroots-based compositor.

## Resource usage

CPU consumption will vary depending on the framerate of the provided animation.
I have seen around 2.0% utilization with the images I'm testing. Please note
that this tool is not actually a good idea. That said, I'll continue to poke at
it to try to make it more reasonable.

## Acknowledgements

Many thanks to the [swaywm](https://github.com/swaywm) project for both
providing quality code to reference during development, and for creating the
[Wayland protocols](https://github.com/swaywm/wlr-protocols) necessary for a
wallpaper tool to even work.

Equal credit goes to [mako](https://github.com/emersion/mako), which provided
just as much code to ~~steal~~ reference (and which I use, hack on, and
recommend).
