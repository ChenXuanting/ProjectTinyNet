K=kernel
U=user
LWIP=lwip

OBJS = \
  $K/entry.o \
  $K/start.o \
  $K/console.o \
  $K/printf.o \
  $K/uart.o \
  $K/kalloc.o \
  $K/spinlock.o \
  $K/string.o \
  $K/main.o \
  $K/vm.o \
  $K/proc.o \
  $K/swtch.o \
  $K/trampoline.o \
  $K/trap.o \
  $K/syscall.o \
  $K/sysproc.o \
  $K/bio.o \
  $K/fs.o \
  $K/log.o \
  $K/sleeplock.o \
  $K/file.o \
  $K/pipe.o \
  $K/exec.o \
  $K/sysfile.o \
  $K/kernelvec.o \
  $K/plic.o \
  $K/virtio_disk.o \
  $K/buddy.o \
  $K/list.o

# uncomment for lab net
OBJS += \
  $K/net.o \
  $K/socket.o \
  $K/virtio_net.o \
  $(LWIP)/core/init.o \
  $(LWIP)/core/def.o \
  $(LWIP)/core/dns.o \
  $(LWIP)/core/inet_chksum.o \
  $(LWIP)/core/ip.o \
  $(LWIP)/core/mem.o \
  $(LWIP)/core/memp.o \
  $(LWIP)/core/netif.o \
  $(LWIP)/core/pbuf.o \
  $(LWIP)/core/raw.o \
  $(LWIP)/core/stats.o \
  $(LWIP)/core/sys.o \
  $(LWIP)/core/tcp.o \
  $(LWIP)/core/tcp_in.o \
  $(LWIP)/core/tcp_out.o \
  $(LWIP)/core/timeouts.o \
  $(LWIP)/core/udp.o \
  $(LWIP)/core/ipv4/autoip.o \
  $(LWIP)/core/ipv4/dhcp.o \
  $(LWIP)/core/ipv4/etharp.o \
  $(LWIP)/core/ipv4/icmp.o \
  $(LWIP)/core/ipv4/igmp.o \
  $(LWIP)/core/ipv4/ip4_frag.o \
  $(LWIP)/core/ipv4/ip4.o \
  $(LWIP)/core/ipv4/ip4_addr.o \
  $(LWIP)/api/err.o \
  $(LWIP)/netif/ethernet.o \

# riscv64-unknown-elf- or riscv64-linux-gnu-
# perhaps in /opt/riscv/bin
#TOOLPREFIX =

# Try to infer the correct TOOLPREFIX if not set
ifndef TOOLPREFIX
TOOLPREFIX := $(shell if riscv64-unknown-elf-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1; \
	then echo 'riscv64-unknown-elf-'; \
	elif riscv64-linux-gnu-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1; \
	then echo 'riscv64-linux-gnu-'; \
	else echo "***" 1>&2; \
	echo "*** Error: Couldn't find an riscv64 version of GCC/binutils." 1>&2; \
	echo "*** To turn off this error, run 'gmake TOOLPREFIX= ...'." 1>&2; \
	echo "***" 1>&2; exit 1; fi)
endif

QEMU = qemu-system-riscv64

CC = $(TOOLPREFIX)gcc
AS = $(TOOLPREFIX)gas
LD = $(TOOLPREFIX)ld
# GDB = $(TOOLPREFIX)gdb
GDB = gdb-multiarch
OBJCOPY = $(TOOLPREFIX)objcopy
OBJDUMP = $(TOOLPREFIX)objdump

CFLAGS = -Wall -Werror -O -fno-omit-frame-pointer -ggdb
CFLAGS += -MD
CFLAGS += -mcmodel=medany
CFLAGS += -ffreestanding -fno-common -nostdlib -mno-relax
CFLAGS += -I.
CFLAGS += $(shell $(CC) -fno-stack-protector -E -x c /dev/null >/dev/null 2>&1 && echo -fno-stack-protector)

# Disable PIE when possible (for Ubuntu 16.10 toolchain)
ifneq ($(shell $(CC) -dumpspecs 2>/dev/null | grep -e '[^f]no-pie'),)
CFLAGS += -fno-pie -no-pie
endif
ifneq ($(shell $(CC) -dumpspecs 2>/dev/null | grep -e '[^f]nopie'),)
CFLAGS += -fno-pie -nopie
endif

CFLAGS += -I $K/lwip -I $(LWIP)/include

LDFLAGS = -z max-page-size=4096

$K/kernel: $(OBJS) $K/kernel.ld $U/initcode
	$(LD) $(LDFLAGS) -T $K/kernel.ld -o $K/kernel $(OBJS)
	$(OBJDUMP) -S $K/kernel > $K/kernel.asm
	$(OBJDUMP) -t $K/kernel | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $K/kernel.sym

$U/initcode: $U/initcode.S
	$(CC) $(CFLAGS) -march=rv64g -nostdinc -I. -Ikernel -c $U/initcode.S -o $U/initcode.o
	$(LD) $(LDFLAGS) -N -e start -Ttext 0 -o $U/initcode.out $U/initcode.o
	$(OBJCOPY) -S -O binary $U/initcode.out $U/initcode
	$(OBJDUMP) -S $U/initcode.o > $U/initcode.asm

tags: $(OBJS) _init
	etags *.S *.c

ULIB = $U/ulib.o $U/usys.o $U/printf.o $U/umalloc.o

_%: %.o $(ULIB)
	$(LD) $(LDFLAGS) -N -e main -Ttext 0 -o $@ $^
	$(OBJDUMP) -S $@ > $*.asm
	$(OBJDUMP) -t $@ | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $*.sym

$U/usys.S : $U/usys.pl
	perl $U/usys.pl > $U/usys.S

$U/usys.o : $U/usys.S
	$(CC) $(CFLAGS) -c -o $U/usys.o $U/usys.S

$U/_forktest: $U/forktest.o $(ULIB)
	# forktest has less library code linked in - needs to be small
	# in order to be able to max out the proc table.
	$(LD) $(LDFLAGS) -N -e main -Ttext 0 -o $U/_forktest $U/forktest.o $U/ulib.o $U/usys.o
	$(OBJDUMP) -S $U/_forktest > $U/forktest.asm

$U/_uthread: $U/uthread.o $U/uthread_switch.o $(ULIB)
	$(LD) $(LDFLAGS) -N -e main -Ttext 0 -o $U/_uthread $U/uthread.o $U/uthread_switch.o $(ULIB)
	$(OBJDUMP) -S $U/_uthread > $U/uthread.asm

mkfs/mkfs: mkfs/mkfs.c $K/fs.h
	gcc -Werror -Wall -I. -o mkfs/mkfs mkfs/mkfs.c

# Prevent deletion of intermediate files, e.g. cat.o, after first build, so
# that disk image changes after first build are persistent until clean.  More
# details:
# http://www.gnu.org/software/make/manual/html_node/Chained-Rules.html
.PRECIOUS: %.o

UPROGS=\
	$U/_lazytests\
	$U/_cat\
	$U/_echo\
	$U/_forktest\
	$U/_grep\
	$U/_init\
	$U/_kill\
	$U/_ln\
	$U/_ls\
	$U/_mkdir\
	$U/_rm\
	$U/_sh\
	$U/_stressfs\
	$U/_usertests\
	$U/_wc\
	$U/_zombie\
	$U/_cowtest\
	$U/_uthread\
	$U/_call\
	$U/_kalloctest\
	$U/_bcachetest\
	$U/_alloctest\
	$U/_specialtest\
	# $U/_symlinktest\

fs.img: mkfs/mkfs README user/xargstest.sh $(UPROGS)
	mkfs/mkfs fs.img README user/xargstest.sh $(UPROGS)

-include kernel/*.d user/*.d
-include lwip/api/*.d lwip/core/*.d lwip/core/ipv4/*.d lwip/netif/*.d

clean:
	rm -f *.tex *.dvi *.idx *.aux *.log *.ind *.ilg \
	*/*.o */*.d */*.asm */*.sym \
	$(LWIP)/*/*.o $(LWIP)/*/*.d \
	$(LWIP)/*/*/*.o $(LWIP)/*/*/*.d \
	$U/initcode $U/initcode.out $K/kernel fs.img \
	mkfs/mkfs .gdbinit \
        $U/usys.S \
	$(UPROGS)

# try to generate a unique GDB port
GDBPORT = $(shell expr `id -u` % 5000 + 25000)
PORT80  = $(shell expr $(GDBPORT) + 1)
# QEMU's gdb stub command line changed in 0.11
QEMUGDB = $(shell if $(QEMU) -help | grep -q '^-gdb'; \
	then echo "-gdb tcp::$(GDBPORT)"; \
	else echo "-s -p $(GDBPORT)"; fi)
ifndef CPUS
CPUS := 3
endif

QEMUEXTRA = 
QEMUOPTS = -machine virt -global virtio-mmio.force-legacy=false
QEMUOPTS += -bios none -kernel $K/kernel -m 128M -smp $(CPUS) -nographic
QEMUOPTS += -drive file=fs.img,if=none,format=raw,id=x0 -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0
QEMUOPTS += -no-user-config
QEMUOPTS += -device virtio-net-device,bus=virtio-mmio-bus.1,netdev=en0 -object filter-dump,id=f0,netdev=en0,file=en0.pcap
# to foward a host port $(PORT80) to port 80 inside QEMU,
# use "-netdev type=user,id=en0,hostfwd=tcp::$(PORT80)-:80"
QEMUOPTS += -netdev type=user,id=en0

qemu: $K/kernel fs.img
	$(QEMU) $(QEMUOPTS)

qemu-trace: $K/kernel stacktrace fs.img
	$(QEMU) $(QEMUOPTS) | ./stacktrace

.gdbinit: .gdbinit.tmpl-riscv
	sed "s/:1234/:$(GDBPORT)/" < $^ > $@

qemu-gdb: $K/kernel .gdbinit fs.img
	@echo "*** Now run 'make gdb' in another window." 1>&2
	$(QEMU) $(QEMUOPTS) -S $(QEMUGDB)

gdb: $K/kernel .gdbinit
	$(GDB)

##
##  FOR submitting lab solutions
##

LAB := $(shell git symbolic-ref --short HEAD 2> /dev/null)
ifeq ($(LAB),)
LAB := $(shell cat conf/LAB)
endif

ifneq ($(V),@)
GRADEFLAGS += -v
endif

print-gdbport:
	@echo $(GDBPORT)

grade:
	@echo $(MAKE) clean
	@$(MAKE) clean || \
	  (echo "'make clean' failed.  HINT: Do you have another running instance of xv6?" && exit 1)
	./grade-lab-$(LAB) $(GRADEFLAGS)

handin-check:
	@if ! test -d .git; then \
		echo No .git directory, is this a git repository?; \
		false; \
	fi
	@if test "$$(git symbolic-ref HEAD)" != refs/heads/$(LAB); then \
		git branch; \
		read -p "You are not on the $(LAB) branch.  Hand-in the current branch? [y/N] " r; \
		test "$$r" = y; \
	fi
	@if ! git diff-files --quiet || ! git diff-index --quiet --cached HEAD; then \
		git status -s; \
		echo; \
		echo "You have uncomitted changes.  Please commit or stash them."; \
		false; \
	fi
	@if test -n "`git status -s`"; then \
		git status -s; \
		read -p "Untracked files will not be handed in.  Continue? [y/N] " r; \
		test "$$r" = y; \
	fi

tarball: handin-check
	echo $(LAB) > conf/LAB
	git archive --format=tar -o lab-$(LAB)-handin.tar HEAD
	tar rf lab-$(LAB)-handin.tar conf/LAB
	gzip -f lab-$(LAB)-handin.tar

.PHONY: qemu qemu-gdb gdb qemu-trace tarball clean grade handin-check
