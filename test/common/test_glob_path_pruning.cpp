#include "catch.hpp"
#include "duckdb/common/file_system.hpp"
#include "duckdb/common/fstream.hpp"
#include "duckdb/common/hive_partitioning.hpp"
#include "duckdb/common/multi_file/multi_file_list.hpp"
#include "duckdb/common/set.hpp"
#include "test_helpers.hpp"

using namespace duckdb;

namespace {

//! Prunes every path whose "k" hive partition key does not match, and records which paths were considered
class TestPathFilter : public MultiFilePathFilter {
public:
	explicit TestPathFilter(string key_value_p) : key_value(std::move(key_value_p)) {
	}

	bool PrunePath(const string &path) override {
		checked.insert(path);
		// directories are passed in without a trailing separator - add one so the last component is parsed
		auto partitions = HivePartitioning::Parse(path + "/");
		auto entry = partitions.find("k");
		if (key_value.empty() || entry == partitions.end() || entry->second == key_value) {
			expanded.insert(path);
			return false;
		}
		return true;
	}

	//! The number of paths below "root" that we were asked about
	idx_t CheckedBelow(const string &root) const {
		return CountBelow(checked, root);
	}
	//! The number of paths below "root" that were actually expanded (i.e. that required a directory listing)
	idx_t ExpandedBelow(const string &root) const {
		return CountBelow(expanded, root);
	}

private:
	static idx_t CountBelow(const set<string> &paths, const string &root) {
		idx_t count = 0;
		for (auto &path : paths) {
			if (path.size() > root.size() && StringUtil::StartsWith(path, root)) {
				count++;
			}
		}
		return count;
	}

private:
	string key_value;
	set<string> checked;
	set<string> expanded;
};

void CreateFile(FileSystem &fs, const string &path) {
	ofstream outfile(path);
	outfile << "I_AM_A_DUMMY" << endl;
	outfile.close();
	REQUIRE(fs.FileExists(path));
}

} // namespace

TEST_CASE("Test that a path filter prunes directories before they are listed", "[file_system]") {
	constexpr idx_t KEY_COUNT = 8;
	constexpr idx_t SUBKEY_COUNT = 8;

	auto fs = FileSystem::CreateLocal();
	auto root = TestCreatePath("glob_path_pruning");
	if (fs->DirectoryExists(root)) {
		fs->RemoveDirectory(root);
	}
	fs->CreateDirectory(root);

	// create a two-level hive partitioned directory tree with a single file in each leaf directory
	for (idx_t k = 0; k < KEY_COUNT; k++) {
		auto key_dir = fs->JoinPath(root, "k=" + to_string(k));
		fs->CreateDirectory(key_dir);
		for (idx_t j = 0; j < SUBKEY_COUNT; j++) {
			auto subkey_dir = fs->JoinPath(key_dir, "j=" + to_string(j));
			fs->CreateDirectory(subkey_dir);
			CreateFile(*fs, fs->JoinPath(subkey_dir, "data.parquet"));
		}
	}

	// both the recursive crawl and the regular glob must prune
	vector<string> patterns {fs->JoinPath(fs->JoinPath(root, "**"), "*.parquet"),
	                         fs->JoinPath(fs->JoinPath(fs->JoinPath(root, "*"), "*"), "*.parquet")};
	for (auto &pattern : patterns) {
		// without pruning every directory in the tree is listed
		auto all_files_filter = make_shared_ptr<TestPathFilter>("");
		auto file_list = fs->Glob(pattern, FileGlobOptions::ALLOW_EMPTY, nullptr);
		file_list->SetPathFilter(all_files_filter);
		REQUIRE(file_list->GetAllFiles().size() == KEY_COUNT * SUBKEY_COUNT);
		REQUIRE(all_files_filter->ExpandedBelow(root) == KEY_COUNT + KEY_COUNT * SUBKEY_COUNT);

		// when pruning on "k" we only ever list the directories of the matching partition
		auto pruning_filter = make_shared_ptr<TestPathFilter>("3");
		auto pruned_list = fs->Glob(pattern, FileGlobOptions::ALLOW_EMPTY, nullptr);
		pruned_list->SetPathFilter(pruning_filter);
		auto pruned_files = pruned_list->GetAllFiles();

		REQUIRE(pruned_files.size() == SUBKEY_COUNT);
		for (auto &file : pruned_files) {
			REQUIRE(StringUtil::Contains(file.path, "k=3"));
		}
		// the sub-trees of the other partitions are never listed - we only look at their top-level directory
		REQUIRE(pruning_filter->CheckedBelow(root) == KEY_COUNT + SUBKEY_COUNT);
		REQUIRE(pruning_filter->ExpandedBelow(root) == 1 + SUBKEY_COUNT);
	}
}
