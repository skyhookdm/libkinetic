**This document provides instructions for building libkinetic on CentOS Linux platforms.**

# Libkinetic Building Guide for CentOS

CentOS Linux distribution is a production-ready platform derived from the sources of Red Hat Enterprise Linux (RHEL). In this guide, we will cover building libkinetic on CentOS Linux 8 and CentOS Linux 7.

Type `cat /etc/os-release` to determine the type and version of Linux you are using. Follow the main README.md for building libkinetic on Ubuntu based platforms.

# CentOS Linux 8

CentOS Linux 8 mirrors RHEL 8. One can download CentOS 8 at https://www.centos.org/download/. Community support for CentOS Linux 8 will end on 2021-12-31. After that, CentOS Linux 8 will turn into CentOS Stream 8, which will serve as upstream for (rather than a mirror of) RHEL. Support for CentOS Stream 8 will end on 2024-5-31.

*For more information on CentOS, please visit https://www.centos.org/about/. For more information on CentOS Stream, check https://www.redhat.com/en/blog/transforming-development-experience-within-centos.*

Follow the instructions below for building libkinetic on CentOS Linux 8.

## Step 1: Prequisites

Libkinetic is written in C and uses GNU make for building. Some of libkinetic's dependencies are written in C++ and use automake. Use the following package names for installing gcc, g++, automake, and other libkinetic dependencies.

Part of libkinetic's building process requires statically linking standard C libraries. On CentOS Linux 8, this requires an explicit installation of the glibc-static package in addition to gcc. Installing glibc-static further requires enabling the powertools package repository of CentOS 8.

Use the following commands to enable powertools.

```bash
sudo yum install dnf-plugins-core
sudo yum config-manager --set-enabled powertools
```

Use `yum repolist` to verify if powertools has been enabled. After that, use the following commands to install required packages for building libkinetic. Note that CentOS Linux 8 uses pkgconf instead of pkgconfig.

```bash
sudo yum install gcc gcc-c++ cpp glibc-static
sudo yum install autoconf automake libtool pkgconf make
sudo yum install openssl-devel readline-devel
```

Use `gcc --version` to verify if gcc has been successfully installed. CentOS Linux 8 by default ships gcc 8.4.1, make 4.2.1, autoconf 2.69, automake 1.16.1, pkgconf 1.4.2, and openssl 1.1.1g.

## Step 2: Protobuf

Building libkinetic requires Google's protobuf 2.6.0 or later (but not protobuf-3). CentOS Linux 8 ships protobuf 3.5.0, so we need to build on our own. The latest protobuf 2 release is 2.6.1, which can be downloaded from protobuf's github site. Protobuf uses automake, as Step 1 prepares.

```bash
sudo yum install wget
wget https://github.com/protocolbuffers/protobuf/releases/download/v2.6.1/protobuf-2.6.1.tar.gz
tar xzf protobuf-2.6.1.tar.gz -C .
cd protobuf-2.6.1
./configure
make
sudo make install
```

Use `protoc --version` to verify if protobuf has been successfully installed. `sudo make install` will instal protobuf to /usr/local/.

## Step 3: Prepare Libkientic Sources

Libkientic's source code is maintained at gitlab. We will checkout the development branch as this will give us access to some of the kinetic's work-in-process features.

```bash
sudo yum install git
git clone --recurse-submodules -b develop git@gitlab.com:kinetic-storage/libkinetic.git libkinetic-devel
cd libkinetic-devel
```

CentOS Linux 8 installs git 2.27.0.

## Step 4: Build Libkientic

Building libkinetic consists of first generating the autoconf scripts for building protobuf-c embedded in the libkinetic codebase and then invoking a single make to build them all (this also includes other 3rd-party code included in the libkinetic codebase).

Building protobuf-c further requires using pkg-config to locate protobuf as a build dependency. The default pkg-config search path does not include /usr/local, so we need to explicit specify it through `PKG_CONFIG_PATH`.

Finally, we need to patch src/Makefile such that it lists /usr/lib64 in `LDFLAGS` so that the build process can locate the libm.a for static linking.

```bash
sudo yum install vim-enhanced
vim src/Makefile
```

Edit src/Makefile as follows.

```
diff --git a/src/Makefile b/src/Makefile
index 69a06d0..fae3e97 100644
--- a/src/Makefile
+++ b/src/Makefile
@@ -56,7 +56,7 @@ STATS =
 DEBUG =        -g -DLOGLEVEL=0 $(STATS)

 CFLAGS =       $(DEBUG) -I. -I$(BUILDDIR)/include -Wall -fpic -Wl,-export-dynamic
-LDFLAGS =      -L$(BUILDDIR)/lib -L/usr/lib/$(shell gcc -print-multiarch)
+LDFLAGS =      -L$(BUILDDIR)/lib -L /usr/lib64 -L/usr/lib/$(shell gcc -print-multiarch)
 LDLIBS =       -llist -lprotobuf-c -lm

 CP =           /bin/cp
```

Next, invoke the following commands to build libkinetic.

```bash
cd vendor/protobuf-c
./autogen.sh
cd ../..
env PKG_CONFIG_PATH=/usr/local/lib/pkgconfig make
```

After make, we will see a program named `kctl` in the `build/bin` directory. Ldd'ing it produces the following output.

```bash
linux-vdso.so.1 (0x00007ffce67f2000)
libreadline.so.7 => /lib64/libreadline.so.7 (0x00007fa4c71d3000)
libpthread.so.0 => /lib64/libpthread.so.0 (0x00007fa4c6fb3000)
libssl.so.1.1 => /lib64/libssl.so.1.1 (0x00007fa4c6d1f000)
libcrypto.so.1.1 => /lib64/libcrypto.so.1.1 (0x00007fa4c6839000)
libm.so.6 => /lib64/libm.so.6 (0x00007fa4c64b7000)
libc.so.6 => /lib64/libc.so.6 (0x00007fa4c60f2000)
libtinfo.so.6 => /lib64/libtinfo.so.6 (0x00007fa4c5ec5000)
/lib64/ld-linux-x86-64.so.2 (0x00007fa4c7422000)
libz.so.1 => /lib64/libz.so.1 (0x00007fa4c5cae000)
libdl.so.2 => /lib64/libdl.so.2 (0x00007fa4c5aaa000)
```

# CentOS Linux 7

CentOS Linux 7 mirrors RHEL 7. One can download CentOS 7 at https://www.centos.org/download/. Community support for CentOS Linux 7 will end on 2024-06-30. Follow the instructions below for building libkinetic on CentOS Linux 7.

## Step 1: Prequisites

Libkinetic is written in C and uses GNU make for building. Some of libkinetic's dependencies are written in C++ and use automake. Use the following package names for installing gcc, g++, automake, and a subset of libkinetic dependencies.

Part of libkinetic's building process requires statically linking standard C libraries. On CentOS Linux 7, this requires an explicit installation of the glibc-static package in addition to gcc.

```bash
sudo yum install gcc gcc-c++ cpp glibc-static
sudo yum install autoconf automake libtool pkgconfig make
sudo yum install openssl-devel readline-devel
```

Use `gcc --version` to verify if gcc has been successfully installed. CentOS Linux 7 by default ships gcc 4.8.5, make 3.82, autoconf 2.69, automake 1.13.4, pkgconfig 0.27.1, and openssl 1.0.2k-fips.

## Step 2: Protobuf

Building libkinetic requires Google's protobuf 2 with a version that is at least 2.6.0. CentOS Linux 7 ships protobuf 2.5.0, so we need to build on our own. The latest protobuf 2 release is 2.6.1, which can be downloaded from protobuf's github site. Protobuf uses automake, as Step 1 prepares.

```bash
sudo yum install wget
wget https://github.com/protocolbuffers/protobuf/releases/download/v2.6.1/protobuf-2.6.1.tar.gz
tar xzf protobuf-2.6.1.tar.gz -C .
cd protobuf-2.6.1
./configure
make
sudo make install
```

Use `protoc --version` to verify if protobuf has been successfully installed. `sudo make install` will instal protobuf to /usr/local/.

## Step 3: Prepare Libkientic Sources

Libkientic's source code is maintained at gitlab. We will checkout the development branch as this will give us access to some of the kinetic's work-in-process features.

```bash
sudo yum install git
git clone --recurse-submodules -b develop git@gitlab.com:kinetic-storage/libkinetic.git libkinetic-devel
cd libkinetic-devel
```

## Step 4: Build Libkientic

Building libkinetic consists of first generating the autoconf scripts for building protobuf-c embedded in the libkinetic codebase and then invoking a single make to build them all (this also includes other 3rd-party code included in the libkinetic codebase).

Building protobuf-c further requires using pkg-config to locate protobuf as a build dependency. The default pkg-config search path does not include /usr/local, so we need to explicit specify it through `PKG_CONFIG_PATH`.

I cannot find a solution in which libkinetic is statically linked into kctl. I got link errors *undefined reference to _dl_x86_cpu_features*. The following guide will try to build kctl in which libkinetic is dynamically linked.

First, we need to force gcc to compile with the gnu99 mode. Otherwise I got error *‘for’ loop initial declarations are only allowed in C99 mode* (when no mode is specified) and error *field ‘kiot_start’ has incomplete type: struct timespec kiot_start;* (when -std=c99 is specified).

To resolve this issue, add "CC = gcc -std=gnu99" to src/Makefile as follows.

```
diff --git a/src/Makefile b/src/Makefile
index 69a06d0..44a3fa2 100644
--- a/src/Makefile
+++ b/src/Makefile
@@ -55,6 +55,7 @@ STATS =
 # debuglevel: 0=no debug, 1=info, 2=debug; -fmax-errors=10 reduces shown errs
 DEBUG =        -g -DLOGLEVEL=0 $(STATS)

+CC =            gcc -std=gnu99
 CFLAGS =       $(DEBUG) -I. -I$(BUILDDIR)/include -Wall -fpic -Wl,-export-dynamic
 LDFLAGS =      -L$(BUILDDIR)/lib -L/usr/lib/$(shell gcc -print-multiarch)
 LDLIBS =       -llist -lprotobuf-c -lm
```

Next, the current libkinetic code is written against openssl 1.1.0 while CentOS Linux 7 ships openssl 1.0.0. Openssl made a few minor API changes along its 1.0 to 1.1 upgrade. So we need to patch the code to use the old calls.

To resolve this issue, change src/util.c as follows.

```
diff --git a/src/util.c b/src/util.c
index 91ad55c..9d20fd8 100644
--- a/src/util.c
+++ b/src/util.c
@@ -414,7 +414,7 @@ struct kbuffer compute_digest(struct kiovec *io_vec, size_t io_cnt, const char *
     if (!digest_result) { return (struct kbuffer) { .base = NULL, .len = 0 }; }

     // initialize context for calculating the digest message
-    EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
+    EVP_MD_CTX *mdctx = EVP_MD_CTX_create();

     EVP_DigestInit_ex(mdctx, digestfn_info, NULL);

@@ -427,7 +427,7 @@ struct kbuffer compute_digest(struct kiovec *io_vec, size_t io_cnt, const char *
     EVP_DigestFinal_ex(mdctx, digest_result, &final_digestlen);

     // cleanup the context
-    EVP_MD_CTX_free(mdctx);
+    EVP_MD_CTX_destroy(mdctx);

     return (struct kbuffer) {
         .len  = final_digestlen,
```

Then, change src/protocol_interface.c as follows.

```
diff --git a/src/protocol_interface.c b/src/protocol_interface.c
index 89aef18..3595544 100644
--- a/src/protocol_interface.c
+++ b/src/protocol_interface.c
@@ -73,12 +73,13 @@ typedef Com__Seagate__Kinetic__Proto__Message__PINauth      kauth_pin;
 int compute_hmac(kproto_msg_t *msg_data, char *key, uint32_t key_len) {
        int result_status;

-       HMAC_CTX *hmac_context = HMAC_CTX_new();
+       HMAC_CTX hmac_context;
+        HMAC_CTX_init(&hmac_context);

        // to self-document that we are using the default ENGINE (or whatever this param is used for)
        ENGINE *hash_impl = NULL;

-       result_status = HMAC_Init_ex(hmac_context, key, key_len, EVP_sha1(), hash_impl);
+       result_status = HMAC_Init_ex(&hmac_context, key, key_len, EVP_sha1(), hash_impl);
        if (!result_status) { return -1; }

        // TODO: what if the message has no command bytes?
@@ -86,14 +87,14 @@ int compute_hmac(kproto_msg_t *msg_data, char *key, uint32_t key_len) {
                uint32_t msg_len_bigendian = htonl(msg_data->commandbytes.len);

                result_status = HMAC_Update(
-                       hmac_context,
+                       &hmac_context,
                        (unsigned char *) &msg_len_bigendian,
                        sizeof(uint32_t)
                );
                if (!result_status) { return -1; }

                result_status = HMAC_Update(
-                       hmac_context,
+                       &hmac_context,
                        (unsigned char *) msg_data->commandbytes.data,
                        msg_data->commandbytes.len
                );
@@ -103,14 +104,14 @@ int compute_hmac(kproto_msg_t *msg_data, char *key, uint32_t key_len) {
        // finalize the digest into a string (allocated)
        void *hmac_digest = KI_MALLOC(sizeof(char) * SHA_DIGEST_LENGTH);
        result_status = HMAC_Final(
-               hmac_context,
+               &hmac_context,
                (unsigned char *) hmac_digest,
                (unsigned int *) &(msg_data->hmacauth->hmac.len)
        );
        if (!result_status) { return -1; }

        // free the HMAC context (leaves the result intact)

```

After that, we need to patch src/Makefile such that it lists /usr/lib64 in `LDFLAGS`  in order that the build process can locate the libm.a for making libkinetic.a (even though we will not use it for building kctl).

```
diff --git a/src/Makefile b/src/Makefile
index 69a06d0..10dc79a 100644
--- a/src/Makefile
+++ b/src/Makefile
@@ -55,8 +55,9 @@ STATS =
 # debuglevel: 0=no debug, 1=info, 2=debug; -fmax-errors=10 reduces shown errs
 DEBUG =        -g -DLOGLEVEL=0 $(STATS)

+CC =            gcc -std=gnu99
 CFLAGS =       $(DEBUG) -I. -I$(BUILDDIR)/include -Wall -fpic -Wl,-export-dynamic
-LDFLAGS =      -L$(BUILDDIR)/lib -L/usr/lib/$(shell gcc -print-multiarch)
+LDFLAGS =      -L$(BUILDDIR)/lib -L/usr/lib64 -L/usr/lib/$(shell gcc -print-multiarch)
 LDLIBS =       -llist -lprotobuf-c -lm

 CP =           /bin/cp
```

Next, force gcc gnu99 for toolbox in toolbox/bkv/Makefile and toolbox/kctl/Makefile. Otherwise I got error *\x used with no following hex digits*.

```
diff --git a/toolbox/bkv/Makefile b/toolbox/bkv/Makefile
index cace02a..e035538 100644
--- a/toolbox/bkv/Makefile
+++ b/toolbox/bkv/Makefile
@@ -28,6 +28,7 @@ DEPS =                $(SRCS:.c=.d)

 LIBDEPS =      $(BUILDLIB)/libkinetic.a

+CC =            gcc -std=gnu99
 BKV=           bkv
 CPPFLAGS =     -I$(BUILDINC)
 CFLAGS =       -g -Wall
diff --git a/toolbox/kctl/Makefile b/toolbox/kctl/Makefile
index 5709758..79bf4ee 100644
--- a/toolbox/kctl/Makefile
+++ b/toolbox/kctl/Makefile
@@ -33,6 +33,7 @@ DEPS =                $(SRCS:.c=.d)

 KCTLDEPS =     $(BUILDLIB)/libkinetic.a

+CC =            gcc -std=gnu99
 KCTL=          kctl
 CPPFLAGS =     -I$(BUILDINC)
 CFLAGS =       -g -Wall

```

Finally, change kctl and bkv to dynamically link libkinetic. This can be done through changing toolbox/bkv/Makefile and toolbox/kctl/Makefile as follows.

```
diff --git a/toolbox/bkv/Makefile b/toolbox/bkv/Makefile
index cace02a..62aee34 100644
--- a/toolbox/bkv/Makefile
+++ b/toolbox/bkv/Makefile
@@ -28,6 +28,7 @@ DEPS =                $(SRCS:.c=.d)

 LIBDEPS =      $(BUILDLIB)/libkinetic.a

+CC =            gcc -std=gnu99
 BKV=           bkv
 CPPFLAGS =     -I$(BUILDINC)
 CFLAGS =       -g -Wall
@@ -40,7 +41,7 @@ DYNAMIC =     -Wl,-Bdynamic
 # the shared libs will need to be installed or LD_LIBRARY_PATH
 # used to locate the shared libs
 LDLIBS =       -lreadline                              \
-               $(STATIC) -lkinetic                     \
+               -lkinetic -lprotobuf-c                  \
                $(DYNAMIC) -lpthread -lssl -lcrypto

 SANITY =       Sanity.bkv
diff --git a/toolbox/kctl/Makefile b/toolbox/kctl/Makefile
index 5709758..fa12486 100644
--- a/toolbox/kctl/Makefile
+++ b/toolbox/kctl/Makefile
@@ -33,6 +33,7 @@ DEPS =                $(SRCS:.c=.d)

 KCTLDEPS =     $(BUILDLIB)/libkinetic.a

+CC =            gcc -std=gnu99
 KCTL=          kctl
 CPPFLAGS =     -I$(BUILDINC)
 CFLAGS =       -g -Wall
@@ -45,7 +46,7 @@ DYNAMIC =     -Wl,-Bdynamic
 # the shared libs will need to be installed or LD_LIBRARY_PATH
 # used to locate the shared libs
 LDLIBS =       -lreadline                              \
-               $(STATIC) -lkinetic                     \
+               -lkinetic -lprotobuf-c                  \
                $(DYNAMIC) -lpthread -lssl -lcrypto -lm

 SANITY =       Sanity.kctl
```

Now, we may hit the build button.

```bash
cd vendor/protobuf-c
./autogen.sh
cd ../..
env PKG_CONFIG_PATH=/usr/local/lib/pkgconfig make
```

After make, we will see a program named `kctl` in the `build/bin` directory. Typing "env LD_LIBRARY_PATH=`pwd`/../lib ldd kctl" gives us the following output.

```
 env LD_LIBRARY_PATH=`pwd`/../lib ldd kctl
        linux-vdso.so.1 =>  (0x00007ffecddfb000)
        libreadline.so.6 => /lib64/libreadline.so.6 (0x00007f5d2c491000)
        libkinetic.so.1 => /root/libkinetic-devel2/build/bin/../lib/libkinetic.so.1 (0x00007f5d2c255000)
        libprotobuf-c.so.1 => /root/libkinetic-devel2/build/bin/../lib/libprotobuf-c.so.1 (0x00007f5d2c04c000)
        libpthread.so.0 => /lib64/libpthread.so.0 (0x00007f5d2be30000)
        libssl.so.10 => /lib64/libssl.so.10 (0x00007f5d2bbbe000)
        libcrypto.so.10 => /lib64/libcrypto.so.10 (0x00007f5d2b75b000)
        libm.so.6 => /lib64/libm.so.6 (0x00007f5d2b459000)
        libc.so.6 => /lib64/libc.so.6 (0x00007f5d2b08b000)
        libtinfo.so.5 => /lib64/libtinfo.so.5 (0x00007f5d2ae61000)
        /lib64/ld-linux-x86-64.so.2 (0x00007f5d2c6d7000)
        libgssapi_krb5.so.2 => /lib64/libgssapi_krb5.so.2 (0x00007f5d2ac14000)
        libkrb5.so.3 => /lib64/libkrb5.so.3 (0x00007f5d2a92b000)
        libcom_err.so.2 => /lib64/libcom_err.so.2 (0x00007f5d2a727000)
        libk5crypto.so.3 => /lib64/libk5crypto.so.3 (0x00007f5d2a4f4000)
        libdl.so.2 => /lib64/libdl.so.2 (0x00007f5d2a2f0000)
        libz.so.1 => /lib64/libz.so.1 (0x00007f5d2a0da000)
        libkrb5support.so.0 => /lib64/libkrb5support.so.0 (0x00007f5d29eca000)
        libkeyutils.so.1 => /lib64/libkeyutils.so.1 (0x00007f5d29cc6000)
        libresolv.so.2 => /lib64/libresolv.so.2 (0x00007f5d29aac000)
        libselinux.so.1 => /lib64/libselinux.so.1 (0x00007f5d29885000)
        libpcre.so.1 => /lib64/libpcre.so.1 (0x00007f5d29623000)
```

Done!
