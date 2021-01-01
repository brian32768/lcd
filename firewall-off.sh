#!/bin/sh
# Turn off firewall

iptables=/sbin/iptables

$iptables -P INPUT ACCEPT
$iptables -P FORWARD ACCEPT
$iptables -P OUTPUT ACCEPT
$iptables -F

# Delete file to notify the LCD program
rm -f /var/tmp/firewall_is_on

exit 0
