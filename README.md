
# UDP Serial-Wifi bridge

## What

A serial bridge over UDP. It is really rough and mostly for benchmarking purposes. It is not really usable in this state.

## Why

To try making wireless midi in various ways.

## How

Set your pico sdk path
```shell
export PICO_SDK_PATH=...
```

```shell
mkdir -p build ; cd build
cmake --fresh ..
make -j32
```

Push to your boards

```aiignore
# Client
openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg -c "adapter speed 5000" -c "program udp_client.elf verify; reset; exit"
# Server
openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg -c "adapter speed 5000" -c "program udp_server.elf verify; reset; exit"
```


Test latency:

```shell 
python3 ../tools/latency_stats.py --server /dev/ttyACM1 --client /dev/ttyACM2 --tests 10000 -
-chunk-size 3 --throughput-duration 2.0
```
