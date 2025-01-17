#include "mozilla/Logging.h"

#include <dlfcn.h>
#include <stdlib.h>
#include <inttypes.h>
#include <unistd.h>
#include <mutex>

#include "jpeglib_naclport.h"

#ifdef PRINT_FUNCTION_LOGS
  using mozilla::LogLevel;
  static mozilla::LazyLogModule sJPEGLog("JPEGNaclPort");
#endif

//Note USE_SANDBOXING MAY be defined as a macro in the moz.build of this folder

#ifndef USE_SANDBOXING
  #error "USE_SANDBOXING value not provided"
#endif

#if(USE_SANDBOXING != 0 && USE_SANDBOXING != 1 && USE_SANDBOXING != 2 && USE_SANDBOXING != 3)
  #error "Bad USE_SANDBOXING value provided"
#endif

#if(USE_SANDBOXING == 2)
  #include "dyn_ldr_lib.h"

  #define MY_ERROR_EXIT_CALLBACK_SLOT 0
  #define INIT_SOURCE_SLOT 1
  #define FILL_INPUT_BUFFER_SLOT 2
  #define SKIP_INPUT_DATA_SLOT 3
  #define JPEG_RESYNC_TO_RESTART_SLOT 4
  #define TERM_SOURCE_SLOT 5

  NaClSandbox* jpegSandbox;

#elif(USE_SANDBOXING == 3)
  #undef USE_LIBPNG
  #define USE_LIBJPEG
  #include "ProcessSandbox.h"
  #undef USE_LIBJPEG

  JPEGProcessSandbox* jpegSandbox = 0;

#endif

#ifdef PRINT_FUNCTION_TIMES

  #include <chrono>
  #include <atomic>
  using namespace std::chrono;

  std::atomic_ullong timeSpentInJpeg{0};
  std::atomic_ullong timeSpentInJpegCore{0};
  std::atomic_ullong jpegSandboxFuncOrCallbackInvocations{0};
  std::atomic_ullong jpegSandboxFuncOrCallbackInvocationsCore{0};

  __thread high_resolution_clock::time_point JpegSandboxEnterTime;
  __thread high_resolution_clock::time_point JpegSandboxExitTime;

  #define START_TIMER(NAME) JpegSandboxEnterTime = high_resolution_clock::now(); \
    jpegSandboxFuncOrCallbackInvocations++

  #define START_TIMER_CORE(NAME) JpegSandboxEnterTime = high_resolution_clock::now(); \
    jpegSandboxFuncOrCallbackInvocations++; \
    jpegSandboxFuncOrCallbackInvocationsCore++

  #define END_TIMER(NAME)   JpegSandboxExitTime = high_resolution_clock::now(); \
    timeSpentInJpeg+= duration_cast<nanoseconds>(JpegSandboxExitTime - JpegSandboxEnterTime).count()

  #define END_TIMER_CORE(NAME)   JpegSandboxExitTime = high_resolution_clock::now(); \
    timeSpentInJpeg+= duration_cast<nanoseconds>(JpegSandboxExitTime - JpegSandboxEnterTime).count(); \
    timeSpentInJpegCore+= duration_cast<nanoseconds>(JpegSandboxExitTime - JpegSandboxEnterTime).count()

#else
  #define START_TIMER(NAME) do {} while(0)
  #define END_TIMER(NAME) do {} while(0)
  #define START_TIMER_CORE(NAME) do {} while(0)
  #define END_TIMER_CORE(NAME) do {} while(0)
#endif

#if(USE_SANDBOXING != 3)
t_jpeg_std_error              ptr_jpeg_std_error;
t_jpeg_CreateCompress         ptr_jpeg_CreateCompress;
t_jpeg_stdio_dest             ptr_jpeg_stdio_dest;
t_jpeg_set_defaults           ptr_jpeg_set_defaults;
t_jpeg_set_quality            ptr_jpeg_set_quality;
t_jpeg_start_compress         ptr_jpeg_start_compress;
t_jpeg_write_scanlines        ptr_jpeg_write_scanlines;
t_jpeg_finish_compress        ptr_jpeg_finish_compress;
t_jpeg_destroy_compress       ptr_jpeg_destroy_compress;
t_jpeg_CreateDecompress       ptr_jpeg_CreateDecompress;
t_jpeg_stdio_src              ptr_jpeg_stdio_src;
t_jpeg_read_header            ptr_jpeg_read_header;
t_jpeg_start_decompress       ptr_jpeg_start_decompress;
t_jpeg_read_scanlines         ptr_jpeg_read_scanlines;
t_jpeg_finish_decompress      ptr_jpeg_finish_decompress;
t_jpeg_destroy_decompress     ptr_jpeg_destroy_decompress;
t_jpeg_save_markers           ptr_jpeg_save_markers;
t_jpeg_has_multiple_scans     ptr_jpeg_has_multiple_scans;
t_jpeg_calc_output_dimensions ptr_jpeg_calc_output_dimensions;
t_jpeg_start_output           ptr_jpeg_start_output;
t_jpeg_finish_output          ptr_jpeg_finish_output;
t_jpeg_input_complete         ptr_jpeg_input_complete;
t_jpeg_consume_input          ptr_jpeg_consume_input;
#endif

//Callback stubs
t_my_error_exit          cb_my_error_exit;
t_init_source            cb_init_source;
t_fill_input_buffer      cb_fill_input_buffer;
t_skip_input_data        cb_skip_input_data;
t_jpeg_resync_to_restart cb_jpeg_resync_to_restart;
t_term_source            cb_term_source;

#if(USE_SANDBOXING == 3)
  t_my_error_exit           my_error_exit_stub_reg{0};
  t_init_source             init_source_stub_reg{0};
  t_fill_input_buffer       fill_input_buffer_stub_reg{0};
  t_skip_input_data         skip_input_data_stub_reg{0};
  t_jpeg_resync_to_restart  jpeg_resync_to_restart_stub_reg{0};
  t_term_source             term_source_stub_reg{0};
#endif

unsigned long long getTimeSpentInJpeg()
{
  #ifdef PRINT_FUNCTION_TIMES
    return timeSpentInJpeg;
  #else
    return 0;
  #endif
}
unsigned long long getTimeSpentInJpegCore()
{
  #ifdef PRINT_FUNCTION_TIMES
    return timeSpentInJpegCore;
  #else
    return 0;
  #endif
}
unsigned long long getInvocationsInJpeg()
{
  #ifdef PRINT_FUNCTION_TIMES
    return jpegSandboxFuncOrCallbackInvocations;
  #else
    return 0;
  #endif
}
unsigned long long getInvocationsInJpegCore()
{
  #ifdef PRINT_FUNCTION_TIMES
    return jpegSandboxFuncOrCallbackInvocationsCore;
  #else
    return 0;
  #endif
}


void jpegStartTimer()
{
  START_TIMER("");
}

void jpegStartTimerCore()
{
  START_TIMER_CORE("");
}

void jpegEndTimer()
{
  END_TIMER("");
}

void jpegEndTimerCore()
{
  END_TIMER_CORE("");
}

#if(USE_SANDBOXING == 2 || USE_SANDBOXING == 3)
  std::mutex jpegProcessExitMutex;
#endif
void SandboxOnFirefoxExitingPNG();
extern "C" void SandboxOnFirefoxExiting_JPEGDecoder();
void SandboxOnFirefoxExiting()
{
  #if(USE_SANDBOXING == 2 || USE_SANDBOXING == 3)
  {
    std::lock_guard<std::mutex> guard(jpegProcessExitMutex);

    #if(USE_SANDBOXING == 3)
      if(jpegSandbox != nullptr)
      {
        jpegSandbox->destroySandbox();
        jpegSandbox = nullptr;
      }
    #endif

    SandboxOnFirefoxExiting_JPEGDecoder();
  }
  #endif
  SandboxOnFirefoxExitingPNG();
}

#if(USE_SANDBOXING == 2)

  std::mutex naclInitMutex;
  int naclFinishedInit = 0;

  void ensureNaClSandboxInit()
  {
      std::lock_guard<std::mutex> guard(naclInitMutex);
      if(naclFinishedInit == 1) { return; }

      printf("Initializing NaCl Sandbox\n");

      if(!initializeDlSandboxCreator(0 /* Should enable detailed logging */))
      {
        printf("Error initializing NaCl Sandbox\n");
      }

      naclFinishedInit = 1;
  }

#endif

void* jpegDlPtr;
std::once_flag jpegFinishedInit;

void getSandboxingFolder(char* SandboxingCodeRootFolder)
{
    if(!getcwd(SandboxingCodeRootFolder, 256))
    {
      abort();
    }

    char * found = strstr(SandboxingCodeRootFolder, "/mozilla-release");
    if (found == NULL)
    {
      printf("Error initializing start directory for NaCl\n");   
      exit(1);
    }

    int index = found - SandboxingCodeRootFolder + 1;
    SandboxingCodeRootFolder[index] = '\0';
}

void initializeLibJpegSandbox(void(*additionalSetup)())
{
  std::call_once(jpegFinishedInit, [](void(*additionalSetup)()){
    //Note STARTUP_LIBRARY_PATH, SANDBOX_INIT_APP, JPEG_NON_NACL_DL_PATH, PS_OTHERSIDE_PATH are defined as macros in the moz.build of this folder

    char SandboxingCodeRootFolder[1024];
    getSandboxingFolder(SandboxingCodeRootFolder);

    #if(USE_SANDBOXING == 0)
    {
      printf("Using static libjpeg\n");
      if(additionalSetup != nullptr)
      {
        additionalSetup();
      }
      return;
    }
    #elif(USE_SANDBOXING == 1)
    {
      char full_JPEG_NON_NACL_DL_PATH[1024];

      strcpy(full_JPEG_NON_NACL_DL_PATH, SandboxingCodeRootFolder);
      strcat(full_JPEG_NON_NACL_DL_PATH, JPEG_NON_NACL_DL_PATH);

      printf("Loading dynamic library %s\n", full_JPEG_NON_NACL_DL_PATH);
      jpegDlPtr = dlopen(full_JPEG_NON_NACL_DL_PATH, RTLD_LAZY | RTLD_DEEPBIND);
      if(!jpegDlPtr)
      {
        printf("Loading of dynamic library %s has failed\n", full_JPEG_NON_NACL_DL_PATH);
        exit(1);
      }
    }
    #elif(USE_SANDBOXING == 2)
    {
      char full_STARTUP_LIBRARY_PATH[1024];
      char full_SANDBOX_INIT_APP[1024];

      strcpy(full_STARTUP_LIBRARY_PATH, SandboxingCodeRootFolder);
      strcat(full_STARTUP_LIBRARY_PATH, STARTUP_LIBRARY_PATH);

      strcpy(full_SANDBOX_INIT_APP, SandboxingCodeRootFolder);
      strcat(full_SANDBOX_INIT_APP, SANDBOX_INIT_APP);

      printf("Creating NaCl Sandbox %s, %s\n", full_STARTUP_LIBRARY_PATH, full_SANDBOX_INIT_APP);

      ensureNaClSandboxInit();
      jpegSandbox = createDlSandbox(full_STARTUP_LIBRARY_PATH, full_SANDBOX_INIT_APP);

      if(!jpegSandbox)
      {
        printf("Error creating jpeg Sandbox");
        exit(1);
      }
    }
    #elif(USE_SANDBOXING == 3)
    {
      char full_PS_OTHERSIDE_PATH[1024];

      strcpy(full_PS_OTHERSIDE_PATH, SandboxingCodeRootFolder);
      strcat(full_PS_OTHERSIDE_PATH, PS_OTHERSIDE_PATH);

      printf("Creating JPEG process sandbox %s\n", full_PS_OTHERSIDE_PATH);
      jpegSandbox = new JPEGProcessSandbox(full_PS_OTHERSIDE_PATH, 1, 5);
    }
    #endif

    printf("Loading symbols.\n");
    int failed = 0;

  #if(USE_SANDBOXING != 3)

    #if(USE_SANDBOXING == 2)
      #define loadSymbol(symbol) do { \
        void* dlSymRes = symbolTableLookupInSandbox(jpegSandbox, #symbol); \
        if(dlSymRes == NULL) { printf("Symbol resolution failed for" #symbol "\n"); failed = 1; } \
        *((void **) &ptr_##symbol) = dlSymRes; \
      } while(0)

    #elif(USE_SANDBOXING == 1)
      #define loadSymbol(symbol) do { \
        void* dlSymRes = dlsym(jpegDlPtr, #symbol); \
        if(dlSymRes == NULL) { printf("Symbol resolution failed for" #symbol "\n"); failed = 1; } \
        *((void **) &ptr_##symbol) = dlSymRes; \
      } while(0)

    #else
      #define loadSymbol(symbol) do {} while(0)
    #endif

    loadSymbol(jpeg_std_error);
    loadSymbol(jpeg_CreateCompress);
    loadSymbol(jpeg_stdio_dest);
    loadSymbol(jpeg_set_defaults);
    loadSymbol(jpeg_set_quality);
    loadSymbol(jpeg_start_compress);
    loadSymbol(jpeg_write_scanlines);
    loadSymbol(jpeg_finish_compress);
    loadSymbol(jpeg_destroy_compress);
    loadSymbol(jpeg_CreateDecompress);
    loadSymbol(jpeg_stdio_src);
    loadSymbol(jpeg_read_header);
    loadSymbol(jpeg_start_decompress);
    loadSymbol(jpeg_read_scanlines);
    loadSymbol(jpeg_finish_decompress);
    loadSymbol(jpeg_destroy_decompress);
    loadSymbol(jpeg_save_markers);
    loadSymbol(jpeg_has_multiple_scans);
    loadSymbol(jpeg_calc_output_dimensions);
    loadSymbol(jpeg_start_output);
    loadSymbol(jpeg_finish_output);
    loadSymbol(jpeg_input_complete);
    loadSymbol(jpeg_consume_input);

    #undef loadSymbol

  #else  // USE_SANDBOXING == 3

    #define ptr_jpeg_std_error              (jpegSandbox->inv_jpeg_std_error)
    #define ptr_jpeg_CreateCompress         (jpegSandbox->inv_jpeg_CreateCompress)
    #define ptr_jpeg_stdio_dest             (jpegSandbox->inv_jpeg_stdio_dest)
    #define ptr_jpeg_set_defaults           (jpegSandbox->inv_jpeg_set_defaults)
    #define ptr_jpeg_set_quality            (jpegSandbox->inv_jpeg_set_quality)
    #define ptr_jpeg_start_compress         (jpegSandbox->inv_jpeg_start_compress)
    #define ptr_jpeg_write_scanlines        (jpegSandbox->inv_jpeg_write_scanlines)
    #define ptr_jpeg_finish_compress        (jpegSandbox->inv_jpeg_finish_compress)
    #define ptr_jpeg_destroy_compress       (jpegSandbox->inv_jpeg_destroy_compress)
    #define ptr_jpeg_CreateDecompress       (jpegSandbox->inv_jpeg_CreateDecompress)
    #define ptr_jpeg_stdio_src              (jpegSandbox->inv_jpeg_stdio_src)
    #define ptr_jpeg_read_header            (jpegSandbox->inv_jpeg_read_header)
    #define ptr_jpeg_start_decompress       (jpegSandbox->inv_jpeg_start_decompress)
    #define ptr_jpeg_read_scanlines         (jpegSandbox->inv_jpeg_read_scanlines)
    #define ptr_jpeg_finish_decompress      (jpegSandbox->inv_jpeg_finish_decompress)
    #define ptr_jpeg_destroy_decompress     (jpegSandbox->inv_jpeg_destroy_decompress)
    #define ptr_jpeg_save_markers           (jpegSandbox->inv_jpeg_save_markers)
    #define ptr_jpeg_has_multiple_scans     (jpegSandbox->inv_jpeg_has_multiple_scans)
    #define ptr_jpeg_calc_output_dimensions (jpegSandbox->inv_jpeg_calc_output_dimensions)
    #define ptr_jpeg_start_output           (jpegSandbox->inv_jpeg_start_output)
    #define ptr_jpeg_finish_output          (jpegSandbox->inv_jpeg_finish_output)
    #define ptr_jpeg_input_complete         (jpegSandbox->inv_jpeg_input_complete)
    #define ptr_jpeg_consume_input          (jpegSandbox->inv_jpeg_consume_input)
    #define ptr_alloc_sarray                (jpegSandbox->inv_alloc_sarray_ps)

  #endif

    if(failed) { exit(1); }

    printf("Loaded symbols\n");

    if(additionalSetup != nullptr)
    {
      additionalSetup();
    }
  }, additionalSetup);
}

uintptr_t getUnsandboxedJpegPtr(uintptr_t uaddr)
{
  #if(USE_SANDBOXING == 2)
    return getUnsandboxedAddress(jpegSandbox, uaddr);
  #else
    return uaddr;
  #endif
}
uintptr_t getSandboxedJpegPtr(uintptr_t uaddr)
{
  #if(USE_SANDBOXING == 2)
    return getSandboxedAddress(jpegSandbox, uaddr);
  #else
    return uaddr;
  #endif
}
int isAddressInJpegSandboxMemoryOrNull(uintptr_t uaddr)
{
  #if(USE_SANDBOXING == 2)
    return isAddressInSandboxMemoryOrNull(jpegSandbox, uaddr);
  #else
    return 0;
  #endif
}
int isAddressInNonJpegSandboxMemoryOrNull(uintptr_t uaddr)
{
  #if(USE_SANDBOXING == 2)
    return isAddressInNonSandboxMemoryOrNull(jpegSandbox, uaddr);
  #else
    return 0;
  #endif
}
void* mallocInJpegSandbox(size_t size)
{
  #if(USE_SANDBOXING == 2)
    return mallocInSandbox(jpegSandbox, size);
  #elif(USE_SANDBOXING == 3)
    return jpegSandbox->mallocInSandbox(size);
  #else
    return malloc(size);
  #endif
}
void freeInJpegSandbox(void* ptr)
{
  #if(USE_SANDBOXING == 2)
    freeInSandbox(jpegSandbox, ptr);
  #elif(USE_SANDBOXING == 3)
    jpegSandbox->freeInSandbox(ptr);
  #else
    free(ptr);
  #endif
}


  //API stubs

  struct jpeg_error_mgr * d_jpeg_std_error(struct jpeg_error_mgr * err)
  {
    #ifdef PRINT_FUNCTION_LOGS
      MOZ_LOG(sJPEGLog, LogLevel::Debug, ("d_jpeg_std_error"));
    #endif
    //printf("Calling func d_jpeg_std_error\n");
    START_TIMER(d_jpeg_std_error);

#if(USE_SANDBOXING == 2)
    NaClSandbox_Thread* threadData = preFunctionCall(jpegSandbox, sizeof(err), 0 /* size of any arrays being pushed on the stack */);
    PUSH_PTR_TO_STACK(threadData, struct jpeg_error_mgr *, err);
    invokeFunctionCall(threadData, (void *)ptr_jpeg_std_error);
    struct jpeg_error_mgr * ret = (struct jpeg_error_mgr *)functionCallReturnPtr(threadData);
#elif(USE_SANDBOXING == 1 || USE_SANDBOXING == 3)
    struct jpeg_error_mgr * ret = ptr_jpeg_std_error(err);
#elif(USE_SANDBOXING == 0)
    struct jpeg_error_mgr * ret = jpeg_std_error(err);
#else
#error Missed case of USE_SANDBOXING
#endif

    END_TIMER(d_jpeg_std_error);
    return ret;
  }

  void d_jpeg_CreateCompress(j_compress_ptr cinfo, int version, size_t structsize)
  {
    #ifdef PRINT_FUNCTION_LOGS
      MOZ_LOG(sJPEGLog, LogLevel::Debug, ("d_jpeg_CreateCompress"));
    #endif
    //printf("Calling func d_jpeg_CreateCompress\n");
    START_TIMER(d_jpeg_CreateCompress);

#if(USE_SANDBOXING == 2)
    NaClSandbox_Thread* threadData = preFunctionCall(jpegSandbox, sizeof(cinfo) + sizeof(version) + sizeof(structsize), 0 /* size of any arrays being pushed on the stack */);
    PUSH_PTR_TO_STACK(threadData, j_compress_ptr, cinfo);
    PUSH_VAL_TO_STACK(threadData, int, version);
    PUSH_VAL_TO_STACK(threadData, size_t, structsize);
    invokeFunctionCall(threadData, (void *)ptr_jpeg_CreateCompress);
#elif(USE_SANDBOXING == 1 || USE_SANDBOXING == 3)
    ptr_jpeg_CreateCompress(cinfo, version, structsize);
#elif(USE_SANDBOXING == 0)
    jpeg_CreateCompress(cinfo, version, structsize);
#else
#error Missed case of USE_SANDBOXING
#endif

    END_TIMER(d_jpeg_CreateCompress);
  }

  void d_jpeg_stdio_dest(j_compress_ptr cinfo, FILE * outfile)
  {
    #ifdef PRINT_FUNCTION_LOGS
      MOZ_LOG(sJPEGLog, LogLevel::Debug, ("d_jpeg_stdio_dest"));
    #endif
    //printf("Calling func d_jpeg_stdio_dest\n");
    START_TIMER(d_jpeg_stdio_dest);

#if(USE_SANDBOXING == 2)
    NaClSandbox_Thread* threadData = preFunctionCall(jpegSandbox, sizeof(cinfo) + sizeof(outfile), 0 /* size of any arrays being pushed on the stack */);
    PUSH_PTR_TO_STACK(threadData, j_compress_ptr, cinfo);
    PUSH_PTR_TO_STACK(threadData, FILE *, outfile);
    invokeFunctionCall(threadData, (void *)ptr_jpeg_stdio_dest);
#elif(USE_SANDBOXING == 1 || USE_SANDBOXING == 3)
    ptr_jpeg_stdio_dest(cinfo, outfile);
#elif(USE_SANDBOXING == 0)
    jpeg_stdio_dest(cinfo, outfile);
#else
#error Missed case of USE_SANDBOXING
#endif

    END_TIMER(d_jpeg_stdio_dest);
  }

  void d_jpeg_set_defaults(j_compress_ptr cinfo)
  {
    #ifdef PRINT_FUNCTION_LOGS
      MOZ_LOG(sJPEGLog, LogLevel::Debug, ("d_jpeg_set_defaults"));
    #endif
    //printf("Calling func d_jpeg_set_defaults\n");
    START_TIMER(d_jpeg_set_defaults);

#if(USE_SANDBOXING == 2)
    NaClSandbox_Thread* threadData = preFunctionCall(jpegSandbox, sizeof(cinfo), 0 /* size of any arrays being pushed on the stack */);
    PUSH_PTR_TO_STACK(threadData, j_compress_ptr, cinfo);
    invokeFunctionCall(threadData, (void *)ptr_jpeg_set_defaults);
#elif(USE_SANDBOXING == 1 || USE_SANDBOXING == 3)
    ptr_jpeg_set_defaults(cinfo);
#elif(USE_SANDBOXING == 0)
    jpeg_set_defaults(cinfo);
#else
#error Missed case of USE_SANDBOXING
#endif

    END_TIMER(d_jpeg_set_defaults);
  }

  void d_jpeg_set_quality(j_compress_ptr cinfo, int quality, boolean force_baseline)
  {
    #ifdef PRINT_FUNCTION_LOGS
      MOZ_LOG(sJPEGLog, LogLevel::Debug, ("d_jpeg_set_quality"));
    #endif
    //printf("Calling func d_jpeg_set_quality\n");
    START_TIMER(d_jpeg_set_quality);

#if(USE_SANDBOXING == 2)
    NaClSandbox_Thread* threadData = preFunctionCall(jpegSandbox, sizeof(cinfo) + sizeof(quality) + sizeof(force_baseline), 0 /* size of any arrays being pushed on the stack */);
    PUSH_PTR_TO_STACK(threadData, j_compress_ptr, cinfo);
    PUSH_VAL_TO_STACK(threadData, int, quality);
    PUSH_VAL_TO_STACK(threadData, boolean, force_baseline);
    invokeFunctionCall(threadData, (void *)ptr_jpeg_set_quality);
#elif(USE_SANDBOXING == 1 || USE_SANDBOXING == 3)
    ptr_jpeg_set_quality(cinfo, quality, force_baseline);
#elif(USE_SANDBOXING == 0)
    jpeg_set_quality(cinfo, quality, force_baseline);
#else
#error Missed case of USE_SANDBOXING
#endif

    END_TIMER(d_jpeg_set_quality);
  }

  void d_jpeg_start_compress(j_compress_ptr cinfo, boolean write_all_tables)
  {
    #ifdef PRINT_FUNCTION_LOGS
      MOZ_LOG(sJPEGLog, LogLevel::Debug, ("d_jpeg_start_compress"));
    #endif
    //printf("Calling func d_jpeg_start_compress\n");
    START_TIMER(d_jpeg_start_compress);

#if(USE_SANDBOXING == 2)
    NaClSandbox_Thread* threadData = preFunctionCall(jpegSandbox, sizeof(cinfo) + sizeof(write_all_tables), 0 /* size of any arrays being pushed on the stack */);
    PUSH_PTR_TO_STACK(threadData, j_compress_ptr, cinfo);
    PUSH_VAL_TO_STACK(threadData, boolean, write_all_tables);
    invokeFunctionCall(threadData, (void *)ptr_jpeg_start_compress);
#elif(USE_SANDBOXING == 1 || USE_SANDBOXING == 3)
    ptr_jpeg_start_compress(cinfo, write_all_tables);
#elif(USE_SANDBOXING == 0)
    jpeg_start_compress(cinfo, write_all_tables);
#else
#error Missed case of USE_SANDBOXING
#endif

    END_TIMER(d_jpeg_start_compress);
  }

  JDIMENSION d_jpeg_write_scanlines(j_compress_ptr cinfo, JSAMPARRAY scanlines, JDIMENSION num_lines)
  {
    #ifdef PRINT_FUNCTION_LOGS
      MOZ_LOG(sJPEGLog, LogLevel::Debug, ("d_jpeg_write_scanlines"));
    #endif
    //printf("Calling func d_jpeg_write_scanlines\n");
    START_TIMER(d_jpeg_write_scanlines);

#if(USE_SANDBOXING == 2)
    NaClSandbox_Thread* threadData = preFunctionCall(jpegSandbox, sizeof(cinfo) + sizeof(scanlines) + sizeof(num_lines), 0 /* size of any arrays being pushed on the stack */);
    PUSH_PTR_TO_STACK(threadData, j_compress_ptr, cinfo);
    PUSH_PTR_TO_STACK(threadData, JSAMPARRAY, scanlines);
    PUSH_VAL_TO_STACK(threadData, JDIMENSION, num_lines);
    invokeFunctionCall(threadData, (void *)ptr_jpeg_write_scanlines);
    JDIMENSION ret = (JDIMENSION) functionCallReturnRawPrimitiveInt(threadData);
#elif(USE_SANDBOXING == 1 || USE_SANDBOXING == 3)
    JDIMENSION ret = ptr_jpeg_write_scanlines(cinfo, scanlines, num_lines);
#elif(USE_SANDBOXING == 0)
    JDIMENSION ret = jpeg_write_scanlines(cinfo, scanlines, num_lines);
#else
#error Missed case of USE_SANDBOXING
#endif

    END_TIMER(d_jpeg_write_scanlines);
    return ret;
  }
  void d_jpeg_finish_compress(j_compress_ptr cinfo)
  {
    #ifdef PRINT_FUNCTION_LOGS
      MOZ_LOG(sJPEGLog, LogLevel::Debug, ("d_jpeg_finish_compress"));
    #endif
    //printf("Calling func d_jpeg_finish_compress\n");
    START_TIMER(d_jpeg_finish_compress);

#if(USE_SANDBOXING == 2)
    NaClSandbox_Thread* threadData = preFunctionCall(jpegSandbox, sizeof(cinfo), 0 /* size of any arrays being pushed on the stack */);
    PUSH_PTR_TO_STACK(threadData, j_compress_ptr, cinfo);
    invokeFunctionCall(threadData, (void *)ptr_jpeg_finish_compress);
#elif(USE_SANDBOXING == 1 || USE_SANDBOXING == 3)
    ptr_jpeg_finish_compress(cinfo);
#elif(USE_SANDBOXING == 0)
    jpeg_finish_compress(cinfo);
#else
#error Missed case of USE_SANDBOXING
#endif

    END_TIMER(d_jpeg_finish_compress);
  }
  void d_jpeg_destroy_compress(j_compress_ptr cinfo)
  {
    #ifdef PRINT_FUNCTION_LOGS
      MOZ_LOG(sJPEGLog, LogLevel::Debug, ("d_jpeg_destroy_compress"));
    #endif
    //printf("Calling func d_jpeg_destroy_compress\n");
    START_TIMER(d_jpeg_destroy_compress);

#if(USE_SANDBOXING == 2)
    NaClSandbox_Thread* threadData = preFunctionCall(jpegSandbox, sizeof(cinfo), 0 /* size of any arrays being pushed on the stack */);
    PUSH_PTR_TO_STACK(threadData, j_compress_ptr, cinfo);
    invokeFunctionCall(threadData, (void *)ptr_jpeg_destroy_compress);
#elif(USE_SANDBOXING == 1 || USE_SANDBOXING == 3)
    ptr_jpeg_destroy_compress(cinfo);
#elif(USE_SANDBOXING == 0)
    jpeg_destroy_compress(cinfo);
#else
#error Missed case of USE_SANDBOXING
#endif

    END_TIMER(d_jpeg_destroy_compress);
  }
  void d_jpeg_CreateDecompress(j_decompress_ptr cinfo, int version, size_t structsize)
  {
    #ifdef PRINT_FUNCTION_LOGS
      MOZ_LOG(sJPEGLog, LogLevel::Debug, ("d_jpeg_CreateDecompress"));
    #endif
    //printf("Calling func d_jpeg_CreateDecompress\n");
    START_TIMER(d_jpeg_CreateDecompress);

#if(USE_SANDBOXING == 2)
    NaClSandbox_Thread* threadData = preFunctionCall(jpegSandbox, sizeof(cinfo) + sizeof(version) + sizeof(structsize), 0 /* size of any arrays being pushed on the stack */);
    PUSH_PTR_TO_STACK(threadData, j_decompress_ptr, cinfo);
    PUSH_VAL_TO_STACK(threadData, int, version);
    PUSH_VAL_TO_STACK(threadData, size_t, structsize);
    invokeFunctionCall(threadData, (void *)ptr_jpeg_CreateDecompress);
#elif(USE_SANDBOXING == 1 || USE_SANDBOXING == 3)
    ptr_jpeg_CreateDecompress(cinfo, version, structsize);
#elif(USE_SANDBOXING == 0)
    jpeg_CreateDecompress(cinfo, version, structsize);
#else
#error Missed case of USE_SANDBOXING
#endif

    END_TIMER(d_jpeg_CreateDecompress);
  }
  void d_jpeg_stdio_src(j_decompress_ptr cinfo, FILE * infile)
  {
    #ifdef PRINT_FUNCTION_LOGS
      MOZ_LOG(sJPEGLog, LogLevel::Debug, ("d_jpeg_stdio_src"));
    #endif
    //printf("Calling func d_jpeg_stdio_src\n");
    START_TIMER(d_jpeg_stdio_src);

#if(USE_SANDBOXING == 2)
    NaClSandbox_Thread* threadData = preFunctionCall(jpegSandbox, sizeof(cinfo) + sizeof(infile), 0 /* size of any arrays being pushed on the stack */);
    PUSH_PTR_TO_STACK(threadData, j_decompress_ptr, cinfo);
    PUSH_PTR_TO_STACK(threadData, FILE *, infile);
    invokeFunctionCall(threadData, (void *)ptr_jpeg_stdio_src);
#elif(USE_SANDBOXING == 1 || USE_SANDBOXING == 3)
    ptr_jpeg_stdio_src(cinfo, infile);
#elif(USE_SANDBOXING == 0)
    jpeg_stdio_src(cinfo, infile);
#else
#error Missed case of USE_SANDBOXING
#endif

    END_TIMER(d_jpeg_stdio_src);
  }
  int d_jpeg_read_header(j_decompress_ptr cinfo, boolean require_image)
  {
    #ifdef PRINT_FUNCTION_LOGS
      MOZ_LOG(sJPEGLog, LogLevel::Debug, ("d_jpeg_read_header"));
    #endif
    //printf("Calling func d_jpeg_read_header\n");
    START_TIMER(d_jpeg_read_header);

#if(USE_SANDBOXING == 2)
    NaClSandbox_Thread* threadData = preFunctionCall(jpegSandbox, sizeof(cinfo) + sizeof(require_image), 0 /* size of any arrays being pushed on the stack */);
    PUSH_PTR_TO_STACK(threadData, j_decompress_ptr, cinfo);
    PUSH_VAL_TO_STACK(threadData, boolean, require_image);
    invokeFunctionCall(threadData, (void *)ptr_jpeg_read_header);
    int ret = (int) functionCallReturnRawPrimitiveInt(threadData);
#elif(USE_SANDBOXING == 1 || USE_SANDBOXING == 3)
    int ret = ptr_jpeg_read_header(cinfo, require_image);
#elif(USE_SANDBOXING == 0)
    int ret = jpeg_read_header(cinfo, require_image);
#else
#error Missed case of USE_SANDBOXING
#endif

    END_TIMER(d_jpeg_read_header);
    return ret;
  }
  boolean d_jpeg_start_decompress(j_decompress_ptr cinfo)
  {
    #ifdef PRINT_FUNCTION_LOGS
      MOZ_LOG(sJPEGLog, LogLevel::Debug, ("d_jpeg_start_decompress"));
    #endif
    //printf("Calling func d_jpeg_start_decompress\n");
    START_TIMER(d_jpeg_start_decompress);

#if(USE_SANDBOXING == 2)
    NaClSandbox_Thread* threadData = preFunctionCall(jpegSandbox, sizeof(cinfo), 0 /* size of any arrays being pushed on the stack */);
    PUSH_PTR_TO_STACK(threadData, j_decompress_ptr, cinfo);
    invokeFunctionCall(threadData, (void *)ptr_jpeg_start_decompress);
    boolean ret = (boolean) functionCallReturnRawPrimitiveInt(threadData);
#elif(USE_SANDBOXING == 1 || USE_SANDBOXING == 3)
    boolean ret = ptr_jpeg_start_decompress(cinfo);
#elif(USE_SANDBOXING == 0)
    boolean ret = jpeg_start_decompress(cinfo);
#else
#error Missed case of USE_SANDBOXING
#endif

    END_TIMER(d_jpeg_start_decompress);
    return ret;
  }
  JDIMENSION d_jpeg_read_scanlines(j_decompress_ptr cinfo, JSAMPARRAY scanlines, JDIMENSION max_lines)
  {
    #ifdef PRINT_FUNCTION_LOGS
      MOZ_LOG(sJPEGLog, LogLevel::Debug, ("d_jpeg_read_scanlines"));
    #endif
    //printf("Calling func d_jpeg_read_scanlines\n");
    START_TIMER_CORE(d_jpeg_read_scanlines);

#if(USE_SANDBOXING == 2)
    NaClSandbox_Thread* threadData = preFunctionCall(jpegSandbox, sizeof(cinfo) + sizeof(scanlines) + sizeof(max_lines), 0 /* size of any arrays being pushed on the stack */);
    PUSH_PTR_TO_STACK(threadData, j_decompress_ptr, cinfo);
    PUSH_PTR_TO_STACK(threadData, JSAMPARRAY, scanlines);
    PUSH_VAL_TO_STACK(threadData, JDIMENSION, max_lines);
    invokeFunctionCall(threadData, (void *)ptr_jpeg_read_scanlines);
    JDIMENSION ret = (JDIMENSION) functionCallReturnRawPrimitiveInt(threadData);
#elif(USE_SANDBOXING == 1 || USE_SANDBOXING == 3)
    JDIMENSION ret = ptr_jpeg_read_scanlines(cinfo, scanlines, max_lines);
#elif(USE_SANDBOXING == 0)
    JDIMENSION ret = jpeg_read_scanlines(cinfo, scanlines, max_lines);
#else
#error Missed case of USE_SANDBOXING
#endif

    END_TIMER_CORE(d_jpeg_read_scanlines);
    return ret;
  }
  boolean d_jpeg_finish_decompress(j_decompress_ptr cinfo)
  {
    #ifdef PRINT_FUNCTION_LOGS
      MOZ_LOG(sJPEGLog, LogLevel::Debug, ("d_jpeg_finish_decompress"));
    #endif
    //printf("Calling func d_jpeg_finish_decompress\n");
    START_TIMER(d_jpeg_finish_decompress);

#if(USE_SANDBOXING == 2)
    NaClSandbox_Thread* threadData = preFunctionCall(jpegSandbox, sizeof(cinfo), 0 /* size of any arrays being pushed on the stack */);
    PUSH_PTR_TO_STACK(threadData, j_decompress_ptr, cinfo);
    invokeFunctionCall(threadData, (void *)ptr_jpeg_finish_decompress);
    boolean ret = (boolean) functionCallReturnRawPrimitiveInt(threadData);
#elif(USE_SANDBOXING == 1 || USE_SANDBOXING == 3)
    boolean ret = ptr_jpeg_finish_decompress(cinfo);
#elif(USE_SANDBOXING == 0)
    boolean ret = jpeg_finish_decompress(cinfo);
#else
#error Missed case of USE_SANDBOXING
#endif

    END_TIMER(d_jpeg_finish_decompress);
    return ret;
  }
  void d_jpeg_destroy_decompress(j_decompress_ptr cinfo)
  {
    #ifdef PRINT_FUNCTION_LOGS
      MOZ_LOG(sJPEGLog, LogLevel::Debug, ("d_jpeg_destroy_decompress"));
    #endif
    //printf("Calling func d_jpeg_destroy_decompress\n");
    START_TIMER(d_jpeg_destroy_decompress);

#if(USE_SANDBOXING == 2)
    NaClSandbox_Thread* threadData = preFunctionCall(jpegSandbox, sizeof(cinfo), 0 /* size of any arrays being pushed on the stack */);
    PUSH_PTR_TO_STACK(threadData, j_decompress_ptr, cinfo);
    invokeFunctionCall(threadData, (void *)ptr_jpeg_destroy_decompress);
#elif(USE_SANDBOXING == 1 || USE_SANDBOXING == 3)
    ptr_jpeg_destroy_decompress(cinfo);
#elif(USE_SANDBOXING == 0)
    jpeg_destroy_decompress(cinfo);
#else
#error Missed case of USE_SANDBOXING
#endif

    END_TIMER(d_jpeg_destroy_decompress);
  }
  void d_jpeg_save_markers (j_decompress_ptr cinfo, int marker_code, unsigned int length_limit)
  {
    #ifdef PRINT_FUNCTION_LOGS
      MOZ_LOG(sJPEGLog, LogLevel::Debug, ("d_jpeg_save_markers"));
    #endif
    //printf("Calling func d_jpeg_save_markers\n");
    START_TIMER(d_jpeg_save_markers);

#if(USE_SANDBOXING == 2)
    NaClSandbox_Thread* threadData = preFunctionCall(jpegSandbox, sizeof(cinfo) + sizeof(marker_code) + sizeof(length_limit), 0 /* size of any arrays being pushed on the stack */);
    PUSH_PTR_TO_STACK(threadData, j_decompress_ptr, cinfo);
    PUSH_VAL_TO_STACK(threadData, int, marker_code);
    PUSH_VAL_TO_STACK(threadData, unsigned int, length_limit);
    invokeFunctionCall(threadData, (void *)ptr_jpeg_save_markers);
#elif(USE_SANDBOXING == 1 || USE_SANDBOXING == 3)
    ptr_jpeg_save_markers(cinfo, marker_code, length_limit);
#elif(USE_SANDBOXING == 0)
    jpeg_save_markers(cinfo, marker_code, length_limit);
#else
#error Missed case of USE_SANDBOXING
#endif

    END_TIMER(d_jpeg_save_markers);
  }
  boolean d_jpeg_has_multiple_scans (j_decompress_ptr cinfo)
  {
    #ifdef PRINT_FUNCTION_LOGS
      MOZ_LOG(sJPEGLog, LogLevel::Debug, ("d_jpeg_has_multiple_scans"));
    #endif
    //printf("Calling func d_jpeg_has_multiple_scans\n");
    START_TIMER(d_jpeg_has_multiple_scans);

#if(USE_SANDBOXING == 2)
    NaClSandbox_Thread* threadData = preFunctionCall(jpegSandbox, sizeof(cinfo), 0 /* size of any arrays being pushed on the stack */);
    PUSH_PTR_TO_STACK(threadData, j_decompress_ptr, cinfo);
    invokeFunctionCall(threadData, (void *)ptr_jpeg_has_multiple_scans);
    boolean ret = (boolean) functionCallReturnRawPrimitiveInt(threadData);
#elif(USE_SANDBOXING == 1 || USE_SANDBOXING == 3)
    boolean ret = ptr_jpeg_has_multiple_scans(cinfo);
#elif(USE_SANDBOXING == 0)
    boolean ret = jpeg_has_multiple_scans(cinfo);
#else
#error Missed case of USE_SANDBOXING
#endif

    END_TIMER(d_jpeg_has_multiple_scans);
    return ret;
  }
  void d_jpeg_calc_output_dimensions (j_decompress_ptr cinfo)
  {
    #ifdef PRINT_FUNCTION_LOGS
      MOZ_LOG(sJPEGLog, LogLevel::Debug, ("d_jpeg_calc_output_dimensions"));
    #endif
    //printf("Calling func d_jpeg_calc_output_dimensions\n");
    START_TIMER(d_jpeg_calc_output_dimensions);

#if(USE_SANDBOXING == 2)
    NaClSandbox_Thread* threadData = preFunctionCall(jpegSandbox, sizeof(cinfo), 0 /* size of any arrays being pushed on the stack */);
    PUSH_PTR_TO_STACK(threadData, j_decompress_ptr, cinfo);
    invokeFunctionCall(threadData, (void *)ptr_jpeg_calc_output_dimensions);
#elif(USE_SANDBOXING == 1 || USE_SANDBOXING == 3)
    ptr_jpeg_calc_output_dimensions(cinfo);
#elif(USE_SANDBOXING == 0)
    jpeg_calc_output_dimensions(cinfo);
#else
#error Missed case of USE_SANDBOXING
#endif

    END_TIMER(d_jpeg_calc_output_dimensions);
  }
  boolean d_jpeg_start_output (j_decompress_ptr cinfo, int scan_number)
  {
    #ifdef PRINT_FUNCTION_LOGS
      MOZ_LOG(sJPEGLog, LogLevel::Debug, ("d_jpeg_start_output"));
    #endif
    //printf("Calling func d_jpeg_start_output\n");
    START_TIMER(d_jpeg_start_output);

#if(USE_SANDBOXING == 2)
    NaClSandbox_Thread* threadData = preFunctionCall(jpegSandbox, sizeof(cinfo) + sizeof(scan_number), 0 /* size of any arrays being pushed on the stack */);
    PUSH_PTR_TO_STACK(threadData, j_decompress_ptr, cinfo);
    PUSH_VAL_TO_STACK(threadData, int, scan_number);
    invokeFunctionCall(threadData, (void *)ptr_jpeg_start_output);
    boolean ret = (boolean) functionCallReturnRawPrimitiveInt(threadData);
#elif(USE_SANDBOXING == 1 || USE_SANDBOXING == 3)
    boolean ret = ptr_jpeg_start_output(cinfo, scan_number);
#elif(USE_SANDBOXING == 0)
    boolean ret = jpeg_start_output(cinfo, scan_number);
#else
#error Missed case of USE_SANDBOXING
#endif

    END_TIMER(d_jpeg_start_output);
    return ret;
  }
  boolean d_jpeg_finish_output (j_decompress_ptr cinfo)
  {
    #ifdef PRINT_FUNCTION_LOGS
      MOZ_LOG(sJPEGLog, LogLevel::Debug, ("d_jpeg_finish_output"));
    #endif
    //printf("Calling func d_jpeg_finish_output\n");
    START_TIMER(d_jpeg_finish_output);

#if(USE_SANDBOXING == 2)
    NaClSandbox_Thread* threadData = preFunctionCall(jpegSandbox, sizeof(cinfo), 0 /* size of any arrays being pushed on the stack */);
    PUSH_PTR_TO_STACK(threadData, j_decompress_ptr, cinfo);
    invokeFunctionCall(threadData, (void *)ptr_jpeg_finish_output);
    boolean ret = (boolean) functionCallReturnRawPrimitiveInt(threadData);
#elif(USE_SANDBOXING == 1 || USE_SANDBOXING == 3)
    boolean ret = ptr_jpeg_finish_output(cinfo);
#elif(USE_SANDBOXING == 0)
    boolean ret = jpeg_finish_output(cinfo);
#else
#error Missed case of USE_SANDBOXING
#endif

    END_TIMER(d_jpeg_finish_output);
    return ret;
  }
  boolean d_jpeg_input_complete (j_decompress_ptr cinfo)
  {
    #ifdef PRINT_FUNCTION_LOGS
      MOZ_LOG(sJPEGLog, LogLevel::Debug, ("d_jpeg_input_complete"));
    #endif
    //printf("Calling func d_jpeg_input_complete\n");
    START_TIMER(d_jpeg_input_complete);

#if(USE_SANDBOXING == 2)
    NaClSandbox_Thread* threadData = preFunctionCall(jpegSandbox, sizeof(cinfo), 0 /* size of any arrays being pushed on the stack */);
    PUSH_PTR_TO_STACK(threadData, j_decompress_ptr, cinfo);
    invokeFunctionCall(threadData, (void *)ptr_jpeg_input_complete);
    boolean ret = (boolean) functionCallReturnRawPrimitiveInt(threadData);
#elif(USE_SANDBOXING == 1 || USE_SANDBOXING == 3)
    boolean ret = ptr_jpeg_input_complete(cinfo);
#elif(USE_SANDBOXING == 0)
    boolean ret = jpeg_input_complete(cinfo);
#else
#error Missed case of USE_SANDBOXING
#endif

    END_TIMER(d_jpeg_input_complete);
    return ret;
  }
  int d_jpeg_consume_input (j_decompress_ptr cinfo)
  {
    #ifdef PRINT_FUNCTION_LOGS
      MOZ_LOG(sJPEGLog, LogLevel::Debug, ("d_jpeg_consume_input"));
    #endif
    //printf("Calling func d_jpeg_consume_input\n");
    START_TIMER(d_jpeg_consume_input);

#if(USE_SANDBOXING == 2)
    NaClSandbox_Thread* threadData = preFunctionCall(jpegSandbox, sizeof(cinfo), 0 /* size of any arrays being pushed on the stack */);
    PUSH_PTR_TO_STACK(threadData, j_decompress_ptr, cinfo);
    invokeFunctionCall(threadData, (void *)ptr_jpeg_consume_input);
    int ret = (int) functionCallReturnRawPrimitiveInt(threadData);
#elif(USE_SANDBOXING == 1 || USE_SANDBOXING == 3)
    int ret = ptr_jpeg_consume_input(cinfo);
#elif(USE_SANDBOXING == 0)
    int ret = jpeg_consume_input(cinfo);
#else
#error Missed case of USE_SANDBOXING
#endif

    END_TIMER(d_jpeg_consume_input);
    return ret;
  }

  //Fn pointer calls
  JSAMPARRAY d_alloc_sarray(void* alloc_sarray, j_common_ptr cinfo, int pool_id, JDIMENSION samplesperrow, JDIMENSION numrows)
  {
    #ifdef PRINT_FUNCTION_LOGS
      MOZ_LOG(sJPEGLog, LogLevel::Debug, ("d_alloc_sarray"));
    #endif
    //printf("Calling func d_alloc_sarray\n");
    START_TIMER(d_alloc_sarray);

#if(USE_SANDBOXING == 2)
    NaClSandbox_Thread* threadData = preFunctionCall(jpegSandbox, sizeof(cinfo) + sizeof(pool_id) + sizeof(samplesperrow) + sizeof(numrows), 0 /* size of any arrays being pushed on the stack */);
    PUSH_PTR_TO_STACK(threadData, j_common_ptr, cinfo);
    PUSH_VAL_TO_STACK(threadData, int, pool_id);
    PUSH_VAL_TO_STACK(threadData, JDIMENSION, samplesperrow);
    PUSH_VAL_TO_STACK(threadData, JDIMENSION, numrows);
    invokeFunctionCall(threadData, alloc_sarray);
    JSAMPARRAY ret = (JSAMPARRAY)functionCallReturnPtr(threadData);
#elif(USE_SANDBOXING == 1)
    typedef JSAMPARRAY (*t_alloc_sarray)(j_common_ptr, int, JDIMENSION, JDIMENSION);
    t_alloc_sarray ptr_alloc_sarray = (t_alloc_sarray) alloc_sarray;
    JSAMPARRAY ret = ptr_alloc_sarray(cinfo, pool_id, samplesperrow, numrows);
#elif(USE_SANDBOXING == 3)
    JSAMPARRAY ret = ptr_alloc_sarray(cinfo, pool_id, samplesperrow, numrows);
#elif(USE_SANDBOXING == 0)
    typedef JSAMPARRAY (*t_alloc_sarray)(j_common_ptr, int, JDIMENSION, JDIMENSION);
    t_alloc_sarray ptr_alloc_sarray = (t_alloc_sarray) alloc_sarray;
    JSAMPARRAY ret = ptr_alloc_sarray(cinfo, pool_id, samplesperrow, numrows);
#else
#error Missed case of USE_SANDBOXING
#endif

    END_TIMER(d_alloc_sarray);
    return ret;
  }

  void d_format_message(void* format_message, j_common_ptr cinfo, char *buffer)
  {
    #ifdef PRINT_FUNCTION_LOGS
      MOZ_LOG(sJPEGLog, LogLevel::Debug, ("d_format_message"));
    #endif
    //printf("Calling func d_format_message\n");
    START_TIMER(d_format_message);

#if(USE_SANDBOXING == 2)
    NaClSandbox_Thread* threadData = preFunctionCall(jpegSandbox, sizeof(cinfo) + sizeof(buffer), 0 /* size of any arrays being pushed on the stack */);
    PUSH_PTR_TO_STACK(threadData, j_common_ptr, cinfo);
    PUSH_PTR_TO_STACK(threadData, char *, buffer);
    invokeFunctionCall(threadData, format_message);
#elif(USE_SANDBOXING == 1 || USE_SANDBOXING == 3)
    typedef void (*t_format_message)(j_common_ptr, char *);
    t_format_message ptr_format_message = (t_format_message) format_message;
    ptr_format_message(cinfo, buffer);
#elif(USE_SANDBOXING == 0)
    typedef void (*t_format_message)(j_common_ptr, char *);
    t_format_message ptr_format_message = (t_format_message) format_message;
    ptr_format_message(cinfo, buffer);
#else
#error Missed case of USE_SANDBOXING
#endif

    END_TIMER(d_format_message);
  }

#if(USE_SANDBOXING == 2)
  SANDBOX_CALLBACK void my_error_exit_stub(uintptr_t sandboxPtr)
#else
  void my_error_exit_stub(j_common_ptr cinfo)
#endif
  {
    #ifdef PRINT_FUNCTION_LOGS
      MOZ_LOG(sJPEGLog, LogLevel::Debug, ("my_error_exit_stub"));
    #endif
    END_TIMER(my_error_exit_stub);
    //printf("Callback my_error_exit_stub\n");

#if(USE_SANDBOXING == 2)
    NaClSandbox* sandboxC = (NaClSandbox*) sandboxPtr;
    NaClSandbox_Thread* threadData = callbackParamsBegin(sandboxC);
    j_common_ptr cinfo = COMPLETELY_UNTRUSTED_CALLBACK_PTR_PARAM(threadData, j_common_ptr);

    //We should not assume anything about - need to have some sort of validation here
#elif(USE_SANDBOXING == 1 || USE_SANDBOXING == 3)
#elif(USE_SANDBOXING == 0)
#else
#error Missed case of USE_SANDBOXING
#endif

    cb_my_error_exit(cinfo);
    START_TIMER(my_error_exit_stub);
  }

#if(USE_SANDBOXING == 2)
  SANDBOX_CALLBACK void init_source_stub(uintptr_t sandboxPtr)
#else
  void init_source_stub(j_decompress_ptr jd)
#endif
  {
    #ifdef PRINT_FUNCTION_LOGS
      MOZ_LOG(sJPEGLog, LogLevel::Debug, ("init_source_stub"));
    #endif
    END_TIMER(init_source_stub);
    //printf("Callback init_source_stub\n");

#if(USE_SANDBOXING == 2)
    NaClSandbox* sandboxC = (NaClSandbox*) sandboxPtr;
    NaClSandbox_Thread* threadData = callbackParamsBegin(sandboxC);
    j_decompress_ptr jd = COMPLETELY_UNTRUSTED_CALLBACK_PTR_PARAM(threadData, j_decompress_ptr);

    //We should not assume anything about - need to have some sort of validation here
#elif(USE_SANDBOXING == 1 || USE_SANDBOXING == 3)
#elif(USE_SANDBOXING == 0)
#else
#error Missed case of USE_SANDBOXING
#endif

    cb_init_source(jd);
    START_TIMER(init_source_stub);
  }

#if(USE_SANDBOXING == 2)
  SANDBOX_CALLBACK void skip_input_data_stub(uintptr_t sandboxPtr)
#else
  void skip_input_data_stub(j_decompress_ptr jd, long num_bytes)
#endif
  {
    #ifdef PRINT_FUNCTION_LOGS
      MOZ_LOG(sJPEGLog, LogLevel::Debug, ("skip_input_data_stub"));
    #endif
    END_TIMER(skip_input_data_stub);
    //printf("Callback skip_input_data_stub\n");

#if(USE_SANDBOXING == 2)
    NaClSandbox* sandboxC = (NaClSandbox*) sandboxPtr;
    NaClSandbox_Thread* threadData = callbackParamsBegin(sandboxC);
    j_decompress_ptr jd = COMPLETELY_UNTRUSTED_CALLBACK_PTR_PARAM(threadData, j_decompress_ptr);
    long num_bytes = COMPLETELY_UNTRUSTED_CALLBACK_STACK_PARAM(threadData, long);

    //We should not assume anything about - need to have some sort of validation here
#elif(USE_SANDBOXING == 1 || USE_SANDBOXING == 3)
#elif(USE_SANDBOXING == 0)
#else
#error Missed case of USE_SANDBOXING
#endif

    cb_skip_input_data(jd, num_bytes);
    START_TIMER(skip_input_data_stub);
  }

#if(USE_SANDBOXING == 2)
  SANDBOX_CALLBACK boolean fill_input_buffer_stub(uintptr_t sandboxPtr)
#else
  boolean fill_input_buffer_stub(j_decompress_ptr jd)
#endif
  {
    #ifdef PRINT_FUNCTION_LOGS
      MOZ_LOG(sJPEGLog, LogLevel::Debug, ("fill_input_buffer_stub"));
    #endif
    END_TIMER(fill_input_buffer_stub);
    //printf("Callback fill_input_buffer_stub\n");

#if(USE_SANDBOXING == 2)
    NaClSandbox* sandboxC = (NaClSandbox*) sandboxPtr;
    NaClSandbox_Thread* threadData = callbackParamsBegin(sandboxC);
    j_decompress_ptr jd = COMPLETELY_UNTRUSTED_CALLBACK_PTR_PARAM(threadData, j_decompress_ptr);

    //We should not assume anything about - need to have some sort of validation here
#elif(USE_SANDBOXING == 1 || USE_SANDBOXING == 3)
#elif(USE_SANDBOXING == 0)
#else
#error Missed case of USE_SANDBOXING
#endif

    boolean ret = cb_fill_input_buffer(jd);
    START_TIMER(fill_input_buffer_stub);
    return ret;
  }

#if(USE_SANDBOXING == 2)
  SANDBOX_CALLBACK void term_source_stub(uintptr_t sandboxPtr)
#else
  void term_source_stub(j_decompress_ptr jd)
#endif
  {
    #ifdef PRINT_FUNCTION_LOGS
      MOZ_LOG(sJPEGLog, LogLevel::Debug, ("term_source_stub"));
    #endif
    END_TIMER(term_source_stub);
    //printf("Callback term_source_stub\n");

#if(USE_SANDBOXING == 2)
    NaClSandbox* sandboxC = (NaClSandbox*) sandboxPtr;
    NaClSandbox_Thread* threadData = callbackParamsBegin(sandboxC);
    j_decompress_ptr jd = COMPLETELY_UNTRUSTED_CALLBACK_PTR_PARAM(threadData, j_decompress_ptr);

    //We should not assume anything about - need to have some sort of validation here
#elif(USE_SANDBOXING == 1 || USE_SANDBOXING == 3)
#elif(USE_SANDBOXING == 0)
#else
#error Missed case of USE_SANDBOXING
#endif

    cb_term_source(jd);
    START_TIMER(term_source_stub);
  }

#if(USE_SANDBOXING == 2)
  SANDBOX_CALLBACK boolean jpeg_resync_to_restart_stub(uintptr_t sandboxPtr)
#else
  boolean jpeg_resync_to_restart_stub(j_decompress_ptr jd, int desired)
#endif
  {
    #ifdef PRINT_FUNCTION_LOGS
      MOZ_LOG(sJPEGLog, LogLevel::Debug, ("jpeg_resync_to_restart_stub"));
    #endif
    END_TIMER(jpeg_resync_to_restart_stub);
    //printf("Callback jpeg_resync_to_restart_stub\n");

#if(USE_SANDBOXING == 2)
    NaClSandbox* sandboxC = (NaClSandbox*) sandboxPtr;
    NaClSandbox_Thread* threadData = callbackParamsBegin(sandboxC);
    j_decompress_ptr jd = COMPLETELY_UNTRUSTED_CALLBACK_PTR_PARAM(threadData, j_decompress_ptr);
    int desired = COMPLETELY_UNTRUSTED_CALLBACK_STACK_PARAM(threadData, int);

    //We should not assume anything about - need to have some sort of validation here
#elif(USE_SANDBOXING == 1 || USE_SANDBOXING == 3)
#elif(USE_SANDBOXING == 0)
#else
#error Missed case of USE_SANDBOXING
#endif

    boolean ret = cb_jpeg_resync_to_restart(jd, desired);
    START_TIMER(jpeg_resync_to_restart_stub);
    return ret;
  }

  t_my_error_exit d_my_error_exit(t_my_error_exit callback)
  {
    #ifdef PRINT_FUNCTION_LOGS
      MOZ_LOG(sJPEGLog, LogLevel::Debug, ("d_my_error_exit"));
    #endif
    cb_my_error_exit = callback;

#if(USE_SANDBOXING == 2)
    uintptr_t registeredCallback = registerSandboxCallback(jpegSandbox, MY_ERROR_EXIT_CALLBACK_SLOT, (uintptr_t) my_error_exit_stub);
    if(registeredCallback == 0)
    {
        printf("Failed in registering the error handler my_error_exit");
    }
    return (t_my_error_exit) registeredCallback;
#elif(USE_SANDBOXING == 1 || USE_SANDBOXING == 0)
    return my_error_exit_stub;
#elif(USE_SANDBOXING == 3)
    if(my_error_exit_stub_reg == nullptr)
      my_error_exit_stub_reg = jpegSandbox->registerCallback(my_error_exit_stub, (void*) nullptr);
    return my_error_exit_stub_reg;
#else
#error Missed case of USE_SANDBOXING
#endif
  }

  t_init_source d_init_source(t_init_source callback)
  {
    #ifdef PRINT_FUNCTION_LOGS
      MOZ_LOG(sJPEGLog, LogLevel::Debug, ("d_init_source"));
    #endif
    cb_init_source = callback;

#if(USE_SANDBOXING == 2)
    uintptr_t registeredCallback = registerSandboxCallback(jpegSandbox, INIT_SOURCE_SLOT, (uintptr_t) init_source_stub);
    if(registeredCallback == 0)
    {
        printf("Failed in registering the error handler init_source");
    }
    return (t_init_source) registeredCallback;
#elif(USE_SANDBOXING == 1 || USE_SANDBOXING == 0)
    return init_source_stub;
#elif(USE_SANDBOXING == 3)
    if(init_source_stub_reg == nullptr)
      init_source_stub_reg = jpegSandbox->registerCallback(init_source_stub, (void*) nullptr);
    return init_source_stub_reg;
#else
#error Missed case of USE_SANDBOXING
#endif
  }

  t_skip_input_data d_skip_input_data(t_skip_input_data callback)
  {
    #ifdef PRINT_FUNCTION_LOGS
      MOZ_LOG(sJPEGLog, LogLevel::Debug, ("d_skip_input_data"));
    #endif
    cb_skip_input_data = callback;

#if(USE_SANDBOXING == 2)
    uintptr_t registeredCallback = registerSandboxCallback(jpegSandbox, FILL_INPUT_BUFFER_SLOT, (uintptr_t) skip_input_data_stub);
    if(registeredCallback == 0)
    {
        printf("Failed in registering the error handler skip_input_data");
    }
    return (t_skip_input_data) registeredCallback;
#elif(USE_SANDBOXING == 1 || USE_SANDBOXING == 0)
    return skip_input_data_stub;
#elif(USE_SANDBOXING == 3)
    if(skip_input_data_stub_reg == nullptr)
      skip_input_data_stub_reg = jpegSandbox->registerCallback(skip_input_data_stub, (void*) nullptr);
    return skip_input_data_stub_reg;
#else
#error Missed case of USE_SANDBOXING
#endif
  }

  t_fill_input_buffer d_fill_input_buffer(t_fill_input_buffer callback)
  {
    #ifdef PRINT_FUNCTION_LOGS
      MOZ_LOG(sJPEGLog, LogLevel::Debug, ("d_fill_input_buffer"));
    #endif
    cb_fill_input_buffer = callback;

#if(USE_SANDBOXING == 2)
    uintptr_t registeredCallback = registerSandboxCallback(jpegSandbox, SKIP_INPUT_DATA_SLOT, (uintptr_t) fill_input_buffer_stub);
    if(registeredCallback == 0)
    {
        printf("Failed in registering the error handler fill_input_buffer");
    }
    return (t_fill_input_buffer) registeredCallback;
#elif(USE_SANDBOXING == 1 || USE_SANDBOXING == 0)
    return fill_input_buffer_stub;
#elif(USE_SANDBOXING == 3)
    if(fill_input_buffer_stub_reg == nullptr)
      fill_input_buffer_stub_reg = jpegSandbox->registerCallback(fill_input_buffer_stub, (void*) nullptr);
    return fill_input_buffer_stub_reg;
#else
#error Missed case of USE_SANDBOXING
#endif
  }

  t_term_source d_term_source(t_term_source callback)
  {
    #ifdef PRINT_FUNCTION_LOGS
      MOZ_LOG(sJPEGLog, LogLevel::Debug, ("d_term_source"));
    #endif
    cb_term_source = callback;

#if(USE_SANDBOXING == 2)
    uintptr_t registeredCallback = registerSandboxCallback(jpegSandbox, JPEG_RESYNC_TO_RESTART_SLOT, (uintptr_t) term_source_stub);
    if(registeredCallback == 0)
    {
        printf("Failed in registering the error handler term_source");
    }
    return (t_term_source) registeredCallback;
#elif(USE_SANDBOXING == 1 || USE_SANDBOXING == 0)
    return term_source_stub;
#elif(USE_SANDBOXING == 3)
    if(term_source_stub_reg == nullptr)
      term_source_stub_reg = jpegSandbox->registerCallback(term_source_stub, (void*) nullptr);
    return term_source_stub_reg;
#else
#error Missed case of USE_SANDBOXING
#endif
  }

  t_jpeg_resync_to_restart d_jpeg_resync_to_restart(t_jpeg_resync_to_restart callback)
  {
    #ifdef PRINT_FUNCTION_LOGS
      MOZ_LOG(sJPEGLog, LogLevel::Debug, ("d_jpeg_resync_to_restart"));
    #endif
    cb_jpeg_resync_to_restart = callback;

#if(USE_SANDBOXING == 2)
    uintptr_t registeredCallback = registerSandboxCallback(jpegSandbox, TERM_SOURCE_SLOT, (uintptr_t) jpeg_resync_to_restart_stub);
    if(registeredCallback == 0)
    {
        printf("Failed in registering the error handler jpeg_resync_to_restart");
    }
    return (t_jpeg_resync_to_restart) registeredCallback;
#elif(USE_SANDBOXING == 1 || USE_SANDBOXING == 0)
    return jpeg_resync_to_restart_stub;
#elif(USE_SANDBOXING == 3)
    if(jpeg_resync_to_restart_stub_reg == nullptr)
      jpeg_resync_to_restart_stub_reg = jpegSandbox->registerCallback(jpeg_resync_to_restart_stub, (void*) nullptr);
    return jpeg_resync_to_restart_stub_reg;
#else
#error Missed case of USE_SANDBOXING
#endif
  }
