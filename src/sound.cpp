#include "sound.h"
#include <SFML/System.hpp>
#include <daScript/daScript.h>
#include <daScript/simulate/interop.h>
#include <daScript/simulate/simulate_visit_op.h>
#include <array>
#include <unordered_set>
#include "globals.h"
#include "fileSystem.h"

#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>


using namespace std;
using namespace das;

#define OUTPUT_SAMPLE_RATE 48000
#define OUTPUT_CHANNELS 2

#define MAX_PLAYING_SOUNDS 128 // power of 2
#define PLAYING_SOUNDS_MASK (MAX_PLAYING_SOUNDS - 1)
#define ONE_DIV_256 (1.f / 256)
#define ONE_DIV_512 (1.f / 512)

namespace sound
{

int64_t total_samples_played = 0;
double total_time_played = 0.0;

static sf::Mutex sound_cs;
static volatile bool sound_cs_manual_entered = false;

static float master_volume = 1.0f;

static unordered_set<float *> sound_data_pointers;

struct WinAutoLock
{
  sf::Mutex * mutex;
  WinAutoLock(sf::Mutex * mutex_) :
    mutex(mutex_)
  {
    mutex->lock();
  }

  ~WinAutoLock()
  {
    mutex->unlock();
  }
};


struct PcmSound
{
private:
  float * data;
public:
  int frequency;
  int samples;
  int channels;

  float * getData() const
  {
    return data;
  }

  void newData(size_t size)
  {
    data = new float[size];
    sound_data_pointers.insert(data);
  }

  void deleteData()
  {
    if (data)
    {
      sound_data_pointers.erase(data);
      delete[] data;
    }
  }

  bool isValid() const
  {
    return !!data;
  }

  inline int getDataMemorySize() const
  {
    return channels * (samples + 4) * sizeof(float);
  }

  float getDuration() const
  {
    return samples / float(frequency);
  }

  int getFrequency() const
  {
    return frequency;
  }

  int getSamples() const
  {
    return samples;
  }

  int getChannels() const
  {
    return channels;
  }

  PcmSound()
  {
    frequency = 44100;
    samples = 0;
    channels = 1;
    data = nullptr;
  }

  PcmSound(const PcmSound & b)
  {
    frequency = b.frequency;
    samples = b.samples;
    channels = b.channels;
    newData(getDataMemorySize());
    memcpy(data, b.data, getDataMemorySize());
  }

  PcmSound& operator=(const PcmSound & b)
  {
    deleteData();
    frequency = b.frequency;
    samples = b.samples;
    channels = b.channels;
    newData(getDataMemorySize());
    memcpy(data, b.data, getDataMemorySize());
    return *this;
  }

  PcmSound(PcmSound && b);
  PcmSound& operator=(PcmSound && b);
  ~PcmSound();

  friend void delete_sound(PcmSound * sound);
};


struct PlayingSound
{
  const PcmSound * sound;
  double pos;      // in samples
  double startPos; // in samples
  double stopPos;  // in samples
  float pitch;
  float volume;
  float pan;
  float volumeL;
  float volumeR;
  float volumeTrendL;
  float volumeTrendR;
  double timeToStart; // in seconds
  int channels;
  int version;
  bool loop;
  bool stopMode;
  bool waitingStart;

  PlayingSound()
  {
    memset(this, 0, sizeof(*this));
  }

  bool isEmpty()
  {
    return !sound && !stopMode && !waitingStart;
  }

  void setStopMode()
  {
    if (!sound)
    {
      waitingStart = false;
      return;
    }

    if (stopMode)
    {
      waitingStart = false;
      return;
    }

    version += MAX_PLAYING_SOUNDS;

    if (waitingStart)
    {
      waitingStart = false;
      sound = nullptr;
      return;
    }

    if (channels == 1)
    {
      float val = sound->getData()[unsigned(pos)];
      volumeL *= val;
      volumeR *= val;
    }
    else
    {
      volumeL *= sound->getData()[unsigned(pos) * 2];
      volumeR *= sound->getData()[unsigned(pos) * 2 + 1];
    }
    volumeTrendL = sign(volumeL) * -(1.f / 10000);
    volumeTrendR = sign(volumeR) * -(1.f / 10000);
    stopMode = true;
    sound = nullptr;
  }

  void mixTo(float * __restrict mix, int count, int frequency, double inv_frequency, double buffer_time)
  {
    float wishVolumeL = master_volume * volume * min(1.0f + pan, 1.0f);
    float wishVolumeR = master_volume * volume * min(1.0f - pan, 1.0f);
    float * __restrict sndData = sound ? sound->getData() : nullptr;
    if (!sndData)
      return;

    double advance = sound ? double(sound->frequency) * inv_frequency * pitch : 1.0;

    if (!stopMode && !waitingStart && sound && volumeL > 0.0f && volumeR > 0.0f &&
        wishVolumeL == volumeL && wishVolumeR == volumeR &&
        pos + advance * count < stopPos)
    {
      if (channels == 1)
      {
        for (int i = 0; i < count; i++, mix += 2)
        {
          unsigned ip = unsigned(pos);
          float t = float(pos - ip);
          float v = lerp(sndData[ip], sndData[ip + 1], t);
          mix[0] += v * volumeL;
          mix[1] += v * volumeR;
          pos += advance;
        }
      }
      else // channels == 2
      {
        for (int i = 0; i < count; i++, mix += 2)
        {
          unsigned ip = unsigned(pos);
          float t = float(pos - ip);
          float vl = lerp(sndData[ip * 2], sndData[ip * 2 + 2], t);
          float vr = lerp(sndData[ip * 2 + 1], sndData[ip * 2 + 2 + 1], t);
          mix[0] += vl * volumeL;
          mix[1] += vr * volumeR;
          pos += advance;
        }
      }

      return;
    }

    if (waitingStart && timeToStart > buffer_time)
    {
      timeToStart -= buffer_time;
      return;
    }


    if (channels == 1)
    {
      for (int i = 0; i < count; i++, mix += 2)
      {
        if (waitingStart)
        {
          timeToStart -= inv_frequency;
          if (timeToStart <= 0.0)
          {
            waitingStart = false;
            pos = startPos;
          }
        }
        else if (!stopMode)
        {
          unsigned ip = unsigned(pos);
          float t = float(pos - ip);
          float v = lerp(sndData[ip], sndData[ip + 1], t);

          mix[0] += v * volumeL;
          mix[1] += v * volumeR;

          if (volumeL != wishVolumeL)
          {
            if (fabsf(volumeL - wishVolumeL) <= ONE_DIV_512)
              volumeL = wishVolumeL;
            else if (volumeL < wishVolumeL)
              volumeL += ONE_DIV_512;
            else
              volumeL -= ONE_DIV_512;
          }

          if (volumeR != wishVolumeR)
          {
            if (fabsf(volumeR - wishVolumeR) <= ONE_DIV_512)
              volumeR = wishVolumeR;
            else if (volumeR < wishVolumeR)
              volumeR += ONE_DIV_512;
            else
              volumeR -= ONE_DIV_512;
          }

          pos += advance;
          if (pos >= stopPos)
          {
            if (loop)
              pos = startPos;
            else
            {
              pos = stopPos;
              setStopMode();
            }
          }
        }
        else // stopMode
        {
          if (fabsf(volumeL) <= ONE_DIV_512)
            volumeL = 0.0f;
          else
          {
            volumeL += volumeTrendL;
            volumeL *= 0.997f;
          }

          if (fabsf(volumeR) <= ONE_DIV_512)
            volumeR = 0.0f;
          else
          {
            volumeR += volumeTrendR;
            volumeR *= 0.997f;
          }

          if (volumeR == 0.f && volumeL == 0.f)
            stopMode = false;

          mix[0] += volumeL;
          mix[1] += volumeR;
        }
      }
    }
    else // channels == 2
    {
      for (int i = 0; i < count; i++, mix += 2)
      {
        if (waitingStart)
        {
          timeToStart -= inv_frequency;
          if (timeToStart <= 0.0)
          {
            waitingStart = false;
            pos = startPos;
          }
        }
        else if (!stopMode)
        {
          unsigned ip = unsigned(pos);
          float t = float(pos - ip);
          float vl = lerp(sndData[ip * 2], sndData[ip * 2 + 2], t);
          float vr = lerp(sndData[ip * 2 + 1], sndData[ip * 2 + 2 + 1], t);

          mix[0] += vl * volumeL;
          mix[1] += vr * volumeR;

          if (volumeL != wishVolumeL)
          {
            if (fabsf(volumeL - wishVolumeL) <= ONE_DIV_512)
              volumeL = wishVolumeL;
            else if (volumeL < wishVolumeL)
              volumeL += ONE_DIV_512;
            else
              volumeL -= ONE_DIV_512;
          }

          if (volumeR != wishVolumeR)
          {
            if (fabsf(volumeR - wishVolumeR) <= ONE_DIV_512)
              volumeR = wishVolumeR;
            else if (volumeR < wishVolumeR)
              volumeR += ONE_DIV_512;
            else
              volumeR -= ONE_DIV_512;
          }

          pos += advance;
          if (pos >= stopPos)
          {
            if (loop)
              pos = startPos;
            else
            {
              pos = stopPos;
              setStopMode();
            }
          }
        }
        else // stopMode
        {
          if (fabsf(volumeL) <= ONE_DIV_512)
            volumeL = 0.0f;
          else
          {
            volumeL += volumeTrendL;
            volumeL *= 0.997f;
          }

          if (fabsf(volumeR) <= ONE_DIV_512)
            volumeR = 0.0f;
          else
          {
            volumeR += volumeTrendR;
            volumeR *= 0.997f;
          }

          if (volumeR == 0.f && volumeL == 0.f)
          {
            stopMode = false;
            break;
          }

          mix[0] += volumeL;
          mix[1] += volumeR;
        }
      }
    }
  }
};

static array<PlayingSound, MAX_PLAYING_SOUNDS> playing_sounds;

static int allocate_playing_sound()
{
  for (int i = 1; i < MAX_PLAYING_SOUNDS; i++)
    if (playing_sounds[i].isEmpty())
    {
      playing_sounds[i].version += MAX_PLAYING_SOUNDS;
      return i;
    }
  return -1;
}

static bool is_handle_valid(PlayingSoundHandle ps)
{
  unsigned idx = ps.handle & PLAYING_SOUNDS_MASK;
  return (playing_sounds[idx].version == (ps.handle & (~PLAYING_SOUNDS_MASK))) && idx > 0;
}

static int handle_to_index(PlayingSoundHandle ps)
{
  if (!is_handle_valid(ps))
    return -1;
  return ps.handle & PLAYING_SOUNDS_MASK;
}


static void fill_buffer_cb(float * __restrict out_buf, int frequency, int channels, int samples)
{
  WinAutoLock lock(&sound_cs);

  memset(out_buf, 0, samples * channels * sizeof(float));

  double invFrequency = 1.0 / frequency;
  int step = 256;

  while (samples > 0)
  {
    for (auto && s : playing_sounds)
      if (!s.isEmpty())
        s.mixTo(out_buf, min(samples, step), frequency, invFrequency, min(samples, step) * invFrequency);

    samples -= step;
    out_buf += step * channels;
    total_samples_played += min(samples, step);
    total_time_played += min(samples, step) * invFrequency;
  }
}



static ma_device miniaudio_device;
static volatile bool device_initialized = false;
static ma_log ma_log_struct = { 0 };
static ma_context context = { 0 };


void on_error_log(void * user_data, ma_uint32 level, const char * message)
{
  G_UNUSED(user_data);
  if (level <= 1)
    print_error("%s", message);
  else
    print_note("%s", message);
}

static void miniaudio_data_callback(ma_device* p_device, void* p_output, const void* p_input, ma_uint32 frame_count)
{
  G_UNUSED(p_input);

  if (!device_initialized)
  {
    memset(p_output, 0, frame_count * ma_get_bytes_per_frame(p_device->playback.format, p_device->playback.channels));
    return;
  }

  fill_buffer_cb((float *)p_output, OUTPUT_SAMPLE_RATE, p_device->playback.channels, frame_count);
}

void init_sound_lib_internal()
{
  if (device_initialized)
    return;

  ma_log_init(nullptr, &ma_log_struct);
  ma_log_register_callback(&ma_log_struct, {on_error_log, nullptr});

  ma_context_init(NULL, 0, NULL, &context);
  context.pLog = &ma_log_struct;

  ma_device_config deviceConfig;

  deviceConfig = ma_device_config_init(ma_device_type_playback);
  deviceConfig.playback.format = ma_format_f32;
  deviceConfig.playback.channels = OUTPUT_CHANNELS;
  deviceConfig.sampleRate = OUTPUT_SAMPLE_RATE;
  deviceConfig.dataCallback = miniaudio_data_callback;
  deviceConfig.pUserData = nullptr;

  if (ma_device_init(&context, &deviceConfig, &miniaudio_device) != MA_SUCCESS)
  {
    print_error("SOUND: Failed to open playback device");
    return;
  }

  print_note("Sound device name: %s", miniaudio_device.playback.name);

  if (ma_device_start(&miniaudio_device) != MA_SUCCESS)
  {
    print_error("SOUND: Failed to start playback device");
    ma_device_uninit(&miniaudio_device);
    return;
  }

  device_initialized = true;
}

void initialize()
{
  memset(&playing_sounds[0], 0, sizeof(playing_sounds[0]) * playing_sounds.size());
}

void finalize()
{
  device_initialized = false;
  WinAutoLock lock(&sound_cs);
  ma_device_uninit(&miniaudio_device);
}


PcmSound create_sound(int frequency, const das::TArray<float> & data)
{
  if (!device_initialized)
    init_sound_lib_internal();

  if (frequency < 1 || !data.size)
    return PcmSound();

  PcmSound s;
  s.frequency = frequency;
  s.channels = 1;
  s.samples = data.size;
  s.newData(s.getDataMemorySize());
  memcpy(s.getData(), data.data, s.getDataMemorySize());
  s.getData()[data.size] = s.getData()[0];
  return s;
}

PcmSound create_sound_stereo(int frequency, const das::TArray<das::float2> & data)
{
  if (!device_initialized)
    init_sound_lib_internal();

  if (frequency < 1 || !data.size)
    return PcmSound();

  PcmSound s;
  s.frequency = frequency;
  s.channels = 2;
  s.samples = data.size;
  s.newData(s.getDataMemorySize());
  memcpy(s.getData(), data.data, s.getDataMemorySize());
  s.getData()[s.samples * 2] = s.getData()[0];
  s.getData()[s.samples * 2 + 1] = s.getData()[1];
  return s;
}


PcmSound create_sound_from_file(const char * file_name)
{
  if (!device_initialized)
    init_sound_lib_internal();

  if (!file_name || !file_name[0])
  {
    print_error("Cannot create sound. File name is empty.", file_name);
    return PcmSound();
  }

  if (!fs::is_path_string_valid(file_name))
  {
    print_error("Cannot open sound '%s'. Absolute paths or access to the parent directory is prohibited.", file_name);
    return PcmSound();
  }

  PcmSound s;

  unsigned int channels;
  unsigned int sampleRate;
  drwav_uint64 totalPCMFrameCount;
  float * pSampleData = nullptr;

  const char * p = strrchr(file_name, '.');
  if (p && !stricmp(p, ".wav") && !p[4])
    pSampleData = drwav_open_file_and_read_pcm_frames_f32(file_name, &channels, &sampleRate, &totalPCMFrameCount, nullptr);
  else if (p && !stricmp(p, ".mp3") && !p[4])
  {
    drmp3_config config = { 0 };
    pSampleData = drmp3_open_file_and_read_pcm_frames_f32(file_name, &config, &totalPCMFrameCount, nullptr);
    channels = config.channels;
    sampleRate = config.sampleRate;
  }
  else if (p && !stricmp(p, ".flac") && !p[4])
    pSampleData = drwav_open_file_and_read_pcm_frames_f32(file_name, &channels, &sampleRate, &totalPCMFrameCount, nullptr);
  else
  {
    print_error("Cannot create sound from '%s', unrecognized file format. Expected .wav, .flac or .mp3", file_name);
    return PcmSound();
  }

  if (!pSampleData)
  {
    print_error("Cannot create sound from '%s'", file_name);
    return PcmSound();
  }

  if (channels != 1 && channels != 2)
  {
    print_error("Cannot create sound from '%s', invalid channels count = %d", int(channels));
    return PcmSound();
  }

  s.channels = int(channels);
  s.frequency = int(sampleRate);
  s.samples = int(totalPCMFrameCount);
  s.newData(s.getDataMemorySize());
  memcpy(s.getData(), pSampleData, channels * s.samples * sizeof(float));
  if (s.channels == 2)
  {
    s.getData()[s.samples * 2] = s.getData()[0];
    s.getData()[s.samples * 2 + 1] = s.getData()[1];
  }
  else
  {
    s.getData()[s.samples] = s.getData()[0];
  }

  drwav_free(pSampleData, NULL);
  return s;
}


void get_sound_data(const PcmSound & sound, das::TArray<float> & out_data)
{
  if (!sound.getData())
    return;

  int count = sound.samples;
  if (count > int(out_data.size))
    count = int(out_data.size);

  if (count)
  {
    if (sound.channels == 1)
      memcpy(out_data.data, sound.getData(), count * sizeof(float));
    else if (sound.channels == 2)
    {
      float * __restrict ptr = (float *)out_data.data;
      float * __restrict soundData = (float *)sound.getData();
      for (int i = 0; i < count; i++)
        ptr[i] = (soundData[i * 2] + soundData[i * 2 + 1]) * 0.5f;
    }
  }
}

void get_sound_data_stereo(const PcmSound & sound, das::TArray<das::float2> & out_data)
{
  if (!sound.getData())
    return;

  int count = sound.samples;
  if (count > int(out_data.size))
    count = int(out_data.size);

  if (count)
  {
    if (sound.channels == 2)
      memcpy(out_data.data, sound.getData(), count * sizeof(float) * 2);
    else if (sound.channels == 1)
    {
      float * __restrict ptr = (float *)out_data.data;
      float * __restrict soundData = (float *)sound.getData();
      for (int i = 0; i < count; i++)
      {
        ptr[i * 2] = soundData[i];
        ptr[i * 2 + 1] = soundData[i];
      }
    }
  }
}

void set_sound_data(PcmSound & sound, const das::TArray<float> & in_data)
{
  if (!sound.getData())
    return;

  int count = sound.samples;
  if (count > int(in_data.size))
    count = int(in_data.size);

  if (!count)
    return;

  if (sound.channels == 1)
  {
    memcpy(sound.getData(), in_data.data, count * sizeof(float));
    sound.getData()[sound.samples] = sound.getData()[0];
  }
  else if (sound.channels == 2)
  {
    float * __restrict ptr = (float *)in_data.data;
    float * __restrict soundData = (float *)sound.getData();
    for (int i = 0; i < count; i++)
    {
      soundData[i * 2] = ptr[i];
      soundData[i * 2 + 1] = ptr[i];
    }
    soundData[sound.samples * 2] = soundData[0];
    soundData[sound.samples * 2 + 1] = soundData[1];
  }
}

void set_sound_data_stereo(PcmSound & sound, const das::TArray<das::float2> & in_data)
{
  if (!sound.getData())
    return;

  int count = sound.samples;
  if (count > int(in_data.size))
    count = int(in_data.size);

  if (!count)
    return;

  if (sound.channels == 1)
  {
    float * __restrict ptr = (float *)in_data.data;
    float * __restrict soundData = (float *)sound.getData();
    for (int i = 0; i < count; i++)
      soundData[i] = (ptr[i * 2] + ptr[i * 2 + 1]) * 0.5f;

    sound.getData()[sound.samples] = sound.getData()[0];
  }
  else if (sound.channels == 2)
  {
    memcpy(sound.getData(), in_data.data, 2 * count * sizeof(float));
    sound.getData()[sound.samples * 2] = sound.getData()[0];
    sound.getData()[sound.samples * 2 + 1] = sound.getData()[1];
  }
}

void delete_sound(PcmSound * sound)
{
  WinAutoLock lock(&sound_cs);

  for (auto && s : playing_sounds)
    if (s.sound == sound)
      if (!s.isEmpty())
        s.setStopMode();

  sound->deleteData();
  sound->data = nullptr;
  sound->samples = 0;
}

void delete_allocated_sounds()
{
  WinAutoLock lock(&sound_cs);
  for (auto && data : sound_data_pointers)
    delete[] data;

  sound_data_pointers.clear();
}



PlayingSoundHandle play_sound_internal(const PcmSound & sound, float volume, float pitch, float pan, float start_time, float end_time,
                                       bool loop, float defer_time_sec)
{
  if (!device_initialized)
    init_sound_lib_internal();

  WinAutoLock lock(&sound_cs);

  int idx = allocate_playing_sound();
  if (idx < 0 || sound.samples <= 2)
    return PlayingSoundHandle();

  PlayingSound & s = playing_sounds[idx];

  pitch = clamp(pitch, 0.00001f, 1000.0f);
  pan = clamp(pan, -1.0f, 1.0f);
  volume = clamp(volume, 0.0f, 100000.0f);

  double start = clamp(double(int64_t(start_time * sound.frequency)), 0.0, double(sound.samples - 1));
  double stop = clamp(double(int64_t(end_time * sound.frequency)), start, double(sound.samples - 1));
  double pos = start;
  if (defer_time_sec < 0.0f)
    pos = min(double(int(-defer_time_sec * sound.frequency)), stop);

  s.channels = sound.channels;
  s.sound = &sound;
  s.volume = volume;
  s.pitch = pitch;
  s.pan = pan;
  s.volumeL = master_volume * volume * min(1.0f + pan, 1.0f);
  s.volumeR = master_volume * volume * min(1.0f - pan, 1.0f);

  s.pos = pos;
  s.startPos = start;
  s.stopPos = stop;
  s.loop = loop;
  s.stopMode = false;
  s.timeToStart = max(defer_time_sec, 0.0f);
  s.waitingStart = (s.timeToStart != 0.0);

  PlayingSoundHandle res;
  res.handle = idx | s.version;

  return res;
}


PlayingSoundHandle play_sound_1(const PcmSound & sound)
{
  return play_sound_internal(sound, 1.0f, 1.0f, 0.0f, 0.0f, VERY_BIG_NUMBER, false, 0.0f);
}

PlayingSoundHandle play_sound_2(const PcmSound & sound, float volume)
{
  return play_sound_internal(sound, volume, 1.0f, 0.0f, 0.0f, VERY_BIG_NUMBER, false, 0.0f);
}

PlayingSoundHandle play_sound_3(const PcmSound & sound, float volume, float pitch)
{
  return play_sound_internal(sound, volume, pitch, 0.0f, 0.0f, VERY_BIG_NUMBER, false, 0.0f);
}

PlayingSoundHandle play_sound_4(const PcmSound & sound, float volume, float pitch, float pan)
{
  return play_sound_internal(sound, volume, pitch, pan, 0.0f, VERY_BIG_NUMBER, false, 0.0f);
}

PlayingSoundHandle play_sound_5(const PcmSound & sound, float volume, float pitch, float pan, float start_time, float end_time)
{
  return play_sound_internal(sound, volume, pitch, pan, start_time, end_time, false, 0.0f);
}

PlayingSoundHandle play_sound_loop_1(const PcmSound & sound)
{
  return play_sound_internal(sound, 1.0f, 1.0f, 0.0f, 0.0f, VERY_BIG_NUMBER, true, 0.0f);
}

PlayingSoundHandle play_sound_loop_2(const PcmSound & sound, float volume)
{
  return play_sound_internal(sound, volume, 1.0f, 0.0f, 0.0f, VERY_BIG_NUMBER, true, 0.0f);
}

PlayingSoundHandle play_sound_loop_3(const PcmSound & sound, float volume, float pitch)
{
  return play_sound_internal(sound, volume, pitch, 0.0f, 0.0f, VERY_BIG_NUMBER, true, 0.0f);
}

PlayingSoundHandle play_sound_loop_4(const PcmSound & sound, float volume, float pitch, float pan)
{
  return play_sound_internal(sound, volume, pitch, pan, 0.0f, VERY_BIG_NUMBER, true, 0.0f);
}

PlayingSoundHandle play_sound_loop_5(const PcmSound & sound, float volume, float pitch, float pan, float start_time, float end_time)
{
  return play_sound_internal(sound, volume, pitch, pan, start_time, end_time, true, 0.0f);
}


PlayingSoundHandle play_sound_deferred_1(const PcmSound & sound, float defer_seconds)
{
  return play_sound_internal(sound, 1.0f, 1.0f, 0.0f, 0.0f, VERY_BIG_NUMBER, false, defer_seconds);
}

PlayingSoundHandle play_sound_deferred_2(const PcmSound & sound, float defer_seconds, float volume)
{
  return play_sound_internal(sound, volume, 1.0f, 0.0f, 0.0f, VERY_BIG_NUMBER, false, defer_seconds);
}

PlayingSoundHandle play_sound_deferred_3(const PcmSound & sound, float defer_seconds, float volume, float pitch)
{
  return play_sound_internal(sound, volume, pitch, 0.0f, 0.0f, VERY_BIG_NUMBER, false, defer_seconds);
}

PlayingSoundHandle play_sound_deferred_4(const PcmSound & sound, float defer_seconds, float volume, float pitch, float pan)
{
  return play_sound_internal(sound, volume, pitch, pan, 0.0f, VERY_BIG_NUMBER, false, defer_seconds);
}

PlayingSoundHandle play_sound_deferred_5(const PcmSound & sound, float defer_seconds, float volume, float pitch, float pan,
  float start_time, float end_time)
{
  return play_sound_internal(sound, volume, pitch, pan, start_time, end_time, false, defer_seconds);
}


void set_sound_pitch(PlayingSoundHandle handle, float pitch)
{
  WinAutoLock lock(&sound_cs);

  int idx = handle_to_index(handle);
  if (idx < 0)
    return;
  playing_sounds[idx].pitch = pitch;
}

void set_sound_volume(PlayingSoundHandle handle, float volume)
{
  WinAutoLock lock(&sound_cs);

  int idx = handle_to_index(handle);
  if (idx < 0)
    return;
  playing_sounds[idx].volume = volume;
}

void set_sound_pan(PlayingSoundHandle handle, float pan)
{
  WinAutoLock lock(&sound_cs);

  int idx = handle_to_index(handle);
  if (idx < 0)
    return;
  playing_sounds[idx].pan = pan;
}

bool is_playing(PlayingSoundHandle handle)
{
  int idx = handle_to_index(handle);
  if (idx < 0 || playing_sounds[idx].stopMode)
    return false;

  return true;
}

float get_sound_play_pos(PlayingSoundHandle handle)
{
  WinAutoLock lock(&sound_cs);

  int idx = handle_to_index(handle);
  if (idx < 0)
    return 0.0f;

  if (!playing_sounds[idx].sound || playing_sounds[idx].stopMode || playing_sounds[idx].waitingStart)
    return 0.0f;

  return float(playing_sounds[idx].pos / playing_sounds[idx].sound->frequency);
}

void set_sound_play_pos(PlayingSoundHandle handle, float pos_seconds)
{
  WinAutoLock lock(&sound_cs);

  int idx = handle_to_index(handle);
  if (idx < 0)
    return;

  if (!playing_sounds[idx].sound || playing_sounds[idx].stopMode)
    return;

  double p = floor(playing_sounds[idx].sound->frequency * pos_seconds);
  playing_sounds[idx].pos = clamp(p, playing_sounds[idx].startPos, playing_sounds[idx].stopPos);
}

void stop_sound(PlayingSoundHandle handle)
{
  WinAutoLock lock(&sound_cs);

  int idx = handle_to_index(handle);
  if (idx < 0)
    return;

  if (!playing_sounds[idx].sound || playing_sounds[idx].stopMode)
    return;

  playing_sounds[idx].setStopMode();
}

void stop_all_sounds()
{
  WinAutoLock lock(&sound_cs);

  for (auto && s : playing_sounds)
    if (!s.isEmpty())
      s.setStopMode();
}

void enter_sound_critical_section()
{
  if (sound_cs_manual_entered)
    return;

  sound_cs.lock();
  sound_cs_manual_entered = true;
}

void leave_sound_critical_section()
{
  if (sound_cs_manual_entered)
  {
    sound_cs_manual_entered = false;
    sound_cs.unlock();
  }
}

void set_master_volume(float volume)
{
  WinAutoLock lock(&sound_cs);
  master_volume = volume;
}

float get_output_sample_rate()
{
  return OUTPUT_SAMPLE_RATE;
}

int64_t get_total_samples_played()
{
  return total_samples_played;
}

double get_total_time_played()
{
  return total_time_played;
}


PcmSound::PcmSound(PcmSound && b)
{
  WinAutoLock lock(&sound_cs);

  for (auto && s : playing_sounds)
    if (s.sound == &b)
      if (!s.isEmpty())
        s.setStopMode();

  frequency = b.frequency;
  samples = b.samples;
  channels = b.channels;
  data = b.data;
  b.data = nullptr;
}

PcmSound& PcmSound::operator=(PcmSound && b)
{
  WinAutoLock lock(&sound_cs);

  for (auto && s : playing_sounds)
    if (s.sound == this || s.sound == &b)
      if (!s.isEmpty())
        s.setStopMode();

  frequency = b.frequency;
  samples = b.samples;
  channels = b.channels;
  data = b.data;
  b.data = nullptr;

  return *this;
}


PcmSound::~PcmSound()
{
  if (!data)
    return;

  WinAutoLock lock(&sound_cs);

  for (auto && s : playing_sounds)
    if (s.sound == this)
      if (!s.isEmpty())
        s.setStopMode();

  deleteData();
  data = nullptr;
  samples = 0;
}

} // namespace sound


MAKE_TYPE_FACTORY(PcmSound, sound::PcmSound)


struct SimNode_DeletePcmSound : SimNode_Delete
{
  SimNode_DeletePcmSound( const LineInfo & a, SimNode * s, uint32_t t )
    : SimNode_Delete(a, s, t) {}

  virtual SimNode * visit(SimVisitor & vis) override
  {
    V_BEGIN();
    V_OP(DeletePcmSound);
    V_ARG(total);
    V_SUB(subexpr);
    V_END();
  }

  virtual vec4f eval(Context & context) override
  {
    DAS_PROFILE_NODE
      auto pH = (sound::PcmSound *)subexpr->evalPtr(context);
    for (uint32_t i = 0; i != total; ++i, pH++)
      sound::delete_sound(pH);
    return v_zero();
  }
};


struct PcmSoundAnnotation : ManagedStructureAnnotation<sound::PcmSound, true, true>
{
  PcmSoundAnnotation(ModuleLibrary & ml) : ManagedStructureAnnotation("PcmSound", ml)
  {
    addProperty<DAS_BIND_MANAGED_PROP(getDuration)>("duration");
    addProperty<DAS_BIND_MANAGED_PROP(getFrequency)>("frequency");
    addProperty<DAS_BIND_MANAGED_PROP(getSamples)>("samples");
    addProperty<DAS_BIND_MANAGED_PROP(getChannels)>("channels");
    addProperty<DAS_BIND_MANAGED_PROP(isValid)>("valid");
  }

  bool canCopy() const override { return false; }
  virtual bool hasNonTrivialCtor() const override { return false; }
  virtual bool isLocal() const override { return true; }
  virtual bool canClone() const override { return true; }
  virtual bool canMove() const override { return true; }
  virtual bool canNew() const override { return true; }
  virtual bool canDelete() const override { return true; }
  virtual bool needDelete() const override { return true; }
  virtual bool canBePlacedInContainer() const override { return true; }

  virtual SimNode * simulateDelete(Context & context, const LineInfo & at, SimNode * sube, uint32_t count) const override
  {
    return context.code->makeNode<SimNode_DeletePcmSound>(at, sube, count);
  }
};



template <>
struct das::cast <sound::PlayingSoundHandle>
{
  static __forceinline sound::PlayingSoundHandle to(vec4f x)
  {
    sound::PlayingSoundHandle ps;
    ps.handle = cast<uint32_t>::to(x);
    return ps;
  }

  static __forceinline vec4f from(sound::PlayingSoundHandle x)
  {
    return das::cast<uint32_t>::from(x.handle);
  }
};


MAKE_TYPE_FACTORY(PlayingSoundHandle, sound::PlayingSoundHandle)

struct PlayingSoundHandleAnnotation final: das::ManagedValueAnnotation<sound::PlayingSoundHandle>
{
  PlayingSoundHandleAnnotation() : ManagedValueAnnotation("PlayingSoundHandle")
  {
    cppName = " ::sound::PlayingSoundHandle";
  }

  virtual void walk(das::DataWalker & walker, void * data) override
  {
    walker.UInt(((sound::PlayingSoundHandle *)data)->handle);
  }

  virtual bool canCopy() const override { return true; }
  virtual bool hasNonTrivialCtor() const override { return false; }
  virtual bool isLocal() const override { return true; }
  virtual bool canClone() const override { return true; }
  virtual bool canMove() const override { return true; }
  virtual bool canNew() const override { return true; }
  virtual bool canBePlacedInContainer() const override { return true; }
};


class ModuleSound : public Module
{
public:
  ModuleSound() : Module("sound")
  {
    ModuleLibrary lib;
    lib.addModule(this);
    lib.addBuiltInModule();

    addAnnotation(das::make_smart<PlayingSoundHandleAnnotation>());
    addAnnotation(das::make_smart<PcmSoundAnnotation>(lib));
    addCtorAndUsing<sound::PcmSound>(*this, lib, "PcmSound", "PcmSound");

    addExtern<DAS_BIND_FUN(sound::create_sound), SimNode_ExtFuncCallAndCopyOrMove>(*this, lib,
      "create_sound", SideEffects::modifyExternal, "create_sound")
      ->args({"frequency", "data"});

    addExtern<DAS_BIND_FUN(sound::create_sound_stereo), SimNode_ExtFuncCallAndCopyOrMove>(*this, lib,
      "create_sound", SideEffects::modifyExternal, "create_sound_stereo")
      ->args({"frequency", "data"});

    addExtern<DAS_BIND_FUN(sound::create_sound_from_file), SimNode_ExtFuncCallAndCopyOrMove>(*this, lib,
      "create_sound", SideEffects::modifyExternal, "create_sound_from_file")
      ->args({"file_name"});

    addExtern<DAS_BIND_FUN(sound::get_sound_data)>(*this, lib,
      "get_sound_data", SideEffects::modifyArgumentAndExternal, "get_sound_data")
      ->args({"sound", "out_data"});

    addExtern<DAS_BIND_FUN(sound::get_sound_data_stereo)>(*this, lib,
      "get_sound_data", SideEffects::modifyArgumentAndExternal, "get_sound_data_stereo")
      ->args({"sound", "out_data"});

    addExtern<DAS_BIND_FUN(sound::set_sound_data)>(*this, lib,
      "set_sound_data", SideEffects::modifyExternal, "set_sound_data")
      ->args({"sound", "in_data"});

    addExtern<DAS_BIND_FUN(sound::set_sound_data_stereo)>(*this, lib,
      "set_sound_data", SideEffects::modifyExternal, "set_sound_data_stereo")
      ->args({"sound", "in_data"});

    addExtern<DAS_BIND_FUN(sound::play_sound_1)>(*this, lib,
      "play_sound", SideEffects::modifyExternal, "play_sound_1")
      ->args({"sound"});

    addExtern<DAS_BIND_FUN(sound::play_sound_2)>(*this, lib,
      "play_sound", SideEffects::modifyExternal, "play_sound_2")
      ->args({"sound", "volume"});

    addExtern<DAS_BIND_FUN(sound::play_sound_3)>(*this, lib,
      "play_sound", SideEffects::modifyExternal, "play_sound_3")
      ->args({"sound", "volume", "pitch"});

    addExtern<DAS_BIND_FUN(sound::play_sound_4)>(*this, lib,
      "play_sound", SideEffects::modifyExternal, "play_sound_4")
      ->args({"sound", "volume", "pitch", "pan"});

    addExtern<DAS_BIND_FUN(sound::play_sound_5)>(*this, lib,
      "play_sound", SideEffects::modifyExternal, "play_sound_5")
      ->args({"sound", "volume", "pitch", "pan", "start_time", "stop_time"});

    addExtern<DAS_BIND_FUN(sound::play_sound_loop_1)>(*this, lib,
      "play_sound_loop", SideEffects::modifyExternal, "play_sound_loop_1")
      ->args({"sound"});

    addExtern<DAS_BIND_FUN(sound::play_sound_loop_2)>(*this, lib,
      "play_sound_loop", SideEffects::modifyExternal, "play_sound_loop_2")
      ->args({"sound", "volume"});

    addExtern<DAS_BIND_FUN(sound::play_sound_loop_3)>(*this, lib,
      "play_sound_loop", SideEffects::modifyExternal, "play_sound_loop_3")
      ->args({"sound", "volume", "pitch"});

    addExtern<DAS_BIND_FUN(sound::play_sound_loop_4)>(*this, lib,
      "play_sound_loop", SideEffects::modifyExternal, "play_sound_loop_4")
      ->args({"sound", "volume", "pitch", "pan"});

    addExtern<DAS_BIND_FUN(sound::play_sound_loop_5)>(*this, lib,
      "play_sound_loop", SideEffects::modifyExternal, "play_sound_loop_5")
      ->args({"sound", "volume", "pitch", "pan", "start_time", "end_time"});

    addExtern<DAS_BIND_FUN(sound::play_sound_deferred_1)>(*this, lib,
      "play_sound_deferred", SideEffects::modifyExternal, "play_sound_deferred_1")
      ->args({"sound", "defer_seconds"});

    addExtern<DAS_BIND_FUN(sound::play_sound_deferred_2)>(*this, lib,
      "play_sound_deferred", SideEffects::modifyExternal, "play_sound_deferred_2")
      ->args({"sound", "defer_seconds", "volume"});

    addExtern<DAS_BIND_FUN(sound::play_sound_deferred_3)>(*this, lib,
      "play_sound_deferred", SideEffects::modifyExternal, "play_sound_deferred_3")
      ->args({"sound", "defer_seconds", "volume", "pitch"});

    addExtern<DAS_BIND_FUN(sound::play_sound_deferred_4)>(*this, lib,
      "play_sound_deferred", SideEffects::modifyExternal, "play_sound_deferred_4")
      ->args({"sound", "defer_seconds", "volume", "pitch", "pan"});

    addExtern<DAS_BIND_FUN(sound::play_sound_deferred_5)>(*this, lib,
      "play_sound_deferred", SideEffects::modifyExternal, "play_sound_deferred_5")
      ->args({"sound", "defer_seconds", "volume", "pitch", "pan", "start_time", "stop_time"});


    addExtern<DAS_BIND_FUN(sound::set_sound_pitch)>(*this, lib,
      "set_sound_pitch", SideEffects::modifyExternal, "set_sound_pitch")
      ->args({"sound_handle", "pitch"});

    addExtern<DAS_BIND_FUN(sound::set_sound_volume)>(*this, lib,
      "set_sound_volume", SideEffects::modifyExternal, "set_sound_volume")
      ->args({"sound_handle", "volume"});

    addExtern<DAS_BIND_FUN(sound::set_sound_pan)>(*this, lib,
      "set_sound_pan", SideEffects::modifyExternal, "set_sound_pan")
      ->args({"sound_handle", "pan"});

    addExtern<DAS_BIND_FUN(sound::is_playing)>(*this, lib,
      "is_playing", SideEffects::accessExternal, "is_playing")
      ->args({"sound_handle"});

    addExtern<DAS_BIND_FUN(sound::get_sound_play_pos)>(*this, lib,
      "get_sound_play_pos", SideEffects::accessExternal, "get_sound_play_pos")
      ->args({"sound_handle"});

    addExtern<DAS_BIND_FUN(sound::set_sound_play_pos)>(*this, lib,
      "set_sound_play_pos", SideEffects::modifyExternal, "set_sound_play_pos")
      ->args({"sound_handle", "pos_seconds"});

    addExtern<DAS_BIND_FUN(sound::stop_sound)>(*this, lib,
      "stop_sound", SideEffects::modifyExternal, "stop_sound")
      ->args({"sound_handle"});


    addExtern<DAS_BIND_FUN(sound::stop_all_sounds)>(*this, lib,
      "stop_all_sounds", SideEffects::modifyExternal, "stop_all_sounds");

    addExtern<DAS_BIND_FUN(sound::enter_sound_critical_section)>(*this, lib,
      "enter_sound_critical_section", SideEffects::modifyExternal, "enter_sound_critical_section");

    addExtern<DAS_BIND_FUN(sound::leave_sound_critical_section)>(*this, lib,
      "leave_sound_critical_section", SideEffects::modifyExternal, "leave_sound_critical_section");

    addExtern<DAS_BIND_FUN(sound::set_master_volume)>(*this, lib,
      "set_master_volume", SideEffects::modifyExternal, "set_master_volume")
      ->args({"volume"});

    addExtern<DAS_BIND_FUN(sound::get_output_sample_rate)>(*this, lib,
      "get_output_sample_rate", SideEffects::accessExternal, "get_output_sample_rate");

    addExtern<DAS_BIND_FUN(sound::get_total_samples_played)>(*this, lib,
      "get_total_samples_played", SideEffects::accessExternal, "get_total_samples_played");

    addExtern<DAS_BIND_FUN(sound::get_total_time_played)>(*this, lib,
      "get_total_time_played", SideEffects::accessExternal, "get_total_time_played");

    // its AOT ready
    //verifyAotReady();
  }

  virtual ModuleAotType aotRequire(TextWriter & tw) const override
  {
    //tw << "#include \"test_profile.h\"\n";
    return ModuleAotType::cpp;
  }
};

REGISTER_MODULE(ModuleSound);

