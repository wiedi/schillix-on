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
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_VDSK_COMMON_H
#define	_VDSK_COMMON_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * This header file contains the private LDoms Virtual Disk (vDisk) definitions
 * common to both the server (vds) and the client (vdc)
 */

#include <sys/efi_partition.h>
#include <sys/machparam.h>
#include <sys/vtoc.h>

#include <sys/ldc.h>
#include <sys/vio_common.h>
#include <sys/vio_mailbox.h>

/*
 * vDisk definitions
 */

/*
 * The number of Descriptor Ring entries
 *
 * Constraints:
 * 	- overall DRing size must be greater than 8K (MMU_PAGESIZE)
 *	- overall DRing size should be 8K aligned (desirable but not enforced)
 *	- DRing entry must be 8 byte aligned
 */
#define	VD_DRING_LEN		512

/*
 *
 */
#define	VD_DRING_ENTRY_SZ	(sizeof (vd_dring_entry_t) + 		\
		(sizeof (ldc_mem_cookie_t) * (VD_MAX_COOKIES - 1)))

/*
 * The maximum block size we can transmit using one Descriptor Ring entry
 *
 * Currently no FS uses more than 128K and it doesn't look like they
 * will either as there is no perf gain to be had by larger values.
 * ( see ZFS comment at definition of SPA_MAXBLOCKSIZE ).
 *
 * We choose 256K to give us some headroom.
 */
#define	VD_MAX_BLOCK_SIZE	(256 * 1024)

#define	VD_MAX_COOKIES		((VD_MAX_BLOCK_SIZE / PAGESIZE) + 1)
#define	VD_USEC_TIMEOUT		20000
#define	VD_LDC_IDS_PROP		"ldc-ids"
#define	VD_LDC_MTU		256

/*
 * Flags used by ioctl routines to indicate if a copyin/copyout is needed
 */
#define	VD_COPYOUT		0x1
#define	VD_COPYIN		0x2

/*
 * vDisk operations on physical devices
 */
#define	VD_OP_BREAD		0x01	/* Block Read */
#define	VD_OP_BWRITE		0x02	/* Block Write */
#define	VD_OP_FLUSH		0x03	/* Flush disk write cache contents */
#define	VD_OP_GET_WCE		0x04	/* Get disk W$ status */
#define	VD_OP_SET_WCE		0x05	/* Enable/Disable disk W$ */
#define	VD_OP_GET_VTOC		0x06	/* Get VTOC */
#define	VD_OP_SET_VTOC		0x07	/* Set VTOC */
#define	VD_OP_GET_DISKGEOM	0x08	/* Get disk geometry */
#define	VD_OP_SET_DISKGEOM	0x09	/* Set disk geometry */
#define	VD_OP_SCSICMD		0x0a	/* SCSI control command */
#define	VD_OP_GET_DEVID		0x0b	/* Get device id */
#define	VD_OP_GET_EFI 		0x0c	/* Get EFI */
#define	VD_OP_SET_EFI 		0x0d	/* Set EFI */
#define	VD_OP_MASK		0xFF	/* mask of all possible operations */
#define	VD_OP_COUNT		13	/* Number of operations */

/*
 * EFI disks do not have a slice 7. Actually that slice is used to represent
 * the whole disk.
 */
#define	VD_EFI_WD_SLICE	7

/*
 * Definitions of the various ways vds can export disk support to vdc.
 */
typedef enum vd_disk_type {
	VD_DISK_TYPE_UNK = 0,		/* Unknown device type */
	VD_DISK_TYPE_SLICE,		/* slice in block device */
	VD_DISK_TYPE_DISK		/* entire disk (slice 2) */
} vd_disk_type_t;

/*
 * Definitions of the various disk label that vDisk supports.
 */
typedef enum vd_disk_label {
	VD_DISK_LABEL_UNK = 0,		/* Unknown disk label */
	VD_DISK_LABEL_VTOC,		/* VTOC disk label */
	VD_DISK_LABEL_EFI		/* EFI disk label */
} vd_disk_label_t;

/*
 * vDisk Descriptor payload
 */
typedef struct vd_dring_payload {
	uint64_t	req_id;		/* The request ID being processed */
	uint8_t		operation;	/* operation for server to perform */
	uint8_t		slice;		/* The disk slice being accessed */
	uint16_t	resv1;		/* padding */
	uint32_t	status;		/* "errno" of server operation */
	uint64_t	addr;		/* LP64	diskaddr_t (block I/O) */
	uint64_t	nbytes;		/* LP64 size_t */
	uint32_t	ncookies;	/* Number of cookies used */
	uint32_t	resv2;		/* padding */

	ldc_mem_cookie_t	cookie[1];	/* variable sized array */
} vd_dring_payload_t;


/*
 * vDisk Descriptor entry
 */
typedef struct vd_dring_entry {
	vio_dring_entry_hdr_t		hdr;		/* common header */
	vd_dring_payload_t		payload;	/* disk specific data */
} vd_dring_entry_t;


/*
 * vDisk control operation structures
 */

/*
 * vDisk geometry definition (VD_OP_GET_DISKGEOM and VD_OP_SET_DISKGEOM)
 */
typedef struct vd_geom {
	uint16_t	ncyl;		/* number of data cylinders */
	uint16_t	acyl;		/* number of alternate cylinders */
	uint16_t	bcyl;		/* cyl offset for fixed head area */
	uint16_t	nhead;		/* number of heads */
	uint16_t	nsect;		/* number of data sectors per track */
	uint16_t	intrlv;		/* interleave factor */
	uint16_t	apc;		/* alternates per cyl (SCSI only) */
	uint16_t	rpm;		/* revolutions per minute */
	uint16_t	pcyl;		/* number of physical cylinders */
	uint16_t	write_reinstruct;	/* # sectors to skip, writes */
	uint16_t	read_reinstruct;	/* # sectors to skip, reads */
} vd_geom_t;


/*
 * vDisk partition definition
 */
typedef struct vd_partition {
	uint16_t	id_tag;		/* ID tag of partition */
	uint16_t	perm;		/* permission flags for partition */
	uint32_t	reserved;	/* padding */
	uint64_t	start;		/* block number of partition start */
	uint64_t	nblocks;	/* number of blocks in partition */
} vd_partition_t;

/*
 * vDisk VTOC definition (VD_OP_GET_VTOC and VD_OP_SET_VTOC)
 */
#define	VD_VOLNAME_LEN		8	/* length of volume_name field */
#define	VD_ASCIILABEL_LEN	128	/* length of ascii_label field */
typedef struct vd_vtoc {
	char		volume_name[VD_VOLNAME_LEN];	/* volume name */
	uint16_t	sector_size;		/* sector size in bytes */
	uint16_t	num_partitions;		/* number of partitions */
	char		ascii_label[VD_ASCIILABEL_LEN];	/* ASCII label */
	vd_partition_t	partition[V_NUMPAR];	/* partition headers */
} vd_vtoc_t;


/*
 * vDisk EFI definition (VD_OP_GET_EFI and VD_OP_SET_EFI)
 */
typedef struct vd_efi {
	uint64_t	lba;		/* lba of the request */
	uint64_t	length;		/* length of data */
	char		data[1];	/* data of the request */
} vd_efi_t;


/*
 * vDisk DEVID definition (VD_OP_GET_DEVID)
 */
#define	VD_DEVID_SIZE(l)	(sizeof (vd_devid_t) - 1 + l)
#define	VD_DEVID_DEFAULT_LEN	128

typedef struct vd_devid {
	uint16_t	reserved;	/* padding */
	uint16_t	type;		/* type of device id */
	uint32_t	length;		/* length the device id */
	char		id[1];		/* device id */
} vd_devid_t;

/*
 * Copy the contents of a vd_geom_t to the contents of a dk_geom struct
 */
#define	VD_GEOM2DK_GEOM(vd_geom, dk_geom)				\
{									\
	bzero((dk_geom), sizeof (*(dk_geom)));				\
	(dk_geom)->dkg_ncyl		= (vd_geom)->ncyl;		\
	(dk_geom)->dkg_acyl		= (vd_geom)->acyl;		\
	(dk_geom)->dkg_bcyl		= (vd_geom)->bcyl;		\
	(dk_geom)->dkg_nhead		= (vd_geom)->nhead;		\
	(dk_geom)->dkg_nsect		= (vd_geom)->nsect;		\
	(dk_geom)->dkg_intrlv		= (vd_geom)->intrlv;		\
	(dk_geom)->dkg_apc		= (vd_geom)->apc;		\
	(dk_geom)->dkg_rpm		= (vd_geom)->rpm;		\
	(dk_geom)->dkg_pcyl		= (vd_geom)->pcyl;		\
	(dk_geom)->dkg_write_reinstruct	= (vd_geom)->write_reinstruct;	\
	(dk_geom)->dkg_read_reinstruct	= (vd_geom)->read_reinstruct;	\
}

/*
 * Copy the contents of a vd_vtoc_t to the contents of a vtoc struct
 */
#define	VD_VTOC2VTOC(vd_vtoc, vtoc)					\
{									\
	bzero((vtoc), sizeof (*(vtoc)));				\
	bcopy((vd_vtoc)->volume_name, (vtoc)->v_volume,			\
	    MIN(sizeof ((vd_vtoc)->volume_name),			\
		sizeof ((vtoc)->v_volume)));				\
	bcopy((vd_vtoc)->ascii_label, (vtoc)->v_asciilabel,		\
	    MIN(sizeof ((vd_vtoc)->ascii_label),			\
		sizeof ((vtoc)->v_asciilabel)));			\
	(vtoc)->v_sanity	= VTOC_SANE;				\
	(vtoc)->v_version	= V_VERSION;				\
	(vtoc)->v_sectorsz	= (vd_vtoc)->sector_size;		\
	(vtoc)->v_nparts	= (vd_vtoc)->num_partitions;		\
	for (int i = 0; i < (vd_vtoc)->num_partitions; i++) {		\
		(vtoc)->v_part[i].p_tag	= (vd_vtoc)->partition[i].id_tag; \
		(vtoc)->v_part[i].p_flag = (vd_vtoc)->partition[i].perm; \
		(vtoc)->v_part[i].p_start = (vd_vtoc)->partition[i].start; \
		(vtoc)->v_part[i].p_size = (vd_vtoc)->partition[i].nblocks; \
	}								\
}

/*
 * Copy the contents of a dk_geom struct to the contents of a vd_geom_t
 */
#define	DK_GEOM2VD_GEOM(dk_geom, vd_geom)				\
{									\
	bzero((vd_geom), sizeof (*(vd_geom)));				\
	(vd_geom)->ncyl			= (dk_geom)->dkg_ncyl;		\
	(vd_geom)->acyl			= (dk_geom)->dkg_acyl;		\
	(vd_geom)->bcyl			= (dk_geom)->dkg_bcyl;		\
	(vd_geom)->nhead		= (dk_geom)->dkg_nhead;		\
	(vd_geom)->nsect		= (dk_geom)->dkg_nsect;		\
	(vd_geom)->intrlv		= (dk_geom)->dkg_intrlv;	\
	(vd_geom)->apc			= (dk_geom)->dkg_apc;		\
	(vd_geom)->rpm			= (dk_geom)->dkg_rpm;		\
	(vd_geom)->pcyl			= (dk_geom)->dkg_pcyl;		\
	(vd_geom)->write_reinstruct	= (dk_geom)->dkg_write_reinstruct; \
	(vd_geom)->read_reinstruct	= (dk_geom)->dkg_read_reinstruct; \
}

/*
 * Copy the contents of a vtoc struct to the contents of a vd_vtoc_t
 */
#define	VTOC2VD_VTOC(vtoc, vd_vtoc)					\
{									\
	bzero((vd_vtoc), sizeof (*(vd_vtoc)));				\
	bcopy((vtoc)->v_volume, (vd_vtoc)->volume_name,			\
	    MIN(sizeof ((vtoc)->v_volume),				\
		sizeof ((vd_vtoc)->volume_name)));			\
	bcopy((vtoc)->v_asciilabel, (vd_vtoc)->ascii_label,		\
	    MIN(sizeof ((vtoc)->v_asciilabel),				\
		sizeof ((vd_vtoc)->ascii_label)));			\
	(vd_vtoc)->sector_size			= (vtoc)->v_sectorsz;	\
	(vd_vtoc)->num_partitions		= (vtoc)->v_nparts;	\
	for (int i = 0; i < (vtoc)->v_nparts; i++) {			\
		(vd_vtoc)->partition[i].id_tag	= (vtoc)->v_part[i].p_tag; \
		(vd_vtoc)->partition[i].perm	= (vtoc)->v_part[i].p_flag; \
		(vd_vtoc)->partition[i].start	= (vtoc)->v_part[i].p_start; \
		(vd_vtoc)->partition[i].nblocks	= (vtoc)->v_part[i].p_size; \
	}								\
}

/*
 * Copy the contents of a vd_efi_t to the contents of a dk_efi_t.
 * Note that (dk_efi)->dki_data and (vd_efi)->data should be correctly
 * initialized prior to using this macro.
 */
#define	VD_EFI2DK_EFI(vd_efi, dk_efi)					\
{									\
	(dk_efi)->dki_lba	= (vd_efi)->lba;			\
	(dk_efi)->dki_length	= (vd_efi)->length;			\
	bcopy((vd_efi)->data, (dk_efi)->dki_data, (dk_efi)->dki_length); \
}

/*
 * Copy the contents of dk_efi_t to the contents of vd_efi_t.
 * Note that (dk_efi)->dki_data and (vd_efi)->data should be correctly
 * initialized prior to using this macro.
 */
#define	DK_EFI2VD_EFI(dk_efi, vd_efi)					\
{									\
	(vd_efi)->lba		= (dk_efi)->dki_lba;			\
	(vd_efi)->length	= (dk_efi)->dki_length;			\
	bcopy((dk_efi)->dki_data, (vd_efi)->data, (vd_efi)->length);	\
}

/*
 * Hooks for EFI support
 */

/*
 * The EFI alloc_and_read() function will use some ioctls to get EFI data
 * but the device reference we will use is different depending if the command
 * is issued from the vDisk server side (vds) or from the vDisk client side
 * (vdc). From the server side (vds), we will have a layered device reference
 * (ldi_handle_t) while on the client side (vdc) we will have a regular device
 * reference (dev_t).
 */
#ifdef _SUN4V_VDS
int vds_efi_alloc_and_read(ldi_handle_t dev, struct dk_gpt **vtoc,
    size_t *vtoc_len);
#else
void vdc_efi_init(int (*func)(dev_t, int, caddr_t, int));
void vdc_efi_fini(void);
int vdc_efi_alloc_and_read(dev_t dev, struct dk_gpt **vtoc,
    size_t *vtoc_len);
#endif

void vd_efi_free(struct dk_gpt *ptr, size_t length);
void vd_efi_to_vtoc(struct dk_gpt *efi, struct vtoc *vtoc);

#ifdef	__cplusplus
}
#endif

#endif	/* _VDSK_COMMON_H */
