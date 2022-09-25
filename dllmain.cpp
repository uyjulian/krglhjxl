
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "jxl/codestream_header.h"
#include "jxl/decode.h"
#include "jxl/resizable_parallel_runner.h"
#include "jxl/types.h"

#include <memory>
#include "tp_stub.h"
#define EXPORT(hr) extern "C" __declspec(dllexport) hr __stdcall

static const tjs_int JXL_IDECODE_BUFFER_SZ = (1 << 16);

void TVPLoadJXL(void* formatdata, void *callbackdata, tTVPGraphicSizeCallback sizecallback, tTVPGraphicScanLineCallback scanlinecallback, tTVPMetaInfoPushCallback metainfopushcallback, IStream *src, tjs_int keyidx, tTVPGraphicLoadMode mode)
{
	int width, height;
	int bit_width;
	int bit_length;
	uint8_t *bitmap_data;
	JxlParallelRunner * runner;
	JxlDecoder * dec;
	const tjs_char *error_str;
	tjs_uint8* input;
	ULONG input_size;

	JxlBasicInfo info;
	JxlPixelFormat format = {
		.num_channels = 4,
		.data_type = JXL_TYPE_UINT8,
		.endianness = JXL_LITTLE_ENDIAN,
		.align = 0,
	};

	input_size = 0;
	input = (tjs_uint8*)malloc(JXL_IDECODE_BUFFER_SZ);
	if (NULL == input)
	{
		error_str = TJS_W("Could not allocate decode buffer");
		goto cleanup;
	}

	bit_width = 0;
	bit_length = 0;
	bitmap_data = NULL;
	dec = NULL;
	error_str = NULL;

	runner = JxlResizableParallelRunnerCreate(NULL);
	if (NULL == runner)
	{
		error_str = TJS_W("JxlResizableParallelRunnerCreate failed");
		goto cleanup;
	}

	dec = JxlDecoderCreate(NULL);
	if (NULL == dec)
	{
		error_str = TJS_W("JxlDecoderCreate failed");
		goto cleanup;
	}

	if (JXL_DEC_SUCCESS != JxlDecoderSubscribeEvents(dec, JXL_DEC_BASIC_INFO | JXL_DEC_FULL_IMAGE))
	{
		error_str = TJS_W("JxlDecoderSubscribeEvents failed");
		goto cleanup;
	}

	if (JXL_DEC_SUCCESS != JxlDecoderSetParallelRunner(dec, JxlResizableParallelRunner, runner))
	{
		error_str = TJS_W("JxlDecoderSetParallelRunner failed");
		goto cleanup;
	}

	src->Read(input, JXL_IDECODE_BUFFER_SZ, &input_size);
	if (0 == input_size)
	{
		error_str = TJS_W("Error, already provided all input");
		goto cleanup;
	}
	JxlDecoderSetInput(dec, (const uint8_t*)input, input_size);

	for (;;)
	{
		JxlDecoderStatus status = JxlDecoderProcessInput(dec);

		if (status == JXL_DEC_ERROR)
		{
			error_str = TJS_W("Decoder error");
			goto cleanup;
		}
		else if (status == JXL_DEC_NEED_MORE_INPUT)
		{
			JxlDecoderReleaseInput(dec);
			src->Read(input, JXL_IDECODE_BUFFER_SZ, &input_size);
			if (0 == input_size)
			{
				error_str = TJS_W("Error, already provided all input");
				goto cleanup;
			}
			JxlDecoderSetInput(dec, (const uint8_t*)input, input_size);
			goto cleanup;
		}
		else if (status == JXL_DEC_BASIC_INFO)
		{
			if (JXL_DEC_SUCCESS != JxlDecoderGetBasicInfo(dec, &info))
			{
				error_str = TJS_W("JxlDecoderGetBasicInfo failed");
				goto cleanup;
			}
			width = info.xsize;
			height = info.ysize;
			bit_width = width * 4;
			bit_length = bit_width;
			JxlResizableParallelRunnerSetThreads(runner, JxlResizableParallelRunnerSuggestThreads(info.xsize, info.ysize));
		}
		else if (status == JXL_DEC_NEED_IMAGE_OUT_BUFFER)
		{
			size_t buffer_size;
			if (JXL_DEC_SUCCESS != JxlDecoderImageOutBufferSize(dec, &format, &buffer_size))
			{
				error_str = TJS_W("JxlDecoderImageOutBufferSize failed");
				goto cleanup;
			}
			if (buffer_size != width * height * sizeof(uint32_t))
			{
				error_str = TJS_W("Invalid out buffer size");
				goto cleanup;
			}
			bitmap_data = (uint8_t *)malloc(buffer_size);
			if (NULL == bitmap_data)
			{
				error_str = TJS_W("Unable to allocate bitmap data");
				goto cleanup;
			}
			memset(bitmap_data, 0, buffer_size);
			if (JXL_DEC_SUCCESS != JxlDecoderSetImageOutBuffer(dec, &format, bitmap_data, buffer_size))
			{
				error_str = TJS_W("JxlDecoderSetImageOutBuffer failed");
				goto cleanup;
			}
		}
		else if (status == JXL_DEC_FULL_IMAGE)
		{
			// We just need the first frame
			break;
		}
		else if (status == JXL_DEC_SUCCESS)
		{
			// All decoding successfully finished.
			break;
		}
		else
		{
			error_str = TJS_W("Unknown decoder status");
			goto cleanup;
		}
	}

	// Convert from RGBA to BGRA
	for (int j = 0; j < height; j++)
	{
		uint8_t *curbit = bitmap_data + (height - (1 + j)) * bit_length;
		for (int i = 0; i < width; i++)
		{
			uint8_t b = curbit[0];
			uint8_t r = curbit[2];
			curbit[2] = b;
			curbit[0] = r;
			curbit += 4;
		}
	}

	{
		int size_pixel = (glmNormal != mode) ? sizeof(tjs_uint8) : sizeof(tjs_uint32);

		// Allocate the destination bitmap
		sizecallback(callbackdata, width, height);

		// Copy from the temporary buffer line by line
		for (int y = 0; y < height; y += 1)
		{
			void *scanline = scanlinecallback(callbackdata, y);
			if (NULL == scanline)
			{
				break;
			}
			if (sizeof(tjs_uint32) == size_pixel)
			{
				memcpy(scanline, (const void*)&bitmap_data[y * bit_width], width * size_pixel);
			}
			else if (sizeof(tjs_uint8) == size_pixel)
			{
				TVPBLConvert32BitTo8Bit((tjs_uint8*)scanline, (const tjs_uint32*)(tjs_uint8*)&bitmap_data[y * bit_width], width * size_pixel);
			}
			scanlinecallback(callbackdata, -1);
		}
	}

cleanup:
	if (NULL != bitmap_data)
	{
		free(bitmap_data);
		bitmap_data = NULL;
	}
	if (NULL != runner)
	{
		JxlResizableParallelRunnerDestroy(runner);
		runner = NULL;
	}
	if (NULL != dec)
	{
		JxlDecoderCloseInput(dec);
		JxlDecoderDestroy(dec);
		dec = NULL;
	}
	if (NULL != error_str)
	{
		TVPThrowExceptionMessage(error_str);
		return;
	}
}

void TVPLoadHeaderJXL(void* formatdata, IStream *src, iTJSDispatch2** dic)
{
	const tjs_char *error_str = NULL;
	tjs_uint8* input = NULL;
	ULONG input_size = 0;
	JxlDecoder *dec;
	JxlBasicInfo info;

	input = (tjs_uint8*)malloc(JXL_IDECODE_BUFFER_SZ);
	if (NULL == input)
	{
		error_str = TJS_W("Could not allocate decode buffer");
		goto cleanup;
	}

	dec = JxlDecoderCreate(NULL);
	if (NULL == dec)
	{
		error_str = TJS_W("JxlDecoderCreate failed");
		goto cleanup;
	}

	if (JXL_DEC_SUCCESS != JxlDecoderSubscribeEvents(dec, JXL_DEC_BASIC_INFO))
	{
		error_str = TJS_W("JxlDecoderSubscribeEvents failed");
		goto cleanup;
	}

	src->Read(input, JXL_IDECODE_BUFFER_SZ, &input_size);
	if (0 == input_size)
	{
		error_str = TJS_W("Error, already provided all input");
		goto cleanup;
	}
	JxlDecoderSetInput(dec, (const uint8_t*)input, input_size);

	for (;;)
	{
		JxlDecoderStatus status = JxlDecoderProcessInput(dec);

		if (status == JXL_DEC_ERROR)
		{
			error_str = TJS_W("Decoder error");
			goto cleanup;
		}
		else if (status == JXL_DEC_NEED_MORE_INPUT)
		{
			JxlDecoderReleaseInput(dec);
			src->Read(input, JXL_IDECODE_BUFFER_SZ, &input_size);
			if (0 == input_size)
			{
				error_str = TJS_W("Error, already provided all input");
				goto cleanup;
			}
			JxlDecoderSetInput(dec, (const uint8_t*)input, input_size);
			goto cleanup;
		}
		else if (status == JXL_DEC_BASIC_INFO)
		{
			if (JXL_DEC_SUCCESS != JxlDecoderGetBasicInfo(dec, &info))
			{
				error_str = TJS_W("JxlDecoderGetBasicInfo failed");
				goto cleanup;
			}
			// We just need information, break here.
			break;
		}
		else if (status == JXL_DEC_SUCCESS)
		{
			// All decoding successfully finished.
			break;
		}
		else
		{
			error_str = TJS_W("Unknown decoder status");
			goto cleanup;
		}
	}

cleanup:
	if (NULL != input)
	{
		free(input);
		input = NULL;
	}
	if (NULL != dec)
	{
		JxlDecoderCloseInput(dec);
		JxlDecoderDestroy(dec);
		dec = NULL;
	}
	if (NULL != error_str)
	{
		TVPThrowExceptionMessage(error_str);
		return;
	}

	*dic = TJSCreateDictionaryObject();
	tTJSVariant val((tjs_int32)info.xsize);
	(*dic)->PropSet(TJS_MEMBERENSURE, TJS_W("width"), 0, &val, (*dic));
	val = tTJSVariant((tjs_int32)info.ysize);
	(*dic)->PropSet(TJS_MEMBERENSURE, TJS_W("height"), 0, &val, (*dic));
	val = tTJSVariant((info.alpha_bits != 0) ? 32 : 24);
	(*dic)->PropSet(TJS_MEMBERENSURE, TJS_W("bpp"), 0, &val, (*dic));
	val = tTJSVariant((info.alpha_bits != 0) ? 1 : 0);
	(*dic)->PropSet(TJS_MEMBERENSURE, TJS_W("has_alpha"), 0, &val, (*dic));
	val = tTJSVariant(info.have_animation ? 1 : 0);
	(*dic)->PropSet(TJS_MEMBERENSURE, TJS_W("has_animation"), 0, &val, (*dic));
}

bool TVPAcceptSaveAsJXL(void* formatdata, const ttstr & type, class iTJSDispatch2** dic ) {
#if 0
	if( type.StartsWith(TJS_W("jxl")) || (type == TJS_W(".jxl")) ) return true;
#endif
	return false;
}

void TVPSaveAsJXL(void* formatdata, void* callbackdata, IStream* dst, const ttstr & mode, tjs_uint width, tjs_uint height, tTVPGraphicSaveScanLineCallback scanlinecallback, iTJSDispatch2* meta )
{
	TVPThrowExceptionMessage(TJS_W("Saving is not implemented"));
	return;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
	return TRUE;
}

static tjs_int GlobalRefCountAtInit = 0;
EXPORT(HRESULT) V2Link(iTVPFunctionExporter *exporter)
{
	TVPInitImportStub(exporter);

	TVPRegisterGraphicLoadingHandler( ttstr(TJS_W(".jxl")), TVPLoadJXL, TVPLoadHeaderJXL, TVPSaveAsJXL, TVPAcceptSaveAsJXL, NULL );

	GlobalRefCountAtInit = TVPPluginGlobalRefCount;
	return S_OK;
}

EXPORT(HRESULT) V2Unlink()
{
	if(TVPPluginGlobalRefCount > GlobalRefCountAtInit) return E_FAIL;
	
	TVPRegisterGraphicLoadingHandler( ttstr(TJS_W(".jxl")), TVPLoadJXL, TVPLoadHeaderJXL, TVPSaveAsJXL, TVPAcceptSaveAsJXL, NULL );

	TVPUninitImportStub();
	return S_OK;
}
