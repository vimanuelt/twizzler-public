#!/usr/bin/env bash

set -e
set -u

#export SYSROOT=$(pwd)/projects/$PROJECT/build/us/sysroot
#export OBJROOT=$(pwd)/projects/$PROJECT/build/us/objroot

export SYSROOT=$1
export OBJROOT=$2

#echo $1 >&2
#echo $(find $SYSROOT | sed "s|$SYSROOT||g") >&2
for ent in $(find -L $SYSROOT | sed "s|$SYSROOT||g" | grep -v '\.data$'); do
	if [[ -f $SYSROOT/$ent ]]; then
		target=$OBJROOT/${ent//\//_}.obj
		extra=
		perms=rxh

		echo $ent >&2
		if [[ -L $SYSROOT/$ent ]]; then
			link=$(readlink $SYSROOT/$ent)
			echo $ent'*SYM*'$link
	#		echo $ent'*SYM*'$link >&2
		else
			if [[ -f $SYSROOT/$ent.data ]]; then
				target_data=$OBJROOT/${ent//\//_}.data.obj
				projects/$PROJECT/build/utils/file2obj -i $SYSROOT/$ent.data -o $target_data -p rh
				id_data=$(projects/$PROJECT/build/utils/objstat -i $target_data)
				ln $target_data $OBJROOT/$id_data
				echo $ent.data'*'$id_data

				projects/$PROJECT/build/utils/file2obj -i $SYSROOT/$ent -o $target -p $perms -f 1:RWD:$id_data $extra
				id=$(projects/$PROJECT/build/utils/objstat -i $target)
				ln $target $OBJROOT/$id
				echo $ent'*'$id
			else
				projects/$PROJECT/build/utils/file2obj -i $SYSROOT/$ent -o $target -p $perms $extra
				id=$(projects/$PROJECT/build/utils/objstat -i $target)
				ln $target $OBJROOT/$id
				echo $ent'*'$id
			fi
		fi
	fi
done

