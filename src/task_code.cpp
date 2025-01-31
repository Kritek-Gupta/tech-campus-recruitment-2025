#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <thread>
#include <mutex>
#include <filesystem>
#include <chrono>

#define CHUNK_SIZE (100 * 1024 * 1024)

using namespace std;
namespace fs = filesystem;

mutex file_mutex;

bool validate_date(const string &date) {
    if (date.size() != 10 || date[4] != '-' || date[7] != '-') return false;
    int year, month, day;
    char dash1, dash2;
    istringstream ss(date);
    return (ss >> year >> dash1 >> month >> dash2 >> day) && dash1 == '-' && dash2 == '-';
}

void process_chunk(const string &log_file, streampos start, streampos end,
                   const string &target_date, const fs::path &temp_dir, int chunk_id) {
    try {
        ifstream infile(log_file);
        if (!infile) {
            throw runtime_error("Error opening log file");
        }

        fs::path output_file = temp_dir / ("chunk_" + to_string(chunk_id) + ".txt");
        ofstream outfile(output_file);

        infile.seekg(start);
        if (start != 0) {
            string discard;
            getline(infile, discard); 
        }

        string line;
        while (infile.tellg() < end && getline(infile, line)) {
            if (line.rfind(target_date, 0) == 0) {
                outfile << line << '\n';
            }
        }
    } catch (const exception &e) {
        lock_guard<mutex> lock(file_mutex);
        cerr << "Error processing chunk " << chunk_id << ": " << e.what() << endl;
    }
}

void merge_files(const vector<fs::path> &output_files, const fs::path &final_output) {
    ofstream outfile(final_output);
    for (const auto &file : output_files) {
        ifstream infile(file);
        outfile << infile.rdbuf();
        fs::remove(file);
    }
}

void extract_logs_parallel(const string &log_file, const string &target_date) {
    if (!fs::exists(log_file)) {
        throw runtime_error("Log file not found: " + log_file);
    }

    ifstream infile(log_file, ios::ate | ios::binary);
    streampos file_size = infile.tellg();
    infile.close();

    fs::path temp_dir = "temp_output";
    fs::path output_dir = "../output";
    fs::create_directories(temp_dir);
    fs::create_directories(output_dir);

    vector<streampos> chunk_positions;
    for (streampos pos = 0; pos < file_size; pos += CHUNK_SIZE) {
        chunk_positions.push_back(pos);
    }
    chunk_positions.push_back(file_size);

    int total_chunks = chunk_positions.size() - 1;
    cout << "Processing " << total_chunks << " chunks..." << endl;

    vector<thread> threads;
    vector<fs::path> output_files;

    for (int i = 0; i < total_chunks; ++i) {
        output_files.push_back(temp_dir / ("chunk_" + to_string(i) + ".txt"));
        threads.emplace_back(process_chunk, log_file, chunk_positions[i], chunk_positions[i + 1], target_date, temp_dir, i);
    }

    for (auto &thread : threads) {
        thread.join();
    }

    fs::path final_output = output_dir / ("output_" + target_date + ".txt");
    cout << "Merging results..." << endl;
    merge_files(output_files, final_output);

    fs::remove(temp_dir);
    cout << "Logs extracted to " << final_output << endl;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        cerr << "Usage: ./extract_logs YYYY-MM-DD" << endl;
        return 1;
    }

    string target_date = argv[1];
    if (!validate_date(target_date)) {
        cerr << "Invalid date format. Use YYYY-MM-DD" << endl;
        return 1;
    }

    string log_file_path = "test_logs.log";
    try {
        extract_logs_parallel(log_file_path, target_date);
    } catch (const exception &e) {
        cerr << "Error: " << e.what() << endl;
        return 1;
    }

    return 0;
}