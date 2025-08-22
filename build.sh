cd build
cmake ..
make -j
QT_QPA_PLATFORM=minimal ./qt_oled 0x3D
