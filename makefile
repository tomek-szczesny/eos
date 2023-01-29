# -------
# Eos Makefile
#
# By Tomek Szczęsny 2023
#
# -------

SERVICEPATH=/etc/systemd/system

none:
	@echo ""
	@echo "make build		- Builds an executabe"
	@echo "sudo make install	- Installs a program and a service"
	@echo "sudo make uninstall	- Installs Windows instad ;)"

build:
	g++ eos.cpp -o eos

install:
	cp eos /usr/local/sbin 
	chown root:root /usr/local/sbin/eos 
	cp eos.service ${SERVICEPATH} 
	chown root:root ${SERVICEPATH}/eos.service 
	chmod 644 ${SERVICEPATH}/eos.service
	systemctl daemon-reload 
	systemctl enable eos
	systemctl start eos 
	
uninstall:
	systemctl stop eos
	systemctl disable eos
	rm ${SERVICEPATH}/eos.service
	systemctl daemon-reload
	systemctl reset-failed
	rm /usr/local/sbin/eos 


