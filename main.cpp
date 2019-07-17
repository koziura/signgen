#include <iostream>
#include <boost/smart_ptr.hpp>
#include <boost/thread.hpp>
#include <boost/filesystem.hpp>
#include <boost/functional/hash/hash.hpp>
#include <boost/iostreams/device/mapped_file.hpp>
#include <boost/timer/timer.hpp>
#include <boost/crc.hpp>
#include <thread>
#include <future>
#include <string>
#include <vector>
#include <map>
#include <array>
#include <memory>

namespace fs = boost::filesystem;
constexpr uint32_t block_sz_1mb = 1024 * 1024;

typedef std::vector<char> byte_array_t;
typedef std::vector< std::unique_ptr<std::thread> > thread_group_t;
typedef std::vector<uint32_t> uint32_array_t;
/*!
 * \brief simpleCheckSum
 * \param in_path
 * \param out_path
 * \param block_sz
 * \return
 */
int simpleCheckSum(fs::path in_path, fs::path out_path,
	uint32_t block_sz = block_sz_1mb)
{
	using namespace std;

	boost::system::error_code ec;
	auto file_sz = fs::file_size(in_path, ec);

	if (ec) {
		cout << ec.message() << endl;
		return -1;
	}

	auto blockcnt = (uint32_t)std::ceil(file_sz / (float)block_sz);

	cout << "File size: " << file_sz / (float)block_sz_1mb << "Mb " << in_path.filename() << endl;

	std::FILE* pfile = std::fopen(in_path.string().c_str(), "rb");

	thread_group_t thgr;
	uint32_array_t signval(blockcnt);
	byte_array_t buf(block_sz);
	size_t rdbytes, pos;

	while (!std::feof(pfile)) {
		rdbytes = std::fread(buf.data(), sizeof(char), buf.size(), pfile);

		pos = thgr.size();

		thgr.emplace_back(new thread(
			[buf, pos, rdbytes, &signval] () mutable
		{
			boost::crc_32_type result;

			if (rdbytes != buf.size()) {
				memset(buf.data() + rdbytes, 0, buf.size() - rdbytes);
				rdbytes = buf.size();
			}

			result.process_bytes(buf.data(), rdbytes);
			signval[pos] = result.checksum();
		}));
	}

	std::fclose(pfile);

	string outfile(out_path.string() + ".crc32-signature-sm");

	pfile = std::fopen(outfile.c_str(), "wb");

	for (auto i(0u); i < thgr.size(); ++i) {
		thgr.at(i)->join();
		std::fwrite(&signval.at(i), sizeof(uint32_array_t::value_type), 1, pfile);
	}
	std::fclose(pfile);
	thgr.clear();
	signval.clear();

	return 0;
}
/*!
 * \brief mappedCheckSum
 * \param in_path
 * \param out_path
 * \param block_sz
 * \return
 */
int mappedCheckSum(fs::path in_path, fs::path out_path,
	const uint32_t block_sz = block_sz_1mb)
{
	using namespace std;

	boost::system::error_code ec;
	uint64_t file_sz = fs::file_size(in_path, ec);

	if (ec) {
		cout << ec.message() << endl;
		return -1;
	}

	const auto blockcnt = (uint32_t)std::ceil(file_sz / (float)block_sz);
	const auto nthreads = std::thread::hardware_concurrency();
	uint64_t multiplier_th = nthreads * nthreads;

	if ( file_sz < block_sz * multiplier_th ) {
		multiplier_th = nthreads;
	}

	auto rdbytes = block_sz * multiplier_th;
	const auto offsetcnt = (uint32_t)std::ceil(file_sz / (float)rdbytes);

	cout << "File size: " << file_sz / (float)block_sz_1mb << "Mb " << in_path.filename() << endl;

	thread_group_t thgr;
	uint32_array_t signval(blockcnt);
	boost::iostreams::mapped_file_source file;

	uint64_t lgpos;
	uint64_t mmpos;
	uint64_t offset_sz;
	uint64_t thbytes = block_sz;

	for (uint64_t i(0); i < offsetcnt; ++i) {
		if (!file_sz) {
			break;
		}

		offset_sz = i * rdbytes;

		if ( offset_sz + rdbytes > file_sz) {
			rdbytes = file_sz - offset_sz;
		}

		file.open(in_path, rdbytes, offset_sz);

		if (file.is_open()) {
			const char* data = file.data();
			thbytes = block_sz;

			for (auto th(0u); th < multiplier_th; ++th) {
				if (!file_sz) {
					break;
				}

				lgpos = i * nthreads * nthreads + th;
				mmpos = lgpos * block_sz;
				//printf("mpos %llu, of %llu\n", mmpos, file_sz);

				if (mmpos + block_sz > file_sz) {
					thbytes = file_sz - mmpos;
					file_sz = 0;
				}

				mmpos = th * block_sz;

				if (mmpos + thbytes > rdbytes) {
					break;
				}

				thgr.emplace_back(new thread(
					[data, mmpos, lgpos, thbytes, &block_sz, &signval]()
				{
					boost::crc_32_type result;
					result.process_bytes(data + mmpos, thbytes);

					if (thbytes != block_sz) {
						vector<char> buf(block_sz - thbytes);
						memset(buf.data(), 0, buf.size());
						result.process_bytes(buf.data(), buf.size());
					}
					signval[lgpos] = result.checksum();

				}));
			}

			for (auto& t : thgr) {
				t->join();
			}

			thgr.clear();
			file.close();
		}
	}

	file.close();

	std::FILE* pfile = std::fopen((out_path.string() + ".crc32-signature-mp").c_str(), "wb");
	for (auto i(0u); i < signval.size(); ++i) {
		std::fwrite(&signval.at(i), sizeof(uint32_array_t::value_type), 1, pfile);
	}
	std::fclose(pfile);
	thgr.clear();
	signval.clear();

	return 0;
}
/*!
 * \brief The CmdArgsParser class
 */
class CmdArgsParser {
public:
	/*!
	 * \brief CmdArgsParser
	 * \param argc_
	 * \param argv_
	 * \param switches_on_
	 */
	CmdArgsParser(int argc_, const char* argv_[], bool switches_on_ = false) {
		argc = argc_;
		argv.resize(argc);

		std::copy(argv_, argv_ + argc, argv.begin());

		switches_on = switches_on_;

		//map the switches to the actual
		//arguments if necessary
		if (switches_on) {
			std::vector<std::string>::iterator it1, it2;
			it1 = argv.begin();
			it2 = it1 + 1;

			while (true) {
				if (it1 == argv.end() || it2 == argv.end()) {
					if (it1 != argv.end()) {
						if ((*it1)[0] == '-') {
							switch_map[*it1] = "true";
						}
					}

					break;
				}

				if ((*it1)[0] == '-') {
					switch_map[*it1] = *(it2);
				}

				++it1;
				++it2;
			}
		}
	}
	/*!
	 * \brief getArg
	 * \param arg
	 * \return
	 */
	inline std::string getArg(const int& arg) {
		std::string res;

		if (arg >= 0 && arg < argc) {
			res = argv[arg];
		}

		return res;
	}
	/*!
	 * \brief getArg
	 * \param arg
	 * \return
	 */
	inline std::string getArg(const std::string& arg) {
		std::string res;

		if (!switches_on) {
			return res;
		}

		if (switch_map.find(arg) != switch_map.end()) {
			res = switch_map[arg];
		}

		return res;
	}
	/*!
	 * \brief size
	 * \return
	 */
	inline int size () const {
		return static_cast<int>(switch_map.size());
	}

private:
	int argc;
	std::vector<std::string> argv;

	bool switches_on;
	std::map<std::string, std::string> switch_map;
};

int main(int argc, char const* argv[])
{
	using namespace std;

	CmdArgsParser cmd_args(argc, argv, true);

	std::string temp;

	if (cmd_args.size()) {
		temp = cmd_args.getArg("-h");

		if (!temp.empty()) {
			cout << "\n* help {begin} *" << endl;
			cout << "\n[arguments]" << endl;
			cout << "  -i [filename] the full path to the source file." << endl;
			cout << "  -o [filename] the full path to the destination file." << endl;
			cout << "  -b [number] for setting block size in Mb (optional)." << endl;
			cout << "\n[results]" << endl;
			cout << "  the output of results will be written filename with postfix \"crc32-signature\"." << endl;
			cout << "\n* help {end} *\n" << endl;
			return 0;
		}
	}

	temp = cmd_args.getArg("-i");

	fs::path in_path, out_path;
	auto block_sz = block_sz_1mb;
	boost::system::error_code ec;

	if ( temp.empty() ) {
		std::cout << "not inputted source file!" << std::endl;
		return 0;
	} else {
		bool res = fs::exists(temp, ec);

		if (!res || ec) {
			std::cout << ec.message() << std::endl;
			return 0;
		}

		if (res) {
			in_path = temp;
		}
	}

	temp = cmd_args.getArg("-o");

	if ( temp.empty() ) {
		std::cout << "not inputted destination file name!" << std::endl;
		return 0;
	} else {
		bool res = fs::exists(temp, ec);

		if (res) {
			std::cout << "warning: destination file already exist!" << std::endl;
		}

		out_path = temp;

		if (out_path.filename().empty()) {
			out_path = "./" + temp;
		}
	}

	temp = cmd_args.getArg("-b");

	if ( temp.empty() ) {
		std::cout << "default block size: 1Mb" << std::endl;
	} else {
		block_sz = stol(temp);

		if (block_sz <= 0) {
			block_sz = 1;
		}

		std::cout << "block size: " << block_sz << "Mb" << std::endl;
		block_sz *= 1024 * 1024;
	}

	boost::timer::auto_cpu_timer timer;

	simpleCheckSum(in_path, out_path, block_sz);
	//mappedCheckSum(in_path, in_path.filename()); // not use

	return 0;
}
