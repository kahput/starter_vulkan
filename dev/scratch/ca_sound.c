#include "core/image.h"
#include "core/os.h"
#include "core/strings.h"
#include <core.h>
#include <stdio.h>

#include <alsa/asoundlib.h>

#define RIFFID(a, b, c, d) ((uint32_t)a << 0 | (uint32_t)b << 8 | (uint32_t)c << 16 | (uint32_t)d << 24)
typedef enum {
	WAVEID_RIFF = RIFFID('R', 'I', 'F', 'F'),
	WAVEID_WAVE = RIFFID('W', 'A', 'V', 'E'),
	WAVEID_FMT = RIFFID('f', 'm', 't', ' '),
	WAVEID_DATA = RIFFID('d', 'a', 't', 'a'),
} WaveID;

typedef enum {
	WAVE_FORMAT_PCM = 0x1, // PCM
	WAVE_FORMAT_IEEE_FLOAT = 0x3, // IEEE float
	WAVE_FORMAT_ALAW = 0x6, // 8-bit ITU-T G.711 A-law
	WAVE_FORMAT_MULAW = 0x7, // 8-bit ITU-T G.711 µ-law
	WAVE_FORMAT_EXTENSIBLE = 0xFFFE, // Determined by SubFormat
} WaveFormatCode;

#define ALSA_ASOUNDLIB_PATH "/usr/lib/libasound.so.2"

#define ALSA_PROC_LIST(X)                                                                                                         \
	X(snd_pcm_open, int, return 0, snd_pcm_t * *pcm, const char *name, snd_pcm_stream_t stream, int mode)                         \
	X(snd_pcm_close, int, return 0, snd_pcm_t * pcm)                                                                              \
	X(snd_pcm_nonblock, int, return 0, snd_pcm_t * pcm, int nonblock)                                                             \
	X(snd_pcm_hw_params_sizeof, size_t, return 0, void)                                                                           \
	X(snd_pcm_hw_params_any, int, return 0, snd_pcm_t * pcm, snd_pcm_hw_params_t * params)                                        \
	X(snd_pcm_hw_params_set_access, int, return 0, snd_pcm_t * pcm, snd_pcm_hw_params_t * params, snd_pcm_access_t access)        \
	X(snd_pcm_hw_params_set_format, int, return 0, snd_pcm_t * pcm, snd_pcm_hw_params_t * params, snd_pcm_format_t format)        \
	X(snd_pcm_hw_params_set_channels, int, return 0, snd_pcm_t * pcm, snd_pcm_hw_params_t * params, unsigned int channels)        \
	X(snd_pcm_hw_params_set_rate, int, return 0, snd_pcm_t * pcm, snd_pcm_hw_params_t * params, unsigned int rate, int dir)       \
	X(snd_pcm_hw_params_set_periods, int, return 0, snd_pcm_t * pcm, snd_pcm_hw_params_t * params, unsigned int periods, int dir) \
	X(snd_pcm_hw_params_set_period_time, int, return 0, snd_pcm_t * pcm, snd_pcm_hw_params_t * params, unsigned int us, int dir)  \
	X(snd_pcm_hw_params, int, return 0, snd_pcm_t * pcm, snd_pcm_hw_params_t * params)                                            \
	X(snd_pcm_writei, snd_pcm_sframes_t, return 0, snd_pcm_t * pcm, const void *buffer, snd_pcm_uframes_t size)                   \
	X(snd_pcm_drain, int, return 0, snd_pcm_t * pcm)                                                                              \
	X(snd_strerror, const char *, return "Success", int errnum)

#define alsa__snd_alloca(ptr, T)                     \
	do {                                             \
		*ptr = (T##_t *)alloca(alsa_##T##_sizeof()); \
		memset(*ptr, 0, alsa_##T##_sizeof());        \
	} while (0)
#define alsa_snd_pcm_hw_params_alloca(ptr) alsa__snd_alloca(ptr, snd_pcm_hw_params)

#define ALSA_ENUMERATOR(name, ret, stub, ...) ALSA_##name,
#define ALSA_NAME(name, ret, stub, ...) [ALSA_##name] = comp8(#name),
#define ALSA_FORWARD_DECLARE(name, ret, stub, ...) \
	typedef ret (*name##_proc)(__VA_ARGS__);       \
	ret name##_stub(__VA_ARGS__) { stub; }         \
	name##_proc alsa_##name = name##_stub;
#define ALSA_POINTER(name, ret, stub, ...) \
	[ALSA_##name] = &alsa_##name,

typedef enum { ALSA_PROC_LIST(ALSA_ENUMERATOR) ALSA_max,
} AlsaProc;
static string8 alsa_proc_name_list[ALSA_max] = { ALSA_PROC_LIST(ALSA_NAME) };
ALSA_PROC_LIST(ALSA_FORWARD_DECLARE)
static void *alsa_proc_pointer_list[ALSA_max] = { ALSA_PROC_LIST(ALSA_POINTER) };

#undef ALSA_ENUMERATOR
#undef ALSA_NAME
#undef ALSA_FORWARD_DECLARE

int main(void) {
	Arena arena[] = { arena_make(MiB(8)) };

	string8 wav_file = os_file_read(arena, s("resources/sound/test.wav"));
	Stream r[] = { stream_from_string(wav_file) };

	uint32_t riffID = read32le(r);
	uint32_t file_size = read32le(r); // file size, skip
	uint32_t waveID = read32le(r);
	ASSERT(riffID == WAVEID_RIFF && waveID == WAVEID_WAVE);

	typedef struct {
		uint16_t format_code;
		uint16_t channel_count;
		uint32_t sample_rate;
		uint32_t byte_rate;
		uint16_t block_align;
		uint16_t bits_per_sample;
	} WaveFormat;

	WaveFormat fmt = { 0 };
	string8 pcm_view = { 0 };

	/*
$2 = {format_code = 1, channel_count = 2, sample_rate = 44100, byte_rate = 176400,
  block_align = 4, bits_per_sample = 16}
	 * */
	while (stream_at_end(r) == false) {
		string8 name = readchars(r, 4);
		uint32_t chunkid = load32le(name.bytes);
		uint32_t chunk_size = read32le(r);
		printf("%.*s[%u]\n", arg8(name), chunk_size);

		switch (chunkid) {
			case WAVEID_FMT:
				ASSERT(chunk_size >= 16);
				fmt.format_code = read16le(r);
				fmt.channel_count = read16le(r);
				fmt.sample_rate = read32le(r);
				fmt.byte_rate = read32le(r);
				fmt.block_align = read16le(r);
				fmt.bits_per_sample = read16le(r);

				r->at += chunk_size - 16;
				break;
			case WAVEID_DATA:
				pcm_view = readchars(r, chunk_size);
				break;
			default:
				r->at += chunk_size;
				break;
		}

		r->at += chunk_size & 1;
	}
	ASSERT(fmt.channel_count);

	uint32_t bytes_per_sample = fmt.bits_per_sample / 8;
	uint32_t sample_count = pcm_view.length / bytes_per_sample;
	float *samples = arena_push_count(arena, float, sample_count);

	float norm = 1.0f / (powf(2, fmt.bits_per_sample - 1) - 1.0f);
	for (uint32_t sample_index = 0; sample_index < sample_count; ++sample_index) {
		uint32_t offset = sample_index * bytes_per_sample;

		uint8_t *sample_bytes = pcm_view.bytes + offset;
		if (fmt.format_code == WAVE_FORMAT_PCM) {
			int64_t value = 0;
			// clang-format off
			switch (bytes_per_sample) {
				case 1: value = sample_bytes[0] - 128; break;
				case 2: value = (int16_t)load16le(sample_bytes); break;
                case 3: value = (int32_t)load24le(sample_bytes); break;
                case 4: value = (int32_t)load32le(sample_bytes); break;
                default: ASSERT(false); break;
			}
			// clang-format on

			samples[sample_index] = (float)value * norm;
		} else if (fmt.format_code == WAVE_FORMAT_IEEE_FLOAT)
			samples[sample_index] = bitcast_float32(load32le(sample_bytes));
	}

	OS_Library lib = os_dlopen(s(ALSA_ASOUNDLIB_PATH));
	if (lib) {
		for (uint32_t proc_index = 0; proc_index < ALSA_max; ++proc_index)
			os_dlsym(lib, alsa_proc_name_list[proc_index], alsa_proc_pointer_list[proc_index]);
	}

	snd_pcm_t *handle;
	int result = alsa_snd_pcm_open(&handle, "default", SND_PCM_STREAM_PLAYBACK, 0);

	snd_pcm_hw_params_t *hw_params;
	alsa_snd_pcm_hw_params_alloca(&hw_params);

	alsa_snd_pcm_hw_params_any(handle, hw_params);
	alsa_snd_pcm_hw_params_set_access(handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
	alsa_snd_pcm_hw_params_set_format(handle, hw_params, SND_PCM_FORMAT_FLOAT_LE);
	alsa_snd_pcm_hw_params_set_channels(handle, hw_params, fmt.channel_count);
	alsa_snd_pcm_hw_params_set_rate(handle, hw_params, fmt.sample_rate, 0);

	alsa_snd_pcm_hw_params(handle, hw_params);
	alsa_snd_pcm_writei(handle, samples, sample_count / fmt.channel_count);

	alsa_snd_pcm_drain(handle);
	alsa_snd_pcm_close(handle);

	return 0;
}
