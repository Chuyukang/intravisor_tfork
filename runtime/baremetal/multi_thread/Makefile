include ../../../config.mak

CFLAGS=-fPIC -fPIE -pie -g

ifneq ($(MODE),sim)
endif

DEPS = 
OBJ = hello.o tramps.o hostcalls.o

default: libhello_debug.so

%.o: %.c $(DEPS)
	$(CC_CHERI) $(CC_CHERI_FLAGS) -c -o $@ $< $(CFLAGS)

tramps.o: tramps.S
	$(CC_CHERI) $(CC_CHERI_FLAGS) $(CFLAGS) -c -o tramps.o tramps.S

bypass.o: bypass.S
	$(CC_CHERI) $(CC_CHERI_FLAGS) $(CFLAGS) -c -o bypass.o bypass.S

libhello.so: $(OBJ)
	$(CC_CHERI) $(CC_CHERI_FLAGS) $(CFLAGS) -o $@ -nostdinc -nostdlib -Wl,-ehello_asm -fPIE -fPIC $(OBJ)

libhello_debug.so: libhello.so
	$(CC_CHERI) $(CC_CHERI_FLAGS) $(CFLAGS) -o $@ -nostdinc -nostdlib -Wl,--image-base,0x20000000 -Wl,-ehello_asm -fPIE -fPIC $(OBJ)
	$(CC_CHERI) $(CC_CHERI_FLAGS) $(CFLAGS) -o libhello_debug3.so -nostdinc -nostdlib -Wl,--image-base,0x30000000 -Wl,-ehello_asm -fPIE -fPIC $(OBJ)

libhello_bypass_debug.so: $(OBJ) bypass.o
	$(CC_CHERI) $(CC_CHERI_FLAGS) $(CFLAGS) -o libhello_bypass.so -nostdinc -nostdlib -Wl,-ehello_asm -fPIE -fPIC $(OBJ) bypass.o
	$(CC_CHERI) $(CC_CHERI_FLAGS) $(CFLAGS) -o $@ -nostdinc -nostdlib -Wl,--image-base,0x20000000 -Wl,-ehello_asm -fPIE -fPIC $(OBJ) bypass.o
	$(CC_CHERI) $(CC_CHERI_FLAGS) $(CFLAGS) -o libhello_bypass_debug3.so -nostdinc -nostdlib -Wl,--image-base,0x30000000 -Wl,-ehello_asm -fPIE -fPIC $(OBJ) bypass.o

sync:
	scp libhello* root@cheribsd:/intravisor/baremetal/multi_thread
	scp hello.c hostcalls.c root@cheribsd:/intravisor/baremetal/multi_thread
	# scp multi_thread.yaml root@cheribsd:/intravisor/baremetal/multi_thread

clean:
	rm -rf ./*.o ./*.so