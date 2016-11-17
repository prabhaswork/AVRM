.PHONY:	all lib app

CFLAGS = -I$(lindevkit_PATH)/include/gstreamer-0.10 -I$(lindevkit_PATH)/include/glib-2.0 -I$(lindevkit_PATH)/lib/glib-2.0/include -I$(lindevkit_PATH)/include/libxml2  -Iinc -Isrc
LIBSPATH = $(lindevkit_PATH)/lib
LIBS =  -lgstbase-0.10 -lgstreamer-0.10 -lgobject-2.0 -lgmodule-2.0 -lxml2 -lgthread-2.0 -lrt -lglib-2.0 -lz -lgstapp-0.10

all:	lib app
	
lib:
	mkdir -p lib
	$(CROSS_COMPILE)gcc -g $(CFLAGS) -c -fPIC src/avrm.c src/avrmringbuffer.c -L$(LIBSPATH) $(LIBS)
	$(CROSS_COMPILE)gcc -g -shared -o lib/libavrm.so avrm.o avrmringbuffer.o
	rm avrm.o
	rm avrmringbuffer.o
app:
	mkdir -p testApp/bin
	$(CROSS_COMPILE)gcc -g $(CFLAGS) testApp/src/avrm_demo.c -L$(LIBSPATH) $(LIBS) -Llib -lavrm -o testApp/bin/avrm_demo_bin

clean:
	rm -rf lib
	rm -rf testApp/bin
	#$(CROSS_COMPILE)gcc -g $(CFLAGS) -c -fPIC src/avrm.c  -L$(LIBSPATH) $(LIBS)
	#$(CROSS_COMPILE)gcc -g -shared -o lib/libavrm.so avrm.o 
