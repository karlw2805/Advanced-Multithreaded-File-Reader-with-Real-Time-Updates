#include <bits/stdc++.h>
#include <fstream>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <chrono>
#include <unordered_set>

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
    mutex log_mutex;
    condition_variable cv;

    atomic<bool> stop_monitoring{false};
    atomic<bool> refresh_chunks{false};
    atomic<bool> is_modified_once{false};

    map<int, ChunkData> chunks;
    map<int, thread> worker_threads;
    map<int, pair<vector<char>, vector<char>>> logs;
    vector<bool> chunk_changed;
    vector<bool> chunk_checked;

    thread monitor_thread;
    thread log_thread;

    // Used for comparing and checking whether the previous and current contents of a chunk are same or not
    size_t fast_hash(vector<char> &buffer)
    {
        return hash<string_view>{}(string_view(buffer.data(), buffer.size()));
    }

    // Used for showing the changes in the buffers/chunks of the threads
    void log_changed_chunks()
    {
        while (!stop_monitoring)
        {
            bool all_checked = true;
            for (auto checked : chunk_checked)
            {
                if (!checked)
                {
                    all_checked = false;
                    break;
                }
            }
            if (all_checked)
            {
                lock_guard<mutex> io_lock(io_mutex);
                lock_guard<mutex> log_lock(log_mutex);
                bool changes_detected = false;
                for (auto log : logs)
                {
                    if (chunk_changed[log.first])
                    {
                        // Find the index where buffers start to differ
                        size_t diff_index = 0;
                        const vector<char> &old_buffer = log.second.first;
                        const vector<char> &new_buffer = log.second.second;
                        size_t min_size = min(old_buffer.size(), new_buffer.size());

                        while (diff_index < min_size && old_buffer[diff_index] == new_buffer[diff_index])
                        {
                            diff_index++;
                        }

                        // If we reached the end of one buffer but not the other,
                        // the difference is in length (one buffer has additional content)
                        bool length_difference = (diff_index == min_size && old_buffer.size() != new_buffer.size());

                        if (!changes_detected)
                        {
                            cout << "\nChanges detected in file: " << filename << endl;
                            changes_detected = true;
                        }

                        cout << "For thread " << log.first + 1 << " -> " << endl;
                        cout << "Difference starts at position " << diff_index << endl;

                        // If buffers are identical but different lengths
                        if (length_difference)
                        {
                            if (old_buffer.size() > new_buffer.size())
                            {
                                cout << "Old buffer has additional content" << endl;
                            }
                            else
                            {
                                cout << "New buffer has additional content" << endl;
                            }
                        }

                        // Displaying a bit of context before the difference (up to 10 chars)
                        size_t context_start = (diff_index > 10) ? diff_index - 10 : 0;

                        if (context_start < diff_index)
                        {
                            cout << "Context: \"";
                            for (size_t i = context_start; i < diff_index; i++)
                            {
                                cout << old_buffer[i];
                            }
                            cout << "\"" << endl;
                        }

                        cout << "Old content : ";
                        // Printing old content from the difference point
                        for (size_t i = diff_index; i < old_buffer.size(); i++)
                        {
                            char ch = old_buffer[i];
                            if (ch == '\n')
                            {
                                cout << ch << "              ";
                            }
                            else
                            {
                                cout << ch;
                            }
                        }
                        cout << endl;

                        cout << "New content : ";
                        // Printing new content from the difference point
                        for (size_t i = diff_index; i < new_buffer.size(); i++)
                        {
                            char ch = new_buffer[i];
                            if (ch == '\n')
                            {
                                cout << ch << "              ";
                            }
                            else
                            {
                                cout << ch;
                            }
                        }
                        cout << endl
                             << endl;
                    }
                }
                logs.clear();
                fill(chunk_changed.begin(), chunk_changed.end(), false);
                fill(chunk_checked.begin(), chunk_checked.end(), false);
            }
        }
    }

    // Used for checking whether there has been any change in the file since the last time it was read
    void monitor_file()
    {

        auto last_mod_time = fs::last_write_time(filename);
        auto last_size = fs::file_size(filename);

        while (!stop_monitoring)
        {

            this_thread::sleep_for(chrono::milliseconds(100));

            if (!fs::exists(filename))
            {
                lock_guard<mutex> lock(io_mutex);
                cout << "File no longer exists: " << filename << endl;
                continue;
            }

            auto current_time = fs::last_write_time(filename);
            auto current_size = fs::file_size(filename);

            if (current_time != last_mod_time || current_size != last_size)
            {
                last_mod_time = current_time;
                is_modified_once = true;

                // Handle file size changes
                if (current_size != last_size)
                {
                    last_size = current_size;
                    file_size = static_cast<streampos>(current_size);
                    refresh_chunks = true;

                    // Recalculate chunk boundaries without stopping threads
                    update_chunk_boundaries(file_size);
                }

                cv.notify_all();
            }
        }
    }

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

    // For Updating chunk boundaries without recreating threads
    void update_chunk_boundaries(streampos new_size)
    {
        lock_guard<mutex> lock(chunk_mutex);

        // Recalculating chunk boundaries
        chunk_size = static_cast<streamsize>(new_size) / num_threads;

        for (int i = 0; i < num_threads; ++i)
        {
            streampos start = static_cast<streampos>(i * chunk_size);
            streampos end = (i == num_threads - 1) ? new_size : start + chunk_size;

            // Updating existing chunk boundaries
            chunks[i].start = start;
            chunks[i].end = end;
        }

        refresh_chunks = false;
    }

    // For processing a specific chunk
    void process_chunk(int chunk_id)
    {
        bool first_read = true;

        while (!stop_monitoring)
        {
            // Waiting while chunks are being updated
            {
                unique_lock<mutex> lock(chunk_mutex);
                cv.wait(lock, [this, chunk_id]()
                        { return stop_monitoring ||
                                 (!refresh_chunks && chunks.find(chunk_id) != chunks.end()); });

                if (stop_monitoring)
                {
                    break;
                }
            }

            // Reading the chunk
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
                size_t new_hash = fast_hash(new_buffer);

                // Check if content has changed
                {
                    lock_guard<mutex> lock(chunk_mutex);
                    if (chunks[chunk_id].buffer.empty() || new_hash != chunks[chunk_id].hash)
                    {
                        // Reporting the changes
                        if (is_modified_once)
                        {
                            lock_guard<mutex> log_lock(log_mutex);
                            logs[chunk_id].first = chunks[chunk_id].buffer;
                            logs[chunk_id].second = new_buffer;
                            chunk_changed[chunk_id] = true;
                        }

                        chunks[chunk_id].buffer = move(new_buffer);
                        chunks[chunk_id].hash = new_hash;
                        chunks[chunk_id].changed = true;
                    }
                }

                // Report that this chunk has been checked
                if (is_modified_once)
                {
                    lock_guard<mutex> log_lock(log_mutex);
                    chunk_checked[chunk_id] = true;
                }
            }

            file.close();

            {
                unique_lock<mutex> lock(chunk_mutex);
                // Keep on checking in case the size of chunk doesn't change but the data inside it changes
                cv.wait_for(lock, chrono::milliseconds(200), [this]()
                            { return stop_monitoring || refresh_chunks; });
            }
        }
    }

public:
    FileChangeMonitor(const string &file, int threads)
        : filename(file), num_threads(threads), file_size(0)
    {
        if (fs::exists(filename))
        {
            file_size = static_cast<streampos>(fs::file_size(filename));
        }
        else
        {
            file_size = 0;
        }
        chunk_changed.resize(num_threads, false);
        chunk_checked.resize(num_threads, false);
    }

    void start()
    {
        if (!fs::exists(filename))
        {
            cout << "File does not exist: " << filename << endl;
            return;
        }

        // Initializing the chunks
        initialize_chunks(file_size);

        cout << "Starting file monitoring for " << filename << " (" << file_size << " bytes)" << endl;
        cout << "Creating " << num_threads << " worker threads with average chunk size of " << chunk_size << " bytes" << endl
             << endl;

        // Creating worker threads once
        for (int i = 0; i < num_threads; ++i)
        {
            worker_threads[i] = thread(&FileChangeMonitor::process_chunk, this, i);
        }

        log_thread = thread(&FileChangeMonitor::log_changed_chunks, this);
        monitor_thread = thread(&FileChangeMonitor::monitor_file, this);
    }

    void stop()
    {
        stop_monitoring = true;
        cv.notify_all();

        if (monitor_thread.joinable())
        {
            monitor_thread.join();
        }

        for (auto &[id, thread] : worker_threads)
        {
            if (thread.joinable())
            {
                thread.join();
            }
        }

        if (log_thread.joinable())
        {
            log_thread.join();
        }
        cout << "Monitoring stopped for " << filename << endl;
    }
};