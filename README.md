# IOTool
Use RP2040/Raspberry Pi Pico as logic analyzer with Sigrok.

> This is my graduation project, I will try to add documentation later when I finish it.

# Installing
* In this repo you can find sketch you can upload to rp2040. To compile it you need [pico-sdk](https://github.com/raspberrypi/pico-sdk).
* Second thing you need is Sigrok program with customm driver, currently its not supported by default, you need to compile libsigrok yourself with patches above.
  * http://sigrok.org/wiki/Libsigrok
  * http://sigrok.org/wiki/Building

# Issues
* Some samples are lost, idk where
* Sigrok driver is not ideal
* Pico code can be more optimized
* Overall this whole thing is crap but it works somehow
