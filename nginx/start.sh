#!/bin/sh
set -e

cd "$SNAP"

echo "Copying NGINX files to $SNAP_DATA"
cp -rf conf $SNAP_DATA/
cp -rf html $SNAP_DATA/
cp -rf logs $SNAP_DATA/
cp -rf sbin $SNAP_DATA/

cd "$SNAP_DATA"

echo "Creating required folders"
mkdir -p client_body_temp
mkdir -p fastcgi_temp
mkdir -p proxy_temp
mkdir -p scgi_temp
mkdir -p uwsgi_temp

chown nobody client_body_temp
chown nobody fastcgi_temp
chown nobody proxy_temp
chown nobody scgi_temp
chown nobody uwsgi_temp

./sbin/nginx

echo 'started'
