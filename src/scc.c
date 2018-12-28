/*
 * scc.cpp - SCC 85C30 emulation code
 *
 * Adaptions to Hatari:
 *
 * Copyright 2018 Thomas Huth
 *
 * Original code taken from Aranym:
 *
 * Copyright (c) 2001-2004 Petr Stehlik of ARAnyM dev team (see AUTHORS)
 *               2010 Jean Conter
 *
 * This code is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This code is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with ARAnyM; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "main.h"

#if HAVE_TERMIOS_H
# include <termios.h>
# include <unistd.h>
#endif
#if HAVE_SYS_IOCTL_H
# include <sys/ioctl.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include "configuration.h"
#include "ioMem.h"
#include "log.h"
#include "memorySnapShot.h"
#include "scc.h"

#if 0
#define bug printf
#define D(x) x
#else
#define bug(...)
#define D(x)
#endif

#define RCA 0
#define TBE 2
#define CTS 5

static int active_reg;
static int scc_regs[32];
static uint8_t RR3, RR3M;    // common to channel A & B

static int charcount;
static int handle = -1;
static uint16_t oldTBE;
static uint16_t oldStatus;
static bool bFileHandleIsATTY;

void SCC_Init(void)
{
	SCC_Reset();

	oldTBE = 0;
	oldStatus = 0;

	D(bug("SCC: interface initialized\n"));

	if (!ConfigureParams.RS232.bEnableSccB || !ConfigureParams.RS232.sSccBFileName[0])
	{
		handle = -1;
		return;
	}

	handle = open(ConfigureParams.RS232.sSccBFileName,
	              O_RDWR | O_NDELAY | O_NONBLOCK);      /* Raw mode */
	if (handle >= 0)
	{
#if HAVE_TERMIOS_H
		bFileHandleIsATTY = isatty(handle);
#else
		bFileHandleIsATTY = false;
#endif
	}
	else
	{
		Log_Printf(LOG_ERROR, "SCC_Init: Can not open device '%s'\n",
		           ConfigureParams.RS232.sSccBFileName);
	}
}

void SCC_UnInit(void)
{
	D(bug("SCC: interface destroyed\n"));
	if (handle >= 0)
	{
		fcntl(handle, F_SETFL, 0);  // back to (almost...) normal
		close(handle);
		handle = -1;
	}
}

void SCC_MemorySnapShot_Capture(bool bSave)
{
	MemorySnapShot_Store(&active_reg, sizeof(active_reg));
	MemorySnapShot_Store(scc_regs, sizeof(scc_regs));
	MemorySnapShot_Store(&RR3, sizeof(RR3));
	MemorySnapShot_Store(&RR3M, sizeof(RR3M));
	MemorySnapShot_Store(&charcount, sizeof(charcount));
	MemorySnapShot_Store(&oldTBE, sizeof(oldTBE));
	MemorySnapShot_Store(&oldStatus, sizeof(oldStatus));
}

static void SCC_channelAreset(void)
{
	scc_regs[15] = 0xF8;
	scc_regs[14] = 0xA0;
	scc_regs[11] = 0x08;
	scc_regs[9] = 0;
	RR3 &= ~0x38;
	RR3M &= ~0x38;
	scc_regs[0] = 1<<TBE; //RR0A
}

static void SCC_channelBreset(void)
{
	scc_regs[15+16] = 0xF8;
	scc_regs[14+16] = 0xA0;
	scc_regs[11+16] = 0x08;
	scc_regs[9] = 0; //single WR9
	RR3 &= ~7;
	RR3M &= ~7;
	scc_regs[16] = 1<<TBE; //RR0B
}

void SCC_Reset()
{
	active_reg = 0;
	memset(scc_regs, 0, sizeof(scc_regs));
	SCC_channelAreset();
	SCC_channelBreset();
	RR3 = 0;
	RR3M = 0;
	charcount = 0;
}

static void TriggerSCC(bool enable)
{
	if (enable)
	{
		Log_Printf(LOG_TODO, "TriggerSCC\n");
	}
}

static uint8_t SCC_serial_getData(void)
{
	uint8_t value = 0;
	int nb;

	D(bug("SCC: getData\n"));
	if (handle >= 0)
	{
		nb = read(handle, &value, 1);
		if (nb < 0)
		{
			D(bug("SCC: impossible to get data\n"));
		}
	}
	return value;
}

static void SCC_serial_setData(uint8_t value)
{
	int nb;

	D(bug("SCC: setData\n"));
	if (handle >= 0)
	{
		do
		{
			nb = write(handle, &value, 1);
		} while (nb < 0 && (errno == EAGAIN || errno == EINTR));
	}
}

static void SCC_serial_setBaud(int value)
{
#if HAVE_TERMIOS_H
	struct termios options;
	speed_t new_speed = B0;

	D(bug("SCC: setBaud %i\n", value));

	if (handle < 0)
		return;

	switch (value)
	{
	 case 230400:	new_speed = B230400;	break;
	 case 115200:	new_speed = B115200;	break;
	 case 57600:	new_speed = B57600;	break;
	 case 38400:	new_speed = B38400;	break;
	 case 19200:	new_speed = B19200;	break;
	 case 9600:	new_speed = B9600;	break;
	 case 4800:	new_speed = B4800;	break;
	 case 2400:	new_speed = B2400;	break;
	 case 1800:	new_speed = B1800;	break;
	 case 1200:	new_speed = B1200;	break;
	 case 600:	new_speed = B600;	break;
	 case 300:	new_speed = B300;	break;
	 case 200:	new_speed = B200;	break;
	 case 150:	new_speed = B150;	break;
	 case 134:	new_speed = B134;	break;
	 case 110:	new_speed = B110;	break;
	 case 75:	new_speed = B75;	break;
	 case 50:	new_speed = B50;	break;
	 default:	D(bug("SCC: unsupported baud rate %i\n", value)); break;
	}

	if (new_speed == B0)
		return;

	tcgetattr(handle, &options);

	cfsetispeed(&options, new_speed);
	cfsetospeed(&options, new_speed);

	options.c_cflag |= (CLOCAL | CREAD);
	options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG); // raw input
	options.c_iflag &= ~(ICRNL); // CR is not CR+LF

	tcsetattr(handle, TCSANOW, &options);
#endif
}

static uint16_t SCC_getTBE(void) // not suited to serial USB
{
	uint16_t value = 0;

#if defined(HAVE_SYS_IOCTL_H) && defined(TIOCSERGETLSR) && defined(TIOCSER_TEMT)
	int status = 0;
	if (ioctl(handle, TIOCSERGETLSR, &status) < 0)  // OK with ttyS0, not OK with ttyUSB0
	{
		// D(bug("SCC: Can't get LSR"));
		value |= (1<<TBE);   // only for serial USB
	}
	else if (status & TIOCSER_TEMT)
	{
		value = (1 << TBE);  // this is a real TBE for ttyS0
		if ((oldTBE & (1 << TBE)) == 0)
		{
			value |= 0x200;
		} // TBE rise=>TxIP (based on real TBE)
	}
#endif

	oldTBE = value;
	return value;
}

static uint16_t SCC_serial_getStatus(void)
{
	uint16_t value = 0;
	int status = 0;
	uint16_t diff;
	int nbchar = 0;

#if defined(HAVE_SYS_IOCTL_H) && defined(TIOCMGET)
	if (handle >= 0 && bFileHandleIsATTY)
	{
		if (ioctl(handle, FIONREAD, &nbchar) < 0)
		{
			D(bug("SCC: Can't get input fifo count\n"));
		}
		charcount = nbchar; // to optimize input (see UGLY in handleWrite)
		if (nbchar > 0)
			value = 0x0401;  // RxIC+RBF
		value |= SCC_getTBE();   // TxIC
		value |= (1 << TBE);  // fake TBE to optimize output (for ttyS0)
		if (ioctl(handle, TIOCMGET, &status) < 0)
		{
			D(bug("SCC: Can't get status\n"));
		}
		if (status & TIOCM_CTS)
			value |= (1 << CTS);
	}
#endif

	if (handle >= 0 && !bFileHandleIsATTY)
	{
		/* Output is a normal file, thus always set Clear-To-Send
		 * and Transmit-Buffer-Empty: */
		value |= (1 << CTS) | (1 << TBE);
	}

	diff = oldStatus ^ value;
	if (diff & (1 << CTS))
		value |= 0x100;  // ext status IC on CTS change

	D(bug("SCC: getStatus 0x%04x\n", value));

	oldStatus = value;
	return value;
}

static void SCC_serial_setRTS(bool value)
{
#if defined(HAVE_SYS_IOCTL_H) && defined(TIOCMGET)
	int status = 0;

	if (handle >= 0 && bFileHandleIsATTY)
	{
		if (ioctl(handle, TIOCMGET, &status) < 0)
		{
			D(bug("SCC: Can't get status for RTS\n"));
		}
		if (value)
			status |= TIOCM_RTS;
		else
			status &= ~TIOCM_RTS;
		ioctl(handle, TIOCMSET, &status);
	}
#endif
}

static void SCC_serial_setDTR(bool value)
{
#if defined(HAVE_SYS_IOCTL_H) && defined(TIOCMGET)
	int status = 0;

	if (handle >= 0 && bFileHandleIsATTY)
	{
		if (ioctl(handle, TIOCMGET, &status) < 0)
		{
			D(bug("SCC: Can't get status for DTR\n"));
		}
		if (value)
			status |= TIOCM_DTR;
		else
			status &= ~TIOCM_DTR;
		ioctl(handle, TIOCMSET, &status);
	}
#endif
}

static uint8_t SCC_ReadControl(int set2)
{
	uint8_t value = 0;
	uint16_t temp;

	switch (active_reg)
	{
	 case 0:
		if (set2)	// RR0B
		{
			temp = SCC_serial_getStatus();	// only for channel B
			scc_regs[16] = temp & 0xFF;	// define CTS(5), TBE(2) and RBF=RCA(0)
			RR3 = RR3M & (temp >> 8);	// define RxIP(2), TxIP(1) and ExtIP(0)
		}
		else
		{
			scc_regs[0] = 4;
			if (scc_regs[9] == 0x20)RR3 |= 0x8;
		}

		value = scc_regs[set2]; // not yet defined for channel A
		break;
	 case 2:	// not really useful (RR2 seems unaccessed...)
		value = scc_regs[2];
		if (set2 == 0)	// vector base only for RR2A
			break;
		if ((scc_regs[9] & 1) == 0)	// no status bit added
			break;
		// status bit added to vector
		if (scc_regs[9] & 0x10) // modify high bits
		{
			if (RR3 == 0)
			{
				value |= 0x60;
				break;
			}
			if (RR3 & 32)
			{
				value |= 0x30;        // A RxIP
				break;
			}
			if (RR3 & 16)
			{
				value |= 0x10;        // A TxIP
				break;
			}
			if (RR3 & 8)
			{
				value |= 0x50;        // A Ext IP
				break;
			}
			if (RR3 & 4)
			{
				value |= 0x20;        // B RBF
				break;
			}
			if (RR3 & 2)
				break;                // B TBE
			if (RR3 & 1)
				value |= 0x40;        // B Ext Status
		}
		else // modify low bits
		{
			if (RR3 == 0)
			{
				value |= 6;           // no one
				break;
			}
			if (RR3 & 32)
			{
				value |= 0xC;         // A RxIP
				break;
			}
			if (RR3 & 16)
			{
				value |= 0x8;         // A TxIP
				break;
			}
			if (RR3 & 8)
			{
				value |= 0xA;         // A Ext IP
				break;
			}
			if (RR3 & 4)
			{
				value |= 4;           // B RBF
				break;
			}
			if (RR3 & 2)
				break;                // B TBE
			if (RR3 & 1)
				value |= 2;           // B Ext Status (CTS)
		}
		break;
	 case 3:
		value = (set2) ? 0 : RR3;     // access on A channel only
		break;
	 case 4: // RR0
		value = scc_regs[set2];
		break;
	 case 8: // DATA reg
		if (set2)  // only channel B processed
		{
			scc_regs[8 + set2] = SCC_serial_getData();
		}
		value = scc_regs[8 + set2];
		break;
	 case 9: //WR13
		value = scc_regs[13 + set2];
		break;
	 case 11: // WR15
	 case 15: // EXT/STATUS IT Ctrl
		value = scc_regs[15 + set2] &= 0xFA; // mask out D2 and D0
		break;
	 case 12: // BRG LSB
	 case 13: // BRG MSB
		value = scc_regs[active_reg + set2];
		break;

	 default: // RR5,RR6,RR7,RR10,RR14 not processed
		D(bug("scc : unprocessed read address=$%x *********\n", active_reg));
		value = 0;
		break;
	}

	return value;
}

static uint8_t SCC_handleRead(uint32_t addr)
{
	uint8_t value = 0;
	int set2;

	addr &= 0x6;
	set2 = (addr >= 4) ? 16 : 0; //16=channel B
	switch (addr)
	{
	 case 0: // channel A
	 case 4: // channel B
		value = SCC_ReadControl(set2);
		break;
	 case 2: // channel A
		value = scc_regs[8]; // TBD (LAN)
		break;
	 case 6: // channel B
		scc_regs[8 + 16] = SCC_serial_getData();
		value = scc_regs[8 + 16];
		break;
	 default:
		D(bug("scc : illegal read address=$%x\n", addr));
		break;
	}

	active_reg = 0; // next access for RR0 or WR0

	return value;
}

static void SCC_WriteControl(int set2, uint8_t value)
{
	uint32_t BaudRate;
	int i;

	if (active_reg == 0)
	{

		if (value <= 15)
		{
			active_reg = value & 0x0f;
		}
		else
		{
			if ((value & 0x38) == 0x38) // Reset Highest IUS (last operation in IT service routine)
			{
				for (i = 0x20; i; i >>= 1)
				{
					if (RR3 & i)
						break;
				}
#define UGLY
#ifdef UGLY
				// tricky & ugly speed improvement for input
				if (i == 4) // RxIP
				{
					charcount--;
					if (charcount <= 0)
						RR3 &= ~4; // optimize input; don't reset RxIP when chars are buffered
				}
				else
				{
					RR3 &= ~i;
				}
#else
				RR3 &= ~i;
#endif
			}
			else if ((value & 0x38) == 0x28) // Reset Tx int pending
			{
				if (set2)
					RR3 &= ~2;       // channel B
				else
					RR3 &= ~0x10;    // channel A
			}
			else if ((value & 0x38) == 0x10) // Reset Ext/Status ints
			{
				if (set2)
					RR3 &= ~1;       // channel B
				else
					RR3 &= ~8;       // channel A
			}
			// Clear SCC flag if no pending IT or no properly
			// configured WR9. Must be done here to avoid
			// scc_do_Interrupt call without pending IT
			TriggerSCC((RR3 & RR3M) && ((0xB & scc_regs[9]) == 9));
		}
		return;
	}

	// active_reg > 0:
	scc_regs[active_reg + set2] = value;
	if (active_reg == 2)
	{
		scc_regs[active_reg] = value; // single WR2 on SCC
	}
	else if (active_reg == 8)
	{
		if (set2)
			SCC_serial_setData(value); // channel B only
		// channel A to be done if necessary
	}
	else if (active_reg == 1) // Tx/Rx interrupt enable
	{
		if (set2 == 0)
		{
			// channel A
			if (value & 1)
				RR3M |= 8;
			else
				RR3 &= ~8; // no IP(RR3) if not enabled(RR3M)
			if (value & 2)
				RR3M |= 16;
			else
				RR3 &= ~16;
			if (value & 0x18)
				RR3M |= 32;
			else
				RR3 &= ~32;
		}
		else
		{
			// channel B
			if (value & 1)
				RR3M |= 1;
			else
				RR3 &= ~1;
			if (value & 2)
				RR3M |= 2;
			else
				RR3 &= ~2;
			if (value & 0x18)
				RR3M |= 4;
			else
				RR3 &= ~4;
			// set or clear SCC flag if necessary (see later)
		}
	}
	else if (active_reg == 5) // Transmit parameter and control
	{
		SCC_serial_setRTS(value & 2);
		SCC_serial_setDTR(value & 128);
		// Tx character format & Tx CRC would be selected also here (8 bits/char and no CRC assumed)
	}
	else if (active_reg == 9) // Master interrupt control (common for both channels)
	{
		scc_regs[9] = value;// single WR9 (accessible by both channels)
		if (value & 0x40)
		{
			SCC_channelBreset();
		}
		if (value & 0x80)
		{
			SCC_channelAreset();
		}
		//  set or clear SCC flag accordingly (see later)
	}
	else if (active_reg == 13) // set baud rate according to WR13 and WR12
	{
		// Normally we have to set the baud rate according
		// to clock source (WR11) and clock mode (WR4)
		// In fact, we choose the baud rate from the value stored in WR12 & WR13
		// Note: we assume that WR13 is always written last (after WR12)
		// we tried to be more or less compatible with HSMODEM (see below)
		// 75 and 50 bauds are preserved because 153600 and 76800 were not available
		// 3600 and 2000 were also unavailable and are remapped to 57600 and 38400 respectively
		BaudRate = 0;
		switch (value)
		{
		 case 0:
			switch (scc_regs[12 + set2])
			{
			 case 0: // HSMODEM for 200 mapped to 230400
				BaudRate = 230400;
				break;
			 case 2: // HSMODEM for 150 mapped to 115200
				BaudRate = 115200;
				break;
			 case 6:    // HSMODEM for 134 mapped to 57600
			 case 0x7e: // HSMODEM for 3600 remapped to 57600
			 case 0x44: // normal for 3600 remapped to 57600
				BaudRate = 57600;
				break;
			 case 0xa:  // HSMODEM for 110 mapped to 38400
			 case 0xe4: // HSMODEM for 2000 remapped to 38400
			 case 0x7c: // normal for 2000 remapped to 38400
				BaudRate = 38400;
				break;
			 case 0x16: // HSMODEM for 19200
			 case 0xb:  // normal for 19200
				BaudRate = 19200;
				break;
			 case 0x2e: // HSMODEM for 9600
			 case 0x18: // normal for 9600
				BaudRate = 9600;
				break;
			 case 0x5e: // HSMODEM for 4800
			 case 0x32: // normal for 4800
				BaudRate = 4800;
				break;
			 case 0xbe: // HSMODEM for 2400
			 case 0x67: // normal
				BaudRate = 2400;
				break;
			 case 0xfe: // HSMODEM for 1800
			 case 0x8a: // normal for 1800
				BaudRate = 1800;
				break;
			 case 0xd0: // normal for 1200
				BaudRate = 1200;
				break;
			 case 1: // HSMODEM for 75 kept to 75
				BaudRate = 75;
				break;
			 case 4: // HSMODEM for 50 kept to 50
				BaudRate = 50;
				break;
			 default:
				D(bug("SCC: unexpected LSB constant for baud rate\n"));
				break;
			}
			break;
		 case 1:
			switch (scc_regs[12 + set2])
			{
			 case 0xa1: // normal for 600
				BaudRate = 600;
				break;
			 case 0x7e: // HSMODEM for 1200
				BaudRate = 1200;
				break;
			}
			break;
		 case 2:
			if (scc_regs[12 + set2] == 0xfe)
				BaudRate = 600; //HSMODEM
			break;
		 case 3:
			if (scc_regs[12 + set2] == 0x45)
				BaudRate = 300; //normal
			break;
		 case 4:
			if (scc_regs[12 + set2] == 0xe8)
				BaudRate = 200; //normal
			break;
		 case 5:
			if (scc_regs[12 + set2] == 0xfe)
				BaudRate = 300; //HSMODEM
			break;
		 case 6:
			if (scc_regs[12 + set2] == 0x8c)
				BaudRate = 150; //normal
			break;
		 case 7:
			if (scc_regs[12 + set2] == 0x4d)
				BaudRate = 134; //normal
			break;
		 case 8:
			if (scc_regs[12 + set2] == 0xee)
				BaudRate = 110; //normal
			break;
		 case 0xd:
			if (scc_regs[12 + set2] == 0x1a)
				BaudRate = 75; //normal
			break;
		 case 0x13:
			if (scc_regs[12 + set2] == 0xa8)
				BaudRate = 50; //normal
			break;
		 case 0xff: // HSMODEM dummy value->silently ignored
			break;
		 default:
			D(bug("SCC: unexpected MSB constant for baud rate\n"));
			break;
		}
		if (BaudRate)
			SCC_serial_setBaud(BaudRate); // set only if defined

		/* summary of baud rates:
		   Rsconf   Falcon     Falcon(+HSMODEM)   Hatari    Hatari(+HSMODEM)
		   0        19200         19200            19200       19200
		   1         9600          9600             9600        9600
		   2         4800          4800             4800        4800
		   3         3600          3600            57600       57600
		   4         2400          2400             2400        2400
		   5         2000          2000            38400       38400
		   6         1800          1800             1800        1800
		   7         1200          1200             1200        1200
		   8          600           600              600         600
		   9          300           300              300         300
		   10         200        230400              200      230400
		   11         150        115200              150      115200
		   12         134         57600              134       57600
		   13         110         38400              110       38400
		   14          75        153600               75          75
		   15          50         76800               50          50
		*/
	}
	else if (active_reg == 15) // external status int control
	{
		if (value & 1)
		{
			D(bug("SCC WR7 prime not yet processed\n"));
		}
	}

	// set or clear SCC flag accordingly. Yes it's ugly but avoids unnecessary useless calls
	if (active_reg == 1 || active_reg == 2 || active_reg == 9)
		TriggerSCC((RR3 & RR3M) && ((0xB & scc_regs[9]) == 9));

	active_reg = 0; // next access for RR0 or WR0
}

static void SCC_handleWrite(uint32_t addr, uint8_t value)
{
	int set2;

	addr &= 0x6;
	set2 = (addr >= 4) ? 16 : 0; // channel B
	switch (addr)
	{
	 case 0:
	 case 4:
		SCC_WriteControl(set2, value);
		break;
	 case 2: // channel A to be done if necessary
		break;
	 case 6: // channel B
		SCC_serial_setData(value);
		break;
	 default:
		D(bug( "scc : illegal write address =$%x\n", addr));
		break;
	}
}

void SCC_IRQ(void)
{
	uint16_t temp;
	temp = SCC_serial_getStatus();
	if (scc_regs[9] == 0x20)
		temp |= 0x800; // fake ExtStatusChange for HSMODEM install
	scc_regs[16] = temp & 0xFF; // RR0B
	RR3 = RR3M & (temp >> 8);
	if (RR3 && (scc_regs[9] & 0xB) == 9)
		TriggerSCC(true);
}


// return : vector number, or zero if no interrupt
int SCC_doInterrupt()
{
	int vector;
	uint8_t i;
	for (i = 0x20 ; i ; i >>= 1) // highest priority first
	{
		if (RR3 & i & RR3M)
			break ;
	}
	vector = scc_regs[2]; //WR2 = base of vectored interrupts for SCC
	if ((scc_regs[9] & 3) == 0)
		return vector; // no status included in vector
	if ((scc_regs[9] & 0x32) != 0) //shouldn't happen with TOS, (to be completed if needed)
	{
		D(bug( "unexpected WR9 contents \n"));
		// no Soft IACK, Status Low control bit expected, no NV
		return 0;
	}
	switch (i)
	{
	 case 0: /* this shouldn't happen :-) */
		D(bug( "scc_do_interrupt called with no pending interrupt\n"));
		vector = 0; // cancel
		break;
	 case 1:
		vector |= 2; // Ch B Ext/status change
		break;
	 case 2:
		break;// Ch B Transmit buffer Empty
	 case 4:
		vector |= 4; // Ch B Receive Char available
		break;
	 case 8:
		vector |= 0xA; // Ch A Ext/status change
		break;
	 case 16:
		vector |= 8; // Ch A Transmit Buffer Empty
		break;
	 case 32:
		vector |= 0xC; // Ch A Receive Char available
		break;
		// special receive condition not yet processed
	}
#if 0
	D(bug( "SCC_doInterrupt : vector %d\n", vector));
#endif
	return vector ;
}


void SCC_IoMem_ReadByte(void)
{
	int i;

	for (i = 0; i < nIoMemAccessSize; i++)
	{
		uint32_t addr = IoAccessBaseAddress + i;
		if (addr & 1)
			IoMem[addr] = SCC_handleRead(addr);
		else
			IoMem[addr] = 0xff;
	}
}

void SCC_IoMem_WriteByte(void)
{
	int i;

	for (i = 0; i < nIoMemAccessSize; i++)
	{
		uint32_t addr = IoAccessBaseAddress + i;
		if (addr & 1)
			SCC_handleWrite(addr, IoMem[addr]);
	}
}
