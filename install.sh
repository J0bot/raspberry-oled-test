sudo cp boxionfetch /usr/local/bin/
sudo bash -c 'cat > /etc/systemd/system/boxionfetch.service <<EOF
[Unit]
Description=Boxion OLED fetch
After=local-fs.target
[Service]
Type=simple
ExecStart=/usr/local/bin/boxionfetch 0x3D
Restart=always
User=root
ExecStartPre=/bin/sleep 2
[Install]
WantedBy=multi-user.target
EOF'
sudo systemctl daemon-reload
sudo systemctl enable --now boxionfetch
journalctl -u boxionfetch -f
