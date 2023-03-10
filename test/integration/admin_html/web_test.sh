#!/bin/bash
#
# This test is semi-automatic. It builds envoy-static with
# ENVOY_ADMIN_BROWSER_TEST set, then runs the binary in a mode where
# the binary picks the admin port and writes it to a file. Then
# the script can poll the admin port for /ready. Once live, it runs
# Firefox (could be any browser) on the test HTML page, which will
# run through admin tests, printing out test results.
#
# It then leaves Firefox up, so the developer can examine the results
# and then quit firefox.
#
# These steps can also be performed manually rather than via the script.

tmp="/tmp/admin_web_test.$$"
rm -rf "$tmp"
mkdir "$tmp"

echo "Saving temp files to $tmp"

ENVOY_BINARY="bazel-bin/test/integration/admin_html/test_server"
if [ -e "$ENVOY_BINARY" ]; then
  echo "*** Re-using binary..."
else
  echo "*** Building: log file $tmp/build.log..."
  bazel build test/integration:admin_test_server >& "$tmp/build.log"
fi
ls -l "$ENVOY_BINARY"


echo "*** Invoking Envoy: log file $tmp/envoy.log ..."
$ENVOY_BINARY \
      -c test/integration/admin_html/web_test.yaml \
      --admin-address-path "$tmp/admin.port" >& "$tmp/envoy.log" &

echo "*** Waiting for the Envoy server to write the admin port to $tmp/admin.port ..."
while [ ! -s "$tmp/admin.port" ]; do
  sleep 1
done

echo ""
echo "*** Waiting for Envoy /ready to respond with \"LIVE\""

ready=""
while [ "$ready" != "LIVE" ]; do
  admin_port=$(cat $tmp/admin.port)
  ready=$(curl "$admin_port/ready")
  sleep 1
done

echo ""
echo "*** Envoy running with admin port running at $(cat $tmp/admin.port)"

# TODO(jmarantz): at some point it might be worth considering using Selenium
# or other tools to fully automate the testing of the admin UI.
echo "*** Please ensure Browser test passes and the stats UI looks good..."
browser="firefox"
test_url="$admin_port/test?file=web_test.html"
active_stats_url="$admin_port/stats?format=active"
echo $browser "$test_url" "$active_stats_url" ">&" "$tmp/browser.log"
$browser "$test_url" "$active_stats_url" >& "$tmp/browser.log"

curl -X POST "$admin_port/quitquitquit"
rm -rf "$tmp"
wait
