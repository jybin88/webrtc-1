/*
 *  Copyright (c) 2013 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "sdk/android/src/jni/audio_device/audio_track_jni.h"

#include <utility>

#include "rtc_base/arraysize.h"
#include "rtc_base/checks.h"
#include "rtc_base/format_macros.h"
#include "rtc_base/logging.h"
#include "rtc_base/platform_thread.h"
#include "sdk/android/generated_java_audio_device_jni/jni/WebRtcAudioTrack_jni.h"
#include "sdk/android/src/jni/jni_helpers.h"

namespace webrtc {

namespace android_adm {

ScopedJavaLocalRef<jobject> AudioTrackJni::CreateJavaWebRtcAudioTrack(
    JNIEnv* env,
    const JavaRef<jobject>& j_context,
    const JavaRef<jobject>& j_audio_manager) {
  return Java_WebRtcAudioTrack_Constructor(env, j_context, j_audio_manager);
}

AudioTrackJni::AudioTrackJni(JNIEnv* env,
                             const AudioParameters& audio_parameters,
                             const JavaRef<jobject>& j_webrtc_audio_track)
    : j_audio_track_(env, j_webrtc_audio_track),
      audio_parameters_(audio_parameters),
      direct_buffer_address_(nullptr),
      direct_buffer_capacity_in_bytes_(0),
      frames_per_buffer_(0),
      initialized_(false),
      playing_(false),
      audio_device_buffer_(nullptr) {
  RTC_LOG(INFO) << "ctor";
  RTC_DCHECK(audio_parameters_.is_valid());
  Java_WebRtcAudioTrack_setNativeAudioTrack(env, j_audio_track_,
                                            jni::jlongFromPointer(this));
  // Detach from this thread since construction is allowed to happen on a
  // different thread.
  thread_checker_.DetachFromThread();
  thread_checker_java_.DetachFromThread();
}

AudioTrackJni::~AudioTrackJni() {
  RTC_LOG(INFO) << "dtor";
  RTC_DCHECK(thread_checker_.CalledOnValidThread());
  Terminate();
}

int32_t AudioTrackJni::Init() {
  RTC_LOG(INFO) << "Init";
  env_ = AttachCurrentThreadIfNeeded();
  RTC_DCHECK(thread_checker_.CalledOnValidThread());
  return 0;
}

int32_t AudioTrackJni::Terminate() {
  RTC_LOG(INFO) << "Terminate";
  RTC_DCHECK(thread_checker_.CalledOnValidThread());
  StopPlayout();
  return 0;
}

int32_t AudioTrackJni::InitPlayout() {
  RTC_LOG(INFO) << "InitPlayout";
  RTC_DCHECK(thread_checker_.CalledOnValidThread());
  RTC_DCHECK(!initialized_);
  RTC_DCHECK(!playing_);
  if (!Java_WebRtcAudioTrack_initPlayout(env_, j_audio_track_,
                                         audio_parameters_.sample_rate(),
                                         audio_parameters_.channels())) {
    RTC_LOG(LS_ERROR) << "InitPlayout failed";
    return -1;
  }
  initialized_ = true;
  return 0;
}

bool AudioTrackJni::PlayoutIsInitialized() const {
  return initialized_;
}

int32_t AudioTrackJni::StartPlayout() {
  RTC_LOG(INFO) << "StartPlayout";
  RTC_DCHECK(thread_checker_.CalledOnValidThread());
  RTC_DCHECK(!playing_);
  if (!initialized_) {
    RTC_DLOG(LS_WARNING)
        << "Playout can not start since InitPlayout must succeed first";
    return 0;
  }
  if (!Java_WebRtcAudioTrack_startPlayout(env_, j_audio_track_)) {
    RTC_LOG(LS_ERROR) << "StartPlayout failed";
    return -1;
  }
  playing_ = true;
  return 0;
}

int32_t AudioTrackJni::StopPlayout() {
  RTC_LOG(INFO) << "StopPlayout";
  RTC_DCHECK(thread_checker_.CalledOnValidThread());
  if (!initialized_ || !playing_) {
    return 0;
  }
  if (!Java_WebRtcAudioTrack_stopPlayout(env_, j_audio_track_)) {
    RTC_LOG(LS_ERROR) << "StopPlayout failed";
    return -1;
  }
  // If we don't detach here, we will hit a RTC_DCHECK in OnDataIsRecorded()
  // next time StartRecording() is called since it will create a new Java
  // thread.
  thread_checker_java_.DetachFromThread();
  initialized_ = false;
  playing_ = false;
  direct_buffer_address_ = nullptr;
  return 0;
}

bool AudioTrackJni::Playing() const {
  return playing_;
}

bool AudioTrackJni::SpeakerVolumeIsAvailable() {
  return true;
}

int AudioTrackJni::SetSpeakerVolume(uint32_t volume) {
  RTC_LOG(INFO) << "SetSpeakerVolume(" << volume << ")";
  RTC_DCHECK(thread_checker_.CalledOnValidThread());
  return Java_WebRtcAudioTrack_setStreamVolume(env_, j_audio_track_, volume)
             ? 0
             : -1;
}

rtc::Optional<uint32_t> AudioTrackJni::MaxSpeakerVolume() const {
  RTC_DCHECK(thread_checker_.CalledOnValidThread());
  return Java_WebRtcAudioTrack_getStreamMaxVolume(env_, j_audio_track_);
}

rtc::Optional<uint32_t> AudioTrackJni::MinSpeakerVolume() const {
  RTC_DCHECK(thread_checker_.CalledOnValidThread());
  return 0;
}

rtc::Optional<uint32_t> AudioTrackJni::SpeakerVolume() const {
  RTC_DCHECK(thread_checker_.CalledOnValidThread());
  const uint32_t volume =
      Java_WebRtcAudioTrack_getStreamVolume(env_, j_audio_track_);
  RTC_LOG(INFO) << "SpeakerVolume: " << volume;
  return volume;
}

// TODO(henrika): possibly add stereo support.
void AudioTrackJni::AttachAudioBuffer(AudioDeviceBuffer* audioBuffer) {
  RTC_LOG(INFO) << "AttachAudioBuffer";
  RTC_DCHECK(thread_checker_.CalledOnValidThread());
  audio_device_buffer_ = audioBuffer;
  const int sample_rate_hz = audio_parameters_.sample_rate();
  RTC_LOG(INFO) << "SetPlayoutSampleRate(" << sample_rate_hz << ")";
  audio_device_buffer_->SetPlayoutSampleRate(sample_rate_hz);
  const size_t channels = audio_parameters_.channels();
  RTC_LOG(INFO) << "SetPlayoutChannels(" << channels << ")";
  audio_device_buffer_->SetPlayoutChannels(channels);
}

void AudioTrackJni::CacheDirectBufferAddress(
    JNIEnv* env,
    const JavaParamRef<jobject>&,
    const JavaParamRef<jobject>& byte_buffer) {
  RTC_LOG(INFO) << "OnCacheDirectBufferAddress";
  RTC_DCHECK(thread_checker_.CalledOnValidThread());
  RTC_DCHECK(!direct_buffer_address_);
  direct_buffer_address_ = env->GetDirectBufferAddress(byte_buffer.obj());
  jlong capacity = env->GetDirectBufferCapacity(byte_buffer.obj());
  RTC_LOG(INFO) << "direct buffer capacity: " << capacity;
  direct_buffer_capacity_in_bytes_ = static_cast<size_t>(capacity);
  const size_t bytes_per_frame = audio_parameters_.channels() * sizeof(int16_t);
  frames_per_buffer_ = direct_buffer_capacity_in_bytes_ / bytes_per_frame;
  RTC_LOG(INFO) << "frames_per_buffer: " << frames_per_buffer_;
}

// This method is called on a high-priority thread from Java. The name of
// the thread is 'AudioRecordTrack'.
void AudioTrackJni::GetPlayoutData(JNIEnv* env,
                                   const JavaParamRef<jobject>&,
                                   size_t length) {
  RTC_DCHECK(thread_checker_java_.CalledOnValidThread());
  const size_t bytes_per_frame = audio_parameters_.channels() * sizeof(int16_t);
  RTC_DCHECK_EQ(frames_per_buffer_, length / bytes_per_frame);
  if (!audio_device_buffer_) {
    RTC_LOG(LS_ERROR) << "AttachAudioBuffer has not been called";
    return;
  }
  // Pull decoded data (in 16-bit PCM format) from jitter buffer.
  int samples = audio_device_buffer_->RequestPlayoutData(frames_per_buffer_);
  if (samples <= 0) {
    RTC_LOG(LS_ERROR) << "AudioDeviceBuffer::RequestPlayoutData failed";
    return;
  }
  RTC_DCHECK_EQ(samples, frames_per_buffer_);
  // Copy decoded data into common byte buffer to ensure that it can be
  // written to the Java based audio track.
  samples = audio_device_buffer_->GetPlayoutData(direct_buffer_address_);
  RTC_DCHECK_EQ(length, bytes_per_frame * samples);
}

}  // namespace android_adm

}  // namespace webrtc
