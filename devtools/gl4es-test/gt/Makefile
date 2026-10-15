OUT ?= $(HOME)/gl4es-test/bin
CFLAGS ?= -O1 -g -Wall -Wno-unused-function -Wno-misleading-indentation
SRC = main.c pixels.c arb.c ffp.c state.c fbo.c glsl.c corpus.c

$(OUT)/gt: $(SRC) gt.h
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) -o $@.tmp $(SRC) -lGL -lX11 -lm && mv -f $@.tmp $@   # atomic: runs may be using it
