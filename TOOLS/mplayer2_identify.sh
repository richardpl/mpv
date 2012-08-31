#!/bin/sh

case "$0" in
	mplayer2_identify.sh|*/mplayer2_identify.sh)
		# we are NOT being sourced
		case "$1" in
			'')
				;;
			*)
				set -- '' "$@"
				;;
		esac
		;;
esac

if [ $# -lt 2 ]; then
	echo >&2 "Usage 1: source $0 PREFIX filename.mkv"
	echo >&2 "will fill shell variables for each property,"
	echo >&2 "like PREFIXlength, PREFIXvideo_codec, etc."
	echo >&2 "If an error occurred, the property path"
	echo >&2 "(i.e. the variable PREFIXpath) will be unset."
	echo >&2 "This usage is made for scripts."
	echo >&2
	echo >&2 "Usage 2: $0 filename.mkv"
	echo >&2 "will print all property values."
	echo >&2 "This usage is made for humans only."
	exit 1
fi

__midentify__prefix=$1
shift

if [ -n "$__midentify__prefix" ]; then
	# in case of error, we always want this unset
	eval unset $__midentify__prefix'path'
fi

__midentify__t=`mktemp`

(
	allprops=`nplayer --list-properties | grep ^\  | cut -c 2-22`
	propstr=
	for p in $allprops; do
		propstr=$propstr"X-MIDENTIFY:\\n$p\\n\${$p}\\n"
		# clear them all
		echo "X-MIDENTIFY:"
		echo "$p"
		echo
	done
	nplayer --playing-msg="$propstr" --vo=null --ao=null --frames=0 "$@"
) > "$__midentify__t"

while IFS= read -r __midentify__line; do
	case "$__midentify__line" in
		X-MIDENTIFY:)
			IFS= read -r __midentify__key
			IFS= read -r __midentify__value
			if [ -n "$__midentify__prefix" ]; then
				if [ -n "$__midentify__value" ]; then
					eval $__midentify__prefix$__midentify__key=\$__midentify__value
				else
					eval unset $__midentify__prefix$__midentify__key
				fi
			else
				if [ -n "$__midentify__value" ]; then
					echo "$__midentify__key=$__midentify__value"
				fi
			fi
			;;
	esac
done < "$__midentify__t"

rm -f "$__midentify__t"
