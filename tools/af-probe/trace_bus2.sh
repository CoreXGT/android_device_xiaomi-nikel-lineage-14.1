#!/system/bin/sh
# Focused trace: ALL i2c traffic on adapter 2 (the VCM bus) while MOVETO runs.
# The previous run showed i2c-2 ret=-121 (EREMOTEIO) but i2c_result was unfiltered,
# so we now isolate bus 2 completely and print the actual addresses + payloads.
T=/sys/kernel/debug/tracing

echo 0 > $T/tracing_on
echo > $T/trace

for e in i2c_write i2c_read i2c_reply i2c_result; do
  echo 'adapter_nr == 2' > $T/events/i2c/$e/filter
  echo 1 > $T/events/i2c/$e/enable
done

echo 1 > $T/tracing_on
/data/local/tmp/probe13 > /data/local/tmp/probe13.out 2>&1
sleep 1
echo 0 > $T/tracing_on

echo "=== ALL i2c traffic on BUS 2 during probe13 ==="
cat $T/trace
echo "=== end trace ==="

for e in i2c_write i2c_read i2c_reply i2c_result; do
  echo 0 > $T/events/i2c/$e/enable
done
echo
echo "=== probe13 output ==="
cat /data/local/tmp/probe13.out
