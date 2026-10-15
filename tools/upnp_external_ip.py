"""Ask the LAN's UPnP Internet Gateway Device for its WAN (external) IPv4 address. Local network only, read-only."""
import ipaddress
import re
import socket
import urllib.parse
import urllib.request

MSEARCH = ("M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\n"
           "ST: urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\n\r\n")

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
sock.settimeout(3)
sock.sendto(MSEARCH.encode(), ("239.255.255.250", 1900))
locations = set()
try:
    while True:
        data, addr = sock.recvfrom(4096)
        m = re.search(r"(?im)^location:\s*(\S+)", data.decode(errors="replace"))
        if m:
            locations.add(m.group(1))
except socket.timeout:
    pass

if not locations:
    raise SystemExit("No UPnP gateway answered (UPnP is probably disabled on the modem).")

for location in sorted(locations):
    desc = urllib.request.urlopen(location, timeout=5).read().decode(errors="replace")
    model = re.search(r"<modelName>(.*?)</modelName>", desc)
    print(f"gateway: {location}  model: {model.group(1) if model else '?'}")
    for service_type in ("WANIPConnection", "WANPPPConnection"):
        m = re.search(r"<serviceType>(urn:schemas-upnp-org:service:%s:\d)</serviceType>.*?<controlURL>(.*?)</controlURL>"
                      % service_type, desc, re.S)
        if not m:
            continue
        urn, control = m.group(1), urllib.parse.urljoin(location, m.group(2))
        body = ('<?xml version="1.0"?><s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" '
                's:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/"><s:Body>'
                f'<u:GetExternalIPAddress xmlns:u="{urn}"/></s:Body></s:Envelope>')
        req = urllib.request.Request(control, data=body.encode(), headers={
            "Content-Type": 'text/xml; charset="utf-8"', "SOAPAction": f'"{urn}#GetExternalIPAddress"'})
        reply = urllib.request.urlopen(req, timeout=5).read().decode(errors="replace")
        ip = re.search(r"<NewExternalIPAddress>(.*?)</NewExternalIPAddress>", reply)
        if not ip or not ip.group(1):
            print(f"  {service_type}: no external address reported")
            continue
        wan = ipaddress.ip_address(ip.group(1))
        if wan in ipaddress.ip_network("100.64.0.0/10"):
            kind = "CGNAT shared address (100.64.0.0/10) -> port forwarding will NOT work"
        elif wan.is_private:
            kind = "private address -> double NAT / CGNAT, port forwarding will NOT work"
        else:
            kind = "public address -> port forwarding can work"
        print(f"  {service_type}: WAN IP {ip.group(1)}  [{kind}]")
