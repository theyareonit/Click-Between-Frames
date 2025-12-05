# too lazy to make this into a github workflow
winemaker -iws2_32 -ievdev . --single-target linux-input
make
cp linux-input.exe.so ../../resources/linux-input.so
