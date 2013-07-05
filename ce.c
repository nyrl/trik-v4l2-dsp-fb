#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <xdc/std.h>
#include <xdc/runtime/Diags.h>
#include <ti/sdo/ce/CERuntime.h>

#include <trik/vidtranscode_resample/trik_vidtranscode_resample.h>

#include "internal/ce.h"


#warning Check BUFALIGN usage!
#ifndef BUFALIGN
#define BUFALIGN 128
#endif

#define ALIGN_UP(v, a) ((((v)+(a)-1)/(a))*(a))


static int do_memoryAlloc(CodecEngine* _ce, size_t _srcBufferSize, size_t _dstBufferSize)
{
  memset(&_ce->m_allocParams, 0, sizeof(_ce->m_allocParams));
  _ce->m_allocParams.type = Memory_CONTIGPOOL;
  _ce->m_allocParams.flags = Memory_NONCACHED;
  _ce->m_allocParams.align = BUFALIGN;
  _ce->m_allocParams.seg = 0;

  _ce->m_srcBufferSize = ALIGN_UP(_srcBufferSize, BUFALIGN);
  if ((_ce->m_srcBuffer = Memory_alloc(_ce->m_srcBufferSize, &_ce->m_allocParams)) == NULL)
  {
    fprintf(stderr, "Memory_alloc(src, %zu) failed\n", _ce->m_srcBufferSize);
    _ce->m_srcBufferSize = 0;
    return ENOMEM;
  }

  _ce->m_dstBufferSize = ALIGN_UP(_dstBufferSize, BUFALIGN);
  if ((_ce->m_dstBuffer = Memory_alloc(_ce->m_dstBufferSize, &_ce->m_allocParams)) == NULL)
  {
    fprintf(stderr, "Memory_alloc(dst, %zu) failed\n", _ce->m_dstBufferSize);
    _ce->m_dstBufferSize = 0;
    Memory_free(_ce->m_srcBuffer, _ce->m_srcBufferSize, &_ce->m_allocParams);
    _ce->m_srcBuffer = NULL;
    _ce->m_srcBufferSize = 0;
    return ENOMEM;
  }

  return 0;
}

static int do_memoryFree(CodecEngine* _ce)
{
  if (_ce->m_dstBuffer != NULL)
  {
    Memory_free(_ce->m_dstBuffer, _ce->m_dstBufferSize, &_ce->m_allocParams);
    _ce->m_dstBuffer = NULL;
    _ce->m_dstBufferSize = 0;
  }

  if (_ce->m_srcBuffer != NULL)
  {
    Memory_free(_ce->m_srcBuffer, _ce->m_srcBufferSize, &_ce->m_allocParams);
    _ce->m_srcBuffer = NULL;
    _ce->m_srcBufferSize = 0;
  }

  return 0;
}

static int do_setupCodec(CodecEngine* _ce, const char* _codecName)
{
  if (_codecName == NULL)
    return EINVAL;

#warning Ignoring video format for now!
  char* codec = strdup(_codecName);
  if ((_ce->m_vidtranscodeHandle = VIDTRANSCODE_create(_ce->m_handle, codec, NULL)) == NULL)
  {
    free(codec);
    fprintf(stderr, "VIDTRANSCODE_create(%s) failed\n", _codecName);
    return EBADRQC;
  }
  free(codec);

  return 0;
}

static int do_releaseCodec(CodecEngine* _ce)
{
  if (_ce->m_vidtranscodeHandle != NULL)
    VIDTRANSCODE_delete(_ce->m_vidtranscodeHandle);
  _ce->m_vidtranscodeHandle = NULL;

  return 0;
}

static int do_transcodeFrame(CodecEngine* _ce,
                             const void* _srcFramePtr, size_t _srcFrameSize,
                             void* _dstFramePtr, size_t _dstFrameSize, size_t* _dstFrameUsed)
{
  if (_ce->m_srcBuffer == NULL || _ce->m_dstBuffer == NULL)
    return ENOTCONN;
  if (_srcFramePtr == NULL || _dstFramePtr == NULL)
    return EINVAL;
  if (_srcFrameSize > _ce->m_srcBufferSize || _dstFrameSize > _ce->m_dstBufferSize)
    return ENOSPC;


  VIDTRANSCODE_InArgs tcInArgs;
  memset(&tcInArgs, 0, sizeof(tcInArgs));
  tcInArgs.size = sizeof(tcInArgs);
  tcInArgs.numBytes = _srcFrameSize;
  tcInArgs.inputID = 0;

  VIDTRANSCODE_OutArgs tcOutArgs;
  memset(&tcOutArgs,    0, sizeof(tcOutArgs));
  tcOutArgs.size = sizeof(tcOutArgs);

  XDM1_BufDesc tcInBufDesc;
  memset(&tcInBufDesc,  0, sizeof(tcInBufDesc));
  tcInBufDesc.numBufs = 1;
  tcInBufDesc.descs[0].buf = _ce->m_srcBuffer;
  tcInBufDesc.descs[0].bufSize = _srcFrameSize;

  XDM_BufDesc tcOutBufDesc;
  memset(&tcOutBufDesc, 0, sizeof(tcOutBufDesc));
  XDAS_Int8* tcOutBufDesc_bufs[1];
  XDAS_Int32 tcOutBufDesc_bufSizes[1];
  tcOutBufDesc.numBufs = 1;
  tcOutBufDesc.bufs = tcOutBufDesc_bufs;
  tcOutBufDesc.bufs[0] = _ce->m_dstBuffer;
  tcOutBufDesc.bufSizes = tcOutBufDesc_bufSizes;
  tcOutBufDesc.bufSizes[0] = _dstFrameSize;

  memcpy(_ce->m_srcBuffer, _srcFramePtr, _srcFrameSize);

  Memory_cacheWbInv(_ce->m_srcBuffer, _ce->m_srcBufferSize); // invalidate *whole* cache, not only written portion, just in case
  Memory_cacheInv(_ce->m_dstBuffer, _ce->m_dstBufferSize); // invalidate *whole* cache, not only expected portion, just in case

  XDAS_Int32 processResult = VIDTRANSCODE_process(_ce->m_vidtranscodeHandle, &tcInBufDesc, &tcOutBufDesc, &tcInArgs, &tcOutArgs);
  if (processResult != IVIDTRANSCODE_EOK)
  {
    fprintf(stderr, "VIDTRANSCODE_process(%zu -> %zu) failed: %"PRIi32"/%"PRIi32"\n",
            _srcFrameSize, _dstFrameSize, processResult, tcOutArgs.extendedError);
    return EILSEQ;
  }

  if (XDM_ISACCESSMODE_WRITE(tcOutArgs.encodedBuf[0].accessMask))
    Memory_cacheWb(_ce->m_dstBuffer, _ce->m_dstBufferSize); // write-back *whole* cache, not only modified portion, just in case

  if (tcOutArgs.encodedBuf[0].bufSize > _dstFrameSize)
  {
    *_dstFrameUsed = _dstFrameSize;
    fprintf(stderr, "VIDTRANSCODE_process(%zu -> %zu) returned too large buffer %zu, truncated\n",
            _srcFrameSize, _dstFrameSize, *_dstFrameUsed);
  }
  else if (tcOutArgs.encodedBuf[0].bufSize < 0)
  {
    *_dstFrameUsed = 0;
    fprintf(stderr, "VIDTRANSCODE_process(%zu -> %zu) returned negative buffer size\n",
            _srcFrameSize, _dstFrameSize);
  }
  else
    *_dstFrameUsed = tcOutArgs.encodedBuf[0].bufSize;

  memcpy(_dstFramePtr, _ce->m_dstBuffer, *_dstFrameUsed);

  return 0;
}




int codecEngineInit(bool _verbose)
{
  CERuntime_init(); /* init Codec Engine */

  if (_verbose)
  {
    Diags_setMask("xdc.runtime.Main+EX1234567");
    Diags_setMask(Engine_MODNAME"+EX1234567");
  }

  return 0;
}

int codecEngineFini()
{
  return 0;
}


int codecEngineOpen(CodecEngine* _ce, const CodecEngineConfig* _config)
{
  if (_ce == NULL || _config == NULL)
    return EINVAL;

  if (_ce->m_handle != NULL)
    return EALREADY;

  Engine_Error ceError;
  Engine_Desc desc;
  Engine_initDesc(&desc);
  desc.name = "dsp-server";
  desc.remoteName = strdup(_config->m_serverPath);
  errno = 0;

  ceError = Engine_add(&desc);
  if (ceError != Engine_EOK)
  {
    free(desc.remoteName);
    fprintf(stderr, "Engine_add(%s) failed: %d/%"PRIi32"\n", _config->m_serverPath, errno, ceError);
    return ENOMEM;
  }
  free(desc.remoteName);

  if ((_ce->m_handle = Engine_open("dsp-server", NULL, &ceError)) == NULL)
  {
    fprintf(stderr, "Engine_open(%s) failed: %d/%"PRIi32"\n", _config->m_serverPath, errno, ceError);
    return ENOMEM;
  }

  return 0;
}

int codecEngineClose(CodecEngine* _ce)
{
  if (_ce == NULL)
    return EINVAL;

  if (_ce->m_handle == NULL)
    return EALREADY;

  Engine_close(_ce->m_handle);
  _ce->m_handle = NULL;

  return 0;
}


int codecEngineStart(CodecEngine* _ce, const CodecEngineConfig* _config,
                     size_t _srcWidth, size_t _srcHeight,
                     size_t _srcLineLength, size_t _srcImageSize, uint32_t _srcFormat,
                     size_t _dstWidth, size_t _dstHeight,
                     size_t _dstLineLength, size_t _dstImageSize, uint32_t _dstFormat)
{
  int res;

  if (_ce == NULL || _config == NULL)
    return EINVAL;

  if (_ce->m_handle == NULL)
    return ENOTCONN;

  if ((res = do_memoryAlloc(_ce, _srcImageSize, _dstImageSize)) != 0)
    return res;

  if ((res = do_setupCodec(_ce, _config->m_codecName)) != 0)
  {
    do_memoryFree(_ce);
    return res;
  }


#if 1
  printf("Codec engine start: %zux%zu to %zux%zu\n", _srcWidth, _srcHeight, _dstWidth, _dstHeight);
#endif
#warning TODO configure vidtranscode

  return 0;
}

int codecEngineStop(CodecEngine* _ce)
{
  if (_ce == NULL)
    return EINVAL;

  if (_ce->m_handle == NULL)
    return ENOTCONN;

  do_releaseCodec(_ce);
  do_memoryFree(_ce);

  return 0;
}

int codecEngineTranscodeFrame(CodecEngine* _ce,
                              const void* _srcFramePtr, size_t _srcFrameSize,
                              void* _dstFramePtr, size_t _dstFrameSize, size_t* _dstFrameUsed)
{
  if (_ce == NULL)
    return EINVAL;

  if (_ce->m_handle == NULL)
    return ENOTCONN;

  return do_transcodeFrame(_ce, _srcFramePtr, _srcFrameSize, _dstFramePtr, _dstFrameSize, _dstFrameUsed);
}

