# oguri
### A very nice animated wallpaper daemon for Wayland compositors

[![builds.sr.ht status](https://builds.sr.ht/~vilhalmer/oguri.svg)](https://builds.sr.ht/~vilhalmer/oguri?)

	>> oguri -h
	Usage: oguri [-c <config-path>]

	  -c  Path to the configuration file to use.
		  (default: $XDG_CONFIG_HOME/oguri/config)
	  -h  Show this text.

## Features

- Animates gifs on your desktop
- That's all
- (ok, it can actually display static images too)

## Configuration

The configuration file is ini-style. Here's an example:

	[output LVDS-1]
	image=$XDG_CONFIG_HOME/wallpaper
	filter=nearest
	scaling-mode=fill
	anchor=center

Outputs displaying the same image share the animation timer, and are therefore
always in sync.

The output name `*` will match any output not specified elsewhere in the file.
To find your output names, consult your compositor's manual.

### Output options

- `image`: Path to the image on disk, environment variables and ~ are expanded.
- `scaling-mode`: How to scale the image to fit on the output:
	- `fill` (default)
	- `tile`
	- `stretch`
- `anchor`: Some combination of `top`, `bottom`, `left`, `right`, and `center`.
	Can be combined with dashes, such as `center-left`.
- `filter`: Scaling filter to use. Supported values:
	- `fast`: Quickest
	- `good`: Balance of speed and quality
	- `best`: Looks really good
	- `nearest`: Nearest neighbor, good for pixel art
	- `bilinear`: Linear interpolation

These are provided by [cairo](https://cairographics.org/manual/cairo-cairo-pattern-t.html#cairo-filter-t).

### Integrations

For sway users, the included `oguri-swaybg` wrapper can be used as
`swaybg_command` if you would prefer to continue managing your wallpaper from
the sway config file. This will automatically reload oguri any time sway's
config is reloaded, but at the cost of killing it (and therefore flickering)
to do so.

## Build + run

	meson build
	ninja -C build
	build/oguri

For an up-to-date dependency list, check out meson.build.

The host compositor must support the following protocols:

- wlr-layer-shell-unstable-v1
- xdg-output-unstable-v1

Available from the following packagers:

- [Arch Linux AUR](https://aur.archlinux.org/packages/oguri-git/) thanks
to [lamcw](https://github.com/lamcw)
- [nixpkgs-wayland](https://github.com/colemickens/nixpkgs-wayland) thanks to
[colemickens](https://github.com/colemickens)

## Resource usage

CPU consumption will vary depending on the framerate of the chosen image, as
oguri must wake up for every frame. However, with a reasonable (but still
visually interesting) image, I have seen it idling as low as 0.3%. It will be
noticably higher immediately after startup (or after reconfiguration), until it
can cache all of the scaled frames per output.

Memory consumption is a factor of the number of frames in each configured
image, the number of outputs displaying each image, and the resolution of each
display. It will remain constant once frames are cached.

## Other projects I like

Part of this complete ~breakfast~ environment!

- [swaywm](https://github.com/swaywm)
	- Home of the [Wayland protocols](https://github.com/swaywm/wlr-protocols)
	  that oguri needs to exist!
- [mako](https://github.com/emersion/mako)

![Oguri Cap](oguri-cap.gif)
