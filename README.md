# rhsum

`rhsum` (Rheo Sum) is a single-file C++ utility for deterministic polynomial hashing of a file or directory tree treated as one continuous virtual byte stream.

The canonical build command for this project is:

```bash
g++ -O3 -std=c++20 -march=native -pthread rhsum.cpp -o rhsum
```

## Layout

```text
.
├── .github/workflows/ci.yml
├── .gitignore
├── Makefile
├── README.md
├── rhsum.cpp
├── scripts/build.py
├── scripts/run_tests.py
├── tests/test_constant_1mb_42.py
├── tests/test_directory_tree.py
├── tests/testlib.py
├── tests/test_follow_symlink_name.py
├── tests/test_follow_symlinks.py
└── tests/test_special_name_only.py
```

## Requirements

- C++20-capable environment
- `g++` with C++20 support
- `pthread` for the canonical Unix build

Linux and other POSIX platforms use an `mmap` fast path for file reads. Other platforms fall back to standard C++ file I/O.

## Build

Canonical:

```bash
g++ -O3 -std=c++20 -march=native -pthread rhsum.cpp -o rhsum
```

Convenience wrapper:

```bash
make
```

Cross-platform build wrapper:

```bash
python3 scripts/build.py
```

Install into your user `PATH`:

```bash
make install
```

Install system-wide for all users:

```bash
sudo make install-system
```

Run tests:

```bash
make test
```

Run only the Valgrind pass:

```bash
make test-valgrind
```

Cross-platform test runner:

```bash
python3 scripts/run_tests.py
```

Valgrind pass on Linux:

```bash
python3 scripts/run_tests.py --valgrind
```

## Usage

```bash
./rhsum [options] <file|dir>
```

### Options

- `-T, --threads <N>`: number of worker threads
- `-R, --recursive`: recursively process directories
- `-L, --follow-symlinks`: follow symbolic links
- `-v`: print execution statistics to stderr
- `--help`: show usage help

## Examples

```bash
./rhsum rhsum.cpp
./rhsum -R .
./rhsum -R -T 8 -v .
```

## Notes

- Input paths are sorted lexicographically before composition to keep output deterministic.
- If the input is a single file, `rhsum` hashes only the file bytes.
- If the input is a directory, `rhsum` hashes relative paths from the traversal root for both files and directories, plus file contents.
- Filesystem entities that are neither regular files nor directories contribute only their relative names and type markers, not any payload bytes.
- Empty directories therefore affect the resulting hash.
- Symlink and FIFO tests auto-skip on platforms where those primitives are unavailable or restricted.
- `make test` also runs the suite under Valgrind when Valgrind is available; this catches many memory errors and leaks in covered paths, but it is not a formal proof that no memory bugs exist.
- For a 1 MiB file filled with byte value `42`, the direct-file hash is `d97a894407600000`.
- The repository currently has no license file. Add one before publishing if reuse by others is intended.

`make install` copies `rhsum` to `~/.local/bin` by default and appends that directory to `~/.profile` and `~/.bashrc` if needed. It affects new shell sessions; the current shell still needs `export PATH="$HOME/.local/bin:$PATH"` or a restart.

`make install-system` installs `rhsum` into `/usr/local/bin` and writes `/etc/profile.d/rhsum-path.sh`. This usually requires root and affects new login shells for all users.
