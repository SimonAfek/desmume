/*  Copyright (C) 2006 yopyop
    yopyop156@ifrance.com
    yopyop156.ifrance.com

    Copyright 2007 shash
	Copyright 2007-2009 DeSmuME team

    This file is part of DeSmuME

    DeSmuME is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    DeSmuME is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with DeSmuME; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
*/

#include "FIFO.h"
#include <string.h>
#include "armcpu.h"
#include "debug.h"
#include "mem.h"
#include "MMU.h"
#include "NDSSystem.h"

// ========================================================= IPC FIFO
IPC_FIFO ipc_fifo[2];		// 0 - ARM9
							// 1 - ARM7

u8 FORCEINLINE IPC_FIFOgetSize(u8 proc)
{
	if (ipc_fifo[proc].head < ipc_fifo[proc].tail) return (ipc_fifo[proc].tail - ipc_fifo[proc].head);
	if (ipc_fifo[proc].head > ipc_fifo[proc].tail) return (16 - (ipc_fifo[proc].head - ipc_fifo[proc].tail));
	return 0;
}

void IPC_FIFOinit(u8 proc)
{
	memset(&ipc_fifo[proc], 0, sizeof(IPC_FIFO));
	T1WriteWord(MMU.MMU_MEM[proc][0x40], 0x184, 0x00000101);
}

void IPC_FIFOsend(u8 proc, u32 val)
{
	u16 cnt_l = T1ReadWord(MMU.MMU_MEM[proc][0x40], 0x184);
	if (!(cnt_l & 0x8000)) return;			// FIFO disabled
	u8	proc_remote = proc ^ 1;

	if (ipc_fifo[proc].size > 15)
	{
		cnt_l |= 0x4000;
		T1WriteWord(MMU.MMU_MEM[proc][0x40], 0x184, cnt_l);
		return;
	}

	u16 cnt_r = T1ReadWord(MMU.MMU_MEM[proc_remote][0x40], 0x184);

	//LOG("IPC%s send FIFO 0x%08X (l 0x%X, tail %02i) (r 0x%X, tail %02i)\n", 
	//	proc?"7":"9", val, cnt_l, ipc_fifo[proc].tail, cnt_r, ipc_fifo[proc^1].tail);
	
	cnt_l &= 0xBFFC;		// clear send empty bit & full
	cnt_r &= 0xBCFF;		// set recv empty bit & full
	ipc_fifo[proc].buf[ipc_fifo[proc].tail] = val;
	ipc_fifo[proc].tail++;
	ipc_fifo[proc].size = IPC_FIFOgetSize(proc);
	if (ipc_fifo[proc].tail > 15) ipc_fifo[proc].tail = 0;
	
	if (ipc_fifo[proc].size > 15)
	{
		cnt_l |= 0x0002;		// set send full bit
		cnt_r |= 0x0200;		// set recv full bit
	}

	T1WriteWord(MMU.MMU_MEM[proc][0x40], 0x184, cnt_l);
	T1WriteWord(MMU.MMU_MEM[proc_remote][0x40], 0x184, cnt_r);

	setIF(proc_remote, ((cnt_l & 0x0400)<<8));
}

u32 IPC_FIFOrecv(u8 proc)
{
	u16 cnt_l = T1ReadWord(MMU.MMU_MEM[proc][0x40], 0x184);
	if (!(cnt_l & 0x8000)) return (0);									// FIFO disabled
	u8	proc_remote = proc ^ 1;

	u32 val = 0;

	if ( ipc_fifo[proc_remote].size == 0 )		// remote FIFO error
	{
		cnt_l |= 0x4000;
		T1WriteWord(MMU.MMU_MEM[proc][0x40], 0x184, cnt_l);
		return (0);
	}

	u16 cnt_r = T1ReadWord(MMU.MMU_MEM[proc_remote][0x40], 0x184);

	cnt_l &= 0xBCFF;		// clear send full bit & empty
	cnt_r &= 0xBFFC;		// set recv full bit & empty

	val = ipc_fifo[proc_remote].buf[ipc_fifo[proc_remote].head];
	ipc_fifo[proc_remote].head++;
	ipc_fifo[proc_remote].size = IPC_FIFOgetSize(proc_remote);
	if (ipc_fifo[proc_remote].head > 15) ipc_fifo[proc_remote].head = 0;
	
	//LOG("IPC%s recv FIFO 0x%08X (l 0x%X, tail %02i) (r 0x%X, tail %02i)\n", 
	//	proc?"7":"9", val, cnt_l, ipc_fifo[proc].tail, cnt_r, ipc_fifo[proc^1].tail);

	if ( ipc_fifo[proc_remote].size == 0 )		// FIFO empty
	{
		cnt_l |= 0x0100;
		cnt_r |= 0x0001;
	}

	T1WriteWord(MMU.MMU_MEM[proc][0x40], 0x184, cnt_l);
	T1WriteWord(MMU.MMU_MEM[proc_remote][0x40], 0x184, cnt_r);

	setIF(proc_remote, ((cnt_l & 0x0004)<<15));

	return (val);
}

void IPC_FIFOcnt(u8 proc, u16 val)
{
	u16 cnt_l = T1ReadWord(MMU.MMU_MEM[proc][0x40], 0x184);
	u16 cnt_r = T1ReadWord(MMU.MMU_MEM[proc^1][0x40], 0x184);

	//LOG("IPC%s FIFO context 0x%X (local 0x%04X, remote 0x%04X)\n", proc?"7":"9", val, cnt_l, cnt_r);
	if (val & 0x4008)
	{
		ipc_fifo[proc].tail = 0;
		T1WriteWord(MMU.MMU_MEM[proc][0x40], 0x184, (cnt_l & 0x0301) | (val & 0x8404) | 1);
		T1WriteWord(MMU.MMU_MEM[proc^1][0x40], 0x184, (cnt_r & 0x8407) | 0x100);
		//MMU.reg_IF[proc^1] |= ((val & 0x0004) << 15);
		setIF(proc^1, ((val & 0x0004)<<15));
		return;
	}

	T1WriteWord(MMU.MMU_MEM[proc][0x40], 0x184, val);
}

// ========================================================= GFX FIFO
GFX_PIPE	gxPIPE;
GFX_FIFO	gxFIFO;

u8 FORCEINLINE GFX_PIPEgetSize()
{
	if (gxPIPE.head < gxPIPE.tail) return (gxPIPE.tail - gxPIPE.head);
	if (gxPIPE.head > gxPIPE.tail) return (5 - (gxPIPE.head - gxPIPE.tail));
	return 0;
}

u16 FORCEINLINE GFX_FIFOgetSize()
{
	if (gxFIFO.head < gxFIFO.tail) return (gxFIFO.tail - gxFIFO.head);
	if (gxFIFO.head > gxFIFO.tail) return (257 - (gxFIFO.head - gxFIFO.tail));
	return 0;
}

void GFX_PIPEclear()
{
	gxPIPE.head = 0;
	gxPIPE.tail = 0;
	gxPIPE.size = 0;
}

void GFX_FIFOclear()
{
	u32 gxstat = T1ReadLong(MMU.MMU_MEM[ARMCPU_ARM9][0x40], 0x600);
	gxstat &= 0x0000FFFF;

	gxFIFO.head = 0;
	gxFIFO.tail = 0;
	gxFIFO.size = 0;
	gxstat |= 0x06000000;
	T1WriteLong(MMU.MMU_MEM[ARMCPU_ARM9][0x40], 0x600, gxstat);
}

void GFX_FIFOsend(u8 cmd, u32 param)
{
	u32 gxstat = T1ReadLong(MMU.MMU_MEM[ARMCPU_ARM9][0x40], 0x600);

	if (gxFIFO.size == 0)				// FIFO empty
	{
		gxstat &= 0xF000FFFF;
		gxstat |= 0x06000000;
		if (gxPIPE.size < 4)			// pipe not full
		{
			gxPIPE.cmd[gxPIPE.tail] = cmd;
			gxPIPE.param[gxPIPE.tail] = param;
			gxPIPE.tail++;
			gxPIPE.size = GFX_PIPEgetSize();
			if (gxPIPE.tail > 4) gxPIPE.tail = 0;
#ifdef USE_GEOMETRY_FIFO_EMULATION
			gxstat |= 0x08000000;		// set busy flag
			T1WriteLong(MMU.MMU_MEM[ARMCPU_ARM9][0x40], 0x600, gxstat);
#endif
			NDS_RescheduleGXFIFO();
			return;
		}
	}

	gxstat &= 0xF000FFFF;
	//INFO("GFX FIFO: Send GFX 3D cmd 0x%02X to FIFO - 0x%08X (%03i/%02X)\n", cmd, param, gxFIFO.tail, gxFIFO.tail);
	if (gxstat & 0x01000000)					// full
	{
#ifdef USE_GEOMETRY_FIFO_EMULATION
		gxstat &= 0xF000FFFF;
		gxstat |= 0x01000000;
		T1WriteLong(MMU.MMU_MEM[ARMCPU_ARM9][0x40], 0x600, gxstat);
		NDS_RescheduleGXFIFO();
#endif
		//INFO("ERROR: gxFIFO is full (cmd 0x%02X = 0x%08X) (prev cmd 0x%02X = 0x%08X)\n", cmd, param, gxFIFO.cmd[255], gxFIFO.param[255]);
		return;		
	}

	gxFIFO.cmd[gxFIFO.tail] = cmd;
	gxFIFO.param[gxFIFO.tail] = param;
	gxFIFO.tail++;
	gxFIFO.size = GFX_FIFOgetSize();
	if (gxFIFO.tail > 256) gxFIFO.tail = 0;
	
#ifdef USE_GEOMETRY_FIFO_EMULATION
	gxstat |= 0x08000000;		// set busy flag
#endif

	gxstat |= ((gxFIFO.size & 0x1FF) << 16);

	if (gxFIFO.size < 128)				// less half
	{
		gxstat |= 0x02000000;
	}

#ifndef USE_GEOMETRY_FIFO_EMULATION
	gxstat |= 0x02000000;					// this is hack (must be removed later)
#endif

	T1WriteLong(MMU.MMU_MEM[ARMCPU_ARM9][0x40], 0x600, gxstat);
	
	NDS_RescheduleGXFIFO();
}

// this function used ONLY in new gxFIFO
BOOL GFX_PIPErecv(u8 *cmd, u32 *param)
{
	u8	tmp_cmd = 0;
	u32	tmp_param = 0;
	u32 gxstat = T1ReadLong(MMU.MMU_MEM[ARMCPU_ARM9][0x40], 0x600);
	gxstat &= 0xF7FFFFFF;		// clear busy flag

	if (gxPIPE.size > 0)
	{
		*cmd = gxPIPE.cmd[gxPIPE.head];
		*param = gxPIPE.param[gxPIPE.head];
		gxPIPE.head++;
		gxPIPE.size = GFX_PIPEgetSize();
		if (gxPIPE.head > 4) gxPIPE.head = 0;

		if (gxPIPE.size < 2) 
		{
			if (gxFIFO.size > 0)
			{
				gxstat &= 0xF000FFFF;

				gxPIPE.cmd[gxPIPE.tail] = gxFIFO.cmd[gxFIFO.head];
				gxPIPE.param[gxPIPE.tail] = gxFIFO.param[gxFIFO.head];
				gxPIPE.tail++;
				gxPIPE.size = GFX_PIPEgetSize();
				if (gxPIPE.tail > 4) gxPIPE.tail = 0;

				gxFIFO.head++;
				gxFIFO.size = GFX_FIFOgetSize();
				if (gxFIFO.head > 256) gxFIFO.head = 0;

				if (gxFIFO.size > 0)
				{
					gxPIPE.cmd[gxPIPE.tail] = gxFIFO.cmd[gxFIFO.head];
					gxPIPE.param[gxPIPE.tail] = gxFIFO.param[gxFIFO.head];
					gxPIPE.tail++;
					gxPIPE.size = GFX_PIPEgetSize();
					if (gxPIPE.tail > 4) gxPIPE.tail = 0;

					gxFIFO.head++;
					gxFIFO.size = GFX_FIFOgetSize();
					if (gxFIFO.head > 256) gxFIFO.head = 0;
				}

				gxstat |= ((gxFIFO.size & 0x1FF) << 16);

				if (gxFIFO.size < 128)
				{
					gxstat |= 0x02000000;
					execHardware_doAllDma(EDMAMode_GXFifo);
					if (gxstat & 0x40000000)	// IRQ: less half
						setIF(0, (1<<21));
				}

				if (gxFIFO.tail == gxFIFO.head)		// empty
					gxstat |= 0x04000000;
			}
			else		// FIFO empty
			{
				gxstat &= 0xF000FFFF;
				gxstat |= 0x06000000;
			}
		}

		if (gxPIPE.size > 0)
			gxstat |= 0x08000000;	// set busy flag

		T1WriteLong(MMU.MMU_MEM[ARMCPU_ARM9][0x40], 0x600, gxstat);
		return (TRUE);
	}

	if (gxstat & 0x80000000)	// IRQ: empty
			setIF(0, (1<<21));
	T1WriteLong(MMU.MMU_MEM[ARMCPU_ARM9][0x40], 0x600, gxstat);
	return FALSE;
}

void GFX_FIFOcnt(u32 val)
{
	u32 gxstat = T1ReadLong(MMU.MMU_MEM[ARMCPU_ARM9][0x40], 0x600);
	//INFO("GFX FIFO: write context 0x%08X (prev 0x%08X) tail %i\n", val, gxstat, gxFIFO.tail);
	if (val & (1<<29))		// clear? (homebrew)
	{
		GFX_PIPEclear();
		GFX_FIFOclear();
		return;
	}
	T1WriteLong(MMU.MMU_MEM[ARMCPU_ARM9][0x40], 0x600, gxstat);

	NDS_RescheduleGXFIFO();
}

// ========================================================= DISP FIFO
DISP_FIFO	disp_fifo;

void DISP_FIFOinit()
{
	memset(&disp_fifo, 0, sizeof(DISP_FIFO));
}

void DISP_FIFOsend(u32 val)
{
	//INFO("DISP_FIFO send value 0x%08X (head 0x%06X, tail 0x%06X)\n", val, disp_fifo.head, disp_fifo.tail);
	disp_fifo.buf[disp_fifo.tail] = val;
	disp_fifo.tail++;
	if (disp_fifo.tail > 0x5FFF)
		disp_fifo.tail = 0;
}

u32 DISP_FIFOrecv()
{
	//if (disp_fifo.tail == disp_fifo.head) return (0); // FIFO is empty
	u32 val = disp_fifo.buf[disp_fifo.head];
	disp_fifo.head++;
	if (disp_fifo.head > 0x5FFF)
		disp_fifo.head = 0;
	return (val);
}
