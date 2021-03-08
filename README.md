# kinetic-prototype

Prototype of kinetic re-design

## Build
### Prequisites
#### Ubuntu
- sudo apt install build-essential libssl-dev libreadline-dev pkg-config

#### Google Protobuf v2.6.1
- wget https://github.com/google/protobuf/releases/download/v2.6.1/protobuf-2.6.1.tar.gz
- tar xzf protobuf-2.6.1.tar.gz
- cd protobuf-2.6.1
- sudo ./configure
- sudo make
- sudo make check
- sudo make install
- sudo ldconfig


#### Bazel
- sudo apt install curl gnupg
- curl -fsSL https://bazel.build/bazel-release.pub.gpg | gpg --dearmor > bazel.gpg
- sudo mv bazel.gpg /etc/apt/trusted.gpg.d/
- echo "deb [arch=amd64] https://storage.googleapis.com/bazel-apt stable jdk1.8" | sudo tee /etc/apt/
sources.list.d/bazel.list
- sudo apt update && sudo apt install bazel

### Build
- make
- make dist

## API Overview

### Library Overview


<figure align="center">
  <img src="assets/Kinetic_Library_Organization.png" height="600" />
  <figcaption text-align:"middle">
    <strong>Figure 1</strong>
    Overview of library modules and how they fit in to a high-level overview of the lifecycle of a kinetic command.
  </figcaption>
</figure>

### Blocking Operations

Each blocking kinetic interface operation (e.g. `ki_del`) encompasses the end to end logic of:

	1. Access connection configuration

	2. Validate existence of function arguments

	3. Validate values of function arguments

	4. Create the kio (kinetic IO) structure

	5. Populate the request data of the kio structure (`kio->kio_sendmsg`)

	6. Pack request data (into protobufs) and send to the kinetic server

	7. Receive the response data from the kinetic server (`kio->kio_recvmsg`)

	8. Unpack response data into data structure (e.g. `kv_t`, `keyrange_t`, etc.)
