//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/common/multi_file/multi_file_list.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/common/common.hpp"
#include "duckdb/common/multi_file/multi_file_options.hpp"
#include "duckdb/common/extra_operator_info.hpp"
#include "duckdb/common/open_file_info.hpp"
#include "duckdb/common/column_index.hpp"
#include "duckdb/common/enums/file_glob_options.hpp"

namespace duckdb {
class MultiFileList;
class NodeStatistics;
class LogicalGet;
class TableFilterSet;
struct MultiFileDynamicPushdownInfo;

enum class FileExpandResult : uint8_t { NO_FILES, SINGLE_FILE, MULTIPLE_FILES };
enum class MultiFileListScanType { ALWAYS_FETCH, FETCH_IF_AVAILABLE };

enum class FileExpansionType { ALL_FILES_EXPANDED, NOT_ALL_FILES_KNOWN };

struct MultiFileListScanData {
	idx_t current_file_idx = DConstants::INVALID_INDEX;
	MultiFileListScanType scan_type = MultiFileListScanType::ALWAYS_FETCH;
};

struct MultiFileCount {
	explicit MultiFileCount(idx_t count, FileExpansionType type = FileExpansionType::ALL_FILES_EXPANDED)
	    : count(count), type(type) {
	}

	idx_t count;
	FileExpansionType type;
};

class MultiFileListIterationHelper {
public:
	DUCKDB_API explicit MultiFileListIterationHelper(const MultiFileList &collection);

private:
	const MultiFileList &file_list;

private:
	class MultiFileListIterator;

	class MultiFileListIterator {
	public:
		DUCKDB_API explicit MultiFileListIterator(optional_ptr<const MultiFileList> file_list);

		optional_ptr<const MultiFileList> file_list;
		MultiFileListScanData file_scan_data;
		OpenFileInfo current_file;

	public:
		DUCKDB_API void Next();

		DUCKDB_API MultiFileListIterator &operator++();
		DUCKDB_API bool operator!=(const MultiFileListIterator &other) const;
		DUCKDB_API const OpenFileInfo &operator*() const;
	};

public:
	MultiFileListIterator begin(); // NOLINT: match stl API
	MultiFileListIterator end();   // NOLINT: match stl API
};

struct MultiFilePushdownInfo {
	explicit MultiFilePushdownInfo(LogicalGet &get);
	MultiFilePushdownInfo(TableIndex table_index, const vector<Identifier> &column_names,
	                      const vector<ColumnIndex> &column_indexes, ExtraOperatorInfo &extra_info);

	TableIndex table_index;
	const vector<Identifier> &column_names;
	vector<column_t> column_ids;
	vector<ColumnIndex> column_indexes;
	ExtraOperatorInfo &extra_info;
};

//! Abstract class for lazily generated list of file paths/globs
//! NOTE: subclasses are responsible for ensuring thread-safety
//! NOTE: a MultiFileList must be owned by a shared_ptr - filter pushdown hands out a reference to the list it filters
class MultiFileList : public enable_shared_from_this<MultiFileList> {
public:
	MultiFileList();
	virtual ~MultiFileList();

	//! Get Iterator over the files for pretty for loops
	MultiFileListIterationHelper Files() const;

	//! Initialize a sequential scan over a file list
	void InitializeScan(MultiFileListScanData &iterator) const;
	//! Scan the next file into result_file, returns false when out of files
	bool Scan(MultiFileListScanData &iterator, OpenFileInfo &result_file) const;

	//! Returns the first file or an empty string if GetTotalFileCount() == 0
	OpenFileInfo GetFirstFile() const;
	//! Syntactic sugar for GetExpandResult() == FileExpandResult::NO_FILES
	bool IsEmpty() const;

	//! Virtual functions for subclasses
public:
	virtual unique_ptr<MultiFileList> ComplexFilterPushdown(ClientContext &context, const MultiFileOptions &options,
	                                                        MultiFilePushdownInfo &info,
	                                                        vector<unique_ptr<Expression>> &filters) const;
	virtual unique_ptr<MultiFileList> DynamicFilterPushdown(MultiFileDynamicPushdownInfo &dynamic_pushdown_info) const;

	virtual vector<OpenFileInfo> GetAllFiles() const = 0;
	virtual FileExpandResult GetExpandResult() const = 0;
	//! Get the total file count - forces all files to be expanded / known so the exact count can be computed
	virtual idx_t GetTotalFileCount() const = 0;
	//! Get the file count - anything under "min_exact_count" is allowed to be incomplete (i.e. `NOT_ALL_FILES_KNOWN`)
	//! This allows us to get a rough idea of the file count
	virtual MultiFileCount GetFileCount(idx_t min_exact_count = 0) const;
	virtual vector<OpenFileInfo> GetDisplayFileList(optional_idx max_files = optional_idx()) const;
	//! Whether or not the file is part of this list - the default implementation scans the list until the file is
	//! found, lists that can answer this without expanding should override it
	virtual bool ContainsFile(const string &path) const;

	virtual unique_ptr<NodeStatistics> GetCardinality(ClientContext &context) const;
	virtual unique_ptr<MultiFileList> Copy() const;

protected:
	//! Whether or not the file at the index is available instantly - or if this requires additional I/O
	virtual bool FileIsAvailable(idx_t i) const;
	//! Get the i-th expanded file
	virtual OpenFileInfo GetFile(idx_t i) const = 0;

private:
	//! Push the given filters into this list - the filters are applied to the files as this list is expanded, so a
	//! list that is not fully expanded yet does not have to be expanded here
	unique_ptr<MultiFileList> PushdownFilters(ClientContext &context, const MultiFileOptions &options,
	                                          MultiFilePushdownInfo &info,
	                                          vector<unique_ptr<Expression>> &filters) const;

public:
	template <class TARGET>
	TARGET &Cast() {
		DynamicCastCheck<TARGET>(this);
		return reinterpret_cast<TARGET &>(*this);
	}
	template <class TARGET>
	const TARGET &Cast() const {
		DynamicCastCheck<TARGET>(this);
		return reinterpret_cast<const TARGET &>(*this);
	}
};

//! MultiFileList that takes a list of files and produces the same list of paths. Useful for quickly wrapping
//! existing vectors of paths in a MultiFileList without changing any code
class SimpleMultiFileList : public MultiFileList {
public:
	//! Construct a SimpleMultiFileList from a list of already expanded files
	explicit SimpleMultiFileList(vector<OpenFileInfo> paths);

	//! Main MultiFileList API
	vector<OpenFileInfo> GetAllFiles() const override;
	FileExpandResult GetExpandResult() const override;
	idx_t GetTotalFileCount() const override;

protected:
	//! Main MultiFileList API
	OpenFileInfo GetFile(idx_t i) const override;

protected:
	//! The list of input paths
	const vector<OpenFileInfo> paths;
};

//! Lazily expanded MultiFileList
class LazyMultiFileList : public MultiFileList {
public:
	explicit LazyMultiFileList(optional_ptr<ClientContext> context);

	vector<OpenFileInfo> GetAllFiles() const override;
	FileExpandResult GetExpandResult() const override;
	idx_t GetTotalFileCount() const override;
	MultiFileCount GetFileCount(idx_t min_exact_count = 0) const override;

protected:
	bool FileIsAvailable(idx_t i) const override;
	OpenFileInfo GetFile(idx_t i) const override;

	//! Grabs the next path and expands it into Expanded paths: returns false if no more files to expand
	virtual bool ExpandNextPath() const = 0;

private:
	bool ExpandNextPathInternal() const;

protected:
	mutable mutex lock;
	//! The expanded files
	mutable vector<OpenFileInfo> expanded_files;
	//! Whether or not all files have been expanded
	mutable bool all_files_expanded = false;
	optional_ptr<ClientContext> context;
};

//! MultiFileList that takes a list of globs and resolves all of the globs lazily into files
class GlobMultiFileList : public LazyMultiFileList {
public:
	GlobMultiFileList(ClientContext &context, vector<string> globs, FileGlobInput input);

	vector<OpenFileInfo> GetDisplayFileList(optional_idx max_files = optional_idx()) const override;

protected:
	bool ExpandNextPath() const override;

protected:
	//! The ClientContext for globbing
	ClientContext &context;
	//! The list of globs to expand
	const vector<string> globs;
	//! Glob input
	const FileGlobInput glob_input;
	//! The current glob to expand
	mutable idx_t current_glob;
	//! File lists for the underlying globs
	mutable vector<unique_ptr<MultiFileList>> file_lists;
	//! Current scan state
	mutable MultiFileListScanData scan_state;
};

//! MultiFileList that applies a set of filters to the files of another list while that list is expanded. Only the
//! hive partition keys and the filename found in the path of a file are used, so files can be filtered out without
//! ever being opened - and without expanding the underlying list up-front
class FilteredMultiFileList : public LazyMultiFileList {
public:
	FilteredMultiFileList(ClientContext &context, shared_ptr<const MultiFileList> source,
	                      HivePartitioningFilterInfo filter_info, const vector<unique_ptr<Expression>> &filters,
	                      TableIndex table_index);
	~FilteredMultiFileList() override;

	bool ContainsFile(const string &path) const override;

	//! The filters (by index) that have filtered out at least one file so far
	unordered_set<idx_t> GetPruningFilters() const;

protected:
	bool ExpandNextPath() const override;

private:
	//! Whether or not any of the filters evaluates to false for the given path
	bool PathIsFiltered(const string &path) const;

private:
	ClientContext &context;
	//! The list that is being filtered - kept alive because files are pulled out of it lazily
	shared_ptr<const MultiFileList> source;
	//! Describes which columns can be obtained from the path of a file
	HivePartitioningFilterInfo filter_info;
	//! Copies of the filters - the filters of the pushdown are modified while we are filtering
	vector<unique_ptr<Expression>> filters;
	TableIndex table_index;
	//! Scan over the source list - protected by the lock of the base class
	mutable MultiFileListScanData source_scan;
	//! The filters that have filtered out a file - protected by the lock of the base class
	mutable unordered_set<idx_t> pruning_filters;
};

} // namespace duckdb
