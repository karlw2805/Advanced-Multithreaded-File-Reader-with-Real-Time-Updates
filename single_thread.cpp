#include <bits/stdc++.h>
#include <fstream>
#include <string>
#include <chrono>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
using namespace std;
namespace fs = std::filesystem;

int main()
{

    string filename = "150mb.txt";
    streampos file_size = static_cast<streampos>(fs::file_size(filename));

    cout << "Starting file reading for " << filename << " (" << file_size << " bytes)" << endl;

    auto startTime = chrono::high_resolution_clock::now();

    int fd = open(filename.c_str(), O_RDONLY);
    if (fd == -1)
    {
        cerr << "Error: Unable to open file " << filename << endl;
        return 1;
    }

    // Get file size
    struct stat statbuf;
    fstat(fd, &statbuf);
    size_t fileSize = statbuf.st_size;

    vector<char> buffer(fileSize);
    ssize_t bytesRead = read(fd, buffer.data(), fileSize);

    close(fd);

    auto endTime = chrono::high_resolution_clock::now();
    auto duration = chrono::duration_cast<chrono::milliseconds>(endTime - startTime).count();

    // Print time taken and bytes read
    cout << "\n--------------------------------------------------------" << endl;
    cout << "File read completed by single thread" << endl;
    cout << "Time taken: " << duration << " milliseconds" << endl;
    cout << "File size: " << file_size << " bytes" << endl;
    cout << "Read speed: " << (static_cast<double>(file_size) / (1024 * 1024) / (duration / 1000.0)) << " MB/s" << endl;
    cout << "--------------------------------------------------------\n"
         << endl;

    return 0;
}