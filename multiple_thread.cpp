#include <bits/stdc++.h>
#include <fstream>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <chrono>

using namespace std;
namespace fs = std::filesystem;

class FileChangeMonitor
{
private:
    struct ChunkData
    {
        vector<char> buffer;
        bool changed;
        streampos start;
        streampos end;
        size_t hash;

        ChunkData() : buffer(), changed(false), start(0), end(0), hash(0) {}

        ChunkData(streampos s, streampos e)
            : buffer(), changed(false), start(s), end(e), hash(0) {}
    };

    string filename;
    int num_threads;
    streampos file_size;
    streampos chunk_size;

    mutex io_mutex;
    mutex chunk_mutex;

    map<int, ChunkData> chunks;
    map<int, thread> worker_threads;

    atomic<int> threads_completed_initial_read{0};
    chrono::high_resolution_clock::time_point start_time;

    // For initializing the boundaries of the initial chunks
    void initialize_chunks(streampos file_size)
    {
        // Calculate initial chunk boundaries
        chunk_size = static_cast<streamsize>(file_size) / num_threads;

        for (int i = 0; i < num_threads; ++i)
        {
            streampos start = static_cast<streampos>(i * chunk_size);
            streampos end = (i == num_threads - 1) ? file_size : start + chunk_size;
            chunks[i] = ChunkData(start, end);
        }
    }

    // For processing a specific chunk
    void process_chunk(int chunk_id)
    {
        bool first_read = true;

        // Read the chunk
        ChunkData current_chunk;
        {
            lock_guard<mutex> lock(chunk_mutex);
            current_chunk.start = chunks[chunk_id].start;
            current_chunk.end = chunks[chunk_id].end;
        }

        ifstream file(filename, ios::binary);

        file.seekg(current_chunk.start);
        auto chunk_size = current_chunk.end - current_chunk.start;

        vector<char> new_buffer(chunk_size);
        if (file.read(new_buffer.data(), chunk_size))
        {
            // If you want to compare only the reading speed of the two (mult-threaded or simple) then skip the
            // calculation of hash and remove the related code lines
            // Even without the removal of those lines, after some computation of hash too, the multi-threaded reader is
            // still faster than simple reading from the file into a buffer
            // Also note that here we are also using mutex so that the chunks do not get corrupt info which also takes a dig at the throughput of the code

            size_t new_hash = 0;

            {
                lock_guard<mutex> lock(chunk_mutex);
                if (chunks[chunk_id].buffer.empty())
                {

                    chunks[chunk_id].buffer = move(new_buffer);
                    chunks[chunk_id].hash = new_hash;
                    // chunks[chunk_id].hash = 1;
                    chunks[chunk_id].changed = true;
                }
            }

            // increment counter for each thread

            int completed = ++threads_completed_initial_read;

            // If all threads have completed their initial read
            if (completed == num_threads)
            {
                auto end_time = chrono::high_resolution_clock::now();
                auto duration = chrono::duration_cast<chrono::milliseconds>(end_time - start_time).count();

                lock_guard<mutex> lock(io_mutex);
                cout << "\n--------------------------------------------------------" << endl;
                cout << "Initial file read completed by all " << num_threads << " threads" << endl;
                cout << "Time taken: " << duration << " milliseconds" << endl;
                cout << "File size: " << file_size << " bytes" << endl;
                cout << "Average chunk size: " << chunk_size << " bytes" << endl;
                cout << "Read speed: " << (static_cast<double>(file_size) / (1024 * 1024) / (duration / 1000.0)) << " MB/s" << endl;
                cout << "--------------------------------------------------------\n"
                     << endl;
                exit(0);
            }
        }

        file.close();
    }

public:
    FileChangeMonitor(const string &file, int threads)
        : filename(file), num_threads(threads), file_size(0)
    {
        file_size = static_cast<streampos>(fs::file_size(filename));
    }

    void start()
    {
        // Initialize chunks
        initialize_chunks(file_size);

        cout << "Starting file reading for " << filename << " (" << file_size << " bytes)" << endl;
        cout << "Creating " << num_threads << " worker threads with average chunk size of " << chunk_size << " bytes" << endl;

        start_time = chrono::high_resolution_clock::now();

        // Create worker threads once
        for (int i = 0; i < num_threads; ++i)
        {
            worker_threads[i] = thread(&FileChangeMonitor::process_chunk, this, i);
        }

        for (auto &[id, thread] : worker_threads)
        {
            if (thread.joinable())
            {
                thread.join();
            }
        }
    }
};

int main()
{
    string filename = "150mb.txt";
    int num_threads = 10;

    FileChangeMonitor monitor(filename, num_threads);
    monitor.start();

    return 0;
}