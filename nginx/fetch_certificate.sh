fetch_certificate.sh

MAC_ADDRESS=`ip addr show eth0 | sed -nr '/^\s*link\//s~.* ([0-9a-f]{2}:[0-9a-f]{2}:[0-9a-f]{2}:[0-9a-f]{2}:[0-9a-f]{2}:[0-9a-f]{2}) .*~\1~p'`
84:39:be:67:e9:be
