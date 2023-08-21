# lcd

This supports the lcd display on a Toshiba Magnia.

Original work by Hans-Michael Stahl, hms@web.de, fan control code by Eric Dube
Updated and put on github by Brian Wilson <brian@wildsong.biz>

2023/8/21 2.2.3 modified for whatever Linux Ray is using now.

2021/1/1 Currently it's working on an SG30 running Debian 10.7

I don't currently own one of these servers so I can't test it.

Look at the sources to find out how it works. It is reasonably self-documenting.

Today (2021/1/1) I added a define to switch between 24 hour and 12 hour time, no command line switch yet.

I added the fahrenheit -f cmd line option, and the firewall
control and lan address code.

I also add lcd.init which goes to /etc/init.d and must be linked 
to /etc/rc<n>.d/[SK]lcd to start and stop lcd in server mode 
when the system is started or stopped.

The main function of lcd is to make the buttons work again. If you push 
the power button twice the system is shut down and powered off (like in 
the toshiba version), after which you can switch it on again using the 
power button.

The select button (the one near to the display) toggles through a list 
display items - currently 
 date and time 
 cpu/system temperature 
 fan revolution 
 fan control selection
 hard disk temperature
 external ip address
 lan ip addresses
 uptime
 firewall control
 wireless lan control 

The fan control code originates from Eric Dube. Fan control by temperature
is off by default (I have temperature controlled fans installed).

For fan control, disk temperature, wireless control lcd must run as
root. The rest works in normel user mode.

For hard disk temperature to work smartctl must be installed.
For external ip address curl must be installed.
For wireless lan control to work a WLAN card must be in pcmcia
slot 2 (this can be edited in the source).

This all is highly kernel (I have 2.6 and sysfs on sg30), distribution and
configuration dependent and you very likely will have to 
modify the source. 

Also, lcd switches off the display at midnight and switches it on again 
at 7 am.

You can reboot the system (w/o power off) by pressing the power button 
and then the select button.

## LAN address

It displays ip address of eth0 on line 1 and eth1 on line 2.
I had to add a script, lcd calls /usr/local/sbin/ipaddr
with the ethernet device name, eth0 and eth1 to get the
ip address. There is probably some clever way to read the
ip from /proc filesystem but I did not know what it is.

## FIREWALL

To detect firewall status, it looks for a file "/var/tmp/firewall_is_on".

To turn OFF the firewall, it runs /usr/local/sbin/firewall-off.sh
which should also remove the file.

To turn ON the firewall, it runs /usr/local/sbin/network/firewall.sh
which should also remove the file.

My intention is that the Ubuntu configuration file /etc/network/interfaces
file should run firewall.sh to turn on the firewall at boot time.

