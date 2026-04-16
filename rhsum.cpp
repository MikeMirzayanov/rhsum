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

struct FileChunkWork {
    size_t task_index;
    vector<uint8_t> owned_data;
    const uint8_t* data;
    size_t length;
    vector<u64> chunk_hashes;
    vector<size_t> chunk_sizes;
#if defined(__unix__) || defined(__APPLE__)
    bool is_mapped;
#endif
};

struct ChunkTask {
    size_t file_index;
    size_t chunk_index;
    const uint8_t* data;
    size_t size;
};

struct EntryInfo {
    fs::path physical_path;
    string logical_rel_path;
    bool is_dir;
    bool is_file;
    bool is_other;
};

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

bool read_file_bytes(const string& path, vector<uint8_t>* out) {
    ifstream file(path, ios::binary);
    if (!file) return false;

    file.seekg(0, ios::end);
    const streamoff end_pos = file.tellg();
    if (end_pos < 0) return false;
    const size_t size = static_cast<size_t>(end_pos);
    file.seekg(0, ios::beg);

    out->assign(size, 0);
    if (size == 0) return true;

    file.read(reinterpret_cast<char*>(out->data()), size);
    return file.good() || static_cast<size_t>(file.gcount()) == size;
}

bool load_file_data(const string& path, size_t expected_size, FileChunkWork* work) {
#if defined(__unix__) || defined(__APPLE__)
    int fd = open(path.c_str(), O_RDONLY);
    if (fd >= 0) {
        void* mapped = mmap(nullptr, expected_size, PROT_READ, MAP_PRIVATE, fd, 0);
        close(fd);
        if (mapped != MAP_FAILED) {
            work->data = static_cast<const uint8_t*>(mapped);
            work->is_mapped = true;
            return true;
        }
    }
#endif

    if (!read_file_bytes(path, &work->owned_data)) return false;
    work->data = work->owned_data.data();
#if defined(__unix__) || defined(__APPLE__)
    work->is_mapped = false;
#endif
    return true;
}

void release_file_data(FileChunkWork* work) {
#if defined(__unix__) || defined(__APPLE__)
    if (work->is_mapped) {
        munmap(const_cast<uint8_t*>(work->data), work->length);
        work->data = nullptr;
        return;
    }
#endif
    work->owned_data.clear();
    work->data = nullptr;
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

    if (!fs::exists(input_path)) {
        cerr << "Error: Path does not exist: " << input_path << endl;
        return 1;
    }

    const bool input_is_directory = fs::is_directory(input_path);
    vector<HashTask> tasks;
    u64 current_global_offset = 0;
    u64 largest_file_size = 0;

    // 1. Collect entries
    vector<EntryInfo> entries;
    if (input_is_directory) {
        auto options = follow_symlinks ? fs::directory_options::follow_directory_symlink
                                       : fs::directory_options::none;

        if (recursive) {
            for (const auto& entry : fs::recursive_directory_iterator(input_path, options)) {
                const fs::path path = entry.path();
                entries.push_back({
                    path,
                    path.lexically_relative(input_path).string(),
                    entry.is_directory(),
                    entry.is_regular_file(),
                    !entry.is_directory() && !entry.is_regular_file()
                });
            }
        } else {
            for (const auto& entry : fs::directory_iterator(input_path, options)) {
                const fs::path path = entry.path();
                entries.push_back({
                    path,
                    path.lexically_relative(input_path).string(),
                    entry.is_directory(),
                    entry.is_regular_file(),
                    !entry.is_directory() && !entry.is_regular_file()
                });
            }
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
            try {
                task.data_size = fs::file_size(entry.physical_path);
            } catch (...) { task.data_size = 0; }
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

    // 3. Hash file contents with a global worker pool over per-file chunks
    auto start_time = chrono::high_resolution_clock::now();

    vector<FileChunkWork> file_works;
    vector<ChunkTask> chunk_tasks;

    for (size_t task_index = 0; task_index < tasks.size(); ++task_index) {
        auto& task = tasks[task_index];
        if (!task.is_file || task.data_size == 0) continue;

        const size_t chunk_count = choose_chunk_count(task.data_size, num_threads);
        file_works.push_back({
            task_index,
            {},
            nullptr,
            task.data_size,
            vector<u64>(chunk_count, 0),
            vector<size_t>(chunk_count, 0)
#if defined(__unix__) || defined(__APPLE__)
            , false
#endif
        });

        const size_t file_index = file_works.size() - 1;
        auto& file_work = file_works[file_index];
        if (!load_file_data(task.abs_path, task.data_size, &file_work)) {
            file_works.pop_back();
            continue;
        }

        const size_t base_chunk_size = task.data_size / chunk_count;
        const size_t remainder = task.data_size % chunk_count;

        size_t offset = 0;
        for (size_t chunk_index = 0; chunk_index < chunk_count; ++chunk_index) {
            const size_t chunk_size = base_chunk_size + (chunk_index < remainder ? 1 : 0);
            file_work.chunk_sizes[chunk_index] = chunk_size;
            chunk_tasks.push_back({file_index, chunk_index, file_work.data + offset, chunk_size});
            offset += chunk_size;
        }
    }

    if (!chunk_tasks.empty()) {
        atomic<size_t> next_chunk = 0;
        const size_t worker_count = min<size_t>(max(1, num_threads), chunk_tasks.size());
        vector<thread> workers;
        workers.reserve(worker_count);

        for (size_t worker_index = 0; worker_index < worker_count; ++worker_index) {
            workers.emplace_back([&]() {
                while (true) {
                    const size_t chunk_task_index = next_chunk.fetch_add(1);
                    if (chunk_task_index >= chunk_tasks.size()) return;

                    const auto& chunk_task = chunk_tasks[chunk_task_index];
                    auto& file_work = file_works[chunk_task.file_index];
                    file_work.chunk_hashes[chunk_task.chunk_index] = compute_hash_raw(chunk_task.data, chunk_task.size);
                }
            });
        }

        for (auto& worker : workers) worker.join();
    }

    for (auto& file_work : file_works) {
        u64 data_hash = 0;
        u64 offset_power = 1;
        for (size_t chunk_index = 0; chunk_index < file_work.chunk_hashes.size(); ++chunk_index) {
            data_hash += file_work.chunk_hashes[chunk_index] * offset_power;
            offset_power *= power(P, file_work.chunk_sizes[chunk_index]);
        }

        tasks[file_work.task_index].data_hash = data_hash;
        release_file_data(&file_work);
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
