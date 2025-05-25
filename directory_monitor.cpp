#include "file_monitor.hpp"

class DirectoryMonitor
{
private:
    string directory_path;
    int threads_per_file;
    mutex files_mutex;
    map<string, unique_ptr<FileChangeMonitor>> file_monitors;
    set<string> monitored_files;
    thread dir_monitor_thread;
    atomic<bool> stop_monitoring{false};

    void monitor_directory()
    {
        while (!stop_monitoring)
        {

            for (const auto &entry : fs::directory_iterator(directory_path))
            {
                // you can specify any type of file format to it according to the requirements
                if (entry.is_regular_file() &&
                    (entry.path().extension() == ".txt"))
                {

                    string filepath = entry.path().string();

                    lock_guard<mutex> lock(files_mutex);
                    if (monitored_files.find(filepath) == monitored_files.end())
                    {
                        cout << "\nFound new text file: " << filepath << endl;

                        auto monitor = make_unique<FileChangeMonitor>(filepath, threads_per_file);
                        monitor->start();

                        monitored_files.insert(filepath);
                        file_monitors[filepath] = move(monitor);
                    }
                }
            }

            this_thread::sleep_for(chrono::seconds(1));
        }
    }

public:
    DirectoryMonitor(const string &dir_path, int threads_per_file)
        : directory_path(dir_path), threads_per_file(threads_per_file)
    {
        // Error handling
        if (!fs::exists(directory_path) || !fs::is_directory(directory_path))
        {
            throw runtime_error("Invalid directory path: " + directory_path);
        }
    }

    void start()
    {
        cout << "Starting directory monitoring for: " << directory_path << endl;
        cout << "Looking for .txt and .bin files, using " << threads_per_file << " threads per file" << endl;
        cout << "Press Enter to stop..." << endl;

        dir_monitor_thread = thread(&DirectoryMonitor::monitor_directory, this);

        cin.get();

        stop();
    }

    void stop()
    {
        stop_monitoring = true;

        if (dir_monitor_thread.joinable())
        {
            dir_monitor_thread.join();
        }

        lock_guard<mutex> lock(files_mutex);
        for (auto &[filepath, monitor] : file_monitors)
        {
            monitor->stop();
        }

        file_monitors.clear();
        monitored_files.clear();
        cout << "Directory monitoring stopped." << endl;
    }
};

int main()
{
    string directory_path = "/home/g-22/Desktop/OS_Project";
    int threads_per_file = 12;
    DirectoryMonitor dir_monitor(directory_path, threads_per_file);
    dir_monitor.start();

    return 0;
}