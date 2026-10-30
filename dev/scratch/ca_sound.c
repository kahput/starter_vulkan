#include "core/image.h"
#include "core/os.h"
#include "core/strings.h"
#include <core.h>
#include <stdio.h>

typedef struct {
	uint8_t *stream;
} ByteReader;

uint32_t load32le(const uint8_t *bytes) {
	return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}
float loadf32le(const uint8_t *bytes) {
	uint32_t u32 = load32le(bytes);
	float f32;
	memcpy(&f32, &u32, sizeof(f32));
	return f32;
}

uint32_t load24le(const uint8_t *bytes) {
	return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16;
}
uint16_t load16le(const uint8_t *bytes) {
	return (uint16_t)bytes[0] | (uint16_t)bytes[1] << 8;
}

string8 read_chars(ByteReader *r, uint32_t count) {
	return r->stream += count, str8_range(r->stream - count, r->stream);
}

uint32_t read32le(ByteReader *r) {
	return r->stream += 4, load32le(r->stream - 4);
}

uint16_t readu16le(ByteReader *r) {
	return r->stream += 2, load16le(r->stream - 2);
}

typedef enum {
	WAVE_FORMAT_PCM = 0x1, // PCM
	WAVE_FORMAT_IEEE_FLOAT = 0x3, // IEEE float
	WAVE_FORMAT_ALAW = 0x6, // 8-bit ITU-T G.711 A-law
	WAVE_FORMAT_MULAW = 0x7, // 8-bit ITU-T G.711 µ-law
	WAVE_FORMAT_EXTENSIBLE = 0xFFFE, // Determined by SubFormat
} WaveFormatCode;

int main(void) {
	Arena arena[] = { arena_make(MiB(8)) };

	string8 wav_file = os_file_read(arena, s("resources/sound/test.wav"));

	ByteReader r[] = { { wav_file.bytes } };

	string8 riffID = read_chars(r, 4);
	read32le(r); // file size, skip
	string8 waveID = read_chars(r, 4);
	ASSERT(str8_match(riffID, s("RIFF")) && str8_match(waveID, s("WAVE")));

	struct wav_fmt {
		uint16_t format_code;
		uint16_t channel_count;
		uint16_t sample_rate;
		uint32_t byte_rate;
		uint16_t block_align;
		uint16_t bits_per_sample;
	} fmt = { 0 };
	string8 pcm = { 0 };
	while (r->stream < wav_file.bytes + wav_file.length) {
		string8 chunkID = read_chars(r, 4);
		uint32_t chunk_size = read32le(r);
		printf("%.*s[%u]\n", arg8(chunkID), chunk_size);

		if (chunkID.bytes[0] == 'f' && str8_match(chunkID, s("fmt "))) {
			fmt.format_code = readu16le(r);
			fmt.channel_count = readu16le(r);
			fmt.sample_rate = read32le(r);
			fmt.byte_rate = read32le(r);
			fmt.block_align = readu16le(r);
			fmt.bits_per_sample = readu16le(r);
		} else if (chunkID.bytes[0] == 'd' && str8_match(chunkID, s("data"))) {
			pcm = read_chars(r, chunk_size);
		} else {
		}
	}

	uint32_t bytes_per_sample = fmt.bits_per_sample / 8;
	uint32_t sample_count = pcm.length / (bytes_per_sample * fmt.channel_count);
	float *samples = arena_push_count(arena, float, sample_count);
	float norm = 1.0f / (powf(2, fmt.bits_per_sample - 1) - 1.0f);

	for (uint32_t index = 0; index < sample_count; ++index) {
		uint32_t offset = index * bytes_per_sample * fmt.channel_count;

		uint8_t *sample_bytes = pcm.bytes + offset;
		if (fmt.format_code == WAVE_FORMAT_PCM) {
			int64_t value = 0;
			// clang-format off
			switch (bytes_per_sample) {
				case 1: value = sample_bytes[0] - 128; break;
				case 2: value = (int16_t)load16le(sample_bytes); break;
                case 4: value = (int32_t)load32le(sample_bytes); break;
                default: ASSERT(false); break;
			}
			// clang-format on

			samples[index] = (float)value * norm;
		} else if (fmt.format_code == WAVE_FORMAT_IEEE_FLOAT)
			samples[index] = loadf32le(sample_bytes) * norm;
	}

	(void)fmt;
	(void)pcm;

	Image draw = impush(arena, 640, 360, 4);
	memory_set(draw.pixels, 255, imbytes(draw));

	float radius = 2.0f;
	for (uint32_t index = 0; index < sample_count; ++index) {
		float t = (float)index / (sample_count - 1);
		float sx = 32 + t * (640 - 32 * 2);
		float sy = 18 + (samples[index] * 0.5f + 0.5f) * (360 - 18 * 2);

		uint2 start = { floorf(sx - radius), floorf(sy - radius) };
		uint2 end = { ceilf(sx + radius), ceilf(sy + radius) };

		for (uint32_t y = start.y; y <= end.y; ++y) {
			for (uint32_t x = start.x; x <= end.x; ++x) {
				uint32_t pixel = x + y * draw.width;
				draw.pixels[pixel * draw.channels + 0] = 255;
				draw.pixels[pixel * draw.channels + 1] = 0;
				draw.pixels[pixel * draw.channels + 2] = 0;
				draw.pixels[pixel * draw.channels + 3] = 255;
			}
		}
	}

	string8 header = str8_fmt(arena, "P6\n%d %d\n255 255 255 255\n", draw.width, draw.height);
	OS_File fd = os_open(s("output.ppm"), OS_CREAT | OS_WRONLY);
	os_write(fd, header.bytes, header.length);
	os_write(fd, draw.pixels, imbytes(draw));
	os_close(fd);
	imsave(s("output.png"), draw);

	printf("Hello world!\n");

	return 0;
}
