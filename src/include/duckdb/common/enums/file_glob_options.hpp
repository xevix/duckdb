//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/common/enums/file_glob_options.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/common/constants.hpp"
#include "duckdb/common/shared_ptr.hpp"

namespace duckdb {

enum class FileGlobOptions : uint8_t { DISALLOW_EMPTY = 0, ALLOW_EMPTY = 1, FALLBACK_GLOB = 2 };

//! Filter that can be used to prune paths while a glob is being expanded
//! Directories that are excluded are not traversed any further
class GlobPathFilter {
public:
	virtual ~GlobPathFilter() = default;

	//! Whether a path encountered during glob expansion should be included in the result
	virtual bool IncludePath(const string &path, bool is_directory) const = 0;
};

struct FileGlobInput {
	FileGlobInput(FileGlobOptions options) // NOLINT: allow implicit conversion from FileGlobOptions
	    : behavior(options), allow_empty(options == FileGlobOptions::ALLOW_EMPTY) {
	}
	FileGlobInput(FileGlobOptions options, string extension_p)
	    : behavior(options), allow_empty(options == FileGlobOptions::ALLOW_EMPTY), extension(std::move(extension_p)) {
	}

	bool AllowsEmpty() const {
		return allow_empty || behavior == FileGlobOptions::ALLOW_EMPTY;
	}

	bool IncludePath(const string &path, bool is_directory) const {
		return !path_filter || path_filter->IncludePath(path, is_directory);
	}

	FileGlobOptions behavior;
	bool allow_empty;
	string extension;
	//! Optional filter used to prune paths during glob expansion
	shared_ptr<GlobPathFilter> path_filter;
};

} // namespace duckdb
