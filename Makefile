# bC -- batari Basic to cc65 C transpiler

FLEX   ?= flex
BISON  ?= bison
CC     ?= cc
CFLAGS ?= -O2 -Wall -Wno-unused-function
CPPFLAGS += -I$(SRCDIR) -I$(OBJDIR) -Iruntime
CL65   ?= cl65

SRCDIR = src
OBJDIR = build
RUNTIME = runtime/bc_kernel.c

GEN = $(OBJDIR)/bc.tab.c $(OBJDIR)/bc.yy.c
OBJS = $(OBJDIR)/bc.tab.o $(OBJDIR)/bc.yy.o $(OBJDIR)/codegen.o $(OBJDIR)/main.o

all: bc demo.bin

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(OBJDIR)/bc.tab.c: $(SRCDIR)/bc.y | $(OBJDIR)
	$(BISON) -d -o $@ $<

$(OBJDIR)/bc.yy.c: $(SRCDIR)/bc.l | $(OBJDIR)
	$(FLEX) -o $@ $<

$(OBJDIR)/%.o: $(OBJDIR)/%.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(OBJDIR)/codegen.o: $(SRCDIR)/codegen.c $(SRCDIR)/codegen.h $(OBJDIR)/bc.tab.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(OBJDIR)/main.o: $(SRCDIR)/main.c $(SRCDIR)/codegen.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<
$(OBJDIR)/main.o: $(SRCDIR)/main.c $(SRCDIR)/codegen.h

bc: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

# end-to-end: .bas -> .c -> .bin via cc65's Atari 2600 target
demo.c: examples/demo.bas bc
	./bc -o demo.c examples/demo.bas

demo.o: demo.c
	$(CL65) -t atari2600 -Os -Iruntime -c -o $@ $<

demo.bin: demo.o
	$(CL65) -t atari2600 -C /usr/local/share/cc65/cfg/atari2600.cfg -o $@ demo.o

# host-side logic tests: run transpiled output natively against mocked TIA/RIOT
test: bc
	./bc -o $(OBJDIR)/host_gen.c test/host.bas
	$(CC) $(CFLAGS) -c -Dmain=bC_main -Itest/mock -Iruntime -o $(OBJDIR)/host_gen.o $(OBJDIR)/host_gen.c
	$(CC) $(CFLAGS) -Itest/mock -o $(OBJDIR)/host_test test/host_driver.c $(OBJDIR)/host_gen.o
	$(OBJDIR)/host_test

clean:
	rm -rf build demo.c demo.o demo.bin bc
