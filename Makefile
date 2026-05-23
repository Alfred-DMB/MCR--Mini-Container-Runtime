CC       = gcc
CFLAGS   = -Wall -Wextra
LDFLAGS  =
TARGET   = mcr
SRC      = src/main.c src/ns.c src/cgroup.c src/rootfs.c src/seccomp.c src/cap.c src/utils.c
BUILDDIR = build
OBJ      = $(patsubst src/%.c, $(BUILDDIR)/%.o, $(SRC))

$(TARGET): $(OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

$(BUILDDIR)/%.o: src/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

clean:
	rm -rf $(BUILDDIR) $(TARGET)

test: $(TARGET)
	@chmod +x test/run_tests.sh
	@bash test/run_tests.sh

.PHONY: clean test
