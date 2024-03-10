# oguri
### A very nice animated wallpaper daemon for Wayland compositors

	>> oguri -h
	Usage: oguri [-c <config-path>]

	  -c  Path to the configuration file to use.
		  (default: $XDG_CONFIG_HOME/oguri/config)
	  -h  Show this text.

## Notice: unmaintained!

You may have noticed that oguri hasn't gotten much attention for a while. It
was never really intended to be a thing people actually used, I wrote it
originally to learn the Wayland protocol. Lately I haven't had much time for
personal projects at all, and so it has languished. There are a couple
alternatives I'm aware of, please check them out:

- [swww](https://github.com/Horus645/swww), a direct replacement
- [mpvpaper](https://github.com/GhostNaN/mpvpaper) if you want real video

Feel free to fork if you like this codebase for some reason, but as of now I
can't imagine getting back to working on it. If you've used it in the past,
thanks! I didn't think this would actually be useful to anyone, so it's cool
that it was.

The rest of the README remains below:

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
