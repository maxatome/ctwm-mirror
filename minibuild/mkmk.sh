#!/bin/sh
cd `dirname $0`
rtdir=".."
build="build"

# Output into Makefile
echo "Generating Makefile"
exec 1>Makefile

# Get the list of base non-generated and generated sources
ngfiles=`sed \
	-e '1,/##STDSRC-START/d' -e '/##STDSRC-END/,$d' \
	-e 's/#.*//' -e 's/[[:space:]]*//' -e '/^$/d' \
	${rtdir}/cmake_files/ctwm_cmake_vars.cmake`
gfiles=`sed \
	-e '1,/##GENSRC-START/d' -e '/##GENSRC-END/,$d' \
	-e 's/#.*//' -e 's/[[:space:]]*//' -e '/^$/d' \
	${rtdir}/cmake_files/ctwm_cmake_vars.cmake`


# Non-generates files that are optional; separate so they don't end up in
# OFILES in the main run
ongfiles=""
ongfiles="${ongfiles} image_xpm.c"
ongfiles="${ongfiles} image_jpeg.c"
ongfiles="${ongfiles} parse_m4.c"
ongfiles="${ongfiles} ewmh.c"


# Top boilerplate
cat << EOF
## GENERATED FILE

RTDIR=${rtdir}
BDIR=${build}

EOF


# Defs (external)
cat defs.mk
echo


# List the core files first
mkobj()
{
	local o
	o=$1
	o=${o##*/}
	o="${o%.c}.o"

	eval "$2=$o"
}

echo "## Core files"
echo "OFILES = \\"
for i in ${ngfiles}; do
	mkobj $i oret
	echo "    \${BDIR}/${oret} \\"
done
echo

# Have to manually write these; transform rules won't work across dirs
for i in ${ngfiles} ${ongfiles}; do
	src="\${RTDIR}/${i}"
	mkobj $i dst
	dst="\${BDIR}/${dst}"
	echo "${dst}: ${src}"
	echo "	\${CC} \${_CFLAGS} \${CFLAGS} -c -o ${dst} ${src}"
done
echo


# Generated files
echo "## Generated files"
echo "OFILES += \\"
for i in ${gfiles}; do
	echo "    \${BDIR}/${i%.c}.o \\"
done
echo
for i in ${gfiles}; do
	echo "gen: \${BDIR}/${i}"
done
echo
echo


# Various options
cat options.mk


# And the final build bits
cat gen_files.mk
