## Required packages:
 - g++ (version >=8)
 - libboost-all-dev (version >=1.74)
 - libgmp-dev
 - libssl-dev
 - libntl-dev
 - pkg-config
 - libglib2.0-dev

Install Dependencies
```bash
sudo apt install gcc-9 g++-9 make cmake git libboost-thread-dev libboost-system-dev libboost-filesystem-dev libboost-program-options-dev libgmp-dev libssl-dev pkg-config libntl-dev libglib2.0-dev -y
```

## Compilation
```
mkdir build
cd build
cmake ..
make


## Run
Run from `build` directory.
Example:
```
Server: bin/sm_paq -r 0 -p 31000 -c 1 -y SMPAQ1 -n 8 -s 1024
Client: bin/sm_paq -r 1 -a 127.0.0.1 -p 31000 -c 1 -y SMPAQ1 -n 8 -s 1024
```
Description of Parameters:
```
-r: role (0: Server/1: Client)
-a: ip-address
-p: port number
-n: number of servers
-c: number of query identifier
-s: number of server's elements
-y: SMPAQ Type (SMPAQ1/SMPAQ2)
```

## Execution Environment
The code was tested on Ubuntu 22.04.

