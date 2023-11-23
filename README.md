# Reliable Data Transfer

Implementation of Selective Repeat and Go-Back-N over UDP.

## Quick Start

CMake is required to build this project. And if the platform is Windows, winsock library is required.

Below is an example using clang to build the project.

```sh
cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=Debug
cd build && make
```

The `router` under root directory is a simulator for packet loss and delay. Just set the remote ip and port to be the receiver's. Once the sender sends the packet the router will forward it to the receiver.

Below is an example of using the command line interface of the router, receiver and the sender.

```sh
python router.py --port 3333 --ip 127.0.0.1 --remote-port 4321 --remote-ip 127.0.0.1 --loss 0.05 --delay 20
./build/file_receiver --port=4321 --max-window-size=1 # window size is 1 for Go-Back-N
./build/file_sender --remote-ip=127.0.0.1 --remote-port=3333 --timeout=1000 --max-window-size=5 --max-retry-count=100
```

More arguments can be found in the source code.

## About

This repository is for NKU Network Course lab. DO NOT COPY THE CODE FOR YOUR OWN LAB OR YOU MUST TAKE THE RESPONSIBILITY FOR THE CONSEQUENCES.

ALSO THIS CODE MIGHT CONTAIN SOME UNEXPECTED BUGS. USE IT AT YOUR OWN RISK.
