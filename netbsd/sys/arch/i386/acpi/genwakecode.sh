#!/bin/sh
# $NetBSD: genwakecode.sh,v 1.3 2002/06/18 17:21:33 christos Exp $
# FreeBSD: src/sys/i386/acpica/genwakecode.sh,v 1.1 2002/05/01 21:52:34 peter Exp $
echo '/* THIS FILE IS AUTOMATICALLY GENERATED. DO NOT EDIT. */'
head -1 acpi_wakecode.S | sed 's@^.*\$\(NetBSD:.*\)\$.*$@/* from: \1 */@'
echo -n '/* $'
echo 'NetBSD: $ */'
echo

nm -n acpi_wakecode.o | while read offset dummy what
do
    if [ x"${offset}" = x"U" ]; then
	echo error: undefined symbol \"${dummy}\". 1>&2
	exit 1
    fi
    case $dummy in
    t)
	echo "#define	${what}	0x${offset}"
	;;
    *)
	;;
    esac
    true
done || exit 1

echo 
echo 'static const unsigned char wakecode[] = {';
hexdump -v -e '"\t" 8/1 "0x%02x, " "\n"' < acpi_wakecode.bin
echo '};'

exit 0
