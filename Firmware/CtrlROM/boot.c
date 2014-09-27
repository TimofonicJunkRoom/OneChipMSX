/*	Firmware for loading files from SD card.
	Part of the ZPUTest project by Alastair M. Robinson.
	SPI and FAT code borrowed from the Minimig project.
*/


#include "stdarg.h"

#include "uart.h"
#include "spi.h"
#include "minfat.h"
#include "small_printf.h"
#include "host.h"
#include "ps2.h"
#include "keyboard.h"
#include "hexdump.h"
#include "osd.h"
#include "menu.h"

fileTYPE file;
static struct menu_entry topmenu[];


#define BIT_SDCARD 2
#define BIT_JAPANESEKEYBOARD 6
#define BIT_TURBO 7
#define BIT_MOUSEEMULATION 10

#define DEFAULT_DIPSWITCH_SETTINGS 0x638


void OSD_Puts(char *str)
{
	int c;
	while((c=*str++))
		OSD_Putchar(c);
}


void WaitEnter()
{
	while(1)
	{
		HandlePS2RawCodes();
		if(TestKey(KEY_ENTER)&2)
			return;
	}
}


static int Boot()
{
	int result=0;
	int opened;

	OSD_Puts("Initializing SD card\n");
	if(spi_init())
	{
		int dipsw=GetDIPSwitch();

		if(!FindDrive())
			return(0);

		if(sd_ishc())
		{
			OSD_Puts("SDHC card detected but not\nsupported; disabling SD card\n\x10 OK\n");
			WaitEnter();
			dipsw|=4; // Disable SD card.
			HW_HOST(HW_HOST_CTRL)=HW_HOST_CTRLF_RESET;	// Put OCMSX into Reset again
			HW_HOST(HW_HOST_SW)=dipsw;
			SetDIPSwitch(dipsw);
		}
		else if(IsFat32())
		{
			OSD_Puts("Fat32 filesystem detected but\nnot supported; disabling SD card\n\x10 OK\n");
			WaitEnter();
			dipsw|=4; // Disable SD card.
			HW_HOST(HW_HOST_CTRL)=HW_HOST_CTRLF_RESET;	// Put OCMSX into Reset again
			HW_HOST(HW_HOST_SW)=dipsw;
			SetDIPSwitch(dipsw);
		}
		HW_HOST(HW_HOST_CTRL)=HW_HOST_CTRLF_SDCARD;	// Release reset but steal SD card

		OSD_Puts("Trying MSX3BIOS.SYS...\n");
		if(!(opened=FileOpen(&file,"MSX3BIOSSYS")))	// Try and load MSX3 BIOS first
		{
			OSD_Puts("Trying BIOS_M2P.ROM...\n");
			opened=FileOpen(&file,"BIOS_M2PROM"); // If failure, load MSX2 BIOS.
		}
		if(opened)
		{
			OSD_Puts("Opened BIOS, loading...\n");
			int filesize=file.size;
			unsigned int c=0;
			int bits;

			bits=0;
			c=filesize;
			while(c)
			{
				++bits;
				c>>=1;
			}
			bits-=9;

			while(filesize>0)
			{
				OSD_ProgressBar(c,bits);
				if(FileRead(&file,sector_buffer))
				{
					int i;
					int *p=(int *)&sector_buffer;
					for(i=0;i<(filesize<512 ? filesize : 512) ;i+=4)
					{
						int t=*p++;
						int t1=t&255;
						int t2=(t>>8)&255;
						int t3=(t>>16)&255;
						int t4=(t>>24)&255;
						HW_HOST(HW_HOST_BOOTDATA)=t4;
						HW_HOST(HW_HOST_BOOTDATA)=t3;
						HW_HOST(HW_HOST_BOOTDATA)=t2;
						HW_HOST(HW_HOST_BOOTDATA)=t1;
					}
				}
				else
				{
					OSD_Puts("Read block failed\n");
					return(0);
				}
				FileNextSector(&file);
				filesize-=512;
				++c;
			}
			HW_HOST(HW_HOST_CTRL)=HW_HOST_CTRLF_BOOTDONE;	// Release SD card and early-terminate any remaining requests for boot data
			return(1);
		}
	}
	return(0);
}


static void reset()
{
	Menu_Hide();
	OSD_Clear();
	OSD_Show(1);
	HW_HOST(HW_HOST_CTRL)=HW_HOST_CTRLF_RESET;	// Put OCMS into Reset
	PS2Wait();
	PS2Wait();
	HW_HOST(HW_HOST_CTRL)=HW_HOST_CTRLF_SDCARD;	// Release reset but steal SD card
	Boot();
	Menu_Set(topmenu);
	OSD_Show(0);
}


static struct menu_entry topmenu[];

static char *video_labels[]=
{
	"VGA - 31KHz, 60Hz",
	"VGA - 31KHz, 50Hz",
	"TV - 480i, 60Hz"
};

static char *slot1_labels[]=
{
	"Sl1: None",
	"Sl1: ESE-SCC 1MB/SCC-I",
	"Sl1: MegaRAM"
};

static char *slot2_labels[]=
{
	"Sl2: None",
	"Sl2: ESE-SCC 1MB/SCC-I",
	"Sl2: ESE-RAM 1MB/ASCII8",
	"Sl2: ESE-RAM 1MB/ASCII16"
};

static char *ram_labels[]=
{
	"2048LB RAM",
	"4096KB RAM"
};


static struct menu_entry dipswitches[]=
{
	{MENU_ENTRY_CYCLE,(char *)video_labels,3},
	{MENU_ENTRY_TOGGLE,"SD Card",BIT_SDCARD},
	{MENU_ENTRY_CYCLE,(char *)slot1_labels,3},
	{MENU_ENTRY_CYCLE,(char *)slot2_labels,4},
	{MENU_ENTRY_TOGGLE,"Japanese keyboard layout",BIT_JAPANESEKEYBOARD},
	{MENU_ENTRY_CYCLE,(char *)ram_labels,2},
	{MENU_ENTRY_SUBMENU,"Back",MENU_ACTION(topmenu)},
	
	{MENU_ENTRY_NULL,0,0},
};


static struct menu_entry topmenu[]=
{
	{MENU_ENTRY_CALLBACK,"Reset",MENU_ACTION(&reset)},
	{MENU_ENTRY_SUBMENU,"Options \x10",MENU_ACTION(dipswitches)},
	{MENU_ENTRY_TOGGLE,"Turbo (10.74MHz)",BIT_TURBO},
	{MENU_ENTRY_TOGGLE,"Mouse emulation",BIT_MOUSEEMULATION},
	{MENU_ENTRY_CALLBACK,"Exit",MENU_ACTION(&Menu_Hide)},
	{MENU_ENTRY_NULL,0,0},
};


int SetDIPSwitch(int d)
{
	struct menu_entry *m;
	MENU_TOGGLE_VALUES=d^0x84; // Invert sense of SD card and Turbo switches
	m=&dipswitches[0]; MENU_CYCLE_VALUE(m)=d&3; // Video
	m=&dipswitches[6]; MENU_CYCLE_VALUE(m)=d&0x200 ? 1 : 0; // RAM
	m=&dipswitches[2]; MENU_CYCLE_VALUE(m)=(d&0x100 ? 2 : 0) | (d&0x8 ? 1 : 0); // Slot 1
	m=&dipswitches[3]; MENU_CYCLE_VALUE(m)=(d>>4)&3; // Slot 2
}


int GetDIPSwitch()
{
	struct menu_entry *m;
	int result=MENU_TOGGLE_VALUES&0xc4;  // Bits 2, 6 and 7 are direct mapped
	int t;
	m=&dipswitches[6]; 	if(MENU_CYCLE_VALUE(m))
		result|=0x200;	// RAM
	m=&dipswitches[0];  t=MENU_CYCLE_VALUE(m);	// Video
	result|=t;
	m=&dipswitches[2];  t=MENU_CYCLE_VALUE(m); // Slot 1
	result|=t&2 ? 0x100 : 0;
	result|=t&1 ? 0x8 : 0;
	m=&dipswitches[3];  t=MENU_CYCLE_VALUE(m); // Slot 1
	result|=t<<4;
	return(result^0x84); // Invert SD card and Turbo switch
}

#define PS2_TIMEOUT 20

int main(int argc,char **argv)
{
	int i;
	SetDIPSwitch(DEFAULT_DIPSWITCH_SETTINGS);
	HW_HOST(HW_HOST_CTRL)=HW_HOST_CTRLF_RESET;	// Put OCMS into Reset
	HW_HOST(HW_HOST_SW)=DEFAULT_DIPSWITCH_SETTINGS;
	HW_HOST(HW_HOST_CTRL)=HW_HOST_CTRLF_SDCARD;	// Release reset but steal SD card

	PS2Init();
	EnableInterrupts();
	PS2Wait();
	PS2Wait();
	OSD_Clear();
	OSD_Show(1);	// Figure out sync polarity
	PS2Wait();
	PS2Wait();
	OSD_Show(1);	// OSD should now show correctly.

	// Initialize the mouse
	while(PS2MouseRead()>-1)
		; // Drain the buffer;
	PS2MouseWrite(0xf4);

	if(Boot())
	{
		int mousex=0,mousey=0;
		int midx,mctr;
		midx=0;
		mctr=PS2_TIMEOUT;
		OSD_Show(0);
		Menu_Set(topmenu);
		while(1)
		{
			int mdata[4];
			int visible;
			static int prevds;

			PS2Wait(); // Returns after 1 vblank or any PS/2 activity, whichever is sooner

			if((mdata[midx]=PS2MouseRead())==-1)
			{
				if(!(--mctr))	// Timeout
				{
					midx=0;
				}
			}
			else
			{
				++midx;
				mctr=PS2_TIMEOUT;
				if(midx==3)	// Complete packet received?
				{
					midx=0;
					HW_HOST(HW_HOST_MOUSEBUTTONS)=mdata[0]&3;
					if(mdata[0] & (1<<5))
						mousey-=1+(mdata[2]^255);
					else
						mousey+=mdata[2];

					if(mdata[0] & (1<<4))
						mousex-=1+(mdata[1]^255);
					else
						mousex+=mdata[1];
				}	
			}

			if((mousex)||(mousey))
			{
				if(HW_HOST(HW_HOST_MOUSE)&HW_HOST_MOUSEF_IDLE)
				{
					int dx,dy;
					dx=mousex; dy=mousey;
					// Clamp mouse values, since the MSX mouse can only shift an 8-bit signed value,
					// while the PS2 mouse provides 9 bit data.
					if(dx>127)
						dx=127;
					if(dy>127)
						dy=127;
					if(dx<-128)
						dx=-128;
					if(dy<-128)
						dy=-128;
					mousex-=dx;
					mousey-=dy;
					HW_HOST(HW_HOST_MOUSE)=((dx&255)<<8)|(dy&255);
				}
			}
			
			HandlePS2RawCodes();
			visible=Menu_Run();
			HW_HOST(HW_HOST_SW)=GetDIPSwitch();
			if(GetDIPSwitch()!=prevds)
			{
				int i;
				prevds=GetDIPSwitch();
				for(i=0;i<5;++i)
				{
					OSD_Show(visible);	// Refresh OSD position
					PS2Wait();
					PS2Wait();
				}
			}
			if(visible)
				HW_HOST(HW_HOST_CTRL)=HW_HOST_CTRLF_BOOTDONE|HW_HOST_CTRLF_KEYBOARD;	// capture keyboard
			else					
				HW_HOST(HW_HOST_CTRL)=HW_HOST_CTRLF_BOOTDONE;	// release keyboard
		}
	}
	else
	{
		OSD_Puts("Loading BIOS failed\n");
	}
	HW_HOST(HW_HOST_CTRL)=HW_HOST_CTRLF_BOOTDONE;	// Release SD card and early-terminate any remaining requests for boot data

	return(0);
}

