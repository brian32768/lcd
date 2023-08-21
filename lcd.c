/* Handle LCD and button of Toshiba SG 30 */
/* Hans-Michael Stahl, hms@web.de, fan control code by Eric Dube */
/* Copyleft according to Gnu General Public License */
/* see <Http://Www.Gnu.Org/Licenses/Gpl.Html> */
/* Modified to run on Ubuntu 8.04LTS by RJR */

#include <sys/types.h>
#include <sys/select.h>
#include <sys/sysinfo.h>
#include <fcntl.h>
#include <termios.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <signal.h>

#ifdef __FreeBSD__
#include <fcntl.h>
#include <machine/cpufunc.h>
#define OUTL(data, port) outl(port, data)
#else
#include <sys/io.h>
#define OUTL(data, port) outl(data, port)
#endif

#include "version.h"

/* The LCD device */     
#define LCDDEVICE "/dev/ttyS0"
#define LINELEN 16
#define BAUDRATE B9600

/* The ETHERNET devices */
#define ETH0 "/usr/local/sbin/ipaddr enp0s8"
#define ETH1 "/usr/local/sbin/ipaddr enp0s9"

#define CELSIUS    0
#define FAHRENHEIT 1
int tempunit = CELSIUS;

/* 24 hour time */
/* #define TIMEFORMAT "%H:%M" */
/* 12 hour time */
#define TIMEFORMAT "%I:%M %p"

/* this requires a kernel >= 2.6 with sysfs */
/* lmsensors must be installed, see http://secure.netroedge.com/~lm78/ */
/* and smartcntrl for disk temperatures */


/* we use the lm86 to read the temperature information from */
/* the /sys filesystem - adapt these strings if your setup differs */

/* the temperatures at which to switch on and off the fans */
/* are controlled by /etc/sensors.conf - edit and run "sensors -s" */
/* to change to values you like better */ 
 
/* directory where to find lm86 information */
//static const char _SYSBASENAME_LM[] = "/sys/bus/i2c/drivers/lm80/0-002d/";

static const char _SYSTEMPINP[] = "temp1_input";
static const char _SYSTEMPMAX[] = "temp1_max";
#define SYSTEMPHYST 8 /* hysteresis */

/* External CPU temperature */
static const char _CPUTEMPINP[] = "temp2_input";	/* current temp */
static const char _CPUTEMPMAX[] = "temp2_max";		/* max temp -> fan on */
/* Internal CPU temperature (SBr)*/
//static const char _SBRTEMPINP[] = "temp3_input";	/* current temp */
//static const char _SBRTEMPMAX[] = "temp3_max";		/* max temp -> fan on */

#define CPUTEMPHYST 8 /* hysteresis */

/* we use the via686a to find the fan revolutions */

/* directory where to find via686a information */
//static const char _SYSBASENAME_VIA[] = "/sys/bus/i2c/drivers/via686a/9191-6000/";
/* directory where to find via686a information for Ubuntu 8.04LTS */
static const char _SYSBASENAME_VIA[] = "/sys/devices/platform/via686a.24576/";

static const char _SYSFANINP[] =  "fan2_input";
static const char _CPUFANINP[] =  "fan1_input";


#define DEB 0			/* if 1, writes info to stderr */
#define TEST 0			/* if 1, disables shutdown code */
#define HMS_PRIVATE 1		/* if 1, enable private stuff for me */

#define POWERDOWNWAIT 25 	/* about this many seconds a powerdown takes */ 

#if TEST
#define DARKUNTIL 24
#else
#define DARKUNTIL 7
#endif


/* end of preference settings */

#define _POSIX_SOURCE 1 /* POSIX compliant source */

#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))

/* definitions for sensor handling */

typedef struct {
    const char *base;
    const char *name;
} Sensors;

typedef enum {
    CPUTEMPINP = 0,
    CPUTEMPMAX,
    SYSTEMPINP,
    SYSTEMPMAX,
    SYSFANINP,
    CPUFANINP,
} Sensor;

static Sensors sensors[] = {
    {_SYSBASENAME_VIA, _CPUTEMPINP},
    {_SYSBASENAME_VIA, _CPUTEMPMAX},
    {_SYSBASENAME_VIA, _SYSTEMPINP},
    {_SYSBASENAME_VIA, _SYSTEMPMAX},
    {_SYSBASENAME_VIA, _SYSFANINP},
    {_SYSBASENAME_VIA, _CPUFANINP}
};

/* special escape sequences for the SG30 LCD */
const char clearDsp[] = "\033X";
const char darkDsp[] = "\033F";
const char lightDsp[] = "\033b";
const char pwrDownDsp[] = "\033%dq"; /* in %d seconds */


/* possible buttons */
typedef enum {
    POWERBUTTON = 'A',
    SELECTBUTTON = 'S',
    TIMEOUT = 0
} Button;

/* display on or off state */
typedef enum {
    LIGHT = 0,
    WANTSDARK = 1,
    DARK = 2
} Darkness;

/* the displayed information items */ 
typedef enum {
  DATETIME,      /* show current date and time */
  TEMPS,         /* show system temperatures */
  FANS,          /* show fan speeds */
  FANCTL,        /* allow fan control */
  DISKTEMPS,     /* show hard disk temperatures if supported by drive(s) */
  EXTADDR,       /* show ip address of connection (as seen on internet) */
  LANADDR,       /* show ip addresses for ETH0 and ETH1 */
  UPTIME,        /* show system uptime */
  FIREWALLCTL,   /* show status of firewall and allow toggling */
  WLANCTL        /* show status of wireless and allow toggling */   
} Display;

/* Fan control */
typedef enum {
    CPUFAN = 0,
    SYSFAN,
    LASTFAN
} FanType;

typedef enum {
    FANON = 0,
    FANOFF
} FanMode;

typedef enum {
    UNAVAILABLE = 0,
    ALWAYSON,
    CONTROLLED
} FanControl;

typedef struct {
    char *name;
    FanMode mode;
    Sensor readtemp;
    int temp;
    int tempOn;
    int tempOff;
} Fan;


static Fan fan[LASTFAN];

static int lcdfd = 0, terminating = 0;
static FanControl fanControl;


static char *readSys(Sensor sensor)
{
    char pathname[255];
    static char buf[255];
    int fd, res;

    strcpy(pathname, sensors[sensor].base);
    strcat(pathname, sensors[sensor].name);

    /* read information from system (/sys) */
    fd = open(pathname, O_RDONLY | O_NOCTTY); 
    if (fd > 0) {
	res = read(fd, buf, sizeof(buf));
	close(fd);
    } else {
	/* return empty string on error */
	res = 0;
    }
    if ((res > 0) && (buf[res-1] == '\n')) {
	res--;
    }
    buf[res] = 0;
    return buf;
}

static int readSysTemp(Sensor sensor)
{
    /* temperatures are stored in 1/1000 degrees */
    return (atoi(readSys(sensor))+500) / 1000 ;
}


static void initFanControl(void)
{
    fanControl = CONTROLLED;

#ifdef __FreeBSD__
    open("/dev/io", O_RDONLY, 0);
#else
    setuid(0);
    if (iopl(3)) {
	perror("iopl()");
        fanControl = UNAVAILABLE;
    }
#endif
    fan[CPUFAN].name = "CPU";
    fan[CPUFAN].mode = FANON;
    fan[CPUFAN].readtemp = CPUTEMPINP;
    fan[CPUFAN].temp = readSysTemp(fan[CPUFAN].readtemp);
    fan[CPUFAN].tempOn = readSysTemp(CPUTEMPMAX);
    fan[CPUFAN].tempOff = fan[CPUFAN].tempOn - CPUTEMPHYST;

    fan[SYSFAN].name = "SYS";
    fan[SYSFAN].mode = FANON;
    fan[SYSFAN].readtemp = SYSTEMPINP;
    fan[SYSFAN].temp = readSysTemp(fan[SYSFAN].readtemp);
    fan[SYSFAN].tempOn = readSysTemp(SYSTEMPMAX);
    fan[SYSFAN].tempOff = fan[SYSFAN].tempOn - SYSTEMPHYST;
#if DEB
    if (fanControl == UNAVAILABLE) {
	fprintf(stderr, "Fan control unavailable\n");
    } else {
	FanType f;
	for (f = CPUFAN; f < LASTFAN; f++) {
	    fprintf(stderr, "%s temp %d°, fan on at %d°, fan off at %d°\n",
		    fan[f].name, fan[f].temp, fan[f].tempOn, fan[f].tempOff);
	}
    }
#endif
}    

static void toggleFan(FanType fant, FanMode mode)
{
    unsigned long FANCTLPORT = 0x404C;

    mode ? OUTL(inl(FANCTLPORT) & ~(1<<(12+fant)), FANCTLPORT) : 
	OUTL(inl(FANCTLPORT) | (1<<(12+fant)), FANCTLPORT); 
    fan[fant].mode = mode;
#if DEB
    fprintf(stderr, "Turning %s fan %s\n", 
	    fan[fant].name, mode == FANON? "on": "off");
#endif
}

static void controlFans(void)
{
    FanType f;

    if (fanControl == CONTROLLED) {
	for (f = CPUFAN; f < LASTFAN; f++) {
	    fan[f].temp = readSysTemp(fan[f].readtemp);
	    if (fan[f].mode == FANON) {
		/* the fan is on */
		if (fan[f].temp <= fan[f].tempOff) {
		    /* it's cool again */
		    toggleFan(f, FANOFF);
		}
	    } else {
		/* it's getting too warm */
		if (fan[f].temp > fan[f].tempOn) {
		    /* temperature went up */
		    toggleFan(f, FANON);
		}
	    }
	}
    }
}

static void finishFanControl(FanMode mode)
{
    FanType f;

    if (fanControl != UNAVAILABLE) {
	/* always turn on both fans */
	for (f = CPUFAN; f < LASTFAN; f++) {
	    toggleFan(f, mode);
	}
    }
}

static void signalHandler(int sig)
{
    terminating = 1;
    if (sig == SIGTERM || sig == SIGKILL) {
	/* terminating signal received */
	if (lcdfd != 0) {
	    /* if user initiated shutdown */
	    /* powerdown in some seconds */
	    char tmp[sizeof(pwrDownDsp)+12];
	    sprintf(tmp, pwrDownDsp, POWERDOWNWAIT);
    	    write(lcdfd, tmp, strlen(tmp));
	} 
    }
}

static void writeLcd(int fd, char *line1, char *line2) 
{
    int i;
    char display [60];

#if DEB
    fprintf(stderr, "%s %s\n", line1, line2);
#endif
    /* clear display */
    strcpy(display, clearDsp);

    /* write both lines centered */
    if (line1 != NULL) {
	int l = (LINELEN - min(strlen(line1), LINELEN)) / 2;
	for (i=0; i < l; i++) {
	    strcat(display, " ");
	}
	strncat(display, line1, LINELEN);
    }
    strcat(display, "\n");
    if (line2 != NULL) {
	int l = (LINELEN - min(strlen(line2), LINELEN)) / 2;
	for (i=0;  i < l; i++) {
	    strcat(display, " ");
	}
	strncat(display, line2, LINELEN);
    }
    write(fd, display, strlen(display)); 
}

static int waitFor(int fd, int waitSecs)
{
    int rc;
    struct timeval timeout;
    fd_set readfds;

    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);
    if (waitSecs >= 0) {
        /* wait for specified number of seconds */
	timeout.tv_sec = waitSecs;
	timeout.tv_usec = 0;
	rc = select(fd+1, &readfds, NULL, NULL, &timeout);
    } else {
	/* wait forever */
	rc = select(fd+1, &readfds, NULL, NULL, NULL);
    }
    if (rc < 0) {
    	rc = 0;
    }
    return rc;
}

static Button readButton(int fd, int wait) 
{
    int res;
    char buf[20];

    while (1) {		
	/* wait for input */
	if (waitFor(fd, wait)) {
	    res = read(fd, buf, sizeof(buf));
	    buf[res] = 0;  
	    return buf[0];
	} else {
	    return TIMEOUT;
	}
    }
}

typedef enum {
    MODE_NONE = 0,
    MODE_POWEROFF,
    MODE_FANOFF,
    MODE_WLANOFF,
    MODE_FIREWALL
} PowerButtonMode;

/* if this file exists then the firewall is on */
#define FIREWALLSTATUSFILE "/var/tmp/firewall_is_on"
#define firewall_is_on() (!access(FIREWALLSTATUSFILE, F_OK))

static void server(int fd)
{
    int n, wait, fani, firewall, wlan;
    PowerButtonMode powerButtonMode;
    time_t t;
    struct tm *tm;
    char tmp1[50], tmp2[50];
    struct sysinfo info;
    int updays, uphours, upminutes;
    FILE *f;
    Darkness darkness;
    Display display;

    signal(SIGTERM, signalHandler);
    signal(SIGHUP, signalHandler);
    signal(SIGINT, signalHandler);
    signal(SIGQUIT, signalHandler);
    signal(SIGKILL, signalHandler);
    signal(SIGSEGV, signalHandler);
    signal(SIGBUS, signalHandler);

    display = DATETIME;
    powerButtonMode = MODE_NONE;
    darkness = LIGHT;
    wait = 0;
    wlan = 1;

    while (!terminating) {
	Button button = readButton(fd, wait);

	/* fan control */
	controlFans();

	/* get the current time */
	t = time(NULL);
	tm = localtime(&t);

	/* control display lighting */
	if (darkness == WANTSDARK) {
	    /* switch off display now */
	    if (button == TIMEOUT) {
		write(fd, darkDsp, strlen(darkDsp)); 
		darkness = DARK;
		display = DATETIME;
	    }
	} else {
	    /* switch on display because somebody pressed a button */
	    /* or we had a timeout */
	    write(fd, lightDsp, strlen(darkDsp));
	    if (darkness == DARK) 
		darkness = WANTSDARK;
	}
	if (tm->tm_hour < DARKUNTIL) {
	    /* dark between midnight and DARKUNTIL */
	    if (darkness == LIGHT) {
		/* we need to switch from light to dark */
		darkness = WANTSDARK;
	    }
	} else {
	    darkness = LIGHT;
	}
#if DEB
	fprintf(stderr, "button=%d, darkness=%d\n", button, darkness);
#endif 
	switch (button) {
	case POWERBUTTON:
	    /* power button was pressed */
	    switch (powerButtonMode) {
	    case MODE_FANOFF:
		/* toggle fan control */
		if (fanControl == ALWAYSON) {
		    fanControl = CONTROLLED;
		} else if (fanControl == CONTROLLED) {
		    finishFanControl(FANON);
		    fanControl = ALWAYSON; 
		}
		powerButtonMode = MODE_NONE;
		wait = 0;
		fani = 10;
		break;
	    case MODE_FIREWALL:
	        if (firewall_is_on()) {
		/* turn off firewall by running script */
		  system("/usr/local/sbin/firewall-off.sh");
		} else {
		/* turn on firewall by running script */
		  system("/usr/local/sbin/firewall.sh");
		}
		powerButtonMode = MODE_NONE;
		wait = 0;
		fani = 10;
		break;
#if HMS_PRIVATE
	    case MODE_WLANOFF:
		/* toggle wlan by ejecting/inserting the PCMCIA card */
		if (wlan == 1) {
		    if (system("cardctl eject 2") == 0)
			wlan = 0;
		} else {
		    if (system("cardctl insert 2") == 0)
			wlan = 1; 
		}
		powerButtonMode = MODE_NONE;
		wait = 0;
		fani = 10;
		break;
#endif
	    case MODE_POWEROFF:
		/* power off the system */
		writeLcd(fd, "Shutting", "down system");
#if !TEST
		/* we do a shutdown and wait until we are killed */
		/* the signal handler then will use the LCD stuff */
		/* to powerdown the system in such a way that the */
		/* we can switch it on with the power button again */
	        lcdfd = fd;
		system("shutdown -h now");
		/* wait for SIGTERM */
		sleep(120);
#endif
		break;
	    default:
		writeLcd(fd, "Power: Off", "Display: Restart");
		/* a second button must be pressed within 3 seconds */
		powerButtonMode = MODE_POWEROFF;
		wait = 3;
	    }
	    break;

	case SELECTBUTTON:
	    /* display button was pressed */
	    if (powerButtonMode == MODE_POWEROFF) {
		/* reboot the system */
		writeLcd(fd, "Restarting", "system");
#if !TEST
		system("shutdown -r now");
		/* wait for SIGTERM */
		sleep(120);
#endif
		break;
	    }
	    /* switch to next display */
	    display++;
	    fani = 0;
	    /* fall through */

	case TIMEOUT:
	    /* timeout */
	    /* cancel power down cycle */
	    powerButtonMode = MODE_NONE;
	    switch (display) {
	    case TEMPS:
	      {
		/* write temperatures */
	        int cputemp = readSysTemp(CPUTEMPINP);
		int systemp = readSysTemp(SYSTEMPINP); 
		if (tempunit == FAHRENHEIT) {
		  cputemp = ((float)cputemp * 1.8) + 32;
		  systemp = ((float)systemp * 1.8) + 32;
		}
		sprintf(tmp2, "CPU %d°, Sys %d°", cputemp, systemp);
		writeLcd(fd, "Temperature", tmp2);
		wait = fanControl == ALWAYSON? 60 : 10;
		break;
	      }

	    case FANS:
		/* write fan revolutions */
		sprintf(tmp1, "CPU fan %4d",
			atoi(readSys(CPUFANINP))); 
		sprintf(tmp2, "Sys fan %4d",
			atoi(readSys(SYSFANINP))); 
		writeLcd(fd, tmp1, tmp2);
		wait = fanControl == ALWAYSON? 120 : 10;
		break;

	    case FANCTL:
		/* offer fan control setting */
		if (fanControl == UNAVAILABLE) {
		    display++;
		    /* fall through */
		} else {
		    powerButtonMode = MODE_FANOFF;
		    wait = 2;
		    if ((fani++ % 2) == 0) {
			writeLcd(fd, "Fans are", 
				 fanControl == CONTROLLED? 
				 "controlled" : "always on");
		        if (fani > 10) {
			    /* we have seen it five times - that's enough */
			    fani = 0;
			    display = FANS;
			    wait = 5;
			}
		    } else {
			writeLcd(fd, "Power button", "to toggle");
		    }
		    break;
		}

	    case DISKTEMPS:
		/* write harddisk temperature - changed hda & hdb TO sda/sdb for Ubuntu 8.04LTS */
		writeLcd(fd, "Disk temperature", "waiting ...");
		f = popen("smartctl /dev/sda -A|grep 194|awk '{print $10}'", "r");
		n = fread(tmp1, 1, sizeof(tmp1), f);
		pclose(f);
		if (n > 0) {
		  int temp = atoi(tmp1);
		  if (tempunit == FAHRENHEIT) {
		      temp = ((float)temp * 1.8) + 32;
		  }
		  sprintf(tmp2, "sda %d°", temp);
		  f = popen("smartctl /dev/sdb -A|grep 194|awk '{print $10}'", "r");
		    n = fread(tmp1, 1, sizeof(tmp1), f);
		    pclose(f);
		    if (n > 0) {
			/* append data for second disk */
		        temp = atoi(tmp1);
		        if (tempunit == FAHRENHEIT) {
			  temp = ((float)temp * 1.8) + 32;
			}
			sprintf(tmp2+strlen(tmp2), ", sdb %d°", temp);
		    }
		    writeLcd(fd, "Disk temperature", tmp2);
		} else {
		    writeLcd(fd, "Disk temperature", "unknown");
		}
		wait = 300;	/* every 5 minutes only */    
		break;

	    case EXTADDR:
		/* write external address */
		writeLcd(fd, "External IP Addr", "waiting ...");
		f = popen("curl -s -m 10 http://whatismyip.org/", "r");
		n = fread(tmp1, 1, sizeof(tmp1), f);
		pclose(f);
		if (n > 0) {
		    tmp1[n] = 0;
		    writeLcd(fd, "External IP Addr", tmp1);
		    wait = 60*5; /* every 5 minutes */
		} else {
		    writeLcd(fd, "External IP Addr", "None");
		    wait = 10; /* every 10 seconds */
		}
		break;

	    case LANADDR:
		/* show LAN (internal) address */
		wait = 60*5; /* every 5 minutes */
		f = popen(ETH0, "r");
		n = fread(tmp1, 1, sizeof(tmp1), f);
		pclose(f);
		if (n > 0) {
		    tmp1[n] = 0;
		} else {
		    strcpy(tmp1, "eth0 ???");
		    wait = 10; /* bump test up to every 10 seconds */
		}
		f = popen(ETH1, "r");
		n = fread(tmp2, 1, sizeof(tmp2), f);
		pclose(f);
		if (n > 0) {
		    tmp2[n] = 0;
		} else {
		    strcpy(tmp2, "eth1 ???");
		    wait = 10; /* bump test up to every 10 seconds */
		}
		writeLcd(fd, tmp1, tmp2);
		break;

	    case UPTIME:
		/* write system uptime */
		sysinfo(&info);
		updays = (int) info.uptime / (60*60*24);
		upminutes = (int) info.uptime / 60;
		uphours = (upminutes / 60) % 24;
		upminutes %= 60;
		sprintf(tmp1, "%d day%s, %02d:%02d", updays, 
			(updays != 1) ? "s" : "", 
			uphours, upminutes);
		writeLcd(fd, "Uptime", tmp1);
		wait = 60*5; /* every 5 minutes */
		break;

	    case FIREWALLCTL:
		/* offer Firewall control setting */
		firewall = firewall_is_on();
		powerButtonMode = MODE_FIREWALL;
		wait = 2;
		if ((fani++ % 2) == 0) {
		    writeLcd(fd, "Firewall is", 
			     firewall? "enabled": "disabled");
		    if (fani > 10) {
			/* we have seen it five times - that's enough */
			fani = 0;
			display = DATETIME;
			wait = 5;
		    }
		} else {
		    writeLcd(fd, "Power button to", 
			     firewall? "turn OFF": "turn ON");
		}
		break;

#if HMS_PRIVATE		
	    case WLANCTL:
		/* offer WLAN control setting */
		powerButtonMode = MODE_WLANOFF;
		wait = 2;
		if ((fani++ % 2) == 0) {
		    writeLcd(fd, "Wireless LAN is", 
			     wlan? "enabled": "disabled");
		    if (fani > 10) {
			/* we have seen it five times - that's enough */
			fani = 0;
			display = DATETIME;
			wait = 5;
		    }
		} else {
		    writeLcd(fd, "Power button to", 
			     wlan? "disable WLAN": "enable WLAN");
		}
		break;
#endif

	    case DATETIME:
	    default:
		/* write date and time */
		strftime(tmp1, sizeof(tmp1), "%a %d-%b-%Y ", tm);
		strftime(tmp2, sizeof(tmp2), TIMEFORMAT, tm);
		writeLcd(fd, tmp1, tmp2);
		if (darkness == DARK) {
		    /* wait until 07:00 */
		    wait = 3600 * (DARKUNTIL - tm->tm_hour)
			- tm->tm_min * 60 - tm->tm_sec; 
		} else {
		    /* wait a minute */
		    wait = 60;	
		}
		/* restart display cycle */
		display = DATETIME;
		break;
		    
	    }
	    break;	
	} 	
	if (darkness == WANTSDARK) {
	    /* switch off display in two seconds  */
	    wait = 10;
	}
#if DEB
	fprintf(stderr, "darkness=%d, wait=%d\n", darkness, wait);
#endif 
    }
    writeLcd(fd, "LCD process", "terminated");
} /* end server */
 
	
static void usage(int argc, char **argv)
{
    fprintf(stderr,
	    "Usage: %s [-f] <command> [...] \n"
	    "<command> can be server, write, read, fans\n"
	    "   server    run in server mode taking over LCD, buttons and fans\n"
	    "   write     writes next two parameters to display\n"
	    "   read      reads LCD buttons with optional timeout\n"
	    "   fans      turn fans on or off\n"
	    "Use the -f option to display temperatures in Fahrenheit. \n",
	    argv[0]);
} /* end usage */


int main(int argc, char **argv)	
{
    int fd;
    int n = 1;
    struct termios oldtio, newtio;

#if DEB
    fprintf(stderr, "Starting version %s...\n", VERSION);
#endif
    fd = open(LCDDEVICE, O_RDWR | O_NOCTTY ); 
    if (fd < 0) {
	perror(LCDDEVICE); 
	exit(EXIT_FAILURE); 
    }
#if DEB
    fprintf(stderr, "opened %s\n", LCDDEVICE);
#endif
 
    /* save current port settings */
    tcgetattr(fd, &oldtio);

    bzero(&newtio, sizeof(newtio));
    newtio.c_cflag = BAUDRATE | CS8 | CSTOPB | PARENB | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;

    /* set input mode (non-canonical, no echo,...) */
    newtio.c_lflag = 0;
    newtio.c_cc[VTIME]    = 1;   /* inter-character timer unused */
    newtio.c_cc[VMIN]     = 0;   /* blocking read until 5 chars received */

    tcflush(fd, TCIFLUSH);
    tcsetattr(fd, TCSANOW, &newtio);

    if (argc < 2) {
	usage(argc, argv);
	exit(EXIT_SUCCESS);
    }

    if (strcmp(argv[1], "-f") == 0) {
      tempunit = FAHRENHEIT;
      n = 2;
    }

    if (strcmp(argv[n], "server") == 0) {
	initFanControl();
	server(fd);
	finishFanControl(FANON);
    } else if (strcmp(argv[n], "write") == 0) {
	writeLcd(fd, argv[n+1], argv[3]);
    } else if (strcmp(argv[n], "read") == 0) {
	int wait = -1;
	char *answer = "Timeout";
	Button b;

	if (argc >= 3) {
	    wait = atoi(argv[n+1]);
	}
	b = readButton(fd, wait);
	if (b == POWERBUTTON) {
	    answer = "power";
	} else if (b == SELECTBUTTON) {
	    answer = "display";
	}
	printf("%s\n", answer);
    } else if (strcmp(argv[n], "fans") == 0) {
	initFanControl();
	if (strcmp(argv[n+1], "on") == 0) {
	    finishFanControl(FANON);
	} else if (strcmp(argv[n+1], "off") == 0) {
	    finishFanControl(FANOFF);
	} else {
	    usage(argc, argv);
	}
    } else {
	usage(argc, argv);
    }

    /* restore 	modem settings */
    tcsetattr(fd, TCSANOW, &oldtio); 
    close(fd);

    exit(EXIT_SUCCESS);
} /* end main */

/* EOF */
