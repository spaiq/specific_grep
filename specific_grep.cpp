#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <filesystem>
#include <thread>
#include <future>
#include <regex>

namespace fs = std::filesystem;

/**
 * Searches for a given string in a vector of files and returns a vector of tuples that contain
 * the thread ID, file path, line number, and line that matches the search string.
 *
 * @param search_string The string to search for.
 * @param files_to_search A vector of file paths to search in.
 * @return A vector of tuples containing thread ID, file path, line number, and line that match the search string.
 */
std::vector<std::tuple<std::thread::id, std::string, int, std::string>> searchFilesForString(const std::string& search_string, const std::vector<fs::path>& files_to_search) {
	// Initialize the vector that will contain the search results
	std::vector<std::tuple<std::thread::id, std::string, int, std::string>> results;

	// Get the ID of the current thread
	std::thread::id thread_id = std::this_thread::get_id();

	// Loop through each file path in the vector and search for the string
	for (const auto& file_path : files_to_search) {
		// Open the file for reading
		std::ifstream file(file_path);

		// If the file was successfully opened, read each line and search for the string
		if (file.good()) {
			std::string line;
			int line_number = 0;
			while (std::getline(file, line)) {
				++line_number;
				if (line.find(search_string) != std::string::npos) {
					// If the string was found, add the search results to the vector
					results.emplace_back(thread_id, file_path.string(), line_number, line);
				}
			}
			file.close();
		}
		// If the file could not be opened, output an error message
		else {
			std::cerr << "Error: could not open file " << file_path.string() << " due to permission issues." << std::endl;
		}
	}
	// Add entry for thread id storage if there was no file with the string in files subset
	if (results.empty()) {
		results.emplace_back(thread_id, "", NULL, "");
	}

	// Return the vector of search results
	return results;
}


/**
 * Search a directory and its subdirectories for files containing a given string.
 *
 * @param search_string The string to search for.
 * @param directory_path The path to the directory to search.
 * @param thread_count The number of threads to use for the search.
 * @return A pair containing a vector of tuples representing the search results and an integer representing the total number of files searched.
 */
std::pair<std::vector<std::tuple<std::thread::id, std::string, int, std::string>>, int> searchDirectoryForString(const std::string& search_string, const std::string& directory_path, const int thread_count) {
	// Create a vector of paths to all regular files in the directory and its subdirectories.
	std::vector<fs::path> files_to_search;
	for (const auto& file : fs::recursive_directory_iterator(directory_path)) {
		if (fs::is_regular_file(file)) {
			files_to_search.push_back(file.path());
		}
	}

	// Calculate the number of files to search per thread.
	const int files_per_thread = files_to_search.size() / thread_count;

	// Create a vector of futures representing the search results for each thread.
	std::vector<std::future<std::vector<std::tuple<std::thread::id, std::string, int, std::string>>>> futures;
	for (int i = 0; i < thread_count; ++i) {
		const int start_index = i * files_per_thread;
		const int end_index = (i + 1) == thread_count ? files_to_search.size() : (i + 1) * files_per_thread;
		const std::vector<fs::path> files_subset(files_to_search.begin() + start_index, files_to_search.begin() + end_index);

		// Launch a new thread to search the subset of files.
		std::future<std::vector<std::tuple<std::thread::id, std::string, int, std::string>>> result = std::async(std::launch::async, searchFilesForString, std::cref(search_string), files_subset);
		futures.emplace_back(std::move(result));
	}

	// Collect the results from each thread and combine them into a single vector.
	std::vector<std::tuple<std::thread::id, std::string, int, std::string>> results;
	for (auto& future : futures) {
		std::vector<std::tuple<std::thread::id, std::string, int, std::string>> result = future.get();
		results.insert(results.end(), result.begin(), result.end());
	}

	// Return a pair containing the search results and the total number of files searched.
	return std::make_pair(results, files_to_search.size());
}


/**
 * Determines if a given filename is valid, meaning it contains only alphanumeric
 * characters, hyphens, dots, and spaces.
 *
 * @param filename the filename to validate
 * @return true if the filename is valid, false otherwise
 */
bool isValidFilename(const std::string& filename) {
	// Define a regular expression pattern to match characters that are not alphanumeric,
	// hyphens, dots, or spaces.
	static const std::regex pattern("[^\\w\\-. ]");

	// Use regex_search to check if the filename contains any invalid characters.
	// If the search returns true, it means the filename is invalid, so return false.
	// If the search returns false, it means the filename is valid, so return true.
	return !std::regex_search(filename, pattern);
}


int main(int argc, char* argv[]) {
	// Extract the filename from the first argument
	std::string filename = std::filesystem::path(argv[0]).filename().string();

	// If no arguments are given or the number of arguments is invalid, print an error message and exit
	if (argc == 1) {
		std::cerr << "Error: wrong usage of the program\n"
			<< "Usage: " << filename << " <search string> [options]\n"
			<< "Options:\n"
			<< "  -d <directory> - directory to search in (default: current directory)\n"
			<< "  -l <log filename> - log filename (default: <program name>.log)\n"
			<< "  -r <result filename> - result filename (default: <program name>.txt)\n"
			<< "  -t <thread count> - number of threads to use (default: 4)\n";
		return 1;
	}

	if (!(argc % 2 == 0) || argc > 10) {
		std::cerr << "Error: wrong number of arguments" << std::endl;
		return 1;
	}

	// Set default values for directory path, log filename, result filename, and thread count
	std::string directory_path = std::filesystem::current_path().string();
	std::string search_string = argv[1];
	int additional_options_cnt = (argc - 2) / 2, thread_cnt = 4;
	bool dir_opt = false, log_filename_opt = false, result_filename_opt = false, thread_cnt_opt = false;

	// Extract the program name from the filename
	std::size_t last_dot = filename.find_last_of(".");
	std::string program_name = filename.substr(0, last_dot);
	std::string log_filename = program_name;
	std::string result_filename = program_name;

	// Loop through the additional options
	for (int i = 1; i <= additional_options_cnt; i++) {
		// If the option is the -d or --dir option, set the directory path
		if (strcmp(argv[i * 2], "-d") == 0 || strcmp(argv[i * 2], "--dir") == 0) {
			// Check if the directory option has already been set
			if (dir_opt == true) {
				std::cerr << "Error: multiple usage of the starting directory option" << std::endl;
				return 1;
			}
			// Append the directory to the current directory path
			directory_path = directory_path + "\\" + argv[i * 2 + 1];
			// Check if the directory exists
			if (!fs::exists(directory_path)) {
				std::cerr << "Error: directory does not exist" << std::endl;
				return 1;
			}
			// Set the directory option to true
			dir_opt = true;
		}
		// If the option is the -l or --log_file option, set the log filename
		else if (strcmp(argv[i * 2], "-l") == 0 || strcmp(argv[i * 2], "--log_file") == 0) {
			// Check if option already used
			if (log_filename_opt == true) {
				std::cerr << "Error: multiple usage of the log filename option" << std::endl;
				return 1;
			}
			// Set log filename and check if valid
			log_filename = argv[i * 2 + 1];
			if (!isValidFilename(log_filename)) {
				std::cerr << "Error: invalid log filename" << std::endl;
				return 1;
			}
			log_filename_opt = true;
		}
		// If the option is the -r or --result_file option, set the result filename
		else if (strcmp(argv[i * 2], "-r") == 0 || strcmp(argv[i * 2], "--result_file") == 0) {
			// Check if option already used
			if (result_filename_opt == true) {
				std::cerr << "Error: multiple usage of the result filename option" << std::endl;
				return 1;
			}
			// Set result filename and check if valid
			result_filename = argv[i * 2 + 1];
			if (!isValidFilename(result_filename)) {
				std::cerr << "Error: invalid result filename" << std::endl;
				return 1;
			}
			result_filename_opt = true;
		}
		// If the option is the -t or --threads option, set the threads count
		else if (strcmp(argv[i * 2], "-t") == 0 || strcmp(argv[i * 2], "--threads") == 0) {
			// Check if option already used
			if (thread_cnt_opt == true) {
				std::cerr << "Error: multiple usage of the thread count option" << std::endl;
				return 1;
			}
			// Set thread count and catch invalid argument
			try {
				thread_cnt = std::stoi(argv[i * 2 + 1]);
			}
			catch (const std::invalid_argument& e) {
				std::cerr << "Error: invalid thread count" << std::endl;
				return 1;
			}
			thread_cnt_opt = true;
		}
		// If option not recognized, print error message
		else {
			std::cerr << "Wrong usage of the additional parameters." << std::endl;
			return 1;
		}
	}

	// Search directory for string with specified thread count
	std::pair<std::vector<std::tuple<std::thread::id, std::string, int, std::string>>, int> results = searchDirectoryForString(search_string, directory_path, thread_cnt);

	// Return success
	return 0;
