
This is firmware for the JeeLabs JeeNode-Micro:

http://www.digitalsmarties.net/products/jeenode-micro

Hopefully it will record temperatures and light levels at LondonHackspace.

There is still some work to do (rough priority order):

* OneWire support
* report the battery voltage (not the voltage from the buck/boost converter!)
* Power down the radio when sleeping
* Juggle sleeping and power down states etc.
* Try to get the LDR to behave and if not remove it

I've not got the arduino ide uploading code properly, I've been cutting and
pasteing the path to the .hex file and then using:

sudo avrdude  -pattiny84  -cusbtiny -Uflash:w:/path/to/hex/file.hex:i

