#pragma once

#include "common.h"

#include "core/arena.h"
#include "core/strings.h"

bool filesystem_file_exists(String path);
ENGINE_API String filesystem_read(struct arena *arena, String path);

bool filesystem_file_copy(String from, String to);
StringList filesystem_list_files(Arena *arena, String directory_path, bool recursive);

uint64_t filesystem_last_modified(String path);
