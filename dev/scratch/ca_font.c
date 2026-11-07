#include <core.h>
#include <stdio.h>

int main(void) {
	Arena arena[] = { arena_make(MiB(8)) };
	string8 font_path = s("/usr/share/fonts/TTF/IBMPlexMono-Regular.ttf");
	string8 file_content = os_file_read(arena, font_path);

	Stream s[] = { stream_from_string(file_content) };

	s->at += 4;
	uint32_t table_count = read16be(s);
	s->at += 2 + 2 + 2; // search_range, entry_selector, range_shift

	printf("Table count: %hu\n", table_count);
	for (uint32_t table_index = 0; table_index < table_count; ++table_index) {
		string8 tag = readchars(s, 4);
		uint32_t checksum = read32be(s);
		uint32_t offset = read32be(s);
		uint32_t length = read32be(s);

		printf("Tag: %.*s, location: %u\n", arg8(tag), offset);

		if (tag.bytes[0] == 'g' && str8_match(tag, s("glyf"))) {
			Stream glyf[] = { stream(s->start + offset, s->start + offset + length) };
			uint16_t contour_end_indices_length = read16be(glyf);

			printf("Count = %hu\n", contour_end_indices_length);
		}
	}
	return 0;
}
