CXX := g++
CXXFLAGS := -O3 -std=c++20 -march=native -pthread
TARGET := rhsum
SRC := rhsum.cpp
PREFIX ?= $(HOME)/.local
BIN_DIR ?= $(PREFIX)/bin
RC_FILES ?= $(HOME)/.profile $(HOME)/.bashrc
SYSTEM_PREFIX ?= /usr/local
SYSTEM_BIN_DIR ?= $(SYSTEM_PREFIX)/bin
SYSTEM_PROFILED ?= /etc/profile.d/rhsum-path.sh

.PHONY: all clean test test-valgrind install uninstall install-system uninstall-system

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET)

test: $(TARGET)
	./$(TARGET) --help >/dev/null
	python3 scripts/run_tests.py
	python3 scripts/run_tests.py --valgrind

test-valgrind: $(TARGET)
	python3 scripts/run_tests.py --valgrind

install: $(TARGET)
	mkdir -p "$(BIN_DIR)"
	cp "$(TARGET)" "$(BIN_DIR)/$(TARGET)"
	@for rc in $(RC_FILES); do \
		touch "$$rc"; \
		if ! grep -Fq "# rhsum-install-path $(BIN_DIR)" "$$rc"; then \
			printf '\n# rhsum-install-path %s\nexport PATH="%s:$$PATH"\n' "$(BIN_DIR)" "$(BIN_DIR)" >> "$$rc"; \
		fi; \
	done
	@printf 'Installed %s to %s\n' "$(TARGET)" "$(BIN_DIR)"
	@printf 'Open a new shell or run: export PATH="%s:$$PATH"\n' "$(BIN_DIR)"

uninstall:
	rm -f "$(BIN_DIR)/$(TARGET)"
	@for rc in $(RC_FILES); do \
		if [ -f "$$rc" ]; then \
			awk -v marker="# rhsum-install-path $(BIN_DIR)" -v pathline="export PATH=\"$(BIN_DIR):\$$PATH\"" '\
				$$0 == marker { skip=1; next } \
				skip && $$0 == pathline { skip=0; next } \
				{ skip=0; print } \
			' "$$rc" > "$$rc.tmp" && mv "$$rc.tmp" "$$rc"; \
		fi; \
	done
	@printf 'Removed %s from %s\n' "$(TARGET)" "$(BIN_DIR)"

install-system: $(TARGET)
	mkdir -p "$(SYSTEM_BIN_DIR)"
	mkdir -p "$(dir $(SYSTEM_PROFILED))"
	cp "$(TARGET)" "$(SYSTEM_BIN_DIR)/$(TARGET)"
	printf '# rhsum system path\nexport PATH="%s:$$PATH"\n' "$(SYSTEM_BIN_DIR)" > "$(SYSTEM_PROFILED)"
	@printf 'Installed %s to %s\n' "$(TARGET)" "$(SYSTEM_BIN_DIR)"
	@printf 'Installed system PATH snippet to %s\n' "$(SYSTEM_PROFILED)"
	@printf 'This affects new login shells for all users.\n'

uninstall-system:
	rm -f "$(SYSTEM_BIN_DIR)/$(TARGET)"
	rm -f "$(SYSTEM_PROFILED)"
	@printf 'Removed %s from %s\n' "$(TARGET)" "$(SYSTEM_BIN_DIR)"
	@printf 'Removed system PATH snippet %s\n' "$(SYSTEM_PROFILED)"

clean:
	rm -f $(TARGET) $(TARGET).exe
