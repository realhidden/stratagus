#!/bin/sh
DIR=$(cd "$(dirname "$0")"; pwd)
CMD="$DIR/../Resources/stratagus -d $DIR/../Resources/data/"
$CMD

