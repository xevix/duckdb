//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/common/multi_file/multi_file_path_filter.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/common/common.hpp"

namespace duckdb {

//! Filter that decides whether or not a partial path needs to be expanded while a file list is being expanded
//! This allows file lists to skip entire directory sub-trees without ever listing their contents
class MultiFilePathFilter {
public:
	virtual ~MultiFilePathFilter() = default;

	//! Returns true if no file below this path can match - i.e. the path does not need to be expanded
	//! Note that the path is a directory that is passed in without a trailing separator
	virtual bool PrunePath(const string &path) = 0;
};

} // namespace duckdb
