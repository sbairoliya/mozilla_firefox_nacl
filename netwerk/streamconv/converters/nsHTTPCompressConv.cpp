/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=8 et tw=80 : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsHTTPCompressConv.h"
#include "nsMemory.h"
#include "plstr.h"
#include "nsCOMPtr.h"
#include "nsError.h"
#include "nsStreamUtils.h"
#include "nsStringStream.h"
#include "nsComponentManagerUtils.h"
#include "nsThreadUtils.h"
#include "mozilla/Preferences.h"
#include "mozilla/Logging.h"
#include "nsIForcePendingChannel.h"
#include "nsIRequest.h"

// brotli headers
#include "state.h"
#include "brotli/decode.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>

#if defined(NACL_SANDBOX_USE_NEW_CPP_API) || defined(WASM_SANDBOX_USE_NEW_CPP_API) || defined(PS_SANDBOX_USE_NEW_CPP_API)
// RLBoxSandbox<TRLSandbox>* rlbox_zlib = NULL;
#elif SANDBOX_CPP == 1
void ensureNaClSandboxInit();
NaClSandbox* sbox = NULL;
#elif SANDBOX_CPP == 2
static ZProcessSandbox* sbox = NULL;
#endif

#if defined(NACL_SANDBOX_USE_NEW_CPP_API) || defined(WASM_SANDBOX_USE_NEW_CPP_API) || defined(PS_SANDBOX_USE_NEW_CPP_API)
#include <mutex>

rlbox_load_library_api(zlib, TRLSandbox)

static std::mutex mtx;
#elif defined(SANDBOX_CPP)
#include <mutex>

sandbox_nacl_load_library_api(zlib)

static std::mutex mtx;
#endif


#if defined(NACL_SANDBOX_USE_NEW_CPP_API) || defined(WASM_SANDBOX_USE_NEW_CPP_API) || defined(PS_SANDBOX_USE_NEW_CPP_API)

extern "C" {
void getSandboxingFolder(char* SandboxingCodeRootFolder);
}

namespace mozilla {
namespace net {

class ZLIBSandboxResource {
public:
  RLBoxSandbox<TRLSandbox>* rlbox_zlib;

  ZLIBSandboxResource() {
  //   rlbox_zlib = nullptr;
  // }

  // RLBoxSandbox<TRLSandbox>* initialize() {
  //   if (rlbox_zlib) { return rlbox_zlib; }
    char SandboxingCodeRootFolder[1024];
    getSandboxingFolder(SandboxingCodeRootFolder);

    char full_STARTUP_LIBRARY_PATH[1024];
    char full_SANDBOX_INIT_APP[1024];

    strcpy(full_STARTUP_LIBRARY_PATH, SandboxingCodeRootFolder);
    strcat(full_STARTUP_LIBRARY_PATH, STARTUP_LIBRARY_PATH);

    strcpy(full_SANDBOX_INIT_APP, SandboxingCodeRootFolder);
    strcat(full_SANDBOX_INIT_APP, SANDBOX_INIT_APP);

    printf("Creating "
    #if defined(NACL_SANDBOX_USE_NEW_CPP_API)
    "NaCl"
    #elif defined(PS_SANDBOX_USE_NEW_CPP_API)
    "Process"
    #else
    "Wasm"
    #endif
    " Sandbox %s, %s\n", full_STARTUP_LIBRARY_PATH, full_SANDBOX_INIT_APP);

    rlbox_zlib = RLBoxSandbox<TRLSandbox>::createSandbox(full_STARTUP_LIBRARY_PATH, full_SANDBOX_INIT_APP);
    // return rlbox_zlib;
  }

  ~ZLIBSandboxResource() {
    if (rlbox_zlib) {
      rlbox_zlib->destroySandbox();
      free(rlbox_zlib);
      printf("Destroying ZLIB sandbox\n");
    }
  }
};

template<typename T>
class SandboxManager
{
private:
    std::map<std::string, std::shared_ptr<T>> sandboxes;
    std::mutex sandboxMapMutex;
    static const bool SandboxEnforceLimits = true;
    //we can go to higher limits, but this seems fine
    // #if defined(PS_SANDBOX_DONT_USE_SPIN)
    // static const int SandboxSoftLimit = 100;
    // #else
    static const int SandboxSoftLimit = 50;
    // #endif

public:

    // inline void checkSandboxCreation(std::shared_ptr<T> ret) {
    //   auto succeeded = ret->initialize();
    //   if (succeeded) { return; }

    //   printf("ZLIB Sandbox creation failed. Cleaning some ZLIB sandboxes.\n");
    //   auto endIter = sandboxes.end();
    //   for(auto iter = sandboxes.begin(); iter != endIter && !succeeded; ) {
    //     //check if anyone else has a ref i.e. someone is using the sandbox 
    //     if (iter->second.use_count() == 1) {
    //       iter = sandboxes.erase(iter);
    //       succeeded = ret->initialize();
    //     } else {
    //       ++iter;
    //     }
    //   }

    //   if (!succeeded) {
    //     printf("All attempts to create a zlib sandbox failed.\n");
    //     abort();
    //   }
    // }

    inline std::shared_ptr<T> createSandbox(std::string name) {
      //use a fresh temporary sandbox if we couldn't find the origin
      if(name == "") {
        // printf("!!!!!!!!Making empty ZLIB sandbox.\n");
        auto ret = std::make_shared<T>();
        // checkSandboxCreation(ret);
        return ret;
      }

      std::lock_guard<std::mutex> lock(sandboxMapMutex);
      auto iter = sandboxes.find(name) ;
      if (iter != sandboxes.end()) {
        // Found existing Sandbox
        return iter->second;
      }

      if (SandboxEnforceLimits) {
        //just throw away some of the older sandboxes that are not currently in use
        //these will be recreated if needed
        //It could be that more sandboxes in use > SandboxSoftLimit
        //in which case, the total count will temporarily be above the SandboxSoftLimit

        auto endIter = sandboxes.end();
        for(auto iter = sandboxes.begin(); iter != endIter && sandboxes.size() > SandboxSoftLimit; ) {
          //check if anyone else has a ref i.e. someone is using the sandbox 
          if (iter->second.use_count() == 1) {
            iter = sandboxes.erase(iter);
          } else {
            ++iter;
          }
        }
      }

      // Making Sandbox
      // printf("!!!!!!!!Making ZLIB sandbox: %s.\n", name.c_str());
      auto ret = std::make_shared<T>();
      // checkSandboxCreation(ret);
      sandboxes[name] = ret;
      return ret;
    }

    inline void printCounts() {
      std::lock_guard<std::mutex> lock(sandboxMapMutex);
      for (auto it = sandboxes.begin(); it != sandboxes.end(); ++it) {
        printf("Sandbox: %s Count: %ld\n", it->first.c_str(), it->second.use_count());
      }
    }

    inline void destroySandbox(std::string name) {
      std::lock_guard<std::mutex> lock(sandboxMapMutex);
      auto iter = sandboxes.find(name) ;
      if (iter != sandboxes.end()) {
        sandboxes.erase(iter);
      }
    }

    inline void destroyAll() {
      std::lock_guard<std::mutex> lock(sandboxMapMutex);
      sandboxes.clear();
    }
};

static SandboxManager<ZLIBSandboxResource> zlibSandboxManager;

}
}
#endif



void SandboxOnFirefoxExitingZLIB()
{
  #if defined(NACL_SANDBOX_USE_NEW_CPP_API) || defined(WASM_SANDBOX_USE_NEW_CPP_API) || defined(PS_SANDBOX_USE_NEW_CPP_API)
    mozilla::net::zlibSandboxManager.destroyAll();
    // if(rlbox_zlib != nullptr)
    // {
    //   rlbox_zlib->destroySandbox();
    //   free(rlbox_zlib);
    //   rlbox_zlib = nullptr;
    // }
  #endif
  #if SANDBOX_CPP == 2
  {
    std::lock_guard<std::mutex> guard(mtx);
    sbox->destroySandbox();
    sbox = NULL;
  }
  #endif

  #if (SANDBOX_CPP == 2) || defined(PS_SANDBOX_USE_NEW_CPP_API)
  //killProcessSandboxOthersides
  {
    DIR *dir = opendir("/proc/");
    struct dirent *entry = readdir(dir);

    printf("Force killing all otherside processes\n");
    while (entry != NULL)
    {
      if (entry->d_type == DT_DIR)
      {
        long int pid = strtol(entry->d_name,NULL,0);
        if(pid != 0)
        {
          char buffer[4096];
          char filename[512];
          snprintf(filename, 512, "/proc/%s/cmdline", entry->d_name);

          FILE *fp = fopen(filename, "r");                 // do not use "rb"
          if (fgets(buffer, sizeof(buffer), fp)) {
            if(strstr(buffer, "andbox_otherside") != NULL) {
              kill(pid, SIGKILL);
            }
          }
          fclose(fp);
        }
      }

      entry = readdir(dir);
    }
    closedir(dir);
  }
  #endif
}

#if defined(SANDBOX_CPP)

static void constructZlibSandboxIfNecessary() {
  mtx.lock();
    if(!sbox) {
      char SandboxingCodeRootFolder[1024];
      int index;

      if(!getcwd(SandboxingCodeRootFolder, 256)) abort();

      char * found = strstr(SandboxingCodeRootFolder, "/mozilla-release");
      if (found == NULL)
      {
        printf("Error initializing SandboxingCodeRootFolder\n");
        exit(1);
      }

      index = found - SandboxingCodeRootFolder + 1;
      SandboxingCodeRootFolder[index] = '\0';

#if SANDBOX_CPP == 1
      char full_STARTUP_LIBRARY_PATH[1024];
      char full_SANDBOX_INIT_APP[1024];

      strcpy(full_STARTUP_LIBRARY_PATH, SandboxingCodeRootFolder);
      strcat(full_STARTUP_LIBRARY_PATH, STARTUP_LIBRARY_PATH);

      strcpy(full_SANDBOX_INIT_APP, SandboxingCodeRootFolder);
      strcat(full_SANDBOX_INIT_APP, SANDBOX_INIT_APP);

      printf("Creating NaCl Sandbox %s, %s\n", full_STARTUP_LIBRARY_PATH, full_SANDBOX_INIT_APP);
      ensureNaClSandboxInit();
      sbox = createDlSandbox(full_STARTUP_LIBRARY_PATH, full_SANDBOX_INIT_APP);
      initCPPApi(sbox);
#elif SANDBOX_CPP == 2
      char full_PS_OTHERSIDE_PATH[1024];

      strcpy(full_PS_OTHERSIDE_PATH, SandboxingCodeRootFolder);
      strcat(full_PS_OTHERSIDE_PATH, PS_OTHERSIDE_PATH);

      printf("Creating zlib process sandbox %s\n", full_PS_OTHERSIDE_PATH);
      sbox = createDlSandbox<ZProcessSandbox>(full_PS_OTHERSIDE_PATH);
      initCPPApi(sbox);
#endif
    }
  mtx.unlock();
}
#endif

namespace mozilla {
namespace net {

extern LazyLogModule gHttpLog;
#define LOG(args) MOZ_LOG(mozilla::net::gHttpLog, mozilla::LogLevel::Debug, args)

// nsISupports implementation
NS_IMPL_ISUPPORTS(nsHTTPCompressConv,
                  nsIStreamConverter,
                  nsIStreamListener,
                  nsIRequestObserver,
                  nsICompressConvStats,
                  nsIThreadRetargetableStreamListener)

// nsFTPDirListingConv methods
nsHTTPCompressConv::nsHTTPCompressConv()
  : mMode(HTTP_COMPRESS_IDENTITY)
  , mInpBuffer(nullptr)
  #ifdef USE_COPYING_BUFFERS
  , sbOutBuffer(nullptr)
  #endif
  , mOutBuffer(nullptr)
  , mOutBufferLen(0)
  , mInpBufferLen(0)
  , mCheckHeaderDone(false)
  , mStreamEnded(false)
  , mStreamInitialized(false)
  , mLen(0)
  , hMode(0)
  , mSkipCount(0)
  , mFlags(0)
  , mDecodedDataLength(0)
  , mMutex("nsHTTPCompressConv")
{
#if defined(NACL_SANDBOX_USE_NEW_CPP_API) || defined(WASM_SANDBOX_USE_NEW_CPP_API) || defined(PS_SANDBOX_USE_NEW_CPP_API)
  // constructZlibSandboxIfNecessary();
  // p_d_stream = rlbox_zlib->mallocInSandbox<z_stream>();
  // if(p_d_stream == nullptr) {
  //   printf("Error in malloc for p_d_stream\n");
  //   exit(1);
  // }
#elif defined(SANDBOX_CPP)
  constructZlibSandboxIfNecessary();
  p_d_stream = newInSandbox<z_stream>(sbox);
  if(p_d_stream == nullptr) {
    printf("Error in malloc for p_d_stream\n");
    exit(1);
  }
#endif
  LOG(("nsHttpCompressConv %p ctor\n", this));
  if (NS_IsMainThread()) {
    mFailUncleanStops =
      Preferences::GetBool("network.http.enforce-framing.http", false);
  } else {
    mFailUncleanStops = false;
  }
}

nsHTTPCompressConv::~nsHTTPCompressConv()
{
#if defined(NACL_SANDBOX_USE_NEW_CPP_API) || defined(WASM_SANDBOX_USE_NEW_CPP_API) || defined(PS_SANDBOX_USE_NEW_CPP_API)
    // sandbox may not have been initialized, in which case, no cleanup
    if (!rlbox_sbx) { return; }
    auto rlbox_zlib = rlbox_sbx->rlbox_zlib;
#endif
  LOG(("nsHttpCompresssConv %p dtor\n", this));
  if (mInpBuffer != nullptr) {
#if defined(NACL_SANDBOX_USE_NEW_CPP_API) || defined(WASM_SANDBOX_USE_NEW_CPP_API) || defined(PS_SANDBOX_USE_NEW_CPP_API)
    rlbox_zlib->freeInSandbox(mInpBuffer);
#elif defined(SANDBOX_CPP)
    freeInSandbox(sbox, mInpBuffer);
#else
    free(mInpBuffer);
#endif
  }

#ifdef USE_COPYING_BUFFERS
  if (sbOutBuffer != nullptr) {
    #if defined(NACL_SANDBOX_USE_NEW_CPP_API) || defined(WASM_SANDBOX_USE_NEW_CPP_API) || defined(PS_SANDBOX_USE_NEW_CPP_API)
      rlbox_zlib->freeInSandbox(sbOutBuffer);
    #elif defined(SANDBOX_CPP)
      freeInSandbox(sbox, sbOutBuffer);
    #else
      free(sbOutBuffer);
    #endif
  }
#endif

  if(mOutBuffer) {
    free(mOutBuffer);
  }

  // For some reason we are not getting Z_STREAM_END.  But this was also seen
  //    for mozilla bug 198133.  Need to handle this case.
  if (mStreamInitialized && !mStreamEnded) {
#if defined(NACL_SANDBOX_USE_NEW_CPP_API) || defined(WASM_SANDBOX_USE_NEW_CPP_API) || defined(PS_SANDBOX_USE_NEW_CPP_API)
    sandbox_invoke(rlbox_zlib, inflateEnd, p_d_stream);
#elif defined(SANDBOX_CPP)
    sandbox_invoke(sbox, inflateEnd, p_d_stream);
#else
    inflateEnd(&d_stream);
#endif
  }

#if defined(NACL_SANDBOX_USE_NEW_CPP_API) || defined(WASM_SANDBOX_USE_NEW_CPP_API) || defined(PS_SANDBOX_USE_NEW_CPP_API)
  rlbox_zlib->freeInSandbox(p_d_stream);
#elif defined(SANDBOX_CPP)
  freeInSandbox(sbox, p_d_stream);
#endif

  #if defined(NACL_SANDBOX_USE_NEW_CPP_API) || defined(WASM_SANDBOX_USE_NEW_CPP_API) || defined(PS_SANDBOX_USE_NEW_CPP_API)
    // if(rlbox_zlib != nullptr)
    // {
    //   rlbox_zlib->destroySandbox();
    //   free(rlbox_zlib);
    //   rlbox_zlib = nullptr;
    //   printf("Destroying ZLIB Sandbox\n");
    // }
  #endif
}

NS_IMETHODIMP
nsHTTPCompressConv::GetDecodedDataLength(uint64_t *aDecodedDataLength)
{
    *aDecodedDataLength = mDecodedDataLength;
    return NS_OK;
}

NS_IMETHODIMP
nsHTTPCompressConv::AsyncConvertData(const char *aFromType,
                                     const char *aToType,
                                     nsIStreamListener *aListener,
                                     nsISupports *aCtxt)
{
  if (!PL_strncasecmp(aFromType, HTTP_COMPRESS_TYPE, sizeof(HTTP_COMPRESS_TYPE)-1) ||
      !PL_strncasecmp(aFromType, HTTP_X_COMPRESS_TYPE, sizeof(HTTP_X_COMPRESS_TYPE)-1)) {
    mMode = HTTP_COMPRESS_COMPRESS;
  } else if (!PL_strncasecmp(aFromType, HTTP_GZIP_TYPE, sizeof(HTTP_GZIP_TYPE)-1) ||
             !PL_strncasecmp(aFromType, HTTP_X_GZIP_TYPE, sizeof(HTTP_X_GZIP_TYPE)-1)) {
    mMode = HTTP_COMPRESS_GZIP;
  } else if (!PL_strncasecmp(aFromType, HTTP_DEFLATE_TYPE, sizeof(HTTP_DEFLATE_TYPE)-1)) {
    mMode = HTTP_COMPRESS_DEFLATE;
  } else if (!PL_strncasecmp(aFromType, HTTP_BROTLI_TYPE, sizeof(HTTP_BROTLI_TYPE)-1)) {
    mMode = HTTP_COMPRESS_BROTLI;
  }
  LOG(("nsHttpCompresssConv %p AsyncConvertData %s %s mode %d\n",
       this, aFromType, aToType, (CompressMode)mMode));

  MutexAutoLock lock(mMutex);
  // hook ourself up with the receiving listener.
  mListener = aListener;

  return NS_OK;
}

NS_IMETHODIMP
nsHTTPCompressConv::OnStartRequest(nsIRequest* request, nsISupports *aContext)
{
  LOG(("nsHttpCompresssConv %p onstart\n", this));
  nsCOMPtr<nsIStreamListener> listener;
  {
    MutexAutoLock lock(mMutex);
    listener = mListener;
  }
  return listener->OnStartRequest(request, aContext);
}

NS_IMETHODIMP
nsHTTPCompressConv::OnStopRequest(nsIRequest* request, nsISupports *aContext,
                                  nsresult aStatus)
{
  nsresult status = aStatus;
  LOG(("nsHttpCompresssConv %p onstop %" PRIx32 "\n", this, static_cast<uint32_t>(aStatus)));

  // Framing integrity is enforced for content-encoding: gzip, but not for
  // content-encoding: deflate. Note that gzip vs deflate is NOT determined
  // by content sniffing but only via header.
  if (!mStreamEnded && NS_SUCCEEDED(status) &&
      (mFailUncleanStops && (mMode == HTTP_COMPRESS_GZIP)) ) {
    // This is not a clean end of gzip stream: the transfer is incomplete.
    status = NS_ERROR_NET_PARTIAL_TRANSFER;
    LOG(("nsHttpCompresssConv %p onstop partial gzip\n", this));
  }
  if (NS_SUCCEEDED(status) && mMode == HTTP_COMPRESS_BROTLI) {
    nsCOMPtr<nsIForcePendingChannel> fpChannel = do_QueryInterface(request);
    bool isPending = false;
    if (request) {
      request->IsPending(&isPending);
    }
    if (fpChannel && !isPending) {
      fpChannel->ForcePending(true);
    }
    if (mBrotli && (mBrotli->mTotalOut == 0) && !mBrotli->mBrotliStateIsStreamEnd) {
      status = NS_ERROR_INVALID_CONTENT_ENCODING;
    }
    LOG(("nsHttpCompresssConv %p onstop brotlihandler rv %" PRIx32 "\n",
         this, static_cast<uint32_t>(status)));
    if (fpChannel && !isPending) {
      fpChannel->ForcePending(false);
    }
  }

  nsCOMPtr<nsIStreamListener> listener;
  {
    MutexAutoLock lock(mMutex);
    listener = mListener;
  }
  return listener->OnStopRequest(request, aContext, status);
}


/* static */ nsresult
nsHTTPCompressConv::BrotliHandler(nsIInputStream *stream, void *closure, const char *dataIn,
                                  uint32_t, uint32_t aAvail, uint32_t *countRead)
{
  MOZ_ASSERT(stream);
  nsHTTPCompressConv *self = static_cast<nsHTTPCompressConv *>(closure);
  *countRead = 0;

  const size_t kOutSize = 128 * 1024; // just a chunk size, we call in a loop
  uint8_t *outPtr;
  size_t outSize;
  size_t avail = aAvail;
  BrotliDecoderResult res;

  if (!self->mBrotli) {
    *countRead = aAvail;
    return NS_OK;
  }

  auto outBuffer = MakeUniqueFallible<uint8_t[]>(kOutSize);
  if (outBuffer == nullptr) {
    self->mBrotli->mStatus = NS_ERROR_OUT_OF_MEMORY;
    return self->mBrotli->mStatus;
  }

  do {
    outSize = kOutSize;
    outPtr = outBuffer.get();

    // brotli api is documented in brotli/dec/decode.h and brotli/dec/decode.c
    LOG(("nsHttpCompresssConv %p brotlihandler decompress %zu\n", self, avail));
    size_t totalOut = self->mBrotli->mTotalOut;
    res = ::BrotliDecoderDecompressStream(
      &self->mBrotli->mState,
      &avail, reinterpret_cast<const unsigned char **>(&dataIn),
      &outSize, &outPtr, &totalOut);
    outSize = kOutSize - outSize;
    self->mBrotli->mTotalOut = totalOut;
    self->mBrotli->mBrotliStateIsStreamEnd = BrotliDecoderIsFinished(&self->mBrotli->mState);
    LOG(("nsHttpCompresssConv %p brotlihandler decompress rv=%" PRIx32 " out=%zu\n",
         self, static_cast<uint32_t>(res), outSize));

    if (res == BROTLI_DECODER_RESULT_ERROR) {
      LOG(("nsHttpCompressConv %p marking invalid encoding", self));
      self->mBrotli->mStatus = NS_ERROR_INVALID_CONTENT_ENCODING;
      return self->mBrotli->mStatus;
    }

    // in 'the current implementation' brotli must consume everything before
    // asking for more input
    if (res == BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT) {
      MOZ_ASSERT(!avail);
      if (avail) {
        LOG(("nsHttpCompressConv %p did not consume all input", self));
        self->mBrotli->mStatus = NS_ERROR_UNEXPECTED;
        return self->mBrotli->mStatus;
      }
    }
    if (outSize > 0) {
      nsresult rv = self->do_OnDataAvailable(self->mBrotli->mRequest,
                                             self->mBrotli->mContext,
                                             self->mBrotli->mSourceOffset,
                                             reinterpret_cast<const char *>(outBuffer.get()),
                                             outSize);
      LOG(("nsHttpCompressConv %p BrotliHandler ODA rv=%" PRIx32, self, static_cast<uint32_t>(rv)));
      if (NS_FAILED(rv)) {
        self->mBrotli->mStatus = rv;
        return self->mBrotli->mStatus;
      }
    }

    if (res == BROTLI_DECODER_RESULT_SUCCESS ||
        res == BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT) {
      *countRead = aAvail;
      return NS_OK;
    }
    MOZ_ASSERT (res == BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT);
  } while (res == BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT);

  self->mBrotli->mStatus = NS_ERROR_UNEXPECTED;
  return self->mBrotli->mStatus;
}

std::string GetCompressedContentHostString();
std::string GetCompressedContentMime();

NS_IMETHODIMP
nsHTTPCompressConv::OnDataAvailable(nsIRequest* request,
                                    nsISupports *aContext,
                                    nsIInputStream *iStr,
                                    uint64_t aSourceOffset,
                                    uint32_t aCount)
{
  #if defined(NACL_SANDBOX_USE_NEW_CPP_API) || defined(WASM_SANDBOX_USE_NEW_CPP_API) || defined(PS_SANDBOX_USE_NEW_CPP_API)
    if (rlbox_sbx == nullptr) {
      mHostContentString = GetCompressedContentHostString() + GetCompressedContentMime();
      rlbox_sbx_shared = zlibSandboxManager.createSandbox(mHostContentString);
      rlbox_sbx = rlbox_sbx_shared.get();

      p_d_stream = rlbox_sbx->rlbox_zlib->mallocInSandbox<z_stream>();
      if(p_d_stream == nullptr) {
        printf("Error in malloc for p_d_stream\n");
        exit(1);
      }
    }
    auto rlbox_zlib = rlbox_sbx->rlbox_zlib;
  #endif

  nsresult rv = NS_ERROR_INVALID_CONTENT_ENCODING;
  uint32_t streamLen = aCount;
  LOG(("nsHttpCompressConv %p OnDataAvailable %d", this, aCount));

  if (streamLen == 0) {
    NS_ERROR("count of zero passed to OnDataAvailable");
    return NS_ERROR_UNEXPECTED;
  }

  if (mStreamEnded) {
    // Hmm... this may just indicate that the data stream is done and that
    // what's left is either metadata or padding of some sort.... throwing
    // it out is probably the safe thing to do.
    uint32_t n;
    return iStr->ReadSegments(NS_DiscardSegment, nullptr, streamLen, &n);
  }

  #if defined(PS_SANDBOX_USE_NEW_CPP_API)
    class ActiveRAIIWrapper{
      ZProcessSandbox* s;
      public:
      ActiveRAIIWrapper(ZProcessSandbox* ps) : s(ps) { s->makeActiveSandbox(); }
      ~ActiveRAIIWrapper() { s->makeInactiveSandbox(); }
    };
    ActiveRAIIWrapper procSbxActivation(rlbox_zlib->getSandbox());
  #endif

  switch (mMode) {
  case HTTP_COMPRESS_GZIP:
    streamLen = check_header(iStr, streamLen, &rv);

    if (rv != NS_OK) {
      return rv;
    }

    if (streamLen == 0) {
      return NS_OK;
    }

    MOZ_FALLTHROUGH;

  case HTTP_COMPRESS_DEFLATE:

    if (mInpBuffer != nullptr && streamLen > mInpBufferLen) {
#if defined(NACL_SANDBOX_USE_NEW_CPP_API) || defined(WASM_SANDBOX_USE_NEW_CPP_API) || defined(PS_SANDBOX_USE_NEW_CPP_API)
      rlbox_zlib->freeInSandbox(mInpBuffer);
      mInpBuffer = rlbox_zlib->mallocInSandbox<unsigned char>(mInpBufferLen = streamLen);
#elif defined(SANDBOX_CPP)
      freeInSandbox(sbox, mInpBuffer);
      mInpBuffer = newInSandbox<unsigned char>(sbox, mInpBufferLen = streamLen);
#else
      mInpBuffer = (unsigned char *) realloc(mInpBuffer, mInpBufferLen = streamLen);
#endif

      if (mOutBufferLen < streamLen * 2) {
        mOutBufferLen = streamLen * 3;
        mOutBuffer = (unsigned char *) realloc(mOutBuffer, mOutBufferLen);
        #ifdef USE_COPYING_BUFFERS
          #if defined(NACL_SANDBOX_USE_NEW_CPP_API) || defined(WASM_SANDBOX_USE_NEW_CPP_API) || defined(PS_SANDBOX_USE_NEW_CPP_API)
            rlbox_zlib->freeInSandbox(sbOutBuffer);
            sbOutBuffer = rlbox_zlib->mallocInSandbox<unsigned char>(mOutBufferLen);
          #elif defined(SANDBOX_CPP)
            freeInSandbox(sbox, sbOutBuffer);
            sbOutBuffer = newInSandbox<unsigned char>(sbox, mOutBufferLen);
          #else
            free(sbOutBuffer);
            sbOutBuffer = (unsigned char*) malloc(mOutBufferLen);
          #endif
        #endif
      }

      if (mInpBuffer == nullptr || mOutBuffer == nullptr
#ifdef USE_COPYING_BUFFERS
          || sbOutBuffer == nullptr
#endif
         ) {
        return NS_ERROR_OUT_OF_MEMORY;
      }
    }

    if (mInpBuffer == nullptr) {
#if defined(NACL_SANDBOX_USE_NEW_CPP_API) || defined(WASM_SANDBOX_USE_NEW_CPP_API) || defined(PS_SANDBOX_USE_NEW_CPP_API)
      mInpBuffer = rlbox_zlib->mallocInSandbox<unsigned char>(mInpBufferLen = streamLen);
#elif defined(SANDBOX_CPP)
      mInpBuffer = newInSandbox<unsigned char>(sbox, mInpBufferLen = streamLen);
#else
      mInpBuffer = (unsigned char *) malloc(mInpBufferLen = streamLen);
#endif
    }

    if (mOutBuffer == nullptr) {
      mOutBufferLen = streamLen * 3;
      mOutBuffer = (unsigned char *) malloc(mOutBufferLen);
      #ifdef USE_COPYING_BUFFERS
        #if defined(NACL_SANDBOX_USE_NEW_CPP_API) || defined(WASM_SANDBOX_USE_NEW_CPP_API) || defined(PS_SANDBOX_USE_NEW_CPP_API)
          sbOutBuffer = rlbox_zlib->mallocInSandbox<unsigned char>(mOutBufferLen);
        #elif defined(SANDBOX_CPP)
          sbOutBuffer = newInSandbox<unsigned char>(sbox, mOutBufferLen);
        #else
          sbOutBuffer = (unsigned char*) malloc(mOutBufferLen);
        #endif
      #endif
    }

    if (mInpBuffer == nullptr || mOutBuffer == nullptr
#ifdef USE_COPYING_BUFFERS
          || sbOutBuffer == nullptr
#endif
        ) {
      return NS_ERROR_OUT_OF_MEMORY;
    }

    uint32_t unused;
#if defined(NACL_SANDBOX_USE_NEW_CPP_API) || defined(WASM_SANDBOX_USE_NEW_CPP_API) || defined(PS_SANDBOX_USE_NEW_CPP_API)
    iStr->Read((char *)(mInpBuffer.UNSAFE_Unverified()), streamLen, &unused);
#elif defined(SANDBOX_CPP)
    iStr->Read((char *)(mInpBuffer.sandbox_onlyVerifyAddress()), streamLen, &unused);
#else
    iStr->Read((char *)mInpBuffer, streamLen, &unused);
#endif

    if (mMode == HTTP_COMPRESS_DEFLATE) {
      if (!mStreamInitialized) {
#if defined(NACL_SANDBOX_USE_NEW_CPP_API) || defined(WASM_SANDBOX_USE_NEW_CPP_API) || defined(PS_SANDBOX_USE_NEW_CPP_API)
        memset(p_d_stream.UNSAFE_Unverified(), 0, sizeof (*p_d_stream));
        auto rv = sandbox_invoke(rlbox_zlib, inflateInit_, p_d_stream, rlbox_zlib->stackarr(ZLIB_VERSION), sizeof(z_stream)).copyAndVerify([](int i){
                // safe in all cases - we only care about ==Z_OK or not
                return i;
              });
#elif defined(SANDBOX_CPP)
        memset(p_d_stream, 0, sizeof (*p_d_stream));
        auto rv = sandbox_invoke(sbox, inflateInit_, p_d_stream, sandbox_stackarr(ZLIB_VERSION), sizeof(z_stream)).sandbox_copyAndVerify([](int i){
                // safe in all cases - we only care about ==Z_OK or not
                return i;
              });
#else
        memset(&d_stream, 0, sizeof(d_stream));
        auto rv = inflateInit(&d_stream);
#endif
        if (rv != Z_OK) {
          return NS_ERROR_FAILURE;
        }

        mStreamInitialized = true;
      }

#if defined(NACL_SANDBOX_USE_NEW_CPP_API) || defined(WASM_SANDBOX_USE_NEW_CPP_API) || defined(PS_SANDBOX_USE_NEW_CPP_API) || defined(SANDBOX_CPP)
      p_d_stream->next_in  = mInpBuffer;
      p_d_stream->avail_in = (uInt)streamLen;
#else
      d_stream.next_in  = mInpBuffer;
      d_stream.avail_in = (uInt)streamLen;
#endif

      mDummyStreamInitialised = false;
      for (;;) {
#if defined(NACL_SANDBOX_USE_NEW_CPP_API) || defined(WASM_SANDBOX_USE_NEW_CPP_API) || defined(PS_SANDBOX_USE_NEW_CPP_API)
        p_d_stream->next_out  = sbOutBuffer;
        p_d_stream->avail_out = (uInt)mOutBufferLen;

        int code = sandbox_invoke(rlbox_zlib, inflate, p_d_stream, Z_NO_FLUSH).copyAndVerify([](int i){
                        // all values are safe - we handle all values of 'code'
                        return i;
                      });
        uInt avail_out = p_d_stream->avail_out.copyAndVerify([this](uInt i){
                            if(i <= mOutBufferLen) return i;
                            printf("Unexpected avail_out %u vs. mOutBufferLen %u\n", i, mOutBufferLen);
                            exit(1);
                          });
#elif defined(SANDBOX_CPP)
        p_d_stream->next_out  = sbOutBuffer;
        p_d_stream->avail_out = (uInt)mOutBufferLen;

        int code = sandbox_invoke(sbox, inflate, p_d_stream, Z_NO_FLUSH).sandbox_copyAndVerify([](int i){
                        // all values are safe - we handle all values of 'code'
                        return i;
                      });
        uInt avail_out = p_d_stream->avail_out.sandbox_copyAndVerify([this](uInt i){
                            if(i <= mOutBufferLen) return i;
                            printf("Unexpected avail_out %u vs. mOutBufferLen %u\n", i, mOutBufferLen);
                            exit(1);
                          });
#else
        #ifdef USE_COPYING_BUFFERS
          d_stream.next_out = sbOutBuffer;
        #else
          d_stream.next_out  = mOutBuffer;
        #endif
        d_stream.avail_out = (uInt)mOutBufferLen;

        int code = inflate(&d_stream, Z_NO_FLUSH);
        uInt avail_out = d_stream.avail_out;
#endif
        unsigned bytesWritten = (uInt)mOutBufferLen - avail_out;

        if (code == Z_STREAM_END) {
          if (bytesWritten) {

            #ifdef USE_COPYING_BUFFERS
              #if defined(NACL_SANDBOX_USE_NEW_CPP_API) || defined(WASM_SANDBOX_USE_NEW_CPP_API) || defined(PS_SANDBOX_USE_NEW_CPP_API)
                mOutBuffer = sbOutBuffer.copyAndVerifyArray(rlbox_zlib, [](unsigned char* c){
                              // no validation needed, we simply copy the array to prevent
                              // double-fetch / TOCTOU i.e. capture a persistent snapshot of
                              // its contents
                              return RLBox_Verify_Status::SAFE;
                            }, mOutBufferLen, (unsigned char *)nullptr);
              #elif defined(SANDBOX_CPP)
                mOutBuffer = sbOutBuffer.sandbox_copyAndVerifyArray([](unsigned char* c){
                            // no validation needed, we simply copy the array to prevent
                            // double-fetch / TOCTOU i.e. capture a persistent snapshot of
                            // its contents
                            return true;
                          }, mOutBufferLen, (unsigned char *)nullptr);
              #else
                memcpy(mOutBuffer, sbOutBuffer, mOutBufferLen);
              #endif
            #endif
            rv = do_OnDataAvailable(request, aContext, aSourceOffset, (char *)mOutBuffer, bytesWritten);
            if (NS_FAILED (rv)) {
              return rv;
            }
          }

#if defined(NACL_SANDBOX_USE_NEW_CPP_API) || defined(WASM_SANDBOX_USE_NEW_CPP_API) || defined(PS_SANDBOX_USE_NEW_CPP_API)
          sandbox_invoke(rlbox_zlib, inflateEnd, p_d_stream);
#elif defined(SANDBOX_CPP)
          sandbox_invoke(sbox, inflateEnd, p_d_stream);
#else
          inflateEnd(&d_stream);
#endif
          mStreamEnded = true;
          break;
        } else if (code == Z_OK) {
          if (bytesWritten) {
            #ifdef USE_COPYING_BUFFERS
              #if defined(NACL_SANDBOX_USE_NEW_CPP_API) || defined(WASM_SANDBOX_USE_NEW_CPP_API) || defined(PS_SANDBOX_USE_NEW_CPP_API)
                mOutBuffer = sbOutBuffer.copyAndVerifyArray(rlbox_zlib, [](unsigned char* c){
                            // no validation needed, we simply copy the array to prevent
                            // double-fetch / TOCTOU i.e. capture a persistent snapshot of
                            // its contents
                            return RLBox_Verify_Status::SAFE;
                          }, mOutBufferLen, (unsigned char *)nullptr);
              #elif defined(SANDBOX_CPP)
                mOutBuffer = sbOutBuffer.sandbox_copyAndVerifyArray([](unsigned char* c){
                            // no validation needed, we simply copy the array to prevent
                            // double-fetch / TOCTOU i.e. capture a persistent snapshot of
                            // its contents
                            return true;
                          }, mOutBufferLen, (unsigned char *)nullptr);
              #else
                memcpy(mOutBuffer, sbOutBuffer, mOutBufferLen);
              #endif
            #endif
            rv = do_OnDataAvailable(request, aContext, aSourceOffset, (char *)mOutBuffer, bytesWritten);
            if (NS_FAILED (rv)) {
              return rv;
            }
          }
        } else if (code == Z_BUF_ERROR) {
          if (bytesWritten) {
            #ifdef USE_COPYING_BUFFERS
              #if defined(NACL_SANDBOX_USE_NEW_CPP_API) || defined(WASM_SANDBOX_USE_NEW_CPP_API) || defined(PS_SANDBOX_USE_NEW_CPP_API)
                mOutBuffer = sbOutBuffer.copyAndVerifyArray(rlbox_zlib, [](unsigned char* c){
                            // no validation needed, we simply copy the array to prevent
                            // double-fetch / TOCTOU i.e. capture a persistent snapshot of
                            // its contents
                            return RLBox_Verify_Status::SAFE;
                          }, mOutBufferLen, (unsigned char *)nullptr);
              #elif defined(SANDBOX_CPP)
                mOutBuffer = sbOutBuffer.sandbox_copyAndVerifyArray([](unsigned char* c){
                            // no validation needed, we simply copy the array to prevent
                            // double-fetch / TOCTOU i.e. capture a persistent snapshot of
                            // its contents
                            return true;
                          }, mOutBufferLen, (unsigned char *)nullptr);
              #else
                memcpy(mOutBuffer, sbOutBuffer, mOutBufferLen);
              #endif
            #endif
            rv = do_OnDataAvailable(request, aContext, aSourceOffset, (char *)mOutBuffer, bytesWritten);
            if (NS_FAILED (rv)) {
              return rv;
            }
          }
          break;
        } else if (code == Z_DATA_ERROR) {
          // some servers (notably Apache with mod_deflate) don't generate zlib headers
          // insert a dummy header and try again
          static char dummy_head[2] =
            {
              0x8 + 0x7 * 0x10,
              (((0x8 + 0x7 * 0x10) * 0x100 + 30) / 31 * 31) & 0xFF,
            };
#if defined(NACL_SANDBOX_USE_NEW_CPP_API) || defined(WASM_SANDBOX_USE_NEW_CPP_API) || defined(PS_SANDBOX_USE_NEW_CPP_API)
          auto sb_dummy_head = rlbox_zlib->mallocInSandbox<unsigned char>(2);
          memcpy(sb_dummy_head.UNSAFE_Unverified(), dummy_head, 2);
          sandbox_invoke(rlbox_zlib, inflateReset, p_d_stream);
          p_d_stream->next_in  = sb_dummy_head;
          p_d_stream->avail_in = sizeof(dummy_head);

          code = sandbox_invoke(rlbox_zlib, inflate, p_d_stream, Z_NO_FLUSH).copyAndVerify([](int i){
                      // all values of code are safe, we only care about ==Z_OK or not
                      return i;
                    });
#elif defined(SANDBOX_CPP)
          auto sb_dummy_head = newInSandbox<unsigned char>(sbox, 2);
          memcpy(sb_dummy_head, dummy_head, 2);
          sandbox_invoke(sbox, inflateReset, p_d_stream);
          p_d_stream->next_in  = sb_dummy_head;
          p_d_stream->avail_in = sizeof(dummy_head);

          code = sandbox_invoke(sbox, inflate, p_d_stream, Z_NO_FLUSH).sandbox_copyAndVerify([](int i){
                      // all values of code are safe, we only care about ==Z_OK or not
                      return i;
                    });
#else
          inflateReset(&d_stream);
          d_stream.next_in  = (Bytef*) dummy_head;
          d_stream.avail_in = sizeof(dummy_head);

          code = inflate(&d_stream, Z_NO_FLUSH);
#endif
          if (code != Z_OK) {
            return NS_ERROR_FAILURE;
          }

          // stop an endless loop caused by non-deflate data being labelled as deflate
          if (mDummyStreamInitialised) {
            NS_WARNING("endless loop detected"
                       " - invalid deflate");
            return NS_ERROR_INVALID_CONTENT_ENCODING;
          }
          mDummyStreamInitialised = true;
          // reset stream pointers to our original data
#if defined(NACL_SANDBOX_USE_NEW_CPP_API) || defined(WASM_SANDBOX_USE_NEW_CPP_API) || defined(PS_SANDBOX_USE_NEW_CPP_API)
          p_d_stream->next_in  = mInpBuffer;
          p_d_stream->avail_in = (uInt)streamLen;
          rlbox_zlib->freeInSandbox(sb_dummy_head);
#elif defined(SANDBOX_CPP)
          p_d_stream->next_in  = mInpBuffer;
          p_d_stream->avail_in = (uInt)streamLen;
          freeInSandbox(sbox, sb_dummy_head);
#else
          d_stream.next_in  = mInpBuffer;
          d_stream.avail_in = (uInt)streamLen;
#endif
        } else {
          return NS_ERROR_INVALID_CONTENT_ENCODING;
        }
      } /* for */
    } else {
      if (!mStreamInitialized) {
#if defined(NACL_SANDBOX_USE_NEW_CPP_API) || defined(WASM_SANDBOX_USE_NEW_CPP_API) || defined(PS_SANDBOX_USE_NEW_CPP_API)
        memset(p_d_stream.UNSAFE_Unverified(), 0, sizeof (*p_d_stream));
        auto rv = sandbox_invoke(rlbox_zlib, inflateInit2_, p_d_stream, -MAX_WBITS, rlbox_zlib->stackarr(ZLIB_VERSION), sizeof(z_stream)).copyAndVerify([](int i){
              // safe in all cases - we only care about ==Z_OK or not
              return i;
              });
#elif defined(SANDBOX_CPP)
        memset(p_d_stream, 0, sizeof (*p_d_stream));
        auto rv = sandbox_invoke(sbox, inflateInit2_, p_d_stream, -MAX_WBITS, sandbox_stackarr(ZLIB_VERSION), sizeof(z_stream)).sandbox_copyAndVerify([](int i){
              // safe in all cases - we only care about ==Z_OK or not
              return i;
              });
#else
        memset(&d_stream, 0, sizeof(d_stream));
        auto rv = inflateInit2(&d_stream, -MAX_WBITS);
#endif
        if (rv != Z_OK) {
          return NS_ERROR_FAILURE;
        }

        mStreamInitialized = true;
      }

#if defined(SANDBOX_CPP) || defined(NACL_SANDBOX_USE_NEW_CPP_API) || defined(WASM_SANDBOX_USE_NEW_CPP_API) || defined(PS_SANDBOX_USE_NEW_CPP_API)
      p_d_stream->next_in  = mInpBuffer;
      p_d_stream->avail_in = (uInt)streamLen;
#else
      d_stream.next_in  = mInpBuffer;
      d_stream.avail_in = (uInt)streamLen;
#endif

      for (;;) {
#if defined(NACL_SANDBOX_USE_NEW_CPP_API) || defined(WASM_SANDBOX_USE_NEW_CPP_API) || defined(PS_SANDBOX_USE_NEW_CPP_API)
        p_d_stream->next_out  = sbOutBuffer;
        p_d_stream->avail_out = (uInt)mOutBufferLen;

        int code = sandbox_invoke(rlbox_zlib, inflate, p_d_stream, Z_NO_FLUSH).copyAndVerify([](int i){
            // safe in all cases - we handle all possible values of code
            return i;
            });
        //int code = inflate (&d_stream, Z_NO_FLUSH);
        uInt avail_out = p_d_stream->avail_out.copyAndVerify([this](uInt i){
                            if(i <= mOutBufferLen) return i;
                            printf("Unexpected avail_out %u vs mOutBufferLen %u\n", i, mOutBufferLen);
                            exit(1);
                          });
#elif defined(SANDBOX_CPP)
        p_d_stream->next_out  = sbOutBuffer;
        p_d_stream->avail_out = (uInt)mOutBufferLen;

        int code = sandbox_invoke(sbox, inflate, p_d_stream, Z_NO_FLUSH).sandbox_copyAndVerify([](int i){
            // safe in all cases - we handle all possible values of code
            return i;
            });
        //int code = inflate (&d_stream, Z_NO_FLUSH);
        uInt avail_out = p_d_stream->avail_out.sandbox_copyAndVerify([this](uInt i){
                            if(i <= mOutBufferLen) return i;
                            printf("Unexpected avail_out %u vs mOutBufferLen %u\n", i, mOutBufferLen);
                            exit(1);
                          });
#else
#ifdef USE_COPYING_BUFFERS
        d_stream.next_out  = sbOutBuffer;
#else
        d_stream.next_out  = mOutBuffer;
#endif
        d_stream.avail_out = (uInt)mOutBufferLen;

        int code = inflate(&d_stream, Z_NO_FLUSH);
        uInt avail_out = d_stream.avail_out;
#endif
        unsigned bytesWritten = (uInt)mOutBufferLen - avail_out;

        if (code == Z_STREAM_END) {
          if (bytesWritten) {
            #ifdef USE_COPYING_BUFFERS
              #if defined(NACL_SANDBOX_USE_NEW_CPP_API) || defined(WASM_SANDBOX_USE_NEW_CPP_API) || defined(PS_SANDBOX_USE_NEW_CPP_API)
                mOutBuffer = sbOutBuffer.copyAndVerifyArray(rlbox_zlib, [](unsigned char* c){
                            // no validation needed, we simply copy the array to prevent
                            // double-fetch / TOCTOU i.e. capture a persistent snapshot of
                            // its contents
                            return RLBox_Verify_Status::SAFE;
                          }, mOutBufferLen, (unsigned char *)nullptr);
              #elif defined(SANDBOX_CPP)
                mOutBuffer = sbOutBuffer.sandbox_copyAndVerifyArray([](unsigned char* c){
                            // no validation needed, we simply copy the array to prevent
                            // double-fetch / TOCTOU i.e. capture a persistent snapshot of
                            // its contents
                            return true;
                          }, mOutBufferLen, (unsigned char *)nullptr);
              #else
                memcpy(mOutBuffer, sbOutBuffer, mOutBufferLen);
              #endif
            #endif
            rv = do_OnDataAvailable(request, aContext, aSourceOffset, (char *)mOutBuffer, bytesWritten);
            if (NS_FAILED (rv)) {
              return rv;
            }
          }

#if defined(NACL_SANDBOX_USE_NEW_CPP_API) || defined(WASM_SANDBOX_USE_NEW_CPP_API) || defined(PS_SANDBOX_USE_NEW_CPP_API)
          sandbox_invoke(rlbox_zlib, inflateEnd, p_d_stream);
#elif defined(SANDBOX_CPP)
          sandbox_invoke(sbox, inflateEnd, p_d_stream);
#else
          inflateEnd(&d_stream);
#endif
          mStreamEnded = true;
          break;
        } else if (code == Z_OK) {
          if (bytesWritten) {
            #ifdef USE_COPYING_BUFFERS
              #if defined(NACL_SANDBOX_USE_NEW_CPP_API) || defined(WASM_SANDBOX_USE_NEW_CPP_API) || defined(PS_SANDBOX_USE_NEW_CPP_API)
                mOutBuffer = sbOutBuffer.copyAndVerifyArray(rlbox_zlib, [](unsigned char* c){
                            // no validation needed, we simply copy the array to prevent
                            // double-fetch / TOCTOU i.e. capture a persistent snapshot of
                            // its contents
                            return RLBox_Verify_Status::SAFE;
                          }, mOutBufferLen, (unsigned char *)nullptr);
              #elif defined(SANDBOX_CPP)
                mOutBuffer = sbOutBuffer.sandbox_copyAndVerifyArray([](unsigned char* c){
                            // no validation needed, we simply copy the array to prevent
                            // double-fetch / TOCTOU i.e. capture a persistent snapshot of
                            // its contents
                            return true;
                          }, mOutBufferLen, (unsigned char *)nullptr);
              #else
                memcpy(mOutBuffer, sbOutBuffer, mOutBufferLen);
              #endif
            #endif
            rv = do_OnDataAvailable(request, aContext, aSourceOffset, (char *)mOutBuffer, bytesWritten);
            if (NS_FAILED (rv)) {
              return rv;
            }
          }
        } else if (code == Z_BUF_ERROR) {
          if (bytesWritten) {
            #ifdef USE_COPYING_BUFFERS
              #if defined(NACL_SANDBOX_USE_NEW_CPP_API) || defined(WASM_SANDBOX_USE_NEW_CPP_API) || defined(PS_SANDBOX_USE_NEW_CPP_API)
                mOutBuffer = sbOutBuffer.copyAndVerifyArray(rlbox_zlib, [](unsigned char* c){
                            // no validation needed, we simply copy the array to prevent
                            // double-fetch / TOCTOU i.e. capture a persistent snapshot of
                            // its contents
                            return RLBox_Verify_Status::SAFE;
                          }, mOutBufferLen, (unsigned char *)nullptr);
              #elif defined(SANDBOX_CPP)
                mOutBuffer = sbOutBuffer.sandbox_copyAndVerifyArray([](unsigned char* c){
                            // no validation needed, we simply copy the array to prevent
                            // double-fetch / TOCTOU i.e. capture a persistent snapshot of
                            // its contents
                            return true;
                          }, mOutBufferLen, (unsigned char *)nullptr);
              #else
                memcpy(mOutBuffer, sbOutBuffer, mOutBufferLen);
              #endif
            #endif
            rv = do_OnDataAvailable(request, aContext, aSourceOffset, (char *)mOutBuffer, bytesWritten);
            if (NS_FAILED (rv)) {
              return rv;
            }
          }
          break;
        } else {
          return NS_ERROR_INVALID_CONTENT_ENCODING;
        }
      } /* for */
    } /* gzip */
    break;

  case HTTP_COMPRESS_BROTLI:
  {
    if (!mBrotli) {
      mBrotli = new BrotliWrapper();
    }

    mBrotli->mRequest = request;
    mBrotli->mContext = aContext;
    mBrotli->mSourceOffset = aSourceOffset;

    uint32_t countRead;
    rv = iStr->ReadSegments(BrotliHandler, this, streamLen, &countRead);
    if (NS_SUCCEEDED(rv)) {
      rv = mBrotli->mStatus;
    }
    if (NS_FAILED(rv)) {
      return rv;
    }
  }
    break;

  default:
    nsCOMPtr<nsIStreamListener> listener;
    {
      MutexAutoLock lock(mMutex);
      listener = mListener;
    }
    rv = listener->OnDataAvailable(request, aContext, iStr, aSourceOffset, aCount);
    if (NS_FAILED (rv)) {
      return rv;
    }
  } /* switch */

  return NS_OK;
} /* OnDataAvailable */

// XXX/ruslan: need to implement this too

NS_IMETHODIMP
nsHTTPCompressConv::Convert(nsIInputStream *aFromStream,
                            const char *aFromType,
                            const char *aToType,
                            nsISupports *aCtxt,
                            nsIInputStream **_retval)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

nsresult
nsHTTPCompressConv::do_OnDataAvailable(nsIRequest* request,
                                       nsISupports *context, uint64_t offset,
                                       const char *buffer, uint32_t count)
{
  if (!mStream) {
    mStream = do_CreateInstance(NS_STRINGINPUTSTREAM_CONTRACTID);
    NS_ENSURE_STATE(mStream);
  }

  mStream->ShareData(buffer, count);

  nsCOMPtr<nsIStreamListener> listener;
  {
    MutexAutoLock lock(mMutex);
    listener = mListener;
  }
  nsresult rv = listener->OnDataAvailable(request, context, mStream,
                                          offset, count);

  // Make sure the stream no longer references |buffer| in case our listener
  // is crazy enough to try to read from |mStream| after ODA.
  mStream->ShareData("", 0);
  mDecodedDataLength += count;

  return rv;
}

#define ASCII_FLAG   0x01 /* bit 0 set: file probably ascii text */
#define HEAD_CRC     0x02 /* bit 1 set: header CRC present */
#define EXTRA_FIELD  0x04 /* bit 2 set: extra field present */
#define ORIG_NAME    0x08 /* bit 3 set: original file name present */
#define COMMENT      0x10 /* bit 4 set: file comment present */
#define RESERVED     0xE0 /* bits 5..7: reserved */

static unsigned gz_magic[2] = {0x1f, 0x8b}; /* gzip magic header */

uint32_t
nsHTTPCompressConv::check_header(nsIInputStream *iStr, uint32_t streamLen, nsresult *rs)
{
  enum  { GZIP_INIT = 0, GZIP_OS, GZIP_EXTRA0, GZIP_EXTRA1, GZIP_EXTRA2, GZIP_ORIG, GZIP_COMMENT, GZIP_CRC };
  char c;

  *rs = NS_OK;

  if (mCheckHeaderDone) {
    return streamLen;
  }

  while (streamLen) {
    switch (hMode) {
    case GZIP_INIT:
      uint32_t unused;
      iStr->Read(&c, 1, &unused);
      streamLen--;

      if (mSkipCount == 0 && ((unsigned)c & 0377) != gz_magic[0]) {
        *rs = NS_ERROR_INVALID_CONTENT_ENCODING;
        return 0;
      }

      if (mSkipCount == 1 && ((unsigned)c & 0377) != gz_magic[1]) {
        *rs = NS_ERROR_INVALID_CONTENT_ENCODING;
        return 0;
      }

      if (mSkipCount == 2 && ((unsigned)c & 0377) != Z_DEFLATED) {
        *rs = NS_ERROR_INVALID_CONTENT_ENCODING;
        return 0;
      }

      mSkipCount++;
      if (mSkipCount == 4) {
        mFlags = (unsigned) c & 0377;
        if (mFlags & RESERVED) {
          *rs = NS_ERROR_INVALID_CONTENT_ENCODING;
          return 0;
        }
        hMode = GZIP_OS;
        mSkipCount = 0;
      }
      break;

    case GZIP_OS:
      iStr->Read(&c, 1, &unused);
      streamLen--;
      mSkipCount++;

      if (mSkipCount == 6) {
        hMode = GZIP_EXTRA0;
      }
      break;

    case GZIP_EXTRA0:
      if (mFlags & EXTRA_FIELD) {
        iStr->Read(&c, 1, &unused);
        streamLen--;
        mLen = (uInt) c & 0377;
        hMode = GZIP_EXTRA1;
      } else {
        hMode = GZIP_ORIG;
      }
      break;

    case GZIP_EXTRA1:
      iStr->Read(&c, 1, &unused);
      streamLen--;
      mLen |= ((uInt) c & 0377) << 8;
      mSkipCount = 0;
      hMode = GZIP_EXTRA2;
      break;

    case GZIP_EXTRA2:
      if (mSkipCount == mLen) {
        hMode = GZIP_ORIG;
      } else {
        iStr->Read(&c, 1, &unused);
        streamLen--;
        mSkipCount++;
      }
      break;

    case GZIP_ORIG:
      if (mFlags & ORIG_NAME) {
        iStr->Read(&c, 1, &unused);
        streamLen--;
        if (c == 0)
          hMode = GZIP_COMMENT;
      } else {
        hMode = GZIP_COMMENT;
      }
      break;

    case GZIP_COMMENT:
      if (mFlags & COMMENT) {
        iStr->Read(&c, 1, &unused);
        streamLen--;
        if (c == 0) {
          hMode = GZIP_CRC;
          mSkipCount = 0;
        }
      } else {
        hMode = GZIP_CRC;
        mSkipCount = 0;
      }
      break;

    case GZIP_CRC:
      if (mFlags & HEAD_CRC) {
        iStr->Read(&c, 1, &unused);
        streamLen--;
        mSkipCount++;
        if (mSkipCount == 2) {
          mCheckHeaderDone = true;
          return streamLen;
        }
      } else {
        mCheckHeaderDone = true;
        return streamLen;
      }
      break;
    }
  }
  return streamLen;
}

NS_IMETHODIMP
nsHTTPCompressConv::CheckListenerChain()
{
  nsCOMPtr<nsIThreadRetargetableStreamListener> listener;
  {
    MutexAutoLock lock(mMutex);
    listener = do_QueryInterface(mListener);
  }

  if (!listener) {
    return NS_ERROR_NO_INTERFACE;
  }

  return listener->CheckListenerChain();
}

} // namespace net
} // namespace mozilla

nsresult
NS_NewHTTPCompressConv(mozilla::net::nsHTTPCompressConv **aHTTPCompressConv)
{
  NS_PRECONDITION(aHTTPCompressConv != nullptr, "null ptr");
  if (!aHTTPCompressConv) {
    return NS_ERROR_NULL_POINTER;
  }

  RefPtr<mozilla::net::nsHTTPCompressConv> outVal =
    new mozilla::net::nsHTTPCompressConv();
  if (!outVal) {
    return NS_ERROR_OUT_OF_MEMORY;
  }
  outVal.forget(aHTTPCompressConv);
  return NS_OK;
}
