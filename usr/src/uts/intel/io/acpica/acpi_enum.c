/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * ACPI enumerator
 */

#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/sunndi.h>
#include <sys/note.h>
#include "acpi.h"
#include <sys/acpica.h>

extern void free_master_data();
extern void process_master_file();
extern int master_file_lookup(char *, char **, char **, char **);
extern int master_file_lookups(char *, char **, char **, char **, int);

static char keyboard_alias[] = "keyboard";
static char mouse_alias[] = "mouse";
#define	ACPI_ENUM_DEBUG		"acpi_enum_debug"
#define	PARSE_RESOURCES_DEBUG	0x0001
#define	MASTER_LOOKUP_DEBUG	0x0002
#define	DEVICES_NOT_ENUMED	0x0004
#define	PARSE_RES_IRQ		0x0008
#define	PARSE_RES_DMA		0x0010
#define	PARSE_RES_MEMORY	0x0020
#define	PARSE_RES_IO		0x0040
#define	PARSE_RES_ADDRESS	0x0080
#define	ISA_DEVICE_ENUM		0x1000
#define	PROCESS_CIDS		0x2000
static unsigned long acpi_enum_debug = 0x00;

static char USED_RESOURCES[] = "used-resources";
static dev_info_t *usedrdip = NULL;
static unsigned short used_interrupts = 0;
static unsigned short used_dmas = 0;
typedef struct used_io_mem {
	unsigned int start_addr;
	unsigned int length;
	struct used_io_mem *next;
} used_io_mem_t;
static used_io_mem_t *used_io_head = NULL;
static used_io_mem_t *used_mem_head = NULL;
static int used_io_count = 0;
static int used_mem_count = 0;

#define	MAX_PARSED_ACPI_RESOURCES	255
#define	ACPI_ISA_LIMIT	16
static int interrupt[ACPI_ISA_LIMIT], dma[ACPI_ISA_LIMIT];
#define	ACPI_ELEMENT_PACKAGE_LIMIT	32
#define	EISA_ID_SIZE	7

/*
 * insert used io/mem in increasing order
 */
static void
insert_used_resource(used_io_mem_t *used, int *used_count, used_io_mem_t **head)
{
	used_io_mem_t *curr, *prev;

	(*used_count)++;
	if (*head == NULL) {
		*head = used;
		return;
	}
	curr = prev = *head;
	/* find a place to insert */
	while ((curr != NULL) &&
	    (curr->start_addr < used->start_addr)) {
		prev = curr;
		curr = curr->next;
	}
	if (prev == curr) {
		/* head */
		*head = used;
		used->next = curr;
		return;
	} else {
		prev->next = used;
	}
	used->next = curr;
}

static void
add_used_io_mem(struct regspec *io, int io_count)
{
	int i;
	used_io_mem_t *used;

	for (i = 0; i < io_count; i++) {
		used = (used_io_mem_t *)kmem_zalloc(sizeof (used_io_mem_t),
		    KM_SLEEP);
		used->start_addr = io[i].regspec_addr;
		used->length = io[i].regspec_size;
		if (io[i].regspec_bustype == 1) {
			insert_used_resource(used, &used_io_count,
			    &used_io_head);
		} else {
			insert_used_resource(used, &used_mem_count,
			    &used_mem_head);
		}
	}
}

static void
parse_resources_irq(ACPI_RESOURCE *resource_ptr, int *interrupt_count)
{
	int i;

	for (i = 0; i < resource_ptr->Data.Irq.InterruptCount; i++) {
		interrupt[(*interrupt_count)++] =
		    resource_ptr->Data.Irq.Interrupts[i];
		used_interrupts |= 1 << resource_ptr->Data.Irq.Interrupts[i];
		if (acpi_enum_debug & PARSE_RES_IRQ) {
			cmn_err(CE_NOTE, "parse_resources() "\
			    "IRQ num %u, intr # = %u",
			    i, resource_ptr->Data.Irq.Interrupts[i]);
		}
	}
}

static void
parse_resources_dma(ACPI_RESOURCE *resource_ptr, int *dma_count)
{
	int i;

	for (i = 0; i < resource_ptr->Data.Dma.ChannelCount; i++) {
		dma[(*dma_count)++] = resource_ptr->Data.Dma.Channels[i];
		used_dmas |= 1 << resource_ptr->Data.Dma.Channels[i];
		if (acpi_enum_debug & PARSE_RES_DMA) {
			cmn_err(CE_NOTE, "parse_resources() "\
			    "DMA num %u, channel # = %u",
			    i, resource_ptr->Data.Dma.Channels[i]);
		}
	}
}

static void
parse_resources_io(ACPI_RESOURCE *resource_ptr, struct regspec *io,
    int *io_count)
{
	ACPI_RESOURCE_IO acpi_io = resource_ptr->Data.Io;

	if (acpi_io.AddressLength == 0)
		return;

	io[*io_count].regspec_bustype = 1; /* io */
	io[*io_count].regspec_size = acpi_io.AddressLength;
	io[*io_count].regspec_addr = acpi_io.Minimum;
	if (acpi_enum_debug & PARSE_RES_IO) {
		cmn_err(CE_NOTE, "parse_resources() "\
		    "IO min 0x%X, max 0x%X, length: 0x%X",
		    acpi_io.Minimum,
		    acpi_io.Maximum,
		    acpi_io.AddressLength);
	}
	(*io_count)++;
}

static void
parse_resources_fixed_io(ACPI_RESOURCE *resource_ptr, struct regspec *io,
    int *io_count)
{
	ACPI_RESOURCE_FIXED_IO fixed_io = resource_ptr->Data.FixedIo;

	if (fixed_io.AddressLength == 0)
		return;

	io[*io_count].regspec_bustype = 1; /* io */
	io[*io_count].regspec_addr = fixed_io.Address;
	io[*io_count].regspec_size = fixed_io.AddressLength;
	if (acpi_enum_debug & PARSE_RES_IO) {
		cmn_err(CE_NOTE, "parse_resources() "\
		    "Fixed IO 0x%X, length: 0x%X",
		    fixed_io.Address, fixed_io.AddressLength);
	}
	(*io_count)++;
}

static void
parse_resources_fixed_mem32(ACPI_RESOURCE *resource_ptr, struct regspec *io,
    int *io_count)
{
	ACPI_RESOURCE_FIXED_MEMORY32 fixed_mem32 =
	    resource_ptr->Data.FixedMemory32;

	if (fixed_mem32.AddressLength == 0)
		return;

	io[*io_count].regspec_bustype = 0; /* memory */
	io[*io_count].regspec_addr = fixed_mem32.Address;
	io[*io_count].regspec_size = fixed_mem32.AddressLength;
	if (acpi_enum_debug & PARSE_RES_MEMORY) {
		cmn_err(CE_NOTE, "parse_resources() "\
		    "Fixed Mem 32 %ul, length: %ul",
		    fixed_mem32.Address, fixed_mem32.AddressLength);
	}
	(*io_count)++;
}

static void
parse_resources_mem32(ACPI_RESOURCE *resource_ptr, struct regspec *io,
    int *io_count)
{
	ACPI_RESOURCE_MEMORY32 mem32 = resource_ptr->Data.Memory32;

	if (mem32.AddressLength == 0)
		return;

	if (resource_ptr->Data.Memory32.Minimum ==
	    resource_ptr->Data.Memory32.Maximum) {
		io[*io_count].regspec_bustype = 0; /* memory */
		io[*io_count].regspec_addr = mem32.Minimum;
		io[*io_count].regspec_size = mem32.AddressLength;
		(*io_count)++;
		if (acpi_enum_debug & PARSE_RES_MEMORY) {
			cmn_err(CE_NOTE, "parse_resources() "\
			    "Mem 32 0x%X, length: 0x%X",
			    mem32.Minimum, mem32.AddressLength);
		}
		return;
	}
	if (acpi_enum_debug & PARSE_RES_MEMORY) {
		cmn_err(CE_NOTE, "parse_resources() "\
		    "MEM32 Min Max not equal!");
		cmn_err(CE_NOTE, "parse_resources() "\
		    "Mem 32 Minimum 0x%X, Maximum: 0x%X",
		    mem32.Minimum, mem32.Maximum);
	}
}

static void
parse_resources_addr16(ACPI_RESOURCE *resource_ptr, struct regspec *io,
    int *io_count)
{
	ACPI_RESOURCE_ADDRESS16 addr16 =
	    resource_ptr->Data.Address16;

	if (addr16.AddressLength == 0)
		return;

	if (acpi_enum_debug & PARSE_RES_ADDRESS) {
		if (addr16.ResourceType == ACPI_MEMORY_RANGE) {
			cmn_err(CE_NOTE, "parse_resources() "\
			    "ADDRESS 16 MEMORY RANGE");
		} else
		if (addr16.ResourceType == ACPI_IO_RANGE) {
			cmn_err(CE_NOTE, "parse_resources() "\
			    "ADDRESS 16 IO RANGE");
		} else {
			cmn_err(CE_NOTE, "parse_resources() "\
			    "ADDRESS 16 OTHER");
		}
		cmn_err(CE_NOTE, "parse_resources() "\
		    "%s "\
		    "MinAddressFixed 0x%X, "\
		    "MaxAddressFixed 0x%X, "\
		    "Minimum 0x%X, "\
		    "Maximum 0x%X, "\
		    "length: 0x%X\n",
		    addr16.ProducerConsumer == ACPI_CONSUMER ?
		    "CONSUMER" : "PRODUCER",
		    addr16.MinAddressFixed,
		    addr16.MaxAddressFixed,
		    addr16.Minimum,
		    addr16.Maximum,
		    addr16.AddressLength);
	}
	if (addr16.ProducerConsumer == ACPI_PRODUCER ||
	    (addr16.ResourceType != ACPI_MEMORY_RANGE &&
	    addr16.ResourceType != ACPI_IO_RANGE)) {
		return;
	}
	if (addr16.AddressLength > 0) {
		if (addr16.ResourceType == ACPI_MEMORY_RANGE) {
			/* memory */
			io[*io_count].regspec_bustype = 0;
		} else {
			/* io */
			io[*io_count].regspec_bustype = 1;
		}
		io[*io_count].regspec_addr = addr16.Minimum;
		io[*io_count].regspec_size = addr16.AddressLength;
		(*io_count)++;
	}
}

static void
parse_resources_addr32(ACPI_RESOURCE *resource_ptr, struct regspec *io,
    int *io_count)
{
	ACPI_RESOURCE_ADDRESS32 addr32 =
	    resource_ptr->Data.Address32;

	if (addr32.AddressLength == 0)
		return;

	if (acpi_enum_debug & PARSE_RES_ADDRESS) {
		if (addr32.ResourceType == ACPI_MEMORY_RANGE) {
			cmn_err(CE_NOTE, "parse_resources() "\
			    "ADDRESS 32 MEMORY RANGE");
		} else
		if (addr32.ResourceType == ACPI_IO_RANGE) {
			cmn_err(CE_NOTE, "parse_resources() "\
			    "ADDRESS 32 IO RANGE");
		} else {
			cmn_err(CE_NOTE, "parse_resources() "\
			    "ADDRESS 32 OTHER");
		}
		cmn_err(CE_NOTE, "parse_resources() "\
		    "%s "\
		    "MinAddressFixed 0x%X, "\
		    "MaxAddressFixed 0x%X, "\
		    "Minimum 0x%X, "\
		    "Maximum 0x%X, "\
		    "length: 0x%X\n",
		    addr32.ProducerConsumer == ACPI_CONSUMER ?
		    "CONSUMER" : "PRODUCER",
		    addr32.MinAddressFixed,
		    addr32.MaxAddressFixed,
		    addr32.Minimum,
		    addr32.Maximum,
		    addr32.AddressLength);
	}
	if (addr32.ProducerConsumer == ACPI_PRODUCER ||
	    (addr32.ResourceType != ACPI_MEMORY_RANGE &&
	    addr32.ResourceType != ACPI_IO_RANGE)) {
		return;
	}
	if (addr32.AddressLength > 0) {
		if (addr32.ResourceType == ACPI_MEMORY_RANGE) {
			/* memory */
			io[*io_count].regspec_bustype = 0;
		} else {
			/* io */
			io[*io_count].regspec_bustype = 1;
		}
		io[*io_count].regspec_addr = addr32.Minimum;
		io[*io_count].regspec_size = addr32.AddressLength;
		(*io_count)++;
	}
}

static void
parse_resources_addr64(ACPI_RESOURCE *resource_ptr, struct regspec *io,
    int *io_count)
{
	ACPI_RESOURCE_ADDRESS64 addr64 =
	    resource_ptr->Data.Address64;

	if (addr64.AddressLength == 0)
		return;

	if (acpi_enum_debug & PARSE_RES_ADDRESS) {
		if (addr64.ResourceType == ACPI_MEMORY_RANGE) {
			cmn_err(CE_NOTE, "parse_resources() "\
			    "ADDRESS 64 MEMORY RANGE");
		} else
		if (addr64.ResourceType == ACPI_IO_RANGE) {
			cmn_err(CE_NOTE, "parse_resources() "\
			    "ADDRESS 64 IO RANGE");
		} else {
			cmn_err(CE_NOTE, "parse_resources() "\
			    "ADDRESS 64 OTHER");
		}
#ifdef _LP64
		cmn_err(CE_NOTE, "parse_resources() "\
		    "%s "\
		    "MinAddressFixed 0x%X, "\
		    "MaxAddressFixed 0x%X, "\
		    "Minimum 0x%lX, "\
		    "Maximum 0x%lX, "\
		    "length: 0x%lX\n",
		    addr64.ProducerConsumer == ACPI_CONSUMER ?
		    "CONSUMER" : "PRODUCER",
		    addr64.MinAddressFixed,
		    addr64.MaxAddressFixed,
		    addr64.Minimum,
		    addr64.Maximum,
		    addr64.AddressLength);
#else
		cmn_err(CE_NOTE, "parse_resources() "\
		    "%s "\
		    "MinAddressFixed 0x%X, "\
		    "MaxAddressFixed 0x%X, "\
		    "Minimum 0x%llX, "\
		    "Maximum 0x%llX, "\
		    "length: 0x%llX\n",
		    addr64.ProducerConsumer == ACPI_CONSUMER ?
		    "CONSUMER" : "PRODUCER",
		    addr64.MinAddressFixed,
		    addr64.MaxAddressFixed,
		    addr64.Minimum,
		    addr64.Maximum,
		    addr64.AddressLength);
#endif
	}
	if (addr64.ProducerConsumer == ACPI_PRODUCER ||
	    (addr64.ResourceType != ACPI_MEMORY_RANGE &&
	    addr64.ResourceType != ACPI_IO_RANGE)) {
		return;
	}
	if (addr64.AddressLength > 0) {
		if (addr64.ResourceType == ACPI_MEMORY_RANGE) {
			/* memory */
			io[*io_count].regspec_bustype = 0;
		} else {
			/* io */
			io[*io_count].regspec_bustype = 1;
		}
		io[*io_count].regspec_addr = addr64.Minimum;
		io[*io_count].regspec_size = addr64.AddressLength;
		(*io_count)++;
	}
}

static ACPI_STATUS
parse_resources(ACPI_HANDLE handle, dev_info_t *xdip)
{
	ACPI_BUFFER	buf;
	ACPI_RESOURCE	*resource_ptr;
	ACPI_STATUS	status;
	char		*current_ptr, *last_ptr;
	struct		regspec *io;
	int		io_count = 0, interrupt_count = 0, dma_count = 0;
	int		i;

	buf.Length = ACPI_ALLOCATE_BUFFER;
	status = AcpiGetCurrentResources(handle, &buf);
	if (status != AE_OK) {
		return (status);
	}
	io = (struct regspec *)kmem_zalloc(sizeof (struct regspec) *
	    MAX_PARSED_ACPI_RESOURCES, KM_SLEEP);
	current_ptr = buf.Pointer;
	last_ptr = (char *)buf.Pointer + buf.Length;
	while (current_ptr < last_ptr) {
		if (io_count >= MAX_PARSED_ACPI_RESOURCES) {
			break;
		}
		resource_ptr = (ACPI_RESOURCE *)current_ptr;
		current_ptr += resource_ptr->Length;
		switch (resource_ptr->Type) {
		case ACPI_RESOURCE_TYPE_END_TAG:
			current_ptr = last_ptr;
			break;
		case ACPI_RESOURCE_TYPE_IO:
			parse_resources_io(resource_ptr, io, &io_count);
			break;
		case ACPI_RESOURCE_TYPE_FIXED_IO:
			parse_resources_fixed_io(resource_ptr, io, &io_count);
			break;
		case ACPI_RESOURCE_TYPE_FIXED_MEMORY32:
			parse_resources_fixed_mem32(resource_ptr, io,
			    &io_count);
			break;
		case ACPI_RESOURCE_TYPE_MEMORY32:
			parse_resources_mem32(resource_ptr, io, &io_count);
			break;
		case ACPI_RESOURCE_TYPE_ADDRESS16:
			parse_resources_addr16(resource_ptr, io, &io_count);
			break;
		case ACPI_RESOURCE_TYPE_ADDRESS32:
			parse_resources_addr32(resource_ptr, io, &io_count);
			break;
		case ACPI_RESOURCE_TYPE_ADDRESS64:
			parse_resources_addr64(resource_ptr, io, &io_count);
			break;
		case ACPI_RESOURCE_TYPE_IRQ:
			parse_resources_irq(resource_ptr, &interrupt_count);
			break;
		case ACPI_RESOURCE_TYPE_DMA:
			parse_resources_dma(resource_ptr, &dma_count);
			break;
		case ACPI_RESOURCE_TYPE_START_DEPENDENT:
			cmn_err(CE_NOTE,
			    "!ACPI source type"
			    " ACPI_RESOURCE_TYPE_START_DEPENDENT"
			    " not supported");
			break;
		case ACPI_RESOURCE_TYPE_END_DEPENDENT:
			cmn_err(CE_NOTE,
			    "!ACPI source type"
			    " ACPI_RESOURCE_TYPE_END_DEPENDENT"
			    " not supported");
			break;
		case ACPI_RESOURCE_TYPE_VENDOR:
			cmn_err(CE_NOTE,
			    "!ACPI source type"
			    " ACPI_RESOURCE_TYPE_VENDOR"
			    " not supported");
			break;
		case ACPI_RESOURCE_TYPE_MEMORY24:
			cmn_err(CE_NOTE,
			    "!ACPI source type"
			    " ACPI_RESOURCE_TYPE_MEMORY24"
			    " not supported");
			break;
		case ACPI_RESOURCE_TYPE_EXTENDED_IRQ:
			cmn_err(CE_NOTE,
			    "!ACPI source type"
			    " ACPI_RESOURCE_TYPE_EXT_IRQ"
			    " not supported");
			break;
		default:
		/* Some types are not yet implemented (See CA 6.4) */
			cmn_err(CE_NOTE,
			    "!ACPI resource type (0X%X) not yet supported",
			    resource_ptr->Type);
			break;
		}
	}

	if (io_count) {
		/*
		 * on LX50, you get interrupts of mouse and keyboard
		 * from separate PNP id...
		 */
		if (io_count == 2) {
			if ((io[0].regspec_addr == 0x60 &&
			    io[1].regspec_addr == 0x64) ||
			    (io[0].regspec_addr == 0x64 &&
			    io[1].regspec_addr == 0x60)) {
				interrupt[0] = 0x1;
				interrupt[1] = 0xc;
				interrupt_count = 2;
				used_interrupts |=
				    1 << interrupt[0];
				used_interrupts |=
				    1 << interrupt[1];
			}
		}
		add_used_io_mem(io, io_count);
		if (xdip != NULL) {
			(void) ndi_prop_update_int_array(DDI_DEV_T_NONE, xdip,
			    "reg", (int *)io, 3*io_count);
		}
	}
	if (interrupt_count && (xdip != NULL)) {
		(void) ndi_prop_update_int_array(DDI_DEV_T_NONE, xdip,
		    "interrupts", (int *)interrupt, interrupt_count);
	}
	if (dma_count && (xdip != NULL)) {
		(void) ndi_prop_update_int_array(DDI_DEV_T_NONE, xdip,
		    "dma-channels", (int *)dma, dma_count);
	}
	AcpiOsFree(buf.Pointer);
	kmem_free(io, sizeof (struct regspec) * MAX_PARSED_ACPI_RESOURCES);
	return (status);
}

/* keyboard mouse is under i8042, everything else under isa */
static dev_info_t *
get_bus_dip(char *nodename, dev_info_t *isa_dip)
{
	static dev_info_t *i8042_dip = NULL;
	struct regspec i8042_regs[] = {
		{1, 0x60, 0x1},
		{1, 0x64, 0x1}
	};
	int i8042_intrs[] = {0x1, 0xc};

	if (strcmp(nodename, keyboard_alias) != 0 &&
	    strcmp(nodename, mouse_alias) != 0)
		return (isa_dip);

	if (i8042_dip)
		return (i8042_dip);

	ndi_devi_alloc_sleep(isa_dip, "i8042", (pnode_t)DEVI_SID_NODEID,
	    &i8042_dip);
	(void) ndi_prop_update_int_array(DDI_DEV_T_NONE, i8042_dip,
	    "reg", (int *)i8042_regs, 6);
	(void) ndi_prop_update_int_array(DDI_DEV_T_NONE, i8042_dip,
	    "interrupts", (int *)i8042_intrs, 2);
	(void) ndi_prop_update_string(DDI_DEV_T_NONE, i8042_dip,
	    "unit-address", "1,60");
	(void) ndi_devi_bind_driver(i8042_dip, 0);
	return (i8042_dip);
}

/*
 * put content of properties (if any) to dev info tree at branch xdip
 *
 */
static void
process_properties(dev_info_t *xdip, char *properties)
{
	char *tmp, *value, *org1;

	if (properties == NULL) {
		return; /* nothing to add */
	}
	org1 = tmp = strchr(properties, '=');
	if (tmp == NULL) {
		cmn_err(CE_WARN, "!master_ops: incorrect properties: %s\n",
		    properties);
		return; /* don't know how to process this */
	}
	*tmp = '\0';
	tmp++;
	if (*tmp == '"') {
		tmp++;
	}
	value = tmp;
	tmp = strchr(value, '"');
	if (tmp != NULL) {
		*tmp = '\0';
	}
	(void) ndi_prop_update_string(DDI_DEV_T_NONE, xdip, properties, value);
	/* put back original value to avoid kmem corruption */
	if (org1 != NULL) {
		*org1 = '=';
	}
	if (tmp != NULL) {
		*tmp = '"';
	}
}

void
eisa_to_str(ACPI_INTEGER id, char *np)
{
	static const char hextab[] = "0123456789ABCDEF";

	/*
	 *  Expand an EISA device name:
	 *
	 * This is the inverse of the above routine.  It converts a 32-bit EISA
	 * device "id" to a 7-byte ASCII device name, which is stored at "np".
	 */

	*np++ = '@' + ((id >> 2)  & 0x1F);
	*np++ = '@' + ((id << 3)  & 0x18) + ((id >> 13) & 0x07);
	*np++ = '@' + ((id >> 8)  & 0x1F);
	*np++ = hextab[(id >> 20) & 0x0F];
	*np++ = hextab[(id >> 16) & 0x0F];
	*np++ = hextab[(id >> 28) & 0x0F];
	*np++ = hextab[(id >> 24) & 0x0F];
	*np = 0;
}

/*
 * process_cids() -- process multiple CIDs in a package
 */
static void
process_cids(ACPI_OBJECT *rv, char **cidstr, int *cidstr_size)
{
	char *tmp_cidstr;
	int i;

	*cidstr_size = 0;
	*cidstr = NULL;
	if ((rv->Package.Count == 0) || rv->Package.Elements == NULL) {
		return; /* empty package */
	}

	/* figure out the total cid size needed */
	for (i = 0; i < rv->Package.Count; i++) {
		/* total up all CIDs size */
		ACPI_OBJECT obj = rv->Package.Elements[i];
		switch (obj.Type) {
		case ACPI_TYPE_INTEGER:
			*cidstr_size += EISA_ID_SIZE + 1;
			break;
		case ACPI_TYPE_STRING:
			*cidstr_size += obj.String.Length + 1;
			break;
		default:
			break;
		}
	}
	*cidstr = kmem_zalloc(*cidstr_size, KM_SLEEP);
	tmp_cidstr = *cidstr;
	for (i = 0; i < rv->Package.Count; i++) {
		/* get the actual acpi_object */
		ACPI_OBJECT obj = rv->Package.Elements[i];
		switch (obj.Type) {
		case ACPI_TYPE_INTEGER:
			eisa_to_str(obj.Integer.Value, tmp_cidstr);
			if (acpi_enum_debug & PROCESS_CIDS) {
				cmn_err(CE_NOTE, "integer CID: %s", tmp_cidstr);
			}
			tmp_cidstr += EISA_ID_SIZE + 1;
			break;
		case ACPI_TYPE_STRING:
			(void) strcpy(tmp_cidstr, obj.String.Pointer);
			if (acpi_enum_debug & PROCESS_CIDS) {
				cmn_err(CE_NOTE, "string CID: %s", tmp_cidstr);
			}
			tmp_cidstr += strlen(obj.String.Pointer) + 1;
			break;
		default:
			if (acpi_enum_debug & PROCESS_CIDS) {
				cmn_err(CE_NOTE, "unexpected CID type: %d",
				    obj.Type);
			}
			break;
		}
	}
	if (acpi_enum_debug & PROCESS_CIDS) {
		cmn_err(CE_NOTE, "total CIDs: %d", rv->Package.Count);
	}
}

/*
 * isa_acpi_callback()
 */
static ACPI_STATUS
isa_acpi_callback(ACPI_HANDLE ObjHandle, uint32_t NestingLevel, void *a,
    void **b)
{
	_NOTE(ARGUNUSED(NestingLevel, b))

	ACPI_BUFFER	rb;
	ACPI_DEVICE_INFO *info = NULL;
	char		*path = NULL;
	char 		*devname = NULL;
	char		*hidstr = NULL;
	char		*cidstr = NULL;
	int		cidstr_size = 0;
	char		*description = NULL;
	char		*properties = NULL;
	dev_info_t	*dip = (dev_info_t *)a;
	dev_info_t	*xdip = NULL;

	/*
	 * get full ACPI pathname for object
	 */
	rb.Length = ACPI_ALLOCATE_BUFFER;
	rb.Pointer = NULL;
	if (AcpiGetName(ObjHandle, ACPI_FULL_PATHNAME, &rb) != AE_OK) {
		cmn_err(CE_WARN, "!acpi_enum: could not get pathname");
		goto done;
	}
	path = (char *)rb.Pointer;

	/*
	 * Get device info object
	 */
	rb.Length = ACPI_ALLOCATE_BUFFER;
	rb.Pointer = NULL;
	if (AcpiGetObjectInfo(ObjHandle, &rb) != AE_OK) {
		cmn_err(CE_WARN, "!acpi_enum: could not get device"
		    " info for %s", path);
		goto done;
	}
	info = (ACPI_DEVICE_INFO *)rb.Pointer;

	/*
	 * If device isn't present, we don't enumerate
	 * NEEDSWORK: what about docking bays and the like?
	 */
	if (info->Valid & ACPI_VALID_STA) {
		/*
		 * CA 6.3.6 _STA method
		 * Bit 0 -- device is present
		 * Bit 1 -- device is enabled
		 * Bit 2 -- device is shown in UI
		 */
		if (!((info->CurrentStatus & 0x7) == 7)) {
			goto done;
		}
	} else {
		cmn_err(CE_WARN, "!acpi_enum: no _STA for %s", path);
		goto done;
	}

	/*
	 * Keep track of _HID value
	 */
	if (!(info->Valid & ACPI_VALID_HID)) {
		/* No _HID, we skip this node */
		goto done;
	}
	hidstr = info->HardwareId.Value;

	/*
	 * Attempt to get _CID value
	 */
	rb.Length = ACPI_ALLOCATE_BUFFER;
	rb.Pointer = NULL;
	if (AcpiEvaluateObject(ObjHandle, "_CID", NULL, &rb) == AE_OK &&
	    rb.Length != 0) {
		ACPI_OBJECT *rv = rb.Pointer;

		switch (rv->Type) {
		case ACPI_TYPE_INTEGER:
			cidstr_size = 8;
			cidstr = kmem_zalloc(cidstr_size, KM_SLEEP);
			eisa_to_str(rv->Integer.Value, cidstr);
			break;
		case ACPI_TYPE_STRING:
			cidstr_size = strlen(rv->String.Pointer) + 1;
			cidstr = kmem_zalloc(cidstr_size, KM_SLEEP);
			(void) strcpy(cidstr, rv->String.Pointer);
			break;
		case ACPI_TYPE_PACKAGE:
			process_cids(rv, &cidstr, &cidstr_size);
			break;
		default:
			break;
		}
		AcpiOsFree(rb.Pointer);
	}


	/*
	 * Note carefully: expressions are evaluated left to right, so
	 * this first checks for _HID and then for _CID match
	 */
	if (master_file_lookup(hidstr, &devname, &description, &properties) ||
	    master_file_lookups(cidstr, &devname, &description, &properties,
	    cidstr_size)) {
		/* PNP description found in master table */
		if (!(strncmp(hidstr, "ACPI", 4))) {
			dip = ddi_root_node();
		} else {
			dip = get_bus_dip(devname, dip);
		}
		ndi_devi_alloc_sleep(dip, devname,
		    (pnode_t)DEVI_SID_NODEID, &xdip);
		(void) ndi_prop_update_string(DDI_DEV_T_NONE, xdip,
		    "compatible", hidstr);
		(void) ndi_prop_update_string(DDI_DEV_T_NONE, xdip,
		    "model", description);
		(void) process_properties(xdip, properties);
	} else {
		/* for ISA devices not known to the master file */
		if (!(strncmp(hidstr, "PNP03", 5))) {
			/* a keyboard device includes PNP03xx */
			dip = get_bus_dip(keyboard_alias, dip);
			ndi_devi_alloc_sleep(dip, keyboard_alias,
			    (pnode_t)DEVI_SID_NODEID, &xdip);
			(void) ndi_prop_update_string(DDI_DEV_T_NONE, xdip,
			    "compatible", "pnpPNP,303");
			(void) ndi_prop_update_string(DDI_DEV_T_NONE, xdip,
			    "model", "PNP03xx keyboard");
		} else {
			if (!(strncmp(hidstr, "PNP0F", 5))) {
				/* a mouse device include PNP0Fxx */
				dip = get_bus_dip(mouse_alias, dip);
				ndi_devi_alloc_sleep(dip, mouse_alias,
				    (pnode_t)DEVI_SID_NODEID, &xdip);
				(void) ndi_prop_update_string(DDI_DEV_T_NONE,
				    xdip, "compatible", "pnpPNP,f03");
				(void) ndi_prop_update_string(DDI_DEV_T_NONE,
				    xdip, "model", "PNP0Fxx mouse");
			} else {
				if (acpi_enum_debug & DEVICES_NOT_ENUMED) {
					cmn_err(CE_WARN,
					    "Not enum HID(%s), CID(%s)\n",
					    hidstr, cidstr);
				}
				(void) parse_resources(ObjHandle, xdip);
				goto done;
			}
		}
	}
	if (acpi_enum_debug & MASTER_LOOKUP_DEBUG) {
		cmn_err(CE_NOTE, "ACPI devname=(%s), HID(%s), CID(%s)\n",
		    devname, hidstr, cidstr);
		cmn_err(CE_NOTE, "description=(%s) properties=(%s)\n",
		    description, properties);
	}
	(void) ndi_prop_update_string(DDI_DEV_T_NONE, xdip, "acpi-namespace",
	    path);
	if (cidstr) {
		char *cids[ACPI_ELEMENT_PACKAGE_LIMIT];
		char *t = cidstr;
		int i = 0;
		while (t < (cidstr + cidstr_size)) {
			if (*t == NULL) {
				t++;
				continue;
			}
			cids[i++] = t;
			t += strlen(t);
		}
		(void) ndi_prop_update_string_array(DDI_DEV_T_NONE, xdip,
		    "_CID", (char **)cids, i);
	}

	(void) parse_resources(ObjHandle, xdip);

	/* Special processing for mouse and keyboard devices per IEEE 1275 */
	/* if master entry doesn't contain "compatible" then we add default */
	if (strcmp(devname, keyboard_alias) == 0) {
		(void) ndi_prop_update_int(DDI_DEV_T_NONE, xdip, "reg", 0);
		(void) ndi_prop_update_string(DDI_DEV_T_NONE, xdip,
		    "device-type", keyboard_alias);
		if (strncmp(properties, "compatible", 10)) {
			(void) ndi_prop_update_string(DDI_DEV_T_NONE, xdip,
			    "compatible", "pnpPNP,303");
		}
	} else if (strcmp(devname, mouse_alias) == 0) {
		(void) ndi_prop_update_int(DDI_DEV_T_NONE, xdip, "reg", 1);
		(void) ndi_prop_update_string(DDI_DEV_T_NONE, xdip,
		    "device-type", mouse_alias);
		if (strncmp(properties, "compatible", 10)) {
			(void) ndi_prop_update_string(DDI_DEV_T_NONE, xdip,
			    "compatible", "pnpPNP,f03");
		}
	}

	(void) ndi_devi_bind_driver(xdip, 0);

done:
	if (path != NULL)
		AcpiOsFree(path);
	if (info != NULL)
		AcpiOsFree(info);
	if (cidstr != NULL)
		kmem_free(cidstr, cidstr_size);
	if (devname != NULL)
		kmem_free(devname, strlen(devname) + 1);
	if (description != NULL)
		kmem_free(description, strlen(description) + 1);
	if (properties != NULL)
		kmem_free(properties, strlen(properties) + 1);

	return (AE_OK);
}

static void
used_res_interrupts(void)
{
	int intr[ACPI_ISA_LIMIT];
	int count = 0;
	int i;

	for (i = 0; i < ACPI_ISA_LIMIT; i++) {
		if ((used_interrupts >> i) & 1) {
			intr[count++] = i;
		}
	}
	(void) ndi_prop_update_int_array(DDI_DEV_T_NONE, usedrdip,
	    "interrupts", (int *)intr, count);
}

static void
used_res_dmas(void)
{
	int dma[ACPI_ISA_LIMIT];
	int count = 0;
	int i;

	for (i = 0; i < ACPI_ISA_LIMIT; i++) {
		if ((used_dmas >> i) & 1) {
			dma[count++] = i;
		}
	}
	(void) ndi_prop_update_int_array(DDI_DEV_T_NONE, usedrdip,
	    "dma-channels", (int *)dma, count);
}

static void
used_res_io_mem(char *nodename, int *count, used_io_mem_t **head)
{
	int *io;
	used_io_mem_t *used = *head;
	int i;

	*count *= 2;
	io = (int *)kmem_zalloc(sizeof (int)*(*count), KM_SLEEP);
	for (i = 0; i < *count; i += 2) {
		used_io_mem_t *prev;
		if (used != NULL) {
			io[i] = used->start_addr;
			io[i+1] = used->length;
			prev = used;
			used = used->next;
			kmem_free(prev, sizeof (used_io_mem_t));
		}
	}
	(void) ndi_prop_update_int_array(DDI_DEV_T_NONE, usedrdip,
	    nodename, (int *)io, *count);
	kmem_free(io, sizeof (int)*(*count));
	*head = NULL;
}

/*
 * acpi_isa_device_enum() -- call from isa nexus driver
 * returns 1 if deviced enumeration is successful
 *         0 if deviced enumeration fails
 */
int
acpi_isa_device_enum(dev_info_t *isa_dip)
{
	char *acpi_prop;

	if (ddi_prop_lookup_string(DDI_DEV_T_ANY, ddi_root_node(),
	    DDI_PROP_DONTPASS, ACPI_ENUM_DEBUG, &acpi_prop) ==
	    DDI_PROP_SUCCESS) {
		long data;
		if (ddi_strtol(acpi_prop, NULL, 0, &data) == 0) {
			acpi_enum_debug = (unsigned long)data;
			e_ddi_prop_remove(DDI_DEV_T_NONE, ddi_root_node(),
			    ACPI_ENUM_DEBUG);
			e_ddi_prop_update_int(DDI_DEV_T_NONE,
			    ddi_root_node(), ACPI_ENUM_DEBUG, data);
		}
		ddi_prop_free(acpi_prop);
	}

	if (acpi_enum_debug & ISA_DEVICE_ENUM) {
		cmn_err(CE_NOTE, "acpi_isa_device_enum() called");
	}

	if (acpica_init() != AE_OK) {
		cmn_err(CE_WARN, "!isa_enum: init failed");
		/* Note, pickup by i8042 nexus */
		(void) e_ddi_prop_update_string(DDI_DEV_T_NONE,
		    ddi_root_node(), "acpi-enum", "off");
		return (0);
	}

	usedrdip = ddi_find_devinfo(USED_RESOURCES, -1, 0);
	if (usedrdip == NULL) {
		ndi_devi_alloc_sleep(ddi_root_node(), USED_RESOURCES,
		    (pnode_t)DEVI_SID_NODEID, &usedrdip);

	}

	process_master_file();

	/*
	 * Do the actual enumeration
	 */
	(void) AcpiGetDevices(NULL, isa_acpi_callback, isa_dip, 0);

	free_master_data();
	used_res_interrupts();
	used_res_dmas();
	used_res_io_mem("device-memory", &used_mem_count, &used_mem_head);
	used_res_io_mem("io-space", &used_io_count, &used_io_head);
	(void) ndi_devi_bind_driver(usedrdip, 0);

	return (1);
}
