/*
 * Copyright 2007, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Eric Petit <eric.petit@lapsus.org>
 *		Michael Pfeiffer <laplace@users.sourceforge.net>
 */


#include "driver.h"

#include <KernelExport.h>
#include <PCI.h>
#include <OS.h>
#include <graphic_driver.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>


#define get_pci(o, s) (*gPciBus->read_pci_config)(pcii->bus, pcii->device, pcii->function, (o), (s))
#define set_pci(o, s, v) (*gPciBus->write_pci_config)(pcii->bus, pcii->device, pcii->function, (o), (s), (v))


static void
PrintCapabilities(uint32 caps)
{
	TRACE("capabilities:\n");
// 	if (caps & SVGA_CAP_RECT_FILL)			TRACE("RECT_FILL\n");
	if (caps & SVGA_CAP_RECT_COPY)			TRACE("RECT_COPY\n");
//	if (caps & SVGA_CAP_RECT_PAT_FILL)		TRACE("RECT_PAT_FILL\n");
//	if (caps & SVGA_CAP_LEGACY_OFFSCREEN)	TRACE("LEGACY_OFFSCREEN\n");
//	if (caps & SVGA_CAP_RASTER_OP)			TRACE("RASTER_OP\n");
	if (caps & SVGA_CAP_CURSOR)				TRACE("CURSOR\n");
	if (caps & SVGA_CAP_CURSOR_BYPASS)		TRACE("CURSOR_BYPASS\n");
	if (caps & SVGA_CAP_CURSOR_BYPASS_2)	TRACE("CURSOR_BYPASS_2\n");
	if (caps & SVGA_CAP_8BIT_EMULATION)		TRACE("8BIT_EMULATION\n");
	if (caps & SVGA_CAP_ALPHA_CURSOR)		TRACE("ALPHA_CURSOR\n");
//	if (caps & SVGA_CAP_GLYPH)				TRACE("GLYPH\n");
//	if (caps & SVGA_CAP_GLYPH_CLIPPING)		TRACE("GLYPH_CLIPPING\n");
//	if (caps & SVGA_CAP_OFFSCREEN_1)		TRACE("OFFSCREEN_1\n");
//	if (caps & SVGA_CAP_ALPHA_BLEND)		TRACE("ALPHA_BLEND\n");
	if (caps & SVGA_CAP_3D)					TRACE("3D\n");
	if (caps & SVGA_CAP_EXTENDED_FIFO)		TRACE("EXTENDED_FIFO\n");
	if (caps & SVGA_CAP_MULTIMON)			TRACE("MULTIMON\n");
	if (caps & SVGA_CAP_PITCHLOCK)			TRACE("PITCHLOCK\n");
	if (caps & SVGA_CAP_IRQMASK)			TRACE("IRQMASK\n");
	if (caps & SVGA_CAP_DISPLAY_TOPOLOGY)	TRACE("DISPLAY_TOPOLOGY\n");
	if (caps & SVGA_CAP_GMR)				TRACE("GMR\n");
	if (caps & SVGA_CAP_TRACES)				TRACE("TRACES\n");
	if (caps & SVGA_CAP_GMR2)				TRACE("GMR2\n");
	if (caps & SVGA_CAP_SCREEN_OBJECT_2)	TRACE("SCREEN_OBJECT_2\n");
}


static status_t
CheckCapabilities()
{
	SharedInfo* sharedInfo = gPd->sharedInfo;
	pci_info* pciInfo = &gPd->pcii;

	uint32 id = SVGA_ID_INVALID;

	/* Needed to read/write registers */
	sharedInfo->indexPort = pciInfo->u.h0.base_registers[0] + SVGA_INDEX_PORT;
	sharedInfo->valuePort = pciInfo->u.h0.base_registers[0] + SVGA_VALUE_PORT;
	TRACE("index port: %d, value port: %d\n",
		sharedInfo->indexPort, sharedInfo->valuePort);

	/* This should be SVGA II according to the PCI device_id,
	 * but just in case... */
	WriteReg(SVGA_REG_ID, SVGA_ID_2);
	id = ReadReg(SVGA_REG_ID);
	if (id != SVGA_ID_2) {
		TRACE("SVGA_REG_ID is %" B_PRId32 ", not %d\n", id, SVGA_ID_2);
		return B_ERROR;
	}
	TRACE("SVGA_REG_ID OK\n");

	/* Grab some info */
	sharedInfo->maxWidth = ReadReg(SVGA_REG_MAX_WIDTH);
	sharedInfo->maxHeight = ReadReg(SVGA_REG_MAX_HEIGHT);
	TRACE("max resolution: %" B_PRId32 "x%" B_PRId32 "\n", sharedInfo->maxWidth, sharedInfo->maxHeight);

	//sharedInfo->frameBufferDMA = (void*)ReadReg(SVGA_REG_FB_START);
	sharedInfo->frameBufferDMA = (phys_addr_t)pciInfo->u.h0.base_registers[1];
	//sharedInfo->frameBufferSize = ReadReg(SVGA_REG_VRAM_SIZE);
	sharedInfo->frameBufferSize = pciInfo->u.h0.base_register_sizes[1];
	TRACE("frame buffer: %p, size %" B_PRId32 "\n", sharedInfo->frameBufferDMA, sharedInfo->frameBufferSize);

	//sharedInfo->fifoDMA = (void*)ReadReg(SVGA_REG_MEM_START);
	sharedInfo->fifoDMA = (phys_addr_t)pciInfo->u.h0.base_registers[2];
	//sharedInfo->fifoSize = ReadReg(SVGA_REG_MEM_SIZE) & ~3;
	sharedInfo->fifoSize = pciInfo->u.h0.base_register_sizes[2];
	TRACE("fifo: %p, size %" B_PRId32 "\n", sharedInfo->fifoDma, sharedInfo->fifoSize);

	sharedInfo->capabilities = ReadReg(SVGA_REG_CAPABILITIES);
	PrintCapabilities(sharedInfo->capabilities);
	sharedInfo->fifoMin = (sharedInfo->capabilities & SVGA_CAP_EXTENDED_FIFO) ?
		ReadReg(SVGA_REG_MEM_REGS) : 4;

	return B_OK;
}


static status_t
MapDevice()
{
	SharedInfo* sharedInfo = gPd->sharedInfo;
	int writeCombined = 1;

	/* Map the frame buffer */
	sharedInfo->fbArea = map_physical_memory("VMware frame buffer",
		(addr_t)sharedInfo->fbDma, sharedInfo->fbSize, B_ANY_KERNEL_BLOCK_ADDRESS|B_MTR_WC,
		B_READ_AREA|B_WRITE_AREA, (void **)&sharedInfo->fb);
	if (sharedInfo->fbArea < 0) {
		/* Try again without write combining */
		writeCombined = 0;
		sharedInfo->fbArea = map_physical_memory("VMware frame buffer",
			(addr_t)sharedInfo->fbDma, sharedInfo->fbSize, B_ANY_KERNEL_BLOCK_ADDRESS,
			B_READ_AREA|B_WRITE_AREA, (void **)&sharedInfo->fb);
	}
	if (sharedInfo->fbArea < 0) {
		TRACE("failed to map frame buffer\n");
		return sharedInfo->fbArea;
	}
	TRACE("frame buffer mapped: %p->%p, area %" B_PRId32 ", size %" B_PRId32 ", write "
		"combined: %" B_PRId32 "\n", sharedInfo->fbDma, sharedInfo->fb, sharedInfo->fbArea,
		sharedInfo->fbSize, writeCombined);

	/* Map the fifo */
	sharedInfo->fifoArea = map_physical_memory("VMware fifo",
		sharedInfo->fifoDma, sharedInfo->fifoSize, B_ANY_KERNEL_BLOCK_ADDRESS,
		B_READ_AREA|B_WRITE_AREA, (void **)&sharedInfo->fifo);
	if (sharedInfo->fifoArea < 0) {
		TRACE("failed to map fifo\n");
		delete_area(sharedInfo->fbArea);
		return sharedInfo->fifoArea;
	}
	TRACE("fifo mapped: %p->%p, area %" B_PRId32 ", size %" B_PRId32 "\n", sharedInfo->fifoDma,
		sharedInfo->fifo, sharedInfo->fifoArea, sharedInfo->fifoSize);

	return B_OK;
}


static void
UnmapDevice()
{
	SharedInfo* sharedInfo = gPd->sharedInfo;
	pci_info* pciInfo = &gPd->pcii;
	uint32 commandReg = 0;

	/* Disable memory mapped IO */
	commandReg = get_pci(PCI_command, 2);
	commandReg &= ~PCI_command_memory;
	set_pci(PCI_command, 2, commandReg);

	/* Delete the areas */
	if (sharedInfo->fifoArea >= 0)
		delete_area(sharedInfo->fifoArea);

	if (sharedInfo->fbArea >= 0)
		delete_area(sharedInfo->fbArea);

	sharedInfo->fifoArea = -1;
	sharedInfo->fbArea = -1;
	sharedInfo->fb = NULL;
	sharedInfo->fifo = NULL;
}


static status_t
CreateShared()
{
	gPd->sharedArea = create_area("VMware shared", (void**)&gPd->sharedInfo,
		B_ANY_KERNEL_ADDRESS, ROUND_TO_PAGE_SIZE(sizeof(SharedInfo)),
		B_FULL_LOCK,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA | B_CLONEABLE_AREA);
	if (gPd->sharedArea < B_OK) {
		TRACE("failed to create shared area\n");
		return gPd->sharedArea;
	}
	TRACE("shared area created\n");

	memset(gPd->sharedInfo, 0, sizeof(SharedInfo));
	return B_OK;
}


static void
FreeShared()
{
	delete_area(gPd->sharedArea);
	gPd->sharedArea = -1;
	gPd->sharedInfo = NULL;
}


static status_t
OpenHook(const char* name, uint32 flags, void** cookie)
{
	status_t result = B_ERROR;
	pci_info* pcii = &gPd->pcii;
	uint32 commandReg = 0;

	TRACE("OpenHook (%s, %" B_PRId32 ")\n", name, flags);
	ACQUIRE_BEN(gPd->kernel);

	if (gPd->isOpen)
		goto markAsOpen;

	/* Enable memory mapped IO and VGA I/O */
	commandReg = get_pci(PCI_command, 2);
	commandReg |= PCI_command_memory;
	commandReg |= PCI_command_io;
	set_pci(PCI_command, 2, commandReg);

	result = CreateShared();
	if (result != B_OK)
		goto done;

	result = CheckCapabilities();
	if (result != B_OK)
		goto freeShared;

	result = MapDevice();
	if (result != B_OK)
		goto freeShared;

markAsOpen:
	gPd->isOpen++;
	*cookie = gPd;
	goto done;

freeShared:
	FreeShared();

done:
	RELEASE_BEN(gPd->kernel);
	TRACE("OpenHook: %" B_PRId32 "\n", ret);
	return result;
}


/*--------------------------------------------------------------------*/
/* ReadHook, WriteHook, CloseHook: do nothing */

static status_t
ReadHook(void *dev, off_t pos, void *buf, size_t *len)
{
	*len = 0;
	return B_NOT_ALLOWED;
}


static status_t
WriteHook(void *dev, off_t pos, const void *buf, size_t *len)
{
	*len = 0;
	return B_NOT_ALLOWED;
}


static status_t
CloseHook(void *dev)
{
	return B_OK;
}


/*--------------------------------------------------------------------*/
/* FreeHook: closes down the device */

static status_t
FreeHook(void* dev)
{
	TRACE("FreeHook\n");
	ACQUIRE_BEN(gPd->kernel);

	if (gPd->isOpen < 2) {
		UnmapDevice();
		FreeShared();
	}
	gPd->isOpen--;

	RELEASE_BEN(gPd->kernel);
	TRACE("FreeHook ends\n");
	return B_OK;
}


static void
UpdateCursor(SharedInfo *sharedInfo)
{
	WriteReg(SVGA_REG_CURSOR_ID, CURSOR_ID);
	WriteReg(SVGA_REG_CURSOR_X, sharedInfo->cursorX);
	WriteReg(SVGA_REG_CURSOR_Y, sharedInfo->cursorY);
	WriteReg(SVGA_REG_CURSOR_ON, sharedInfo->cursorShow ? SVGA_CURSOR_ON_SHOW :
				SVGA_CURSOR_ON_HIDE);
}


/*--------------------------------------------------------------------*/
/* ControlHook: responds the the ioctl from the accelerant */

static status_t
ControlHook(void* dev, uint32 msg, void* buf, size_t len)
{
	SharedInfo* sharedInfo = gPd->sharedInfo;

	switch (msg) {
		case B_GET_ACCELERANT_SIGNATURE:
			if (user_strlcpy((char*)buf, "vmware.accelerant",
				B_FILE_NAME_LENGTH) < B_OK) {
				return B_BAD_ADDRESS;
			}
			return B_OK;

		case VMWARE_GET_PRIVATE_DATA:
			if (user_memcpy(buf, &gPd->sharedArea, sizeof(gPd->sharedArea))
				< B_OK) {
				return B_BAD_ADDRESS;
			}
			return B_OK;

		case VMWARE_FIFO_START:
			//WriteReg(SVGA_REG_ENABLE, 1);
			WriteReg(SVGA_REG_CONFIG_DONE, 1);
			return B_OK;

		case VMWARE_FIFO_STOP:
			WriteReg(SVGA_REG_CONFIG_DONE, 0);
			WriteReg(SVGA_REG_ENABLE, 0);
			return B_OK;

		case VMWARE_FIFO_SYNC:
			WriteReg(SVGA_REG_SYNC, 1);
			while (ReadReg(SVGA_REG_BUSY));
			return B_OK;

		case VMWARE_SET_MODE:
		{
			display_mode dm;
			if (user_memcpy(&dm, buf, sharedInfozeof(display_mode)) < B_OK)
				return B_BAD_ADDRESS;
			WriteReg(SVGA_REG_WIDTH, dm.virtual_width);
			WriteReg(SVGA_REG_HEIGHT, dm.virtual_height);
			WriteReg(SVGA_REG_BITS_PER_PIXEL, BppForSpace(dm.space));
			WriteReg(SVGA_REG_ENABLE, 1);
			sharedInfo->fbOffset = ReadReg(SVGA_REG_FB_OFFSET);
			sharedInfo->bytesPerRow = ReadReg(SVGA_REG_BYTES_PER_LINE);
			ReadReg(SVGA_REG_DEPTH);
			ReadReg(SVGA_REG_PSEUDOCOLOR);
			ReadReg(SVGA_REG_RED_MASK);
			ReadReg(SVGA_REG_GREEN_MASK);
			ReadReg(SVGA_REG_BLUE_MASK);
			return B_OK;
		}

		case VMWARE_SET_PALETTE:
		{
			uint8 colors[3 * 256];
			uint8 *color = colors;
			uint32 i;
			if (user_memcpy(colors, buf, sizeof(colors)) < B_OK)
				return B_BAD_ADDRESS;
			if (ReadReg(SVGA_REG_PSEUDOCOLOR) != 1)
				return B_ERROR;

			for (i = 0; i < 256; i++) {
				WriteReg(SVGA_PALETTE_BASE + 3 * i, *color++);
				WriteReg(SVGA_PALETTE_BASE + 3 * i + 1, *color++);
				WriteReg(SVGA_PALETTE_BASE + 3 * i + 2, *color++);
			}
			return B_OK;
		}

		case VMWARE_MOVE_CURSOR:
		{
			uint16 pos[2];
			if (user_memcpy(pos, buf, sizeof(pos)) < B_OK)
				return B_BAD_ADDRESS;
			sharedInfo->cursorX = pos[0];
			sharedInfo->cursorY = pos[1];
			UpdateCursor(sharedInfo);
			return B_OK;
		}

		case VMWARE_SHOW_CURSOR:
		{
			if (user_memcpy(&sharedInfo->cursorShow, buf, sizeof(bool)) < B_OK)
				return B_BAD_ADDRESS;
			UpdateCursor(sharedInfo);
			return B_OK;
		}

		case VMWARE_GET_DEVICE_NAME:
			dprintf("device: VMWARE_GET_DEVICE_NAME %s\n", gPd->names[0]);
			if (user_strlcpy((char*)buf, gPd->names[0],
					B_PATH_NAME_LENGTH) < B_OK)
				return B_BAD_ADDRESS;
			return B_OK;

	}

	TRACE("ioctl: %" B_PRId32 ", %p, %" B_PRId32 "\n", msg, buf, (int32)len);
	return B_DEV_INVALID_IOCTL;
}


device_hooks gGraphicsDeviceHooks =
{
	OpenHook,
	CloseHook,
	FreeHook,
	ControlHook,
	ReadHook,
	WriteHook,
	NULL,
	NULL,
	NULL,
	NULL
};

