/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#if !defined(VorbisDecoder_h_)
#define VorbisDecoder_h_

#include <chrono>
#include <atomic>
using namespace std::chrono;

#include "AudioConverter.h"
#include "PlatformDecoderModule.h"
#include "mozilla/Maybe.h"

#ifdef MOZ_TREMOR
#include "tremor/ivorbiscodec.h"
#else
#include "vorbis/codec.h"
#endif

#if !defined(USE_SANDBOXING_BUFFERS)
  #error "No build option defined. File VorbisDecoder.h is being included from an unexpected location"
#endif

#if defined(NACL_SANDBOX_USE_NEW_CPP_API)
  #include "RLBox_NaCl.h"
  using TRLSandbox = RLBox_NaCl;
#elif defined(WASM_SANDBOX_USE_CPP_API)
  #include "RLBox_Wasm.h"
  using TRLSandbox = RLBox_Wasm;
#elif defined(PS_SANDBOX_USE_NEW_CPP_API)
  #define USE_LIBVORBIS
  #include "ProcessSandbox.h"
  #include "RLBox_Process.h"
  using TRLSandbox = RLBox_Process<VorbisProcessSandbox>;
  #undef USE_LIBVORBIS
#endif

#if defined(NACL_SANDBOX_USE_NEW_CPP_API) || defined(WASM_SANDBOX_USE_NEW_CPP_API) || defined(PS_SANDBOX_USE_NEW_CPP_API)
  #include "rlbox.h"
  using namespace rlbox;
  #include "vorbislib_structs_for_cpp_api_new.h"
#endif


namespace mozilla {

class VorbisDataDecoder : public MediaDataDecoder
{
public:
  explicit VorbisDataDecoder(const CreateDecoderParams& aParams);
  ~VorbisDataDecoder();

  RefPtr<InitPromise> Init() override;
  RefPtr<DecodePromise> Decode(MediaRawData* aSample) override;
  RefPtr<DecodePromise> Drain() override;
  RefPtr<FlushPromise> Flush() override;
  RefPtr<ShutdownPromise> Shutdown() override;
  nsCString GetDescriptionName() const override
  {
    return NS_LITERAL_CSTRING("vorbis audio decoder");
  }

  // Return true if mimetype is Vorbis
  static bool IsVorbis(const nsACString& aMimeType);
  static const AudioConfig::Channel* VorbisLayout(uint32_t aChannels);

private:

  std::atomic_ullong vorbisDecodeInvocations{0};
  std::atomic_ullong timeBetweenVorbisDecode{0};
  std::atomic_ullong timeSpentInVorbisDecode{0};
  std::atomic_ullong previousVorbisDecodeCall;
  std::atomic_ullong bitsProcessedByVorbis{0};

  nsresult DecodeHeader(const unsigned char* aData, size_t aLength);
  RefPtr<DecodePromise> ProcessDecode(MediaRawData* aSample);

  const AudioInfo& mInfo;
  const RefPtr<TaskQueue> mTaskQueue;

  // Vorbis decoder state
  #if defined(NACL_SANDBOX_USE_NEW_CPP_API) || defined(WASM_SANDBOX_USE_NEW_CPP_API) || defined(PS_SANDBOX_USE_NEW_CPP_API)
  RLBoxSandbox<TRLSandbox>* rlbox_vorbis;
  tainted<vorbis_info*, TRLSandbox> p_mVorbisInfo;
  tainted<vorbis_comment*, TRLSandbox> p_mVorbisComment;
  tainted<vorbis_dsp_state*, TRLSandbox> p_mVorbisDsp;
  tainted<vorbis_block*, TRLSandbox> p_mVorbisBlock;
  #else
  vorbis_info mVorbisInfo;
  vorbis_comment mVorbisComment;
  vorbis_dsp_state mVorbisDsp;
  vorbis_block mVorbisBlock;
  #endif

  int64_t mPacketCount;
  int64_t mFrames;
  Maybe<int64_t> mLastFrameTime;
  UniquePtr<AudioConverter> mAudioConverter;
};

} // namespace mozilla
#endif
