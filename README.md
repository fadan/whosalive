# WhosAlive
**WhosAlive** is a small program using the [Twitch Api](https://dev.twitch.tv/docs/) to notify when a stream goes live.

![whosalive](https://user-images.githubusercontent.com/18427317/27762026-1833f18a-5e69-11e7-85ff-b3b0f63b9f77.png)

## Usage
* Add or modify `streams.txt` file next to the `win32_whosalive.exe` file.
  In this file, put each stream's username in separate line.
* Run `win32_whosalive.exe`.
* Clicking on the taskbar icon brings up a list of online/offline streams.
* Selecting a stream opens it's Twitch page in the browser.

## Download
[Here](https://github.com/fadan/whosalive/releases/download/v0.2/whosalive.zip) is the latest release in a zip.

## Building
* Install [Visual Studio 2013](https://www.visualstudio.com/vs/older-downloads/)
* Run build.bat

Libraries (single-file, public domain licensed) used:
* [stb_image](https://github.com/nothings/stb/blob/master/stb_image.h) for loading images
* [stb_image_resize](https://github.com/nothings/stb/blob/master/stb_image_resize.h) for resizing images
* [stb_image_write](https://github.com/nothings/stb/blob/master/stb_image_write.h) for writing images

## License
[MIT License](https://opensource.org/licenses/MIT)
