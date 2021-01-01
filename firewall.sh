#!/bin/bash
#
# Simple firewall ruleset for Toshiba Magnia SG20.
# Be careful! A typo here could lock you out of your system!
#
IF_WAN="eth0" # The interface that allows Internet access.
IF_LAN="eth1" # The local area network.

# Uncomment next line to enable openvpn tunnel settings
#IF_TUN="tun0" # OpenVPN connection, if available.

IPTABLES=/sbin/iptables

# -- Default policies --

# Strict policy, drop everything then allow specific access,
$IPTABLES -P INPUT DROP
$IPTABLES -P FORWARD DROP
$IPTABLES -P OUTPUT ACCEPT # Currently we allow all outbound traffic

# Flush all old tables
$IPTABLES -F

# Accept everything from localhost, LAN and VPN.
$IPTABLES -A INPUT -j ACCEPT -i lo 
$IPTABLES -A INPUT -j ACCEPT -i $IF_LAN 

# -- MASQUERADE -- Allow machines on your network to have Internet access

# Internet -- hide machines talking to Internet behind this firewall
$IPTABLES -A FORWARD -j ACCEPT -i $IF_LAN -o $IF_WAN
# Allow replies to come back from Internet
$IPTABLES -A FORWARD -j ACCEPT -i $IF_WAN -m state --state ESTABLISHED,RELATED
$IPTABLES -t nat -A POSTROUTING -j MASQUERADE -o $IF_WAN

if [ "$IF_TUN" != "" ]; then
    # Allow any computer on my OpenVPN connection to talk to this host. 
    $IPTABLES -A INPUT -j ACCEPT -i $IF_TUN
    # VPN-- hide machines talking on VPN network connection behind this firewall
    $IPTABLES -A FORWARD -j ACCEPT -i $IF_LAN -o $IF_TUN
    # Allow all traffic from VPN headed for LAN (replies or originating outside)
    $IPTABLES -A FORWARD -j ACCEPT -i $IF_TUN -o $IF_LAN
    $IPTABLES -A POSTROUTING -j MASQUERADE -o $IF_TUN
fi 

# -- Internet (WAN) --

# (Examples) Allow a couple outside places to talk directly to this host.
# $IPTABLES -A INPUT -i $IF_WAN -j ACCEPT -p tcp -s 67.168.251.210 # work 1
# $IPTABLES -A INPUT -i $IF_WAN -j ACCEPT -p tcp -s 69.12.174.79   # work 2

# (Uncomment to) Allow unrestricted access to my web server from the Internet.
$IPTABLES -A INPUT -i $IF_WAN -j ACCEPT -p tcp --dport 80

# (Uncomment to) Allow unrestricted login access via ssh from the Internet. 
# CHANGE YOUR PASSWORDS FIRST!!! Commands: "passwd root", "passwd sshuser".
# $IPTABLES -A INPUT -i $IF_WAN -j ACCEPT -p tcp --dport 22

# (Another example) Allowing ssh access only from one machine
# $IPTABLES -A INPUT -i $IF_WAN -j ACCEPT -p tcp --dport 22 -s 67.168.251.210

# (Uncomment to) Log all other Internet access attempts via TCP in syslog.
#$IPTABLES -A INPUT -i $IF_WAN -j LOG    -p tcp

# Mark status of firewall for the LCD program
touch /var/tmp/firewall_is_on

exit 0;
# that's all!
