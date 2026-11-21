#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
// Output is an unencrypted OpenSSH key in RAM only; caller wipes and frees it.
int tabremoto_ppk_convert(const char* input,size_t size,const char* password,char** output,size_t* output_size,const char** error);
#ifdef __cplusplus
}
#endif
