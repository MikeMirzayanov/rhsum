#include <iostream>
#include <vector>
#include <thread>
#include <string>
#include <fstream>
#include <cstdint>
#include <iomanip>
#include <chrono>
#include <algorithm>
#include <sstream>
#include <filesystem>
#include <atomic>
#include <limits>
#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

using namespace std;
namespace fs = std::filesystem;

typedef uint64_t u64;
typedef int64_t i64;

const u64 P = 1000000000000037ULL;
const int K = 8;
const u64 AUTO_THREAD_GRANULARITY = 256ULL * 1024 * 1024;
const size_t STREAM_BUFFER_SIZE = 8ULL * 1024 * 1024;

// --- Mathematical Functions ---

u64 power(u64 base, u64 exp) {
    u64 res = 1;
    while (exp > 0) {
        if (exp & 1) res *= base;
        base *= base;
        exp >>= 1;
    }
    return res;
}

// ILP optimized polynomial hash for a memory block
u64 compute_hash_raw(const uint8_t* data, size_t length) {
    u64 h[K] = {0};
    u64 p_pow = 1;
    const u64 P_K = power(P, K);
    u64 p_small[K];
    p_small[0] = 1;
    for (int j = 1; j < K; ++j) p_small[j] = p_small[j-1] * P;

    size_t i = 0;
    for (; i + K <= length; i += K) {
        for (int j = 0; j < K; ++j) {
            h[j] += (u64)data[i + j] * (p_pow * p_small[j]);
        }
        p_pow *= P_K;
    }
    for (; i < length; ++i) {
        h[0] += (u64)data[i] * p_pow;
        p_pow *= P;
    }
    u64 total_h = 0;
    for (int j = 0; j < K; ++j) total_h += h[j];
    return total_h;
}

size_t choose_chunk_count(size_t length, int num_threads) {
    if (length == 0) return 0;
    const u64 size_based_chunks = max<u64>(1, (length + AUTO_THREAD_GRANULARITY - 1) / AUTO_THREAD_GRANULARITY);
    return min<size_t>(max(1, num_threads), size_based_chunks);
}

// --- Directory Traversal & Logic ---

struct HashTask {
    string rel_path;
    string abs_path;
    bool is_dir;
    bool is_file;
    uint8_t meta_type;
    u64 meta_size;
    u64 data_size;   // Size of file content
    u64 global_offset;
    u64 meta_hash;
    u64 data_hash;
};

struct EntryInfo {
    fs::path physical_path;
    string logical_rel_path;
    bool is_dir;
    bool is_file;
    bool is_other;
};

bool classify_entry(
    const fs::directory_entry& entry,
    bool follow_symlinks,
    bool* is_dir,
    bool* is_file,
    bool* is_other,
    string* error
) {
    error_code ec;
    const fs::file_status symlink_st = entry.symlink_status(ec);
    if (ec) {
        *error = "Failed to inspect path: " + entry.path().string() + " (" + ec.message() + ")";
        return false;
    }

    if (fs::is_symlink(symlink_st) && !follow_symlinks) {
        *is_dir = false;
        *is_file = false;
        *is_other = true;
        return true;
    }

    fs::file_status st = symlink_st;
    if (fs::is_symlink(symlink_st) && follow_symlinks) {
        st = entry.status(ec);
        if (ec) {
            *error = "Failed to inspect path: " + entry.path().string() + " (" + ec.message() + ")";
            return false;
        }
    }

    *is_dir = fs::is_directory(st);
    *is_file = fs::is_regular_file(st);
    *is_other = !*is_dir && !*is_file;
    return true;
}

bool collect_directory_entries(
    const fs::path& root_path,
    const fs::path& dir_path,
    bool recursive,
    bool follow_symlinks,
    vector<EntryInfo>* entries,
    string* error
) {
    error_code ec;
    fs::directory_iterator it(dir_path, fs::directory_options::none, ec), end;
    if (ec) {
        *error = "Failed to list directory: " + dir_path.string() + " (" + ec.message() + ")";
        return false;
    }

    for (; it != end; it.increment(ec)) {
        if (ec) {
            *error = "Failed to traverse directory: " + dir_path.string() + " (" + ec.message() + ")";
            return false;
        }

        const fs::directory_entry& entry = *it;
        const fs::path path = entry.path();
        bool is_dir = false;
        bool is_file = false;
        bool is_other = false;
        if (!classify_entry(entry, follow_symlinks, &is_dir, &is_file, &is_other, error)) {
            return false;
        }

        entries->push_back({
            path,
            path.lexically_relative(root_path).string(),
            is_dir,
            is_file,
            is_other
        });

        if (recursive && is_dir) {
            if (!collect_directory_entries(root_path, path, recursive, follow_symlinks, entries, error)) {
                return false;
            }
        }
    }

    return true;
}

string format_size(size_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    double size = (double)bytes;
    int i = 0;
    while (size >= 1024 && i < 4) { size /= 1024; i++; }
    stringstream ss;
    ss << fixed << setprecision(2) << size << " " << units[i];
    return ss.str();
}

u64 compute_meta_hash(const string& rel_path, uint8_t meta_type, u64* meta_size_out) {
    vector<uint8_t> meta_blob;
    meta_blob.push_back(meta_type);
    uint32_t path_len = (uint32_t)rel_path.size();
    for (int b = 0; b < 4; ++b) meta_blob.push_back((path_len >> (b * 8)) & 0xFF);
    for (char c : rel_path) meta_blob.push_back((uint8_t)c);
    *meta_size_out = meta_blob.size();
    return compute_hash_raw(meta_blob.data(), meta_blob.size());
}

u64 compute_hash_range_stream(const string& path, size_t offset, size_t length, bool* ok) {
    *ok = false;
    if (length == 0) {
        *ok = true;
        return 0;
    }

    ifstream file(path, ios::binary);
    if (!file) return 0;

    file.seekg(static_cast<streamoff>(offset), ios::beg);
    if (!file) return 0;

    vector<uint8_t> buffer(min(STREAM_BUFFER_SIZE, length));
    u64 total_hash = 0;
    u64 range_offset_power = 1;
    size_t remaining = length;

    while (remaining > 0) {
        const size_t to_read = min(buffer.size(), remaining);
        file.read(reinterpret_cast<char*>(buffer.data()), static_cast<streamsize>(to_read));
        const size_t bytes_read = static_cast<size_t>(file.gcount());
        if (bytes_read == 0) break;

        total_hash += compute_hash_raw(buffer.data(), bytes_read) * range_offset_power;
        range_offset_power *= power(P, bytes_read);
        remaining -= bytes_read;

        if (bytes_read < to_read) break;
    }

    if (remaining != 0 || file.bad()) return 0;

    *ok = true;
    return total_hash;
}

u64 combine_chunk_hashes(const vector<u64>& chunk_hashes, const vector<size_t>& chunk_sizes) {
    u64 total_hash = 0;
    u64 offset_power = 1;
    for (size_t chunk_index = 0; chunk_index < chunk_hashes.size(); ++chunk_index) {
        total_hash += chunk_hashes[chunk_index] * offset_power;
        offset_power *= power(P, chunk_sizes[chunk_index]);
    }
    return total_hash;
}

#if defined(__unix__) || defined(__APPLE__)
u64 compute_file_hash_mmap(const string& path, size_t length, int num_threads, bool* ok) {
    *ok = false;
    if (length == 0) {
        *ok = true;
        return 0;
    }

    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) return 0;

    void* mapped = mmap(nullptr, length, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (mapped == MAP_FAILED) return 0;

    const uint8_t* data = static_cast<const uint8_t*>(mapped);
    const size_t chunk_count = choose_chunk_count(length, num_threads);
    vector<u64> chunk_hashes(chunk_count, 0);
    vector<size_t> chunk_sizes(chunk_count, 0);
    vector<thread> workers;
    workers.reserve(chunk_count);

    const size_t base_chunk_size = length / chunk_count;
    const size_t remainder = length % chunk_count;

    size_t offset = 0;
    for (size_t chunk_index = 0; chunk_index < chunk_count; ++chunk_index) {
        const size_t chunk_size = base_chunk_size + (chunk_index < remainder ? 1 : 0);
        chunk_sizes[chunk_index] = chunk_size;
        workers.emplace_back([&, chunk_index, offset, chunk_size]() {
            chunk_hashes[chunk_index] = compute_hash_raw(data + offset, chunk_size);
        });
        offset += chunk_size;
    }

    for (auto& worker : workers) worker.join();
    munmap(const_cast<uint8_t*>(data), length);

    *ok = true;
    return combine_chunk_hashes(chunk_hashes, chunk_sizes);
}
#endif

u64 compute_file_hash_streaming(const string& path, size_t length, int num_threads, bool* ok) {
    *ok = false;
    if (length == 0) {
        *ok = true;
        return 0;
    }

    const size_t chunk_count = choose_chunk_count(length, num_threads);
    vector<u64> chunk_hashes(chunk_count, 0);
    vector<size_t> chunk_sizes(chunk_count, 0);
    vector<uint8_t> chunk_ok(chunk_count, 0);
    vector<thread> workers;
    workers.reserve(chunk_count);

    const size_t base_chunk_size = length / chunk_count;
    const size_t remainder = length % chunk_count;

    size_t offset = 0;
    for (size_t chunk_index = 0; chunk_index < chunk_count; ++chunk_index) {
        const size_t chunk_size = base_chunk_size + (chunk_index < remainder ? 1 : 0);
        chunk_sizes[chunk_index] = chunk_size;
        workers.emplace_back([&, chunk_index, offset, chunk_size]() {
            bool local_ok = false;
            chunk_hashes[chunk_index] = compute_hash_range_stream(path, offset, chunk_size, &local_ok);
            chunk_ok[chunk_index] = local_ok ? 1 : 0;
        });
        offset += chunk_size;
    }

    for (auto& worker : workers) worker.join();
    for (uint8_t chunk_result : chunk_ok) {
        if (!chunk_result) return 0;
    }
    *ok = true;
    return combine_chunk_hashes(chunk_hashes, chunk_sizes);
}

u64 compute_file_hash(const string& path, size_t length, int num_threads, bool* ok) {
#if defined(__unix__) || defined(__APPLE__)
    bool mmap_ok = false;
    const u64 mmap_hash = compute_file_hash_mmap(path, length, num_threads, &mmap_ok);
    if (mmap_ok) {
        *ok = true;
        return mmap_hash;
    }
#endif
    return compute_file_hash_streaming(path, length, num_threads, ok);
}

void print_help(const char* prog_name) {
    cerr << "rhsum - Fast Multi-threaded Polynomial Hasher\n\n";
    cerr << "Usage: " << prog_name << " [options] <file|dir>\n\n";
    cerr << "Options:\n";
    cerr << "  -T, --threads <N>   Number of threads\n";
    cerr << "  -R, --recursive     Recursive directory processing\n";
    cerr << "  -L, --follow-symlinks\n";
    cerr << "                      Follow symbolic links\n";
    cerr << "  -v                  Verbose mode\n";
    cerr << "  --help              Show this help message\n";
    cerr << "\nDefault threads without -T: max(hardware cores, ceil(largest file bytes / 256 MiB))\n";
}

int main(int argc, char* argv[]) {
    int num_threads = max(1u, thread::hardware_concurrency());
    bool threads_explicitly_set = false;
    bool verbose = false;
    bool recursive = false;
    bool follow_symlinks = false;
    string input_path;

    for (int i = 1; i < argc; ++i) {
        string arg = argv[i];
        if ((arg == "-T" || arg == "--threads") && i + 1 < argc) {
            num_threads = stoi(argv[++i]);
            threads_explicitly_set = true;
        }
        else if (arg == "-R" || arg == "--recursive") recursive = true;
        else if (arg == "-L" || arg == "--follow-symlinks") follow_symlinks = true;
        else if (arg == "-v") verbose = true;
        else if (arg == "--help") { print_help(argv[0]); return 0; }
        else input_path = arg;
    }

    if (input_path.empty()) {
        print_help(argv[0]);
        return 1;
    }

    error_code fs_ec;
    if (!fs::exists(input_path, fs_ec)) {
        if (fs_ec) {
            cerr << "Error: Failed to access path: " << input_path << " (" << fs_ec.message() << ")\n";
            return 1;
        }
        cerr << "Error: Path does not exist: " << input_path << endl;
        return 1;
    }

    fs_ec.clear();
    const bool input_is_directory = fs::is_directory(input_path, fs_ec);
    if (fs_ec) {
        cerr << "Error: Failed to inspect path: " << input_path << " (" << fs_ec.message() << ")\n";
        return 1;
    }
    vector<HashTask> tasks;
    u64 current_global_offset = 0;
    u64 largest_file_size = 0;

    // 1. Collect entries
    vector<EntryInfo> entries;
    if (input_is_directory) {
        string collect_error;
        if (!collect_directory_entries(input_path, input_path, recursive, follow_symlinks, &entries, &collect_error)) {
            cerr << "Error: " << collect_error << "\n";
            return 1;
        }
    } else {
        entries.push_back({fs::path(input_path), "", false, true, false});
    }

    // Sort paths lexicographically to ensure determinism across different runs/FS
    sort(entries.begin(), entries.end(), [](const EntryInfo& lhs, const EntryInfo& rhs) {
        return lhs.logical_rel_path < rhs.logical_rel_path;
    });

    // 2. Prepare tasks and hash metadata (sequentially to maintain offset order)
    for (const auto& entry : entries) {
        const bool is_dir = entry.is_dir;
        const bool is_file = entry.is_file;
        const bool is_other = entry.is_other;
        if (!is_dir && !is_file && !is_other) continue;

        HashTask task;
        task.abs_path = entry.physical_path.string();
        task.is_dir = is_dir;
        task.is_file = is_file;
        task.meta_type = is_dir ? 1 : (is_file ? 0 : 2);
        task.global_offset = current_global_offset;
        task.meta_size = 0;
        task.meta_hash = 0;
        task.data_hash = 0;
        task.data_size = 0;

        if (input_is_directory) {
            task.rel_path = entry.logical_rel_path;
            task.meta_hash = compute_meta_hash(task.rel_path, task.meta_type, &task.meta_size);
            current_global_offset += task.meta_size;
        } else {
            task.rel_path.clear();
        }

        if (is_file) {
            error_code size_ec;
            task.data_size = fs::file_size(entry.physical_path, size_ec);
            if (size_ec) {
                cerr << "Error: Failed to get file size: " << entry.physical_path.string()
                     << " (" << size_ec.message() << ")\n";
                return 1;
            }
            largest_file_size = max(largest_file_size, task.data_size);
            current_global_offset += task.data_size;
        }
        tasks.push_back(task);
    }

    if (!threads_explicitly_set) {
        const u64 size_based_threads_u64 = max<u64>(1, (largest_file_size + AUTO_THREAD_GRANULARITY - 1) / AUTO_THREAD_GRANULARITY);
        const u64 core_count_u64 = max<u64>(1, thread::hardware_concurrency());
        const u64 auto_threads_u64 = max(core_count_u64, size_based_threads_u64);
        num_threads = (auto_threads_u64 > (u64)numeric_limits<int>::max())
            ? numeric_limits<int>::max()
            : (int)auto_threads_u64;
    }

    // 3. Hash file contents one file at a time to keep memory bounded
    auto start_time = chrono::high_resolution_clock::now();

    for (size_t task_index = 0; task_index < tasks.size(); ++task_index) {
        auto& task = tasks[task_index];
        if (!task.is_file || task.data_size == 0) continue;
        bool hash_ok = false;
        task.data_hash = compute_file_hash(task.abs_path, task.data_size, num_threads, &hash_ok);
        if (!hash_ok) {
            cerr << "Error: Failed to read file: " << task.abs_path << "\n";
            return 1;
        }
    }

    // 4. Combine all hashes using polynomial composition
    u64 final_hash = 0;
    for (const auto& task : tasks) {
        if (task.meta_size > 0) {
            final_hash += task.meta_hash * power(P, task.global_offset);
        }
        if (task.data_size > 0) {
            final_hash += task.data_hash * power(P, task.global_offset + task.meta_size);
        }
    }

    auto end_time = chrono::high_resolution_clock::now();
    chrono::duration<double> diff = end_time - start_time;

    cout << hex << setfill('0') << setw(16) << final_hash << dec << endl;

    if (verbose) {
        cerr << "\n--- Statistics ---" << endl;
        cerr << "Items:           " << tasks.size() << endl;
        cerr << "Virtual stream:  " << format_size(current_global_offset) << endl;
        cerr << "Threads:         " << num_threads << endl;
        cerr << "Time:            " << fixed << setprecision(3) << diff.count() << "s" << endl;
    }

    return 0;
}
