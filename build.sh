sudo apt update
sudo apt install -y build-essential cmake libi2c-dev i2c-tools qtbase5-dev
mkdir build && cd build
cmake ..
make -j
QT_QPA_PLATFORM=offscreen sudo ./qt_oled 0x3D
