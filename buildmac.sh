#!/bin/sh
rm -r macdist

make stratagus -j 4
mkdir macdist

cd macdist

mkdir -p Wargus.app/Contents/Resources/data/
mkdir -p Wargus.app/Contents/MacOS/

cp ../bundlefiles/Wargus Wargus.app/Contents/MacOS/Wargus
cp ../bundlefiles/startup.command Wargus.app/Contents/MacOS/startup.command
chmod 777 Wargus.app/Contents/MacOS/Wargus
chmod 777 Wargus.app/Contents/MacOS/startup.command

cp ../bundlefiles/Wargus.icns Wargus.app/Contents/Resources/Wargus.icns
cp ../bundlefiles/Info.plist Wargus.app/Contents/Info.plist

cp ../stratagus Wargus.app/Contents/Resources/stratagus
cp -R /usr/local/share/games/stratagus/wargus/ Wargus.app/Contents/Resources/data/

dylibbundler -od -b -x ./Wargus.app/Contents/Resources/stratagus -d ./Wargus.app/Contents/libs/
