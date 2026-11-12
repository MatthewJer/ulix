#!/bin/sh
# Fetches the current list of certificate authorities (Mozilla's list as extracted by the curl project) into etc/ssl/cacert.pem.
# Check the result (git diff --stat, and the date in the first lines), then commit it. The file is public data from Mozilla's
# certdata.txt, which is under the Mozilla Public License 2.0.
cd "$(dirname "$0")/.." || exit 1
curl -sS -f -o etc/ssl/cacert.pem.new https://curl.se/ca/cacert.pem || exit 1
grep -q 'BEGIN CERTIFICATE' etc/ssl/cacert.pem.new || { echo "not a certificate list"; exit 1; }
mv etc/ssl/cacert.pem.new etc/ssl/cacert.pem
sed -n 5p etc/ssl/cacert.pem
