#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

# Copyright 2007 Sun Microsystems, Inc.  All rights reserved. 
# Use is subject to license terms.
#
# ident	"%Z%%M%	%I%	%E% SMI"

# utility to pack and unpack a boot/root archive
# both ufs and hsfs (iso9660) format archives are unpacked
# only ufs archives are generated
#
# usage: pack   <archive> <root>
#        unpack <archive> <root>
#        packmedia   <solaris_image> <root>
#        unpackmedia <solaris_image> <root>
#
#   Where <root> is the directory to unpack to and will be cleaned out
#   if it exists.
#
#   In the case of (un)packmedia, the image is packed or unpacked to/from
#   Solaris media and all the things that don't go into the ramdisk image
#   are (un)cpio'd as well
#
# This utility is also used to pack parts (in essence the window system, 
# usr/dt and usr/openwin) of the non ramdisk SPARC 
# miniroot. (un)packmedia will recognize that they are being run a SPARC 
# miniroot and do the appropriate work. 
#

usage()
{
	printf "usage: root_archive pack <archive> <root>\n"
	printf "       root_archive unpack <archive> <root>\n"
	printf "       root_archive packmedia   <solaris_image> <root>\n"
	printf "       root_archive unpackmedia <solaris_image> <root>\n"
}

cleanup()
{
	if [ -d $MNT ] ; then
		umount $MNT 2> /dev/null
		rmdir $MNT
	fi

	lofiadm -d "$TMR" 2>/dev/null
	rm -f "$TMR" "$TMR.gz"
}

archive_Gnome()
{
	MEDIA="$1"
	MINIROOT="$2"

	RELEASE=`/bin/ls -d "$MEDIA/Solaris_"*`
	RELEASE=`basename "$RELEASE"`

	if [ -d "$UNPACKED_ROOT/kernel/drv/sparcv9" ] ; then
		CPIO_DIR="$MEDIA/$RELEASE/Tools/miniroot_extra"
		mkdir -p "$CPIO_DIR"
	else
		CPIO_DIR="$MEDIA/$RELEASE/Tools/Boot"
	fi
	

	# Create the gnome archive
	#
	(
		# Prepopulate the gconf database. This needs to be done and
		# done first for several reasons. 1) Archiving out the gnome
		# libraries and binaries causes the gconftool-2 to not run
		# appropriately at boot time. 2) The binaries and libraries
		# needed to run this are big and thus we want to archive
		# them separately. 3) Having schemas prepopluated in the
		# miniroot means faster boot times.
		#

		cd "$MINIROOT"
		HOME="./tmp/root"
		export HOME
		umask 0022
		GCONF_CONFIG_SOURCE="xml:merged:"$MINIROOT"/.tmp_proto/root/etc/gconf/gconf.xml.defaults"
		export GCONF_CONFIG_SOURCE
		SCHEMADIR="$MINIROOT/.tmp_proto/root/etc/gconf/schemas"
		export SCHEMADIR
		/usr/bin/gconftool-2 --makefile-install-rule $SCHEMADIR/*.schemas >/dev/null 2>&1
		# usr/share gnome stuff
		cd "$MINIROOT"
		find usr/share/GConf usr/share/application-registry \
		    usr/share/autostart usr/share/dbus-1 usr/share/dtds \
		    usr/share/emacs usr/share/gnome usr/share/gnome-2.0 \
		    usr/share/gnome-background-properties \
		    usr/share/gtk-engines usr/share/gui-install \
		    usr/share/icon-naming-utils usr/share/control-center \
		    usr/share/icons usr/share/locale usr/share/metacity \
		    usr/share/mime usr/share/mime-info usr/share/pixmaps \
		    usr/share/scrollkeeper usr/share/sgml usr/share/themes \
		    usr/share/xml \
		    -print > /tmp/gnome_share.$$ 2>/dev/null

		if [ ! -f /tmp/gnome_share.$$ ] ; then
			echo "/tmp/gnome_share.$$ file list not found."
			return 
		fi

		# usr/lib gnome stuff

		find usr/lib/libgnome*\.so\.* \
		    usr/lib/libgst*\.so\.* usr/lib/libgconf*\.so\.* \
		    usr/lib/libgdk*\.so\.* usr/lib/libgtk*\.so\.* \
		    usr/lib/libglade*\.so\.* usr/lib/libmetacity*\.so\.* \
		    usr/lib/libfontconfig*\.so\.* usr/lib/libgmodule*\.so\.* \
		    usr/lib/libgobject*\.so\.* usr/lib/libgthread*\.so\.* \
		    usr/lib/libpopt*\.so\.* usr/lib/libstartup*\.so\.* \
		    usr/lib/libexif*\.so\.* usr/lib/libtiff*\.so\.* \
		    usr/lib/libdbus*\.so\.* usr/lib/libstartup*\.so\.* \
		    usr/lib/libexif*\.so\.* usr/lib/libORBit*\.so\.* \
	 	    usr/lib/libmlib*\.so\.* usr/lib/libxsl*\.so\.* \
		    usr/lib/libpango*\.so\.* usr/lib/libpng*\.so\.* \
		    usr/lib/liboil*\.so\.* usr/lib/libbonobo*\.so\.* \
		    usr/lib/libart*\.so\.* usr/lib/libcairo*\.so\.* \
		    usr/lib/libjpeg*\.so\.* \
		    usr/lib/libpolkit*\.so\.* \
			-print | egrep -v '\.so\.[0]$' > \
		       /tmp/gnome_lib.$$ 2>/dev/null

		find usr/lib/nautilus usr/lib/pango usr/lib/iconv \
		    usr/lib/metacity-dialog usr/lib/window-manager-settings \
		    usr/lib/bonobo-2.0 usr/lib/bononbo usr/lib/gtk-2.0 \
		    usr/lib/GConf usr/lib/bonobo-activation-server \
		    usr/lib/python2.4 usr/lib/gstreamer-0.10 \
		    usr/lib/gconf-sanity-check-2 usr/lib/gconfd \
		    usr/lib/gnome-vfs-2.0 usr/lib/dbus-daemon \
		    usr/lib/gnome-vfs-daemon usr/lib/gnome-settings-daemon \
		    usr/lib/gnome_segv2 usr/lib/orbit-2.0 \
		    usr/lib/libmlib \
		    print > /tmp/gnome_libdir.$$ 2>/dev/null

		if [ ! -f /tmp/gnome_lib.$$  -a ! -f gnome_libdir.$$ ] ; then
			echo "/tmp/gnome_lib.$$ file list not found."
			return
		fi

		# /usr/sfw gnome stuff
		find usr/sfw/bin usr/sfw/include usr/sfw/share usr/sfw/src \
		    -print > /tmp/gnome_sfw.$$ 2>/dev/null

		if [ ! -f /tmp/gnome_sfw.$$ ] ; then
			echo "/tmp/gnome_sfw.$$ file list not found."
			return
		fi

		# gnome app binaries usr/bin
		find usr/bin/gnome* usr/bin/gui-install usr/bin/bonobo* \
		    usr/bin/gtk-* usr/bin/fax* usr/bin/gdk* usr/bin/gif2tiff \
		    usr/bin/install-lan \
		    usr/bin/metacity* usr/bin/gst-* usr/bin/gconftool-2 \
		    usr/bin/pango* usr/bin/desktop* usr/bin/djpeg \
		    usr/bin/notify-send usr/bin/oil-bugreport \
		    usr/bin/bmp2tiff usr/bin/thembus-theme-applier \
		    usr/bin/thumbnail usr/lib/update-* \
		    usr/bin/ras2tiff usr/bin/raw2tiff usr/bin/rdjpgcom \
		    usr/bin/thumbnail usr/bin/dbus* \
		    usr/bin/tiff* usr/bin/rgb2ycbcr \
		    usr/bin/fc-cache usr/bin/fc-list \
			-print > /tmp/gnome_bin.$$ 2>/dev/null

		if [ ! -f /tmp/gnome_bin.$$ ] ; then
			echo "/tmp/gnome_bin.$$ file list not found."
			return 
		fi

		# Cat all the files together and create the gnome archive
		#

		cat /tmp/gnome_libdir.$$ /tmp/gnome_lib.$$ \
		     /tmp/gnome_share.$$ /tmp/gnome_sfw.$$ /tmp/gnome_bin.$$ \
		    > /tmp/gnome.$$

		if [ ! -f /tmp/gnome.$$ ] ; then
			echo "/tmp/gnome.$$ file not found."
			return
		fi
		# Save off this file in the miniroot for use later
		# when unpacking. Clean up old cruft if there.
		#

		if [ -f .tmp_proto/gnome_saved ]; then
			rm -f .tmp_proto/gnome_saved
		fi

		cp /tmp/gnome.$$ .tmp_proto/gnome_saved

		# Create gnome archive
		#

		cpio -ocmPuB < /tmp/gnome.$$ 2>/dev/null | bzip2 > \
		    "$CPIO_DIR/gnome.cpio.bz2"

		# Remove files from miniroot that are in archive. 
		# Create symlinks for files in archive
		
		rm -rf `cat /tmp/gnome_share.$$`

		for i in `cat /tmp/gnome_share.$$`
		do 
			ln -s /tmp/root/$i $i 2>/dev/null
		done

		rm -rf `cat /tmp/gnome_lib.$$`
		for i in `cat /tmp/gnome_lib.$$`
		do	
			ln -s /tmp/root/$i $i 2>/dev/null
		done

		rm -rf `cat /tmp/gnome_libdir.$$`
		for i in `cat /tmp/gnome_libdir.$$`
		do 
			ln -s /tmp/root/$i $i 2>/dev/null
		done

		rm -rf `cat /tmp/gnome_sfw.$$`
		for i in `cat /tmp/gnome_sfw.$$`
		do 
			ln -s /tmp/root/$i $i 2>/dev/null
		done

		rm -rf `cat /tmp/gnome_bin.$$`
		for i in `cat /tmp/gnome_bin.$$`
		do 
			ln -s /tmp/root/$i $i 2>/dev/null
		done
		rm -f /tmp/gnome_share.$$
		rm -f /tmp/gnome_lib.$$
		rm -f /tmp/gnome_libdir.$$
		rm -f /tmp/gnome_bin.$$
	)
}

archive_JavaGUI()
{
	MEDIA="$1"
	MINIROOT="$2"

	RELEASE=`/bin/ls -d "$MEDIA/Solaris_"*`
	RELEASE=`basename "$RELEASE"`

	if [ -d "$UNPACKED_ROOT/kernel/drv/sparcv9" ] ; then
		CPIO_DIR="$MEDIA/$RELEASE/Tools/miniroot_extra"
		mkdir -p "$CPIO_DIR"
	else
		CPIO_DIR="$MEDIA/$RELEASE/Tools/Boot"
	fi
	
	# Archive the java wizard components that are only used in the
	# non developer express path.
	#
	(
		# path is usr/lib/install/data
		cd "$MINIROOT"
		find usr/lib/install/data/wizards \
		    -print > /tmp/java_ui.$$ 2>/dev/null

		if [ ! -f /tmp/java_ui.$$ ] ; then
			echo "/tmp/java_ui.$$ file list not found."
			return 
		fi

		cpio -ocmPuB < /tmp/java_ui.$$ 2>/dev/null | bzip2 > \
		    "$CPIO_DIR/javaui.cpio.bz2"

		rm -rf `cat /tmp/java_ui.$$`
		ln -s /tmp/root/usr/lib/install/data/wizards usr/lib/install/data/wizards 2>/dev/null

		rm -f /tmp/java_ui.$$
	
	)
}

archive_Misc()
{
	MEDIA="$1"
	MINIROOT="$2"

	RELEASE=`/bin/ls -d "$MEDIA/Solaris_"*`
	RELEASE=`basename "$RELEASE"`

	if [ -d "$UNPACKED_ROOT/kernel/drv/sparcv9" ] ; then
		CPIO_DIR="$MEDIA/$RELEASE/Tools/miniroot_extra"
		mkdir -p "$CPIO_DIR"
	else
		CPIO_DIR="$MEDIA/$RELEASE/Tools/Boot"
	fi

	# Archive misc stuff that is needed by non devex installer
	#
	(
		# usr/lib stuff
		cd "$MINIROOT"
		find usr/lib/lp -print > /tmp/lp.$$ 2>/dev/null
		if [ ! -f /tmp/lp.$$ ] ; then
			echo "/tmp/lp.$$ file list not found."
			return 
		fi

		cpio -ocmPuB < /tmp/lp.$$ 2>/dev/null | bzip2 > \
		    "$CPIO_DIR/lpmisc.cpio.bz2"

		rm -rf `cat /tmp/lp.$$`
		ln -s /tmp/root/usr/lib/lp usr/lib/lp 2>/dev/null
		
		rm -f /tmp/lp.$$
	)

}

archive_Perl()
{
	MEDIA="$1"
	MINIROOT="$2"

	RELEASE=`/bin/ls -d "$MEDIA/Solaris_"*`
	RELEASE=`basename "$RELEASE"`

	if [ -d "$UNPACKED_ROOT/kernel/drv/sparcv9" ] ; then
		CPIO_DIR="$MEDIA/$RELEASE/Tools/miniroot_extra"
		mkdir -p "$CPIO_DIR"
	else
		CPIO_DIR="$MEDIA/$RELEASE/Tools/Boot"
	fi

	# Archive perl, it is only needed by gnome gui.
	#
	(
		# in usr
		cd "$MINIROOT"
		find usr/perl5 -print > /tmp/perl.$$ 2>/dev/null

		if [ ! -f /tmp/perl.$$ ] ; then
			echo "/tmp/perl.$$ file list not found."
			return 
		fi
		cpio -ocmPuB < /tmp/perl.$$ 2>/dev/null | bzip2 > \
		    "$CPIO_DIR/perl.cpio.bz2"

		rm -rf `cat /tmp/perl.$$` 2>/dev/null
		ln -s /tmp/root/perl5 usr/perl5 2>/dev/null

		rm -f /tmp/perl.$$
	)
}
archive_X()
{
	MEDIA="$1"
	MINIROOT="$2"

	RELEASE=`/bin/ls -d "$MEDIA/Solaris_"*`
	RELEASE=`basename "$RELEASE"`

	if [ -d "$UNPACKED_ROOT/kernel/drv/sparcv9" ] ; then
		CPIO_DIR="$MEDIA/$RELEASE/Tools/miniroot_extra"
		mkdir -p "$CPIO_DIR"
	else
		CPIO_DIR="$MEDIA/$RELEASE/Tools/Boot"
	fi

	# create the graphics and non-graphics X archive
	#
	(
		cd "$MINIROOT"
		find usr/openwin usr/dt usr/X11 -print 2> /dev/null |\
		    cpio -ocmPuB 2> /dev/null | bzip2 > "$CPIO_DIR/X.cpio.bz2"

		find usr/openwin/bin/mkfontdir \
		     usr/openwin/lib/installalias \
		     usr/openwin/server/lib/libfont.so.1 \
		     usr/openwin/server/lib/libtypesclr.so.0 \
			 -print | cpio -ocmPuB 2> /dev/null | bzip2 > \
			 "$CPIO_DIR/X_small.cpio.bz2"

		rm -rf usr/dt usr/openwin usr/X11
		ln -s /tmp/root/usr/dt usr/dt
		ln -s /tmp/root/usr/openwin usr/openwin
		ln -s /tmp/root/usr/X11 usr/X11
	)
}

packmedia()
{
	MEDIA="$1"
	MINIROOT="$2"

	RELEASE=`/bin/ls -d "$MEDIA/Solaris_"*`
	RELEASE=`basename "$RELEASE"`

	mkdir -p "$MEDIA/$RELEASE/Tools/Boot"
	mkdir -p "$MEDIA/boot/platform/i86pc/kernel"

	# archive package databases to conserve memory
	#
	(
		cd "$MINIROOT"
		find tmp/root/var/sadm/install tmp/root/var/sadm/pkg -print | \
		    cpio -ocmPuB 2> /dev/null | bzip2 > \
		    "$MEDIA/$RELEASE/Tools/Boot/pkg_db.cpio.bz2"
	)

	rm -rf "$MINIROOT/tmp/root/var/sadm/install"
	rm -rf "$MINIROOT/tmp/root/var/sadm/pkg"

	# clear out 64 bit support to conserve memory
	#
	if [ "$STRIP_AMD64" != false ] ; then
		find "$MINIROOT" -name amd64 -type directory | xargs rm -rf
	fi

	archive_X "$MEDIA" "$MINIROOT"

	# Take out the gnome and java parts of the installer from
	# the miniroot. These are not required to boot the system
	# and start the installers.
	
	archive_Gnome "$MEDIA" "$MINIROOT"
	archive_JavaGUI "$MEDIA" "$MINIROOT"
	archive_Perl "$MEDIA" "$MINIROOT"
	archive_Misc "$MEDIA" "$MINIROOT"

	cp "$MINIROOT/platform/i86pc/multiboot" "$MEDIA/boot"
	cp "$MINIROOT/platform/i86pc/kernel/unix" \
	    "$MEDIA/boot/platform/i86pc/kernel/unix"

	# copy the install menu to menu.lst so we have a menu
	# on the install media
	#
	if [ -f "${MINIROOT}/boot/grub/install_menu" ] ; then
		cp ${MINIROOT}/boot/grub/install_menu \
		    ${MEDIA}/boot/grub/menu.lst
	fi

	(
		cd "$MEDIA/$RELEASE/Tools/Boot"
		ln -sf ../../../boot/x86.miniroot
		ln -sf ../../../boot/multiboot
		ln -sf ../../../boot/grub/pxegrub
	)
}

unarchive_X()
{
	MEDIA="$1"
	UNPACKED_ROOT="$2"

	RELEASE=`/bin/ls -d "$MEDIA/Solaris_"*`
	RELEASE=`basename "$RELEASE"`

	if [ -d "$UNPACKED_ROOT/kernel/drv/sparcv9" ] ; then
		CPIO_DIR="$MEDIA/$RELEASE/Tools/miniroot_extra"
	else
		CPIO_DIR="$MEDIA/$RELEASE/Tools/Boot"
	fi

	# unpack X
	#
	(
		cd "$UNPACKED_ROOT"
		rm -rf usr/dt usr/openwin usr/X11
		bzcat "$CPIO_DIR/X.cpio.bz2" | cpio -icdmu 2> /dev/null
	)
}

unpackmedia()
{
	MEDIA="$1"
	UNPACKED_ROOT="$2"

	RELEASE=`/bin/ls -d "$MEDIA/Solaris_"*`
	RELEASE=`basename "$RELEASE"`

	unarchive_X "$MEDIA" "$UNPACKED_ROOT"

	# unpack package databases
	#
	(
		cd "$UNPACKED_ROOT"
		bzcat "$MEDIA/$RELEASE/Tools/Boot/pkg_db.cpio.bz2" |
		    cpio -icdmu 2> /dev/null

		# unpack gnome, perl, java and misc
		# Remove symlinks left from unpacking x86.miniroot so that
		# unpacking subsequent archives will populate appropriately.
		#
		rm -rf usr/perl5
		rm -rf usr/lib/install/data/wizards
		rm -rf usr/lib/lp

		# Gnome list saved off from packmedia
		for i in `cat .tmp_proto/gnome_saved`
		do 
			rm -rf $i
		done
		
		bzcat "$MEDIA/$RELEASE/Tools/Boot/gnome.cpio.bz2" |
		    cpio -icdmu 2>/dev/null
		bzcat "$MEDIA/$RELEASE/Tools/Boot/javaui.cpio.bz2" |
		    cpio -icdmu 2>/dev/null
		bzcat "$MEDIA/$RELEASE/Tools/Boot/lpmisc.cpio.bz2" |
		    cpio -icdmu 2>/dev/null
		bzcat "$MEDIA/$RELEASE/Tools/Boot/perl.cpio.bz2" |
		    cpio -icdmu 2>/dev/null
	)
}

do_unpack()
{
	rm -rf "$UNPACKED_ROOT"
	mkdir -p "$UNPACKED_ROOT"
	(
		cd $MNT
		find . -print | cpio -pdum "$UNPACKED_ROOT" 2> /dev/null
	)
	umount $MNT
}

unpack()
{

	if [ ! -f "$MR" ] ; then
		usage
		exit 1
	fi

	gzcat "$MR" > $TMR

	LOFIDEV=`/usr/sbin/lofiadm -a $TMR`
	if [ $? != 0 ] ; then
		echo lofi plumb failed
		exit 2
	fi

	mkdir -p $MNT

	FSTYP=`fstyp $LOFIDEV`

	if [ "$FSTYP" = ufs ] ; then
		/usr/sbin/mount -o ro,nologging $LOFIDEV $MNT
		do_unpack
	elif [ "$FSTYP" = hsfs ] ; then
		/usr/sbin/mount -F hsfs -o ro $LOFIDEV $MNT
		do_unpack
	else
		printf "invalid root archive\n"
	fi

	rmdir $MNT
	lofiadm -d $TMR ; LOFIDEV=
	rm $TMR
}

pack()
{
	if [ ! -d "$UNPACKED_ROOT" -o -z "$MR" ] ; then
		usage
		exit 1
	fi

	# Estimate image size and add %10 overhead for ufs stuff.
	# Note, we can't use du here in case $UNPACKED_ROOT is on a filesystem,
	# e.g. zfs, in which the disk usage is less than the sum of the file
	# sizes.  The nawk code 
	#
	#	{t += ($7 % 1024) ? (int($7 / 1024) + 1) * 1024 : $7}
	#
	# below rounds up the size of a file/directory, in bytes, to the
	# next multiple of 1024.  This mimics the behavior of ufs especially
	# with directories.  This results in a total size that's slightly
	# bigger than if du was called on a ufs directory.
	size=$(find "$UNPACKED_ROOT" -ls | nawk '
	    {t += ($7 % 1024) ? (int($7 / 1024) + 1) * 1024 : $7}
	    END {print int(t * 1.10 / 1024)}')

	/usr/sbin/mkfile ${size}k "$TMR"

	LOFIDEV=`/usr/sbin/lofiadm -a "$TMR"`
	if [ $? != 0 ] ; then
		echo lofi plumb failed
		exit 2
	fi

	RLOFIDEV=`echo $LOFIDEV | sed s/lofi/rlofi/`
	newfs $RLOFIDEV < /dev/null 2> /dev/null 
	mkdir -p $MNT
	mount -o nologging $LOFIDEV $MNT 
	rmdir $MNT/lost+found
	(
		cd "$UNPACKED_ROOT"
		find . -print | cpio -pdum $MNT 2> /dev/null
	)
	lockfs -f $MNT
	umount $MNT
	rmdir $MNT
	lofiadm -d $LOFIDEV
	LOFIDEV=

	rm -f "$TMR.gz"
	gzip -f "$TMR"
	mv "$TMR.gz" "$MR"
	chmod a+r "$MR"
}

# main
#

EXTRA_SPACE=0
STRIP_AMD64=

while getopts s:6 opt ; do
	case $opt in
	s)	EXTRA_SPACE="$OPTARG"
		;;
	6)	STRIP_AMD64=false
		;;
	*)	usage
		exit 1
		;;
	esac
done
shift `expr $OPTIND - 1`

if [ $# != 3 ] ; then
	usage
	exit 1
fi

UNPACKED_ROOT="$3"
BASE="`pwd`"
MNT=/tmp/mnt$$
TMR=/tmp/mr$$
LOFIDEV=
MR="$2"

if [ "`dirname $MR`" = . ] ; then
	MR="$BASE/$MR"
fi
if [ "`dirname $UNPACKED_ROOT`" = . ] ; then
	UNPACKED_ROOT="$BASE/$UNPACKED_ROOT"
fi

trap cleanup EXIT

case $1 in
	packmedia)
		MEDIA="$MR"
		MR="$MR/boot/x86.miniroot"

		if [ -d "$UNPACKED_ROOT/kernel/drv/sparcv9" ] ; then
			archive_X "$MEDIA" "$UNPACKED_ROOT"
		else
			packmedia "$MEDIA" "$UNPACKED_ROOT"
			pack
		fi ;;
	unpackmedia)
		MEDIA="$MR"
		MR="$MR/boot/x86.miniroot"

		if [ -d "$UNPACKED_ROOT/kernel/drv/sparcv9" ] ; then
			unarchive_X "$MEDIA" "$UNPACKED_ROOT"
		else
			unpack
			unpackmedia "$MEDIA" "$UNPACKED_ROOT"
		fi ;;
	pack)	pack ;;
	unpack)	unpack ;;
	*)	usage ;;
esac
